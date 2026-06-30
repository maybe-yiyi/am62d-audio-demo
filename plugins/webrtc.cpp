#include <string.h>
#include <memory>

#include <lv2/core/lv2.h>

#include <modules/audio_processing/include/audio_processing.h>

#define WEBRTC_URI "urn:am62d:webrtc"
#define WEBRTC_FRAMES 480

using NSLevel = webrtc::AudioProcessing::Config::NoiseSuppression::Level;

enum {
	PORT_IN_L = 0,
	PORT_IN_R = 1,
	PORT_OUT_L = 2,
	PORT_OUT_R = 3,
	PORT_NS_LEVEL = 4,
};

struct priv {
	rtc::scoped_refptr<webrtc::AudioProcessing> apm;
	webrtc::StreamConfig cfg;

	const float *in_l;
	const float *in_r;
	float *out_l;
	float *out_r;
	const float *ns_level;

	float in_buf_l[WEBRTC_FRAMES];
	float in_buf_r[WEBRTC_FRAMES];
	float out_buf_l[WEBRTC_FRAMES];
	float out_buf_r[WEBRTC_FRAMES];
	uint32_t in_fill;
	uint32_t out_avail;
	uint32_t out_pos;
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

static rtc::scoped_refptr<webrtc::AudioProcessing> make_apm(NSLevel level)
{
	auto apm = webrtc::AudioProcessingBuilder().Create();
	if (!apm)
		return nullptr;

	webrtc::AudioProcessing::Config apm_cfg;

	apm_cfg.gain_controller2.enabled = true;
	apm_cfg.gain_controller2.input_volume_controller.enabled = true;
	apm_cfg.gain_controller2.fixed_digital.gain_db = 0.0f;
	apm_cfg.gain_controller2.adaptive_digital.enabled = true;
	apm_cfg.gain_controller2.adaptive_digital.headroom_db = 3.0f;
	apm_cfg.gain_controller2.adaptive_digital.max_gain_db = 20.0f;
	apm_cfg.gain_controller2.adaptive_digital.initial_gain_db = 6.0f;
	apm_cfg.gain_controller2.adaptive_digital.max_gain_change_db_per_second = 3.0f;
	apm_cfg.gain_controller2.adaptive_digital.max_output_noise_level_dbfs = -50.0f;

	apm_cfg.noise_suppression.enabled = true;
	apm_cfg.noise_suppression.level = level;

	apm_cfg.high_pass_filter.enabled = true;

	apm_cfg.transient_suppression.enabled = true;

	apm_cfg.pipeline.multi_channel_capture = true;

	apm->ApplyConfig(apm_cfg);

	return apm;
}

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
				double sample_rate,
				const char *bundle_path,
				const LV2_Feature *const *features)
{
	(void)descriptor; (void)sample_rate; (void)bundle_path; (void)features;

	priv *p = new priv;
	p->in_l = nullptr;
	p->in_r = nullptr;
	p->out_l = nullptr;
	p->out_r = nullptr;
	p->ns_level = nullptr;
	p->cfg = webrtc::StreamConfig(48000, 2);
	p->in_fill = 0;
	p->out_avail = 0;
	p->out_pos = 0;
	p->apm = nullptr;
	return p;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	priv *p = static_cast<priv *>(instance);
	switch (port) {
	case PORT_IN_L:
		p->in_l = static_cast<const float *>(data);
		break;
	case PORT_IN_R:
		p->in_r = static_cast<const float *>(data);
		break;
	case PORT_OUT_L:
		p->out_l = static_cast<float *>(data);
		break;
	case PORT_OUT_R:
		p->out_r = static_cast<float *>(data);
		break;
	case PORT_NS_LEVEL:
		p->ns_level = static_cast<const float *>(data);
		break;
	}
}

static void activate(LV2_Handle instance)
{
	priv *p = static_cast<priv *>(instance);
	int level_int = p->ns_level ? static_cast<int>(*p->ns_level) : 1;
	p->apm = make_apm(map_level(level_int));
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
	priv *p = static_cast<priv *>(instance);

	if (!p->in_l || !p->in_r || !p->out_l || !p->out_r || !p->apm)
		return;

	uint32_t consumed = 0;
	uint32_t produced = 0;

	while (consumed < n_samples) {
		if (p->out_avail > 0 && produced < n_samples) {
			uint32_t to_emit = (n_samples - produced < p->out_avail)
					? (n_samples - produced) : p->out_avail;
			memcpy(p->out_l + produced, p->out_buf_l + p->out_pos, to_emit * sizeof(float));
			memcpy(p->out_r + produced, p->out_buf_r + p->out_pos, to_emit * sizeof(float));
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
		memcpy(p->in_buf_l + p->in_fill, p->in_l + consumed, to_consume * sizeof(float));
		memcpy(p->in_buf_r + p->in_fill, p->in_r + consumed, to_consume * sizeof(float));
		p->in_fill += to_consume;
		consumed += to_consume;

		if (p->in_fill == WEBRTC_FRAMES) {
			const float *src[2] = { p->in_buf_l, p->in_buf_r };
			float *dst[2] = { p->out_buf_l, p->out_buf_r };
			p->apm->ProcessStream(src, p->cfg, p->cfg, dst);
			p->in_fill = 0;
			p->out_avail = WEBRTC_FRAMES;
			p->out_pos = 0;
		}
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
