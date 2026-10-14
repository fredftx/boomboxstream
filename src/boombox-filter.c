/*
 * BoomBox Stream - filtro de vídeo reativo ao áudio.
 *
 * Fluxo:
 *   thread de áudio  -> audio_capture_cb(): mixa para mono, passa-baixa
 *                       (graves) e acumula a energia (RMS) do bloco.
 *   thread de vídeo  -> video_tick(): converte para dB, normaliza,
 *                       envelope (ataque/release) e mola (cone do falante).
 *                    -> video_render(): aplica zoom + bloom no shader.
 */

#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <media-io/audio-io.h>
#include <math.h>
#include <string.h>

#define S_AUDIO_SOURCE "audio_source"
#define S_BAND "band"
#define S_BASS_CUTOFF "bass_cutoff"
#define S_GAIN "gain_db"
#define S_THRESHOLD "threshold_db"
#define S_CURVE "curve"
#define S_ATTACK "attack_ms"
#define S_RELEASE "release_ms"
#define S_ELASTICITY "elasticity"
#define S_ZOOM "zoom_amount"
#define S_CENTER_X "center_x"
#define S_CENTER_Y "center_y"
#define S_BLOOM_BASE "bloom_base"
#define S_BLOOM_AMOUNT "bloom_amount"
#define S_BLOOM_THRESHOLD "bloom_threshold"
#define S_BLOOM_RADIUS "bloom_radius"
#define S_BLOOM_COLOR "bloom_color"
#define S_FLASH "flash"

#define SRC_DESKTOP "__desktop__"
#define SRC_SELF "__self__"

#define BAND_BASS 0
#define BAND_FULL 1

#define RESOLVE_INTERVAL_NS 1000000000ULL
#define BB_PI 3.14159265358979323846

#define T_(s) obs_module_text(s)

struct boombox {
	obs_source_t *context;

	gs_effect_t *effect;
	gs_eparam_t *p_uv_pixel;
	gs_eparam_t *p_zoom_center;
	gs_eparam_t *p_zoom;
	gs_eparam_t *p_bloom_intensity;
	gs_eparam_t *p_bloom_threshold;
	gs_eparam_t *p_bloom_radius;
	gs_eparam_t *p_bloom_tint;
	gs_eparam_t *p_flash;

	/* Fonte de áudio monitorada. */
	pthread_mutex_t name_mutex;
	char *audio_name;
	volatile bool audio_dirty;
	obs_weak_source_t *audio_weak;
	uint64_t last_resolve_ns;

	/* Acumuladores preenchidos pela thread de áudio. */
	pthread_mutex_t level_mutex;
	double acc_sq;
	size_t acc_frames;

	/* Estado do passa-baixa (apenas thread de áudio). */
	float lp1, lp2;

	/* Configurações. */
	int band;
	float bass_cutoff;
	float gain_db;
	float threshold_db;
	float curve;
	float attack_s;
	float release_s;
	float elasticity;
	float zoom_amount;
	struct vec2 center;
	float bloom_base;
	float bloom_amount;
	float bloom_threshold;
	float bloom_radius;
	struct vec3 bloom_tint;
	float flash;

	/* Saída da análise (thread de vídeo). */
	float envelope;
	float cone_pos;
	float cone_vel;
};

/* ------------------------------------------------------------------------ */
/* Áudio                                                                    */

static void audio_capture_cb(void *param, obs_source_t *source,
			     const struct audio_data *audio, bool muted)
{
	struct boombox *bb = param;
	UNUSED_PARAMETER(source);

	if (!audio || !audio->frames)
		return;

	const struct audio_output_info *aoi =
		audio_output_get_info(obs_get_audio());
	size_t channels = aoi ? get_audio_channels(aoi->speakers) : 2;
	double sample_rate = aoi ? (double)aoi->samples_per_sec : 48000.0;
	if (channels > MAX_AV_PLANES)
		channels = MAX_AV_PLANES;

	size_t used = 0;
	for (size_t c = 0; c < channels; c++)
		if (audio->data[c])
			used++;

	double sq = 0.0;

	if (!muted && used) {
		bool lowpass = bb->band == BAND_BASS;
		float a = (float)(1.0 - exp(-2.0 * BB_PI * bb->bass_cutoff /
					    sample_rate));
		float inv = 1.0f / (float)used;

		for (uint32_t i = 0; i < audio->frames; i++) {
			float s = 0.0f;
			for (size_t c = 0; c < channels; c++) {
				const float *plane =
					(const float *)audio->data[c];
				if (plane)
					s += plane[i];
			}
			s *= inv;

			if (lowpass) {
				/* Dois polos em cascata: corte de 12 dB/oitava. */
				bb->lp1 += a * (s - bb->lp1);
				bb->lp2 += a * (bb->lp1 - bb->lp2);
				s = bb->lp2;
			}

			sq += (double)s * s;
		}
	}

	pthread_mutex_lock(&bb->level_mutex);
	bb->acc_sq += sq;
	bb->acc_frames += audio->frames;
	pthread_mutex_unlock(&bb->level_mutex);
}

static void detach_audio(struct boombox *bb)
{
	obs_source_t *src = obs_weak_source_get_source(bb->audio_weak);
	if (src) {
		obs_source_remove_audio_capture_callback(src, audio_capture_cb,
							 bb);
		obs_source_release(src);
	}
	obs_weak_source_release(bb->audio_weak);
	bb->audio_weak = NULL;
}

static void attach_audio(struct boombox *bb, obs_source_t *src)
{
	bb->audio_weak = obs_source_get_weak_source(src);
	bb->lp1 = bb->lp2 = 0.0f;
	obs_source_add_audio_capture_callback(src, audio_capture_cb, bb);
}

static obs_source_t *resolve_audio_source(struct boombox *bb)
{
	obs_source_t *src = NULL;

	pthread_mutex_lock(&bb->name_mutex);
	const char *name = bb->audio_name;

	if (!name || !*name) {
		src = NULL;
	} else if (strcmp(name, SRC_DESKTOP) == 0) {
		/* Canal 1 = "Áudio da área de trabalho" global. */
		src = obs_get_output_source(1);
	} else if (strcmp(name, SRC_SELF) == 0) {
		obs_source_t *parent = obs_filter_get_parent(bb->context);
		src = parent ? obs_source_get_ref(parent) : NULL;
	} else {
		src = obs_get_source_by_name(name);
	}
	pthread_mutex_unlock(&bb->name_mutex);

	if (src && !(obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)) {
		obs_source_release(src);
		src = NULL;
	}
	return src;
}

static void refresh_audio_source(struct boombox *bb)
{
	uint64_t now = os_gettime_ns();
	bool dirty = os_atomic_set_bool(&bb->audio_dirty, false);

	if (!dirty && now - bb->last_resolve_ns < RESOLVE_INTERVAL_NS)
		return;
	bb->last_resolve_ns = now;

	obs_source_t *wanted = resolve_audio_source(bb);
	obs_source_t *current = obs_weak_source_get_source(bb->audio_weak);

	if (wanted != current || (!current && bb->audio_weak)) {
		detach_audio(bb);
		if (wanted)
			attach_audio(bb, wanted);
	}

	obs_source_release(current);
	obs_source_release(wanted);
}

/* ------------------------------------------------------------------------ */
/* Ciclo de vida                                                            */

static const char *boombox_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("BoomBox");
}

static void load_effect(struct boombox *bb)
{
	char *path = obs_module_file("boombox.effect");
	if (!path) {
		blog(LOG_ERROR, "[boomboxstream] boombox.effect not found");
		return;
	}

	obs_enter_graphics();
	char *errors = NULL;
	bb->effect = gs_effect_create_from_file(path, &errors);
	if (bb->effect) {
		bb->p_uv_pixel = gs_effect_get_param_by_name(bb->effect, "uv_pixel");
		bb->p_zoom_center =
			gs_effect_get_param_by_name(bb->effect, "zoom_center");
		bb->p_zoom = gs_effect_get_param_by_name(bb->effect, "zoom");
		bb->p_bloom_intensity = gs_effect_get_param_by_name(
			bb->effect, "bloom_intensity");
		bb->p_bloom_threshold = gs_effect_get_param_by_name(
			bb->effect, "bloom_threshold");
		bb->p_bloom_radius =
			gs_effect_get_param_by_name(bb->effect, "bloom_radius");
		bb->p_bloom_tint =
			gs_effect_get_param_by_name(bb->effect, "bloom_tint");
		bb->p_flash = gs_effect_get_param_by_name(bb->effect, "flash");
	} else {
		blog(LOG_ERROR, "[boomboxstream] failed to compile effect: %s",
		     errors ? errors : "(unknown)");
	}
	bfree(errors);
	obs_leave_graphics();

	bfree(path);
}

static void boombox_update(void *data, obs_data_t *settings)
{
	struct boombox *bb = data;

	const char *name = obs_data_get_string(settings, S_AUDIO_SOURCE);
	pthread_mutex_lock(&bb->name_mutex);
	if (!bb->audio_name || strcmp(bb->audio_name, name) != 0) {
		bfree(bb->audio_name);
		bb->audio_name = bstrdup(name);
		os_atomic_set_bool(&bb->audio_dirty, true);
	}
	pthread_mutex_unlock(&bb->name_mutex);

	bb->band = (int)obs_data_get_int(settings, S_BAND);
	bb->bass_cutoff = (float)obs_data_get_int(settings, S_BASS_CUTOFF);
	bb->gain_db = (float)obs_data_get_double(settings, S_GAIN);
	bb->threshold_db = (float)obs_data_get_double(settings, S_THRESHOLD);
	bb->curve = (float)obs_data_get_double(settings, S_CURVE);
	bb->attack_s = (float)obs_data_get_int(settings, S_ATTACK) / 1000.0f;
	bb->release_s = (float)obs_data_get_int(settings, S_RELEASE) / 1000.0f;
	bb->elasticity =
		(float)obs_data_get_int(settings, S_ELASTICITY) / 100.0f;
	bb->zoom_amount = (float)obs_data_get_double(settings, S_ZOOM) / 100.0f;
	vec2_set(&bb->center,
		 (float)obs_data_get_double(settings, S_CENTER_X) / 100.0f,
		 (float)obs_data_get_double(settings, S_CENTER_Y) / 100.0f);
	bb->bloom_base = (float)obs_data_get_double(settings, S_BLOOM_BASE);
	bb->bloom_amount = (float)obs_data_get_double(settings, S_BLOOM_AMOUNT);
	bb->bloom_threshold =
		(float)obs_data_get_double(settings, S_BLOOM_THRESHOLD);
	bb->bloom_radius = (float)obs_data_get_double(settings, S_BLOOM_RADIUS);
	bb->flash = (float)obs_data_get_double(settings, S_FLASH) / 100.0f;

	uint32_t color = (uint32_t)obs_data_get_int(settings, S_BLOOM_COLOR);
	vec3_set(&bb->bloom_tint, (float)(color & 0xFF) / 255.0f,
		 (float)((color >> 8) & 0xFF) / 255.0f,
		 (float)((color >> 16) & 0xFF) / 255.0f);
}

static void *boombox_create(obs_data_t *settings, obs_source_t *context)
{
	struct boombox *bb = bzalloc(sizeof(*bb));
	bb->context = context;

	pthread_mutex_init(&bb->name_mutex, NULL);
	pthread_mutex_init(&bb->level_mutex, NULL);

	load_effect(bb);
	boombox_update(bb, settings);
	return bb;
}

static void boombox_destroy(void *data)
{
	struct boombox *bb = data;

	detach_audio(bb);

	if (bb->effect) {
		obs_enter_graphics();
		gs_effect_destroy(bb->effect);
		obs_leave_graphics();
	}

	pthread_mutex_destroy(&bb->name_mutex);
	pthread_mutex_destroy(&bb->level_mutex);
	bfree(bb->audio_name);
	bfree(bb);
}

/* ------------------------------------------------------------------------ */
/* Análise por frame                                                        */

static void boombox_tick(void *data, float seconds)
{
	struct boombox *bb = data;

	refresh_audio_source(bb);

	pthread_mutex_lock(&bb->level_mutex);
	double sq = bb->acc_sq;
	size_t frames = bb->acc_frames;
	bb->acc_sq = 0.0;
	bb->acc_frames = 0;
	pthread_mutex_unlock(&bb->level_mutex);

	float rms = frames ? (float)sqrt(sq / (double)frames) : 0.0f;
	/* RMS de senoide * sqrt(2) ~ amplitude: fica perto de 0 dBFS no máximo. */
	float level = rms * 1.41421356f;

	float target = 0.0f;
	if (level > 1e-7f) {
		float db = 20.0f * log10f(level) + bb->gain_db;
		float floor_db = fminf(bb->threshold_db, -1.0f);
		target = (db - floor_db) / -floor_db;
		target = fminf(fmaxf(target, 0.0f), 1.0f);
		target = powf(target, bb->curve);
	}

	/* Envelope: sobe rápido (ataque), desce devagar (release). */
	float tc = target > bb->envelope ? bb->attack_s : bb->release_s;
	float k = tc > 0.0005f ? 1.0f - expf(-seconds / tc) : 1.0f;
	bb->envelope += (target - bb->envelope) * k;

	/*
	 * Cone do alto-falante: mola amortecida perseguindo o envelope.
	 * Elasticidade 0 = segue o envelope direto; 1 = bem "borrachudo".
	 */
	if (bb->elasticity <= 0.001f) {
		bb->cone_pos = bb->envelope;
		bb->cone_vel = 0.0f;
	} else {
		float freq = 14.0f - 8.0f * bb->elasticity; /* Hz */
		float omega = 2.0f * (float)BB_PI * freq;
		float zeta = 1.0f - 0.85f * bb->elasticity;
		float remaining = fminf(seconds, 0.1f);
		const float step = 0.001f;

		while (remaining > 0.0f) {
			float dt = fminf(step, remaining);
			float accel = omega * omega *
					      (bb->envelope - bb->cone_pos) -
				      2.0f * zeta * omega * bb->cone_vel;
			bb->cone_vel += accel * dt;
			bb->cone_pos += bb->cone_vel * dt;
			remaining -= dt;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Render                                                                   */

static void boombox_render(void *data, gs_effect_t *unused)
{
	struct boombox *bb = data;
	UNUSED_PARAMETER(unused);

	obs_source_t *target = obs_filter_get_target(bb->context);
	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	if (!bb->effect || !width || !height) {
		obs_source_skip_video_filter(bb->context);
		return;
	}

	if (!obs_source_process_filter_begin(bb->context, GS_RGBA,
					     OBS_NO_DIRECT_RENDERING))
		return;

	float pulse = bb->cone_pos;
	/* Zoom só "para fora" (como o cone empurrando): nunca abaixo de 1. */
	float zoom = fmaxf(1.0f + pulse * bb->zoom_amount, 1.0f);
	float bloom = fmaxf(bb->bloom_base + pulse * bb->bloom_amount, 0.0f);
	float radius = bb->bloom_radius * (0.6f + 0.4f * fmaxf(pulse, 0.0f));
	float flash = fmaxf(pulse, 0.0f) * bb->flash;

	struct vec2 uv_pixel;
	vec2_set(&uv_pixel, 1.0f / (float)width, 1.0f / (float)height);

	gs_effect_set_vec2(bb->p_uv_pixel, &uv_pixel);
	gs_effect_set_vec2(bb->p_zoom_center, &bb->center);
	gs_effect_set_float(bb->p_zoom, zoom);
	gs_effect_set_float(bb->p_bloom_intensity, bloom);
	gs_effect_set_float(bb->p_bloom_threshold, bb->bloom_threshold);
	gs_effect_set_float(bb->p_bloom_radius, radius);
	gs_effect_set_vec3(bb->p_bloom_tint, &bb->bloom_tint);
	gs_effect_set_float(bb->p_flash, flash);

	obs_source_process_filter_end(bb->context, bb->effect, width, height);
}

/* ------------------------------------------------------------------------ */
/* Propriedades                                                             */

static bool add_audio_source_to_list(void *param, obs_source_t *source)
{
	obs_property_t *list = param;
	uint32_t flags = obs_source_get_output_flags(source);

	if (!(flags & OBS_SOURCE_AUDIO))
		return true;

	const char *name = obs_source_get_name(source);
	if (name && *name)
		obs_property_list_add_string(list, name, name);
	return true;
}

static obs_properties_t *boombox_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	/* Áudio */
	obs_properties_t *audio = obs_properties_create();

	p = obs_properties_add_list(audio, S_AUDIO_SOURCE, T_("AudioSource"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_("AudioSource.Desktop"), SRC_DESKTOP);
	obs_property_list_add_string(p, T_("AudioSource.Self"), SRC_SELF);
	obs_enum_sources(add_audio_source_to_list, p);

	p = obs_properties_add_list(audio, S_BAND, T_("Band"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("Band.Bass"), BAND_BASS);
	obs_property_list_add_int(p, T_("Band.Full"), BAND_FULL);

	p = obs_properties_add_int_slider(audio, S_BASS_CUTOFF,
					  T_("BassCutoff"), 40, 400, 5);
	obs_property_int_set_suffix(p, " Hz");

	p = obs_properties_add_float_slider(audio, S_GAIN, T_("Gain"), -20.0,
					    40.0, 0.5);
	obs_property_float_set_suffix(p, " dB");

	p = obs_properties_add_float_slider(audio, S_THRESHOLD, T_("Threshold"),
					    -80.0, -6.0, 0.5);
	obs_property_float_set_suffix(p, " dB");

	obs_properties_add_float_slider(audio, S_CURVE, T_("Curve"), 0.3, 4.0,
					0.05);

	p = obs_properties_add_int_slider(audio, S_ATTACK, T_("Attack"), 1, 200,
					  1);
	obs_property_int_set_suffix(p, " ms");

	p = obs_properties_add_int_slider(audio, S_RELEASE, T_("Release"), 20,
					  1500, 5);
	obs_property_int_set_suffix(p, " ms");

	p = obs_properties_add_int_slider(audio, S_ELASTICITY,
					  T_("Elasticity"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");

	obs_properties_add_group(props, "group_audio", T_("Group.Audio"),
				 OBS_GROUP_NORMAL, audio);

	/* Zoom */
	obs_properties_t *zoom = obs_properties_create();

	p = obs_properties_add_float_slider(zoom, S_ZOOM, T_("ZoomAmount"), 0.0,
					    50.0, 0.1);
	obs_property_float_set_suffix(p, "%");

	p = obs_properties_add_float_slider(zoom, S_CENTER_X, T_("CenterX"),
					    0.0, 100.0, 0.5);
	obs_property_float_set_suffix(p, "%");

	p = obs_properties_add_float_slider(zoom, S_CENTER_Y, T_("CenterY"),
					    0.0, 100.0, 0.5);
	obs_property_float_set_suffix(p, "%");

	obs_properties_add_group(props, "group_zoom", T_("Group.Zoom"),
				 OBS_GROUP_NORMAL, zoom);

	/* Bloom */
	obs_properties_t *bloom = obs_properties_create();

	obs_properties_add_float_slider(bloom, S_BLOOM_BASE, T_("BloomBase"),
					0.0, 2.0, 0.01);
	obs_properties_add_float_slider(bloom, S_BLOOM_AMOUNT,
					T_("BloomAmount"), 0.0, 5.0, 0.01);
	obs_properties_add_float_slider(bloom, S_BLOOM_THRESHOLD,
					T_("BloomThreshold"), 0.0, 0.99, 0.01);

	p = obs_properties_add_float_slider(bloom, S_BLOOM_RADIUS,
					    T_("BloomRadius"), 1.0, 200.0, 1.0);
	obs_property_float_set_suffix(p, " px");

	obs_properties_add_color(bloom, S_BLOOM_COLOR, T_("BloomColor"));

	p = obs_properties_add_float_slider(bloom, S_FLASH, T_("Flash"), 0.0,
					    100.0, 1.0);
	obs_property_float_set_suffix(p, "%");

	obs_properties_add_group(props, "group_bloom", T_("Group.Bloom"),
				 OBS_GROUP_NORMAL, bloom);

	return props;
}

static void boombox_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_AUDIO_SOURCE, SRC_DESKTOP);
	obs_data_set_default_int(settings, S_BAND, BAND_BASS);
	obs_data_set_default_int(settings, S_BASS_CUTOFF, 120);
	obs_data_set_default_double(settings, S_GAIN, 0.0);
	obs_data_set_default_double(settings, S_THRESHOLD, -42.0);
	obs_data_set_default_double(settings, S_CURVE, 1.6);
	obs_data_set_default_int(settings, S_ATTACK, 8);
	obs_data_set_default_int(settings, S_RELEASE, 180);
	obs_data_set_default_int(settings, S_ELASTICITY, 35);
	obs_data_set_default_double(settings, S_ZOOM, 6.0);
	obs_data_set_default_double(settings, S_CENTER_X, 50.0);
	obs_data_set_default_double(settings, S_CENTER_Y, 50.0);
	obs_data_set_default_double(settings, S_BLOOM_BASE, 0.0);
	obs_data_set_default_double(settings, S_BLOOM_AMOUNT, 1.2);
	obs_data_set_default_double(settings, S_BLOOM_THRESHOLD, 0.55);
	obs_data_set_default_double(settings, S_BLOOM_RADIUS, 40.0);
	obs_data_set_default_int(settings, S_BLOOM_COLOR, 0xFFFFFFFF);
	obs_data_set_default_double(settings, S_FLASH, 12.0);
}

struct obs_source_info boombox_filter_info = {
	.id = "boombox_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = boombox_get_name,
	.create = boombox_create,
	.destroy = boombox_destroy,
	.update = boombox_update,
	.get_defaults = boombox_defaults,
	.get_properties = boombox_properties,
	.video_tick = boombox_tick,
	.video_render = boombox_render,
};
