#include <string.h>
#include <memory>

extern "C" {
#include "am62d_plugin.h"
}

#include <modules/audio_processing/include/audio_processing.h>

#define WEBRTC_FRAMES 480

using NSLevel = webrtc::AudioProcessing::Config::NoiseSuppression::Level;

struct priv {
	rtc::scoped_refptr<webrtc::AudioProcessing> apm;
	webrtc::StreamConfig cfg;

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

static int plugin_init(void **out_priv,
		const struct am62d_param *params, int n_params)
{
	int level_int = 1;
	for (int i = 0; i < n_params; i++) {
		if (params[i].type == AM62D_PARAM_INT &&
			strcmp(params[i].key, "level") == 0)
			level_int = params[i].v.i;
	}
	printf("Set NS level to %d\n", level_int);

	struct priv *p = new priv;
	NSLevel level = map_level(level_int);
	p->apm = make_apm(level);
	if (!p->apm) {
		delete p;
		return -1;
	}

	p->cfg = webrtc::StreamConfig(48000, 2);
	p->in_fill = 0;
	p->out_avail = 0;
	p->out_pos = 0;
	*out_priv = p;
	return 0;
}

static int plugin_process(void *priv,
			const float **in, float **out, uint32_t n_frames,
			struct am62d_data_buf *const *in_meta,
			struct am62d_data_buf **out_meta,
			float *out_ctrl)
{
	(void)in_meta; (void)out_meta; (void)out_ctrl;

	struct priv *p = static_cast<struct priv *>(priv);

	if (!in[0] || !out[0] || !in[1] || !out[1])
		return 1;

	uint32_t consumed = 0;
	uint32_t produced = 0;

	while (consumed < n_frames) {
		if (p->out_avail > 0 && produced < n_frames) {
			uint32_t to_emit = (n_frames - produced < p->out_avail)
					? (n_frames - produced) : p->out_avail;
			memcpy(out[0] + produced, p->out_buf_l + p->out_pos, to_emit * sizeof(float));
			memcpy(out[1] + produced, p->out_buf_r + p->out_pos, to_emit * sizeof(float));
			produced += to_emit;
			p->out_pos += to_emit;
			p->out_avail -= to_emit;
			if (p->out_avail == 0)
				p->out_pos = 0;
			continue;
		}

		uint32_t space = WEBRTC_FRAMES - p->in_fill;
		uint32_t to_consume = (n_frames - consumed < space) ? (n_frames - consumed) : space;
		memcpy(p->in_buf_l + p->in_fill, in[0] + consumed, to_consume * sizeof(float));
		memcpy(p->in_buf_r + p->in_fill, in[1] + consumed, to_consume * sizeof(float));
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

	return 0;
}

static int plugin_set_control(void *priv, const char *key, float value)
{
	(void)priv; (void)key; (void)value;
	return 0;
}

static void plugin_destroy(void *priv)
{
	delete static_cast<struct priv *>(priv);
}

static const struct am62d_port_desc ports[] = {
	{ "audio_in_l", AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN, {{1}} },
	{ "audio_in_r", AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN, {{1}} },
	{ "audio_out_l", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, {{1}} },
	{ "audio_out_r", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, {{1}} },
};

extern "C" AM62D_PLUGIN_EXPORT const struct am62d_plugin AM62D_PLUGIN_ENTRY = {
	.abi_magic = AM62D_ABI_MAGIC,
	.abi_major = AM62D_ABI_MAJOR,
	.abi_minor = AM62D_ABI_MINOR,
	.name = "webrtc",
	.executor = AM62D_EXEC_A53,
	.ports = ports,
	.n_ports = sizeof(ports) / sizeof(ports[0]),
	.init = plugin_init,
	.destroy = plugin_destroy,
	.process = plugin_process,
	.set_control = plugin_set_control,
};
