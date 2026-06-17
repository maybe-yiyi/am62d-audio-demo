#include <string.h>
#include <stdlib.h>

#include "am62d_plugin.h"

struct priv {};

static int plugin_init(void **priv, const struct am62d_param *params, int n_params)
{
	(void)params; (void)n_params;
	struct priv *p = calloc(1, sizeof(*p));
	if (!p)
		return -1;
	*priv = p;
	return 0;
}

static void plugin_destroy(void *priv)
{
	free(priv);
}

static int plugin_process(void *priv,
			  const float **in, float **out, uint32_t n_frames,
			  struct am62d_data_buf *const *in_meta,
			  struct am62d_data_buf **out_meta,
			  float *out_ctrl)
{
	(void)priv; (void)in_meta; (void)out_meta; (void)out_ctrl;

	if (in[0] == NULL || out[0] == NULL || in[1] == NULL || out[1] == NULL)
		return 1;

	memcpy(out[0], in[0], n_frames * sizeof(float));
	memcpy(out[1], in[1], n_frames * sizeof(float));
	return 0;
}

static const struct am62d_port_desc ports[] = {
	{ "in_l", AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN, {{1}} },
	{ "out_l", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, {{1}} },
	{ "in_r", AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN, {{1}} },
	{ "out_r", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, {{1}} }
};

AM62D_PLUGIN_EXPORT const struct am62d_plugin AM62D_PLUGIN_ENTRY = {
	.abi_magic = AM62D_ABI_MAGIC,
	.abi_major = AM62D_ABI_MAJOR,
	.abi_minor = AM62D_ABI_MINOR,
	.name = "passthrough",
	.executor = AM62D_EXEC_A53,
	.ports = ports,
	.n_ports = sizeof(ports) / sizeof(ports[0]),
	.init = plugin_init,
	.destroy = plugin_destroy,
	.process = plugin_process,
};
