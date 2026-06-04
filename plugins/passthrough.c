#include <string.h>
#include <stdlib.h>

#include "am62d_plugin.h"

struct priv {};

static int plugin_init(void **priv, const char *config_json)
{
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

static int plugin_process(void *priv, const float **in, float **out,
			  uint32_t n_frames)
{
	memcpy(*out, *in, n_frames * sizeof(float));
	return 0;
}

static int plugin_set_param(void *priv, const char *key, const char *value_json)
{
	return 0;
}

static int plugin_get_param(void *priv, const char *key, char *out_json,
			    uint32_t out_len)
{
	return 0;
}

static const struct am62d_port_desc ports[] = {
	{ "in", AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN, {{1}} },
	{ "out", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, {{1}} }
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
	.set_param = plugin_set_param,
	.get_param = plugin_get_param
};
