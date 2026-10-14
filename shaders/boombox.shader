// =============================================================================
//  BoomBox Stream — shader reativo ao áudio para o plugin "Shaderfilter"
//  (obs-shaderfilter do exeldro com suporte a áudio — testado na 2.6.0).
//
//  A fonte/tela pulsa como um alto-falante: zoom + vibração do cone,
//  bloom, flash, aberração cromática e tremor de câmera.
//
//  COMO USAR
//   1. Botão direito na fonte (ou na CENA, para a tela inteira) → Filtros →
//      "+" → User-defined shader.
//   2. Marque "Load shader text from file" e escolha este arquivo
//      (ou cole o texto em "Shader text" e clique em "Reload effect").
//   3. Em "Audio source" escolha de onde vem o som (Desktop Audio, música…).
//      Dica: ligue "Simular batida" para ajustar sem música tocando.
//
//  DE ONDE VEM O ÁUDIO
//   O Shaderfilter entrega dois níveis (0..1 = -60..0 dB, faixa completa):
//     audio_magnitude → RMS, suave   → aqui chamado de "Corpo"
//     audio_peak      → pico, rápido → aqui chamado de "Batida"
//   Cada efeito escolhe qual dos dois (ou a mistura) o dirige.
// =============================================================================

uniform float audio_peak;
uniform float audio_magnitude;

// ----------------------------------------------------------------- Áudio -----

uniform float Noise_Floor_dB<
	string label = "Piso de ruído (dB) — abaixo disso nada reage";
	string widget_type = "slider";
	string group = "Áudio";
	float minimum = -60.0;
	float maximum = -6.0;
	float step = 0.5;
> = -40.0;

uniform float Sensitivity<
	string label = "Sensibilidade";
	string widget_type = "slider";
	string group = "Áudio";
	float minimum = 0.25;
	float maximum = 4.0;
	float step = 0.05;
> = 1.0;

uniform float Punch_Curve<
	string label = "Curva de punch (maior = só batidas fortes mexem)";
	string widget_type = "slider";
	string group = "Áudio";
	float minimum = 0.3;
	float maximum = 5.0;
	float step = 0.05;
> = 2.0;

uniform bool Test_Beat<
	string label = "Simular batida (ignora o áudio, para ajustar)";
	string group = "Áudio";
> = false;

uniform float Test_BPM<
	string label = "BPM da simulação";
	string widget_type = "slider";
	string group = "Áudio";
	float minimum = 60.0;
	float maximum = 200.0;
	float step = 1.0;
> = 120.0;

// ------------------------------------------------------------------ Zoom -----

uniform int Zoom_Driver<
	string label = "Zoom reage a";
	string widget_type = "select";
	string group = "Zoom (cone do falante)";
	int    option_0_value = 0;
	string option_0_label = "Corpo (RMS, suave)";
	int    option_1_value = 1;
	string option_1_label = "Batida (pico, rápido)";
	int    option_2_value = 2;
	string option_2_label = "Mistura";
> = 2;

uniform float Zoom_Amount<
	string label = "Zoom máximo (%)";
	string widget_type = "slider";
	string group = "Zoom (cone do falante)";
	float minimum = 0.0;
	float maximum = 40.0;
	float step = 0.1;
> = 6.0;

uniform float Cone_Wobble<
	string label = "Vibração do cone (%)";
	string widget_type = "slider";
	string group = "Zoom (cone do falante)";
	float minimum = 0.0;
	float maximum = 5.0;
	float step = 0.05;
> = 0.6;

uniform float Wobble_Speed<
	string label = "Velocidade da vibração (Hz)";
	string widget_type = "slider";
	string group = "Zoom (cone do falante)";
	float minimum = 1.0;
	float maximum = 30.0;
	float step = 0.5;
> = 12.0;

uniform float Center_X<
	string label = "Centro do zoom X (%)";
	string widget_type = "slider";
	string group = "Zoom (cone do falante)";
	float minimum = 0.0;
	float maximum = 100.0;
	float step = 0.5;
> = 50.0;

uniform float Center_Y<
	string label = "Centro do zoom Y (%)";
	string widget_type = "slider";
	string group = "Zoom (cone do falante)";
	float minimum = 0.0;
	float maximum = 100.0;
	float step = 0.5;
> = 50.0;

// ----------------------------------------------------------------- Bloom -----

uniform int Bloom_Driver<
	string label = "Bloom reage a";
	string widget_type = "select";
	string group = "Bloom";
	int    option_0_value = 0;
	string option_0_label = "Corpo (RMS, suave)";
	int    option_1_value = 1;
	string option_1_label = "Batida (pico, rápido)";
	int    option_2_value = 2;
	string option_2_label = "Mistura";
> = 1;

uniform float Bloom_Base<
	string label = "Bloom base (sem som)";
	string widget_type = "slider";
	string group = "Bloom";
	float minimum = 0.0;
	float maximum = 2.0;
	float step = 0.01;
> = 0.0;

uniform float Bloom_Amount<
	string label = "Bloom na batida";
	string widget_type = "slider";
	string group = "Bloom";
	float minimum = 0.0;
	float maximum = 5.0;
	float step = 0.01;
> = 1.2;

uniform float Bloom_Threshold<
	string label = "Limiar do bloom (só o que é mais claro brilha)";
	string widget_type = "slider";
	string group = "Bloom";
	float minimum = 0.0;
	float maximum = 0.99;
	float step = 0.01;
> = 0.55;

uniform float Bloom_Radius<
	string label = "Raio do bloom (px)";
	string widget_type = "slider";
	string group = "Bloom";
	float minimum = 1.0;
	float maximum = 200.0;
	float step = 1.0;
> = 40.0;

uniform float4 Bloom_Color<
	string label = "Cor do bloom";
	string group = "Bloom";
> = {1.0, 1.0, 1.0, 1.0};

uniform float Flash<
	string label = "Flash de brilho (%)";
	string widget_type = "slider";
	string group = "Bloom";
	float minimum = 0.0;
	float maximum = 100.0;
	float step = 1.0;
> = 12.0;

// --------------------------------------------------- Aberração cromática -----

uniform int Aberration_Driver<
	string label = "Aberração reage a";
	string widget_type = "select";
	string group = "Aberração cromática";
	int    option_0_value = 0;
	string option_0_label = "Corpo (RMS, suave)";
	int    option_1_value = 1;
	string option_1_label = "Batida (pico, rápido)";
	int    option_2_value = 2;
	string option_2_label = "Mistura";
> = 1;

uniform float Aberration_Amount<
	string label = "Separação RGB (px na borda)";
	string widget_type = "slider";
	string group = "Aberração cromática";
	float minimum = 0.0;
	float maximum = 60.0;
	float step = 0.5;
> = 8.0;

// ---------------------------------------------------------------- Tremor -----

uniform int Shake_Driver<
	string label = "Tremor reage a";
	string widget_type = "select";
	string group = "Tremor de câmera";
	int    option_0_value = 0;
	string option_0_label = "Corpo (RMS, suave)";
	int    option_1_value = 1;
	string option_1_label = "Batida (pico, rápido)";
	int    option_2_value = 2;
	string option_2_label = "Mistura";
> = 1;

uniform float Shake_Amount<
	string label = "Tremor (px)";
	string widget_type = "slider";
	string group = "Tremor de câmera";
	float minimum = 0.0;
	float maximum = 60.0;
	float step = 0.5;
> = 6.0;

uniform float Shake_Speed<
	string label = "Velocidade do tremor";
	string widget_type = "slider";
	string group = "Tremor de câmera";
	float minimum = 1.0;
	float maximum = 60.0;
	float step = 0.5;
> = 22.0;

// =============================================================================

// O sampler padrão do Shaderfilter tem borda transparente; com tremor e
// aberração isso deixaria faixas vazias nas bordas, então usamos Clamp.
sampler_state boomSampler {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

float bb_shape(float level)
{
	// level já vem em escala dB linear: 0 = -60 dB, 1 = 0 dB.
	float floor_lvl = (Noise_Floor_dB + 60.0) / 60.0;
	float x = saturate((level - floor_lvl) / max(1.0 - floor_lvl, 0.001));
	return pow(saturate(x * Sensitivity), Punch_Curve);
}

float bb_pick(int driver, float body, float hit)
{
	if (driver == 0)
		return body;
	if (driver == 1)
		return hit;
	return lerp(body, hit, 0.5);
}

float4 bb_sample(float2 uv)
{
	return image.Sample(boomSampler, uv);
}

float4 mainImage(VertData v_in) : TARGET
{
	// --- Nível de áudio ------------------------------------------------------
	float body;
	float hit;
	if (Test_Beat) {
		float phase = frac(elapsed_time * Test_BPM / 60.0);
		hit  = exp(-phase * 9.0);
		body = exp(-phase * 4.0) * 0.8;
	} else {
		body = bb_shape(audio_magnitude);
		hit  = bb_shape(audio_peak);
	}

	float zoom_drv  = bb_pick(Zoom_Driver, body, hit);
	float bloom_drv = bb_pick(Bloom_Driver, body, hit);
	float ab_drv    = bb_pick(Aberration_Driver, body, hit);
	float shake_drv = bb_pick(Shake_Driver, body, hit);

	float2 center = float2(Center_X, Center_Y) / 100.0;

	// --- Zoom + vibração do cone --------------------------------------------
	float wobble = sin(elapsed_time * 6.2831853 * Wobble_Speed) *
		       (Cone_Wobble / 100.0) * zoom_drv;
	float zoom = max(1.0 + zoom_drv * Zoom_Amount / 100.0 + wobble, 1.0);

	// --- Tremor: ruído suave (soma de senos) ---------------------------------
	float t = elapsed_time * Shake_Speed;
	float2 shake = float2(sin(t * 1.13) + sin(t * 2.71 + 1.3),
			      cos(t * 1.37) + sin(t * 2.23 + 4.1)) * 0.5;
	shake *= Shake_Amount * shake_drv * shake_drv * uv_pixel_interval;

	float2 uv = center + (v_in.uv - center) / zoom + shake;

	// --- Aberração cromática radial -----------------------------------------
	float2 dir = uv - center;
	float2 ab = dir * 2.0 * Aberration_Amount * ab_drv * uv_pixel_interval;

	float4 base = bb_sample(uv);
	float3 color = float3(bb_sample(uv + ab).r, base.g, bb_sample(uv - ab).b);

	// --- Flash ----------------------------------------------------------------
	color *= 1.0 + bloom_drv * Flash / 100.0;

	// --- Bloom: espiral de 32 amostras (ângulo áureo) -------------------------
	float bloom = max(Bloom_Base + bloom_drv * Bloom_Amount, 0.0);
	if (bloom > 0.0001) {
		float radius = Bloom_Radius * (0.6 + 0.4 * bloom_drv);
		float knee = max(1.0 - Bloom_Threshold, 0.001);
		float3 acc = float3(0.0, 0.0, 0.0);
		float wsum = 0.0;

		for (int i = 0; i < 32; i++) {
			float fi = float(i) + 0.5;
			float r = sqrt(fi / 32.0);
			float a = fi * 2.39996323;
			float2 off = float2(cos(a), sin(a)) * (r * radius) * uv_pixel_interval;
			float3 s = bb_sample(uv + off).rgb;
			float w = exp(-2.5 * r * r);
			acc += max(s - Bloom_Threshold, 0.0) / knee * w;
			wsum += w;
		}

		color += (acc / wsum) * bloom * Bloom_Color.rgb;
	}

	float glow = max(max(color.r, color.g), color.b);
	return float4(saturate(color), saturate(max(base.a, glow)));
}
