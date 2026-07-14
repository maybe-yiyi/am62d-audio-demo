#include <string.h>
#include <memory>
#include <cmath>

#include <lv2/core/lv2.h>

#include <modules/audio_processing/include/audio_processing.h>

#define WEBRTC_URI "urn:am62d:webrtc"
#define WEBRTC_FRAMES 480

#ifndef AM62D_MAX_CHANNELS
#define AM62D_MAX_CHANNELS 8
#endif

using NSLevel = webrtc::AudioProcessing::Config::NoiseSuppression::Level;

#define PORT_NS_LEVEL (2 * AM62D_MAX_CHANNELS)

struct priv {
	rtc::scoped_refptr<webrtc::AudioProcessing> apm;
	webrtc::StreamConfig cfg;

	const float *in_bufs[AM62D_MAX_CHANNELS];
	float *out_bufs[AM62D_MAX_CHANNELS];
	const float *ns_level;

	float in_stage[AM62D_MAX_CHANNELS][WEBRTC_FRAMES];
	float out_stage[AM62D_MAX_CHANNELS][WEBRTC_FRAMES];
	uint32_t in_fill;
	uint32_t out_avail;
	uint32_t out_pos;
	int n_active;

	float gate_gain[AM62D_MAX_CHANNELS];
};

static NSLevel map_level(int v)
{
	switch(v) {
	case 0:
		return NSLevel::kLow;
	case 2:
		return NSLevel::kHigh;
	case 3:
		return NSLevel::kVeryHigh;
	default:
		return NSLevel::kModerate;
	}
}

static rtc::scoped_refptr<webrtc::AudioProcessing> make_apm(NSLevel level, bool multi_channel)
{
	auto apm = webrtc::AudioProcessingBuilder().Create();
	if (!apm)
		return nullptr;

	webrtc::AudioProcessing::Config apm_cfg;

	apm_cfg.noise_suppression.enabled = true;
	apm_cfg.noise_suppression.level = level;

	apm_cfg.high_pass_filter.enabled = true;

	apm_cfg.transient_suppression.enabled = true;

	apm->ApplyConfig(apm_cfg);

	return apm;
}

#define GATE_THRESHOLD 0.00316f
#define GATE_ATTACK    0.90f
#define GATE_RELEASE   0.05f

static void gate_frame(float out[][WEBRTC_FRAMES], int n_ch,
		       float *gate_gain, int n_signal)
{
	for (int ch = 0; ch < n_ch; ch++) {
		float sum_sq = 0.0f;
		for (int i = 0; i < n_signal; i++)
			sum_sq += out[ch][i] * out[ch][i];
		float rms = n_signal > 0 ? sqrtf(sum_sq / n_signal) : 0.0f;

		float target = rms >= GATE_THRESHOLD ? 1.0f : 0.0f;
		float coeff = target > gate_gain[ch] ? GATE_ATTACK : GATE_RELEASE;
		float new_gain = gate_gain[ch] + coeff * (target - gate_gain[ch]);

		float g0 = gate_gain[ch];
		float dg = (new_gain - g0) / WEBRTC_FRAMES;
		for (int i = 0; i < WEBRTC_FRAMES; i++)
			out[ch][i] *= g0 + i * dg;
		gate_gain[ch] = new_gain;
	}
}

static void activate(LV2_Handle instance)
{
	priv *p = static_cast<priv *>(instance);
	p->n_active = 0;
	p->in_fill = 0;
	p->out_avail = 0;
	p->out_pos = 0;
	p->apm = nullptr;
	for (int i = 0; i < AM62D_MAX_CHANNELS; i++)
		p->gate_gain[i] = 0.0f;
}

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
				double sample_rate,
				const char *bundle_path,
				const LV2_Feature *const *features)
{
	(void)descriptor; (void)sample_rate; (void)bundle_path; (void)features;

	priv *p = new priv;
	for (int i = 0; i < AM62D_MAX_CHANNELS; i++) {
		p->in_bufs[i] = nullptr;
		p->out_bufs[i] = nullptr;
	}
	p->ns_level = nullptr;
	activate(p);
	return p;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	priv *p = static_cast<priv *>(instance);
	if (port < (uint32_t)AM62D_MAX_CHANNELS) {
		p->in_bufs[port] = static_cast<const float *>(data);
	} else if (port < (uint32_t)(2 * AM62D_MAX_CHANNELS)) {
		p->out_bufs[port - AM62D_MAX_CHANNELS] = static_cast<float *>(data);
	} else if (port == PORT_NS_LEVEL) {
		p->ns_level = static_cast<const float *>(data);
	}
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
	priv *p = static_cast<priv *>(instance);

	int n = 0;
	while (n < AM62D_MAX_CHANNELS && p->in_bufs[n] && p->out_bufs[n])
		n++;

	if (n != p->n_active || !p->apm) {
		p->n_active = n;
		p->in_fill = 0;
		p->out_avail = 0;
		p->out_pos = 0;
		int level_int = p->ns_level ? static_cast<int>(*p->ns_level) : 1;
		p->cfg = webrtc::StreamConfig(48000, n > 0 ? n : 1);
		p->apm = n > 0 ? make_apm(map_level(level_int), n > 1) : nullptr;
	}

	if (p->n_active <= 0 || !p->apm)
		return;

	for (int ch = 0; ch < p->n_active; ch++)
		if (p->out_bufs[ch])
			memset(p->out_bufs[ch], 0, n_samples * sizeof(float));

	uint32_t consumed = 0;
	uint32_t produced = 0;

	while (consumed < n_samples) {
		if (p->out_avail > 0 && produced < n_samples) {
			uint32_t to_emit = (n_samples - produced < p->out_avail)
					? (n_samples - produced) : p->out_avail;
			for (int ch = 0; ch < p->n_active; ch++)
				memcpy(p->out_bufs[ch] + produced, p->out_stage[ch] + p->out_pos,
						to_emit * sizeof(float));
			produced += to_emit;
			p->out_pos += to_emit;
			p->out_avail -= to_emit;
			if (p->out_avail == 0)
				p->out_pos = 0;
			continue;
		}

		uint32_t space = WEBRTC_FRAMES - p->in_fill;
		uint32_t to_consume = (n_samples - consumed < space)
				? (n_samples - consumed) : space;
		for (int ch = 0; ch < p->n_active; ch++)
			memcpy(p->in_stage[ch] + p->in_fill, p->in_bufs[ch] + consumed,
					to_consume * sizeof(float));
		p->in_fill += to_consume;
		consumed += to_consume;

		if (p->in_fill == WEBRTC_FRAMES) {
			const float *src[AM62D_MAX_CHANNELS];
			float *dst[AM62D_MAX_CHANNELS];
			for (int ch = 0; ch < p->n_active; ch++) {
				src[ch] = p->in_stage[ch];
				dst[ch] = p->out_stage[ch];
			}
			p->apm->ProcessStream(src, p->cfg, p->cfg, dst);
			gate_frame(p->out_stage, p->n_active, p->gate_gain, WEBRTC_FRAMES);
			p->in_fill = 0;
			p->out_avail = WEBRTC_FRAMES;
			p->out_pos = 0;
		}
	}

	if (produced < n_samples && p->in_fill > 0) {
		uint32_t to_emit = n_samples - produced;
		uint32_t real = p->in_fill;
		for (int ch = 0; ch < p->n_active; ch++)
			memset(p->in_stage[ch] + real, 0, (WEBRTC_FRAMES - real) * sizeof(float));
		const float *src[AM62D_MAX_CHANNELS];
		float *dst[AM62D_MAX_CHANNELS];
		for (int ch = 0; ch < p->n_active; ch++) {
			src[ch] = p->in_stage[ch];
			dst[ch] = p->out_stage[ch];
		}
		p->apm->ProcessStream(src, p->cfg, p->cfg, dst);
		gate_frame(p->out_stage, p->n_active, p->gate_gain, real);
		for (int ch = 0; ch < p->n_active; ch++)
			memcpy(p->out_bufs[ch] + produced, p->out_stage[ch], to_emit * sizeof(float));
		p->out_avail = WEBRTC_FRAMES - to_emit;
		p->out_pos = to_emit;
		p->in_fill = 0;
	}
}

static void deactivate(LV2_Handle instance)
{
	priv *p = static_cast<priv *>(instance);
	p->apm = nullptr;
}

static void cleanup(LV2_Handle instance)
{
	delete static_cast<priv *>(instance);
}

static const LV2_Descriptor descriptor = {
	.URI = WEBRTC_URI,
	.instantiate = instantiate,
	.connect_port = connect_port,
	.activate = activate,
	.run = run,
	.deactivate = deactivate,
	.cleanup = cleanup,
	.extension_data = nullptr,
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
	return index == 0 ? &descriptor : nullptr;
}
