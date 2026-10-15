/*
 * BoomBox Stream - filtro de vídeo reativo ao áudio.
 *
 * Cada efeito (zoom, bloom, flash, aberração, tremor) escuta a sua própria
 * faixa de frequência:
 *
 *   thread de áudio  -> audio_capture_cb(): mixa para mono e passa o sinal
 *                       por um passa-banda Butterworth de 4ª ordem por
 *                       efeito, acumulando a energia (RMS) de cada banda.
 *   thread de vídeo  -> video_tick(): por banda converte para dB, aplica
 *                       sensibilidade/piso/curva e envelope (ataque/release);
 *                       o zoom ainda passa pela mola do cone do falante.
 *                    -> video_render(): shader de uma passada com tudo.
 */

#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <media-io/audio-io.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BB_PI 3.14159265358979323846

#define S_AUDIO_SOURCE "audio_source"
#define S_THRESHOLD "threshold_db"
#define S_CURVE "curve"
#define S_ELASTICITY "elasticity"
#define S_SHOW_METER "show_meter"

#define S_ZOOM_AMOUNT "zoom_amount"
#define S_CENTER_X "center_x"
#define S_CENTER_Y "center_y"
#define S_BLOOM_BASE "bloom_base"
#define S_BLOOM_AMOUNT "bloom_amount"
#define S_BLOOM_THRESHOLD "bloom_threshold"
#define S_BLOOM_RADIUS "bloom_radius"
#define S_BLOOM_COLOR "bloom_color"
#define S_FLASH_AMOUNT "flash_amount"
#define S_ABERR_AMOUNT "aberr_amount"
#define S_SHAKE_AMOUNT "shake_amount"
#define S_SHAKE_SPEED "shake_speed"

/* Sufixos das chaves por efeito: "<prefixo>_<sufixo>". */
#define K_ENABLED "enabled"
#define K_BAND "band"
#define K_FREQ_LO "freq_lo"
#define K_FREQ_HI "freq_hi"
#define K_GAIN "gain_db"
#define K_ATTACK "attack_ms"
#define K_RELEASE "release_ms"

#define SRC_DESKTOP "__desktop__"
#define SRC_SELF "__self__"

#define RESOLVE_INTERVAL_NS 1000000000ULL

/* Acima disso o passa-baixa é dispensado (faixa vai até o fim). */
#define FREQ_TOP 20000.0f
/* Abaixo disso o passa-alta é dispensado (faixa começa do zero). */
#define FREQ_BOTTOM 20.0f

#define T_(s) obs_module_text(s)

/* ------------------------------------------------------------------------ */
/* Faixas pré-definidas                                                     */

#define PRESET_CUSTOM 99

struct band_preset {
	const char *text;
	float lo, hi;
};

static const struct band_preset band_presets[] = {
	{"Preset.Kick", 40.0f, 120.0f},     {"Preset.Bass", 60.0f, 250.0f},
	{"Preset.LowMid", 250.0f, 800.0f},  {"Preset.Vocals", 300.0f, 3400.0f},
	{"Preset.Mid", 500.0f, 4000.0f},    {"Preset.Snare", 1500.0f, 5000.0f},
	{"Preset.Highs", 6000.0f, 16000.0f}, {"Preset.Full", 0.0f, 22000.0f},
};
#define NUM_PRESETS (sizeof(band_presets) / sizeof(band_presets[0]))

enum { PRESET_KICK, PRESET_BASS, PRESET_LOWMID, PRESET_VOCALS, PRESET_MID,
       PRESET_SNARE, PRESET_HIGHS, PRESET_FULL };

/* ------------------------------------------------------------------------ */
/* Efeitos                                                                  */

enum { FX_ZOOM, FX_BLOOM, FX_FLASH, FX_ABERR, FX_SHAKE, FX_COUNT };

struct fx_info {
	const char *prefix;
	const char *group_text;
	int preset;
	double gain_db;
	int attack_ms;
	int release_ms;
	bool enabled;
};

static const struct fx_info fx_infos[FX_COUNT] = {
	[FX_ZOOM] = {"zoom", "Group.Zoom", PRESET_KICK, 0.0, 5, 160, true},
	[FX_BLOOM] = {"bloom", "Group.Bloom", PRESET_MID, 6.0, 10, 220, true},
	[FX_FLASH] = {"flash", "Group.Flash", PRESET_HIGHS, 12.0, 4, 120, true},
	[FX_ABERR] = {"aberr", "Group.Aberration", PRESET_KICK, 0.0, 3, 120,
		      false},
	[FX_SHAKE] = {"shake", "Group.Shake", PRESET_KICK, 0.0, 3, 140, false},
};

static inline const char *fx_key(char *buf, size_t size, int fx,
				 const char *suffix)
{
	snprintf(buf, size, "%s_%s", fx_infos[fx].prefix, suffix);
	return buf;
}

/* ------------------------------------------------------------------------ */
/* Biquad (forma transposta direta II)                                      */

struct biquad {
	float b0, b1, b2, a1, a2;
	float z1, z2;
};

static inline float biquad_run(struct biquad *q, float x)
{
	float y = q->b0 * x + q->z1;
	q->z1 = q->b1 * x - q->a1 * y + q->z2;
	q->z2 = q->b2 * x - q->a2 * y;
	return y;
}

/* Coeficientes RBJ; highpass=false gera passa-baixa. */
static void biquad_design(struct biquad *q, bool highpass, double fc,
			  double fs, double Q)
{
	double w0 = 2.0 * BB_PI * fc / fs;
	double cw = cos(w0);
	double alpha = sin(w0) / (2.0 * Q);
	double a0 = 1.0 + alpha;
	double b0, b1, b2;

	if (highpass) {
		b0 = (1.0 + cw) / 2.0;
		b1 = -(1.0 + cw);
		b2 = (1.0 + cw) / 2.0;
	} else {
		b0 = (1.0 - cw) / 2.0;
		b1 = 1.0 - cw;
		b2 = (1.0 - cw) / 2.0;
	}

	q->b0 = (float)(b0 / a0);
	q->b1 = (float)(b1 / a0);
	q->b2 = (float)(b2 / a0);
	q->a1 = (float)(-2.0 * cw / a0);
	q->a2 = (float)((1.0 - alpha) / a0);
	q->z1 = q->z2 = 0.0f;
}

/* Butterworth de 4ª ordem = 2 biquads com estes Q. */
static const double butter4_q[2] = {0.54119610, 1.30656296};

/* ------------------------------------------------------------------------ */

struct fx_band {
	/* Configurações (escritas em update). */
	bool enabled;
	float lo, hi;
	float gain_db;
	float attack_s;
	float release_s;

	/* Filtros (apenas thread de áudio). */
	struct biquad hp[2];
	struct biquad lp[2];
	bool use_hp, use_lp;

	/* Acumulador (protegido por level_mutex). */
	double acc_sq;

	/* Saída (thread de vídeo). */
	float env;
};

struct boombox {
	obs_source_t *context;

	gs_effect_t *effect;
	gs_eparam_t *p_uv_pixel;
	gs_eparam_t *p_tex_size;
	gs_eparam_t *p_zoom_center;
	gs_eparam_t *p_zoom;
	gs_eparam_t *p_shake;
	gs_eparam_t *p_aberration;
	gs_eparam_t *p_bloom_intensity;
	gs_eparam_t *p_bloom_threshold;
	gs_eparam_t *p_bloom_radius;
	gs_eparam_t *p_bloom_tint;
	gs_eparam_t *p_flash;
	gs_eparam_t *p_show_meter;
	gs_eparam_t *p_meter_a;
	gs_eparam_t *p_meter_b;

	/* Fonte de áudio monitorada. */
	pthread_mutex_t name_mutex;
	char *audio_name;
	volatile bool audio_dirty;
	obs_weak_source_t *audio_weak;
	uint64_t last_resolve_ns;

	/* Bandas. */
	struct fx_band fx[FX_COUNT];
	volatile long band_gen;
	long audio_band_gen;
	double audio_rate;

	pthread_mutex_t level_mutex;
	size_t acc_frames;

	/* Configurações globais. */
	float threshold_db;
	float curve;
	float elasticity;
	bool show_meter;

	float zoom_amount;
	struct vec2 center;
	float bloom_base;
	float bloom_amount;
	float bloom_threshold;
	float bloom_radius;
	struct vec3 bloom_tint;
	float flash_amount;
	float aberr_amount;
	float shake_amount;
	float shake_speed;

	/* Estado de animação (thread de vídeo). */
	float cone_pos;
	float cone_vel;
	float time;
};

/* ------------------------------------------------------------------------ */
/* Áudio                                                                    */

static void design_bands(struct boombox *bb, double rate)
{
	double nyquist = rate * 0.5;

	for (int f = 0; f < FX_COUNT; f++) {
		struct fx_band *b = &bb->fx[f];
		double lo = b->lo;
		double hi = b->hi;

		if (hi > nyquist * 0.95)
			hi = nyquist * 0.95;
		if (lo > hi * 0.9)
			lo = hi * 0.9;

		b->use_hp = lo >= FREQ_BOTTOM;
		b->use_lp = b->hi < FREQ_TOP && hi < nyquist * 0.95;

		for (int i = 0; i < 2; i++) {
			biquad_design(&b->hp[i], true, b->use_hp ? lo : 20.0,
				      rate, butter4_q[i]);
			biquad_design(&b->lp[i], false,
				      b->use_lp ? hi : nyquist * 0.9, rate,
				      butter4_q[i]);
		}
	}
}

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
	double rate = aoi ? (double)aoi->samples_per_sec : 48000.0;
	if (channels > MAX_AV_PLANES)
		channels = MAX_AV_PLANES;

	long gen = os_atomic_load_long(&bb->band_gen);
	if (gen != bb->audio_band_gen || rate != bb->audio_rate) {
		design_bands(bb, rate);
		bb->audio_band_gen = gen;
		bb->audio_rate = rate;
	}

	size_t used = 0;
	for (size_t c = 0; c < channels; c++)
		if (audio->data[c])
			used++;

	double sq[FX_COUNT] = {0};

	if (!muted && used) {
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

			for (int f = 0; f < FX_COUNT; f++) {
				struct fx_band *b = &bb->fx[f];
				if (!b->enabled)
					continue;

				float y = s;
				if (b->use_hp) {
					y = biquad_run(&b->hp[0], y);
					y = biquad_run(&b->hp[1], y);
				}
				if (b->use_lp) {
					y = biquad_run(&b->lp[0], y);
					y = biquad_run(&b->lp[1], y);
				}
				sq[f] += (double)y * y;
			}
		}
	}

	pthread_mutex_lock(&bb->level_mutex);
	for (int f = 0; f < FX_COUNT; f++)
		bb->fx[f].acc_sq += sq[f];
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
	/* Força o audio thread a recriar (e zerar) os filtros. */
	os_atomic_inc_long(&bb->band_gen);
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
#define GET_PARAM(field, name) \
	bb->field = gs_effect_get_param_by_name(bb->effect, name)
		GET_PARAM(p_uv_pixel, "uv_pixel");
		GET_PARAM(p_tex_size, "tex_size");
		GET_PARAM(p_zoom_center, "zoom_center");
		GET_PARAM(p_zoom, "zoom");
		GET_PARAM(p_shake, "shake");
		GET_PARAM(p_aberration, "aberration");
		GET_PARAM(p_bloom_intensity, "bloom_intensity");
		GET_PARAM(p_bloom_threshold, "bloom_threshold");
		GET_PARAM(p_bloom_radius, "bloom_radius");
		GET_PARAM(p_bloom_tint, "bloom_tint");
		GET_PARAM(p_flash, "flash");
		GET_PARAM(p_show_meter, "show_meter");
		GET_PARAM(p_meter_a, "meter_a");
		GET_PARAM(p_meter_b, "meter_b");
#undef GET_PARAM
	} else {
		blog(LOG_ERROR, "[boomboxstream] failed to compile effect: %s",
		     errors ? errors : "(unknown)");
	}
	bfree(errors);
	obs_leave_graphics();

	bfree(path);
}

static void get_band_range(obs_data_t *settings, int fx, float *lo, float *hi)
{
	char key[64];
	int preset = (int)obs_data_get_int(settings,
					   fx_key(key, sizeof(key), fx, K_BAND));

	if (preset >= 0 && preset < (int)NUM_PRESETS) {
		*lo = band_presets[preset].lo;
		*hi = band_presets[preset].hi;
	} else {
		*lo = (float)obs_data_get_int(
			settings, fx_key(key, sizeof(key), fx, K_FREQ_LO));
		*hi = (float)obs_data_get_int(
			settings, fx_key(key, sizeof(key), fx, K_FREQ_HI));
	}

	if (*hi < *lo) {
		float t = *lo;
		*lo = *hi;
		*hi = t;
	}
}

static void boombox_update(void *data, obs_data_t *settings)
{
	struct boombox *bb = data;
	char key[64];

	const char *name = obs_data_get_string(settings, S_AUDIO_SOURCE);
	pthread_mutex_lock(&bb->name_mutex);
	if (!bb->audio_name || strcmp(bb->audio_name, name) != 0) {
		bfree(bb->audio_name);
		bb->audio_name = bstrdup(name);
		os_atomic_set_bool(&bb->audio_dirty, true);
	}
	pthread_mutex_unlock(&bb->name_mutex);

	for (int f = 0; f < FX_COUNT; f++) {
		struct fx_band *b = &bb->fx[f];

		b->enabled = obs_data_get_bool(
			settings, fx_key(key, sizeof(key), f, K_ENABLED));
		get_band_range(settings, f, &b->lo, &b->hi);
		b->gain_db = (float)obs_data_get_double(
			settings, fx_key(key, sizeof(key), f, K_GAIN));
		b->attack_s = (float)obs_data_get_int(
				      settings,
				      fx_key(key, sizeof(key), f, K_ATTACK)) /
			      1000.0f;
		b->release_s = (float)obs_data_get_int(
				       settings,
				       fx_key(key, sizeof(key), f, K_RELEASE)) /
			       1000.0f;
	}
	os_atomic_inc_long(&bb->band_gen);

	bb->threshold_db = (float)obs_data_get_double(settings, S_THRESHOLD);
	bb->curve = (float)obs_data_get_double(settings, S_CURVE);
	bb->elasticity =
		(float)obs_data_get_int(settings, S_ELASTICITY) / 100.0f;
	bb->show_meter = obs_data_get_bool(settings, S_SHOW_METER);

	bb->zoom_amount =
		(float)obs_data_get_double(settings, S_ZOOM_AMOUNT) / 100.0f;
	vec2_set(&bb->center,
		 (float)obs_data_get_double(settings, S_CENTER_X) / 100.0f,
		 (float)obs_data_get_double(settings, S_CENTER_Y) / 100.0f);
	bb->bloom_base = (float)obs_data_get_double(settings, S_BLOOM_BASE);
	bb->bloom_amount = (float)obs_data_get_double(settings, S_BLOOM_AMOUNT);
	bb->bloom_threshold =
		(float)obs_data_get_double(settings, S_BLOOM_THRESHOLD);
	bb->bloom_radius = (float)obs_data_get_double(settings, S_BLOOM_RADIUS);
	bb->flash_amount =
		(float)obs_data_get_double(settings, S_FLASH_AMOUNT) / 100.0f;
	bb->aberr_amount = (float)obs_data_get_double(settings, S_ABERR_AMOUNT);
	bb->shake_amount = (float)obs_data_get_double(settings, S_SHAKE_AMOUNT);
	bb->shake_speed = (float)obs_data_get_double(settings, S_SHAKE_SPEED);

	uint32_t color = (uint32_t)obs_data_get_int(settings, S_BLOOM_COLOR);
	vec3_set(&bb->bloom_tint, (float)(color & 0xFF) / 255.0f,
		 (float)((color >> 8) & 0xFF) / 255.0f,
		 (float)((color >> 16) & 0xFF) / 255.0f);
}

static void *boombox_create(obs_data_t *settings, obs_source_t *context)
{
	struct boombox *bb = bzalloc(sizeof(*bb));
	bb->context = context;
	bb->audio_band_gen = -1;

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

static float shape_level(const struct boombox *bb, float rms, float gain_db)
{
	/* RMS * sqrt(2) ~ amplitude de pico de uma senoide (0 dBFS = 1.0). */
	float level = rms * 1.41421356f;
	if (level <= 1e-7f)
		return 0.0f;

	float db = 20.0f * log10f(level) + gain_db;
	float floor_db = fminf(bb->threshold_db, -1.0f);
	float x = (db - floor_db) / -floor_db;
	x = fminf(fmaxf(x, 0.0f), 1.0f);
	return powf(x, bb->curve);
}

static void boombox_tick(void *data, float seconds)
{
	struct boombox *bb = data;
	double sq[FX_COUNT];

	refresh_audio_source(bb);

	pthread_mutex_lock(&bb->level_mutex);
	size_t frames = bb->acc_frames;
	for (int f = 0; f < FX_COUNT; f++) {
		sq[f] = bb->fx[f].acc_sq;
		bb->fx[f].acc_sq = 0.0;
	}
	bb->acc_frames = 0;
	pthread_mutex_unlock(&bb->level_mutex);

	for (int f = 0; f < FX_COUNT; f++) {
		struct fx_band *b = &bb->fx[f];
		float target = 0.0f;

		if (b->enabled && frames) {
			float rms = (float)sqrt(sq[f] / (double)frames);
			target = shape_level(bb, rms, b->gain_db);
		}

		/* Envelope: sobe rápido (ataque), desce devagar (release). */
		float tc = target > b->env ? b->attack_s : b->release_s;
		float k = tc > 0.0005f ? 1.0f - expf(-seconds / tc) : 1.0f;
		b->env += (target - b->env) * k;
	}

	/*
	 * Cone do alto-falante (zoom): mola amortecida perseguindo o envelope.
	 * Elasticidade 0 = segue o envelope direto; 1 = bem "borrachudo".
	 */
	float zoom_env = bb->fx[FX_ZOOM].env;
	if (bb->elasticity <= 0.001f) {
		bb->cone_pos = zoom_env;
		bb->cone_vel = 0.0f;
	} else {
		float freq = 14.0f - 8.0f * bb->elasticity; /* Hz */
		float omega = 2.0f * (float)BB_PI * freq;
		float zeta = 1.0f - 0.85f * bb->elasticity;
		float remaining = fminf(seconds, 0.1f);
		const float step = 0.001f;

		while (remaining > 0.0f) {
			float dt = fminf(step, remaining);
			float accel = omega * omega * (zoom_env - bb->cone_pos) -
				      2.0f * zeta * omega * bb->cone_vel;
			bb->cone_vel += accel * dt;
			bb->cone_pos += bb->cone_vel * dt;
			remaining -= dt;
		}
	}

	bb->time += seconds;
	if (bb->time > 3600.0f)
		bb->time -= 3600.0f;
}

/* ------------------------------------------------------------------------ */
/* Render                                                                   */

static inline float fx_level(const struct boombox *bb, int fx)
{
	return bb->fx[fx].enabled ? fmaxf(bb->fx[fx].env, 0.0f) : 0.0f;
}

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

	float cone = bb->fx[FX_ZOOM].enabled ? bb->cone_pos : 0.0f;
	float l_bloom = fx_level(bb, FX_BLOOM);
	float l_flash = fx_level(bb, FX_FLASH);
	float l_aberr = fx_level(bb, FX_ABERR);
	float l_shake = fx_level(bb, FX_SHAKE);

	/* Zoom só "para fora" (como o cone empurrando): nunca abaixo de 1. */
	float zoom = fmaxf(1.0f + cone * bb->zoom_amount, 1.0f);
	float bloom = bb->fx[FX_BLOOM].enabled
			      ? fmaxf(bb->bloom_base + l_bloom * bb->bloom_amount,
				      0.0f)
			      : 0.0f;
	float radius = bb->bloom_radius * (0.6f + 0.4f * l_bloom);

	/* Tremor: ruído suave (soma de senos) escalado pelo nível. */
	float t = bb->time * bb->shake_speed;
	float shake_px = bb->shake_amount * l_shake * l_shake;
	struct vec2 shake;
	vec2_set(&shake,
		 0.5f * (sinf(t * 1.13f) + sinf(t * 2.71f + 1.3f)) * shake_px /
			 (float)width,
		 0.5f * (cosf(t * 1.37f) + sinf(t * 2.23f + 4.1f)) * shake_px /
			 (float)height);

	struct vec2 uv_pixel, tex_size;
	vec2_set(&uv_pixel, 1.0f / (float)width, 1.0f / (float)height);
	vec2_set(&tex_size, (float)width, (float)height);

	struct vec4 meter_a;
	vec4_set(&meter_a, fmaxf(cone, 0.0f), l_bloom, l_flash, l_aberr);

	gs_effect_set_vec2(bb->p_uv_pixel, &uv_pixel);
	gs_effect_set_vec2(bb->p_tex_size, &tex_size);
	gs_effect_set_vec2(bb->p_zoom_center, &bb->center);
	gs_effect_set_float(bb->p_zoom, zoom);
	gs_effect_set_vec2(bb->p_shake, &shake);
	gs_effect_set_float(bb->p_aberration, bb->aberr_amount * l_aberr);
	gs_effect_set_float(bb->p_bloom_intensity, bloom);
	gs_effect_set_float(bb->p_bloom_threshold, bb->bloom_threshold);
	gs_effect_set_float(bb->p_bloom_radius, radius);
	gs_effect_set_vec3(bb->p_bloom_tint, &bb->bloom_tint);
	gs_effect_set_float(bb->p_flash, l_flash * bb->flash_amount);
	gs_effect_set_float(bb->p_show_meter, bb->show_meter ? 1.0f : 0.0f);
	gs_effect_set_vec4(bb->p_meter_a, &meter_a);
	gs_effect_set_float(bb->p_meter_b, l_shake);

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

static bool band_preset_modified(obs_properties_t *props, obs_property_t *p,
				 obs_data_t *settings)
{
	char prefix[32];
	char key[64];
	const char *name = obs_property_name(p);
	const char *us = strrchr(name, '_');
	size_t len = us ? (size_t)(us - name) : strlen(name);

	if (len >= sizeof(prefix))
		return false;
	memcpy(prefix, name, len);
	prefix[len] = 0;

	bool custom = obs_data_get_int(settings, name) == PRESET_CUSTOM;

	snprintf(key, sizeof(key), "%s_%s", prefix, K_FREQ_LO);
	obs_property_set_visible(obs_properties_get(props, key), custom);
	snprintf(key, sizeof(key), "%s_%s", prefix, K_FREQ_HI);
	obs_property_set_visible(obs_properties_get(props, key), custom);
	return true;
}

static void add_band_controls(obs_properties_t *grp, int fx)
{
	char key[64];
	obs_property_t *p;

	p = obs_properties_add_list(grp, fx_key(key, sizeof(key), fx, K_BAND),
				    T_("Band"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	for (size_t i = 0; i < NUM_PRESETS; i++)
		obs_property_list_add_int(p, T_(band_presets[i].text),
					  (long long)i);
	obs_property_list_add_int(p, T_("Preset.Custom"), PRESET_CUSTOM);
	obs_property_set_modified_callback(p, band_preset_modified);

	p = obs_properties_add_int_slider(
		grp, fx_key(key, sizeof(key), fx, K_FREQ_LO), T_("FreqLow"), 0,
		20000, 5);
	obs_property_int_set_suffix(p, " Hz");

	p = obs_properties_add_int_slider(
		grp, fx_key(key, sizeof(key), fx, K_FREQ_HI), T_("FreqHigh"),
		20, 22000, 5);
	obs_property_int_set_suffix(p, " Hz");

	p = obs_properties_add_float_slider(
		grp, fx_key(key, sizeof(key), fx, K_GAIN), T_("Gain"), -20.0,
		40.0, 0.5);
	obs_property_float_set_suffix(p, " dB");

	p = obs_properties_add_int_slider(
		grp, fx_key(key, sizeof(key), fx, K_ATTACK), T_("Attack"), 1,
		200, 1);
	obs_property_int_set_suffix(p, " ms");

	p = obs_properties_add_int_slider(
		grp, fx_key(key, sizeof(key), fx, K_RELEASE), T_("Release"), 20,
		1500, 5);
	obs_property_int_set_suffix(p, " ms");
}

static obs_properties_t *add_fx_group(obs_properties_t *props, int fx)
{
	char key[64];
	obs_properties_t *grp = obs_properties_create();

	add_band_controls(grp, fx);
	obs_properties_add_group(props, fx_key(key, sizeof(key), fx, K_ENABLED),
				 T_(fx_infos[fx].group_text),
				 OBS_GROUP_CHECKABLE, grp);
	return grp;
}

static obs_properties_t *boombox_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *grp;
	obs_property_t *p;

	/* Áudio (global) */
	grp = obs_properties_create();

	p = obs_properties_add_list(grp, S_AUDIO_SOURCE, T_("AudioSource"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_("AudioSource.Desktop"), SRC_DESKTOP);
	obs_property_list_add_string(p, T_("AudioSource.Self"), SRC_SELF);
	obs_enum_sources(add_audio_source_to_list, p);

	p = obs_properties_add_float_slider(grp, S_THRESHOLD, T_("Threshold"),
					    -80.0, -6.0, 0.5);
	obs_property_float_set_suffix(p, " dB");

	obs_properties_add_float_slider(grp, S_CURVE, T_("Curve"), 0.3, 4.0,
					0.05);
	obs_properties_add_bool(grp, S_SHOW_METER, T_("ShowMeter"));

	obs_properties_add_group(props, "group_audio", T_("Group.Audio"),
				 OBS_GROUP_NORMAL, grp);

	/* Zoom */
	grp = add_fx_group(props, FX_ZOOM);
	p = obs_properties_add_float_slider(grp, S_ZOOM_AMOUNT,
					    T_("ZoomAmount"), 0.0, 50.0, 0.1);
	obs_property_float_set_suffix(p, "%");
	p = obs_properties_add_int_slider(grp, S_ELASTICITY, T_("Elasticity"),
					  0, 100, 1);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_float_slider(grp, S_CENTER_X, T_("CenterX"), 0.0,
					    100.0, 0.5);
	obs_property_float_set_suffix(p, "%");
	p = obs_properties_add_float_slider(grp, S_CENTER_Y, T_("CenterY"), 0.0,
					    100.0, 0.5);
	obs_property_float_set_suffix(p, "%");

	/* Bloom */
	grp = add_fx_group(props, FX_BLOOM);
	obs_properties_add_float_slider(grp, S_BLOOM_AMOUNT, T_("BloomAmount"),
					0.0, 5.0, 0.01);
	obs_properties_add_float_slider(grp, S_BLOOM_BASE, T_("BloomBase"), 0.0,
					2.0, 0.01);
	obs_properties_add_float_slider(grp, S_BLOOM_THRESHOLD,
					T_("BloomThreshold"), 0.0, 0.99, 0.01);
	p = obs_properties_add_float_slider(grp, S_BLOOM_RADIUS,
					    T_("BloomRadius"), 1.0, 200.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	obs_properties_add_color(grp, S_BLOOM_COLOR, T_("BloomColor"));

	/* Flash */
	grp = add_fx_group(props, FX_FLASH);
	p = obs_properties_add_float_slider(grp, S_FLASH_AMOUNT,
					    T_("FlashAmount"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, "%");

	/* Aberração cromática */
	grp = add_fx_group(props, FX_ABERR);
	p = obs_properties_add_float_slider(grp, S_ABERR_AMOUNT,
					    T_("AberrationAmount"), 0.0, 60.0,
					    0.5);
	obs_property_float_set_suffix(p, " px");

	/* Tremor */
	grp = add_fx_group(props, FX_SHAKE);
	p = obs_properties_add_float_slider(grp, S_SHAKE_AMOUNT,
					    T_("ShakeAmount"), 0.0, 60.0, 0.5);
	obs_property_float_set_suffix(p, " px");
	obs_properties_add_float_slider(grp, S_SHAKE_SPEED, T_("ShakeSpeed"),
					1.0, 60.0, 0.5);

	return props;
}

static void boombox_defaults(obs_data_t *settings)
{
	char key[64];

	obs_data_set_default_string(settings, S_AUDIO_SOURCE, SRC_DESKTOP);
	obs_data_set_default_double(settings, S_THRESHOLD, -42.0);
	obs_data_set_default_double(settings, S_CURVE, 1.6);
	obs_data_set_default_bool(settings, S_SHOW_METER, false);

	for (int f = 0; f < FX_COUNT; f++) {
		const struct fx_info *fi = &fx_infos[f];
		obs_data_set_default_bool(
			settings, fx_key(key, sizeof(key), f, K_ENABLED),
			fi->enabled);
		obs_data_set_default_int(settings,
					 fx_key(key, sizeof(key), f, K_BAND),
					 fi->preset);
		obs_data_set_default_int(
			settings, fx_key(key, sizeof(key), f, K_FREQ_LO),
			(long long)band_presets[fi->preset].lo);
		obs_data_set_default_int(
			settings, fx_key(key, sizeof(key), f, K_FREQ_HI),
			(long long)band_presets[fi->preset].hi);
		obs_data_set_default_double(
			settings, fx_key(key, sizeof(key), f, K_GAIN),
			fi->gain_db);
		obs_data_set_default_int(settings,
					 fx_key(key, sizeof(key), f, K_ATTACK),
					 fi->attack_ms);
		obs_data_set_default_int(
			settings, fx_key(key, sizeof(key), f, K_RELEASE),
			fi->release_ms);
	}

	obs_data_set_default_double(settings, S_ZOOM_AMOUNT, 6.0);
	obs_data_set_default_int(settings, S_ELASTICITY, 35);
	obs_data_set_default_double(settings, S_CENTER_X, 50.0);
	obs_data_set_default_double(settings, S_CENTER_Y, 50.0);
	obs_data_set_default_double(settings, S_BLOOM_BASE, 0.0);
	obs_data_set_default_double(settings, S_BLOOM_AMOUNT, 1.2);
	obs_data_set_default_double(settings, S_BLOOM_THRESHOLD, 0.55);
	obs_data_set_default_double(settings, S_BLOOM_RADIUS, 40.0);
	obs_data_set_default_int(settings, S_BLOOM_COLOR, 0xFFFFFFFF);
	obs_data_set_default_double(settings, S_FLASH_AMOUNT, 10.0);
	obs_data_set_default_double(settings, S_ABERR_AMOUNT, 6.0);
	obs_data_set_default_double(settings, S_SHAKE_AMOUNT, 6.0);
	obs_data_set_default_double(settings, S_SHAKE_SPEED, 22.0);
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
