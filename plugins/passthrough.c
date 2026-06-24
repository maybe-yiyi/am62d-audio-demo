#include <am62d_plugin.h>
#include <am62d_spa.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

struct priv {};

static int plugin_init(void **p, const struct am62d_param *params, int n)
{
	(void)params; (void)n;
	*p = calloc(1, sizeof(struct priv));
	return *p ? 0 : -ENOMEM;
}

static void plugin_destroy(void *p)
{
	free(p);
}

static int plugin_process(void *p,
                          const float **in, float **out, uint32_t n_frames,
                          const struct am62d_param *in_params, int n_in,
                          struct am62d_param *out_params, int *n_out)
{
	(void)p; (void)in_params; (void)n_in;
	*n_out = 0;
	if (in[0] && out[0]) memcpy(out[0], in[0], n_frames * sizeof(float));
	if (in[1] && out[1]) memcpy(out[1], in[1], n_frames * sizeof(float));
	return 0;
}

static const struct am62d_port_desc ports[] = {
	{ "in_l",  AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN,  { .pcm = { 1 } } },
	{ "out_l", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, { .pcm = { 1 } } },
	{ "in_r",  AM62D_PORT_AUDIO_PCM, AM62D_DIR_IN,  { .pcm = { 1 } } },
	{ "out_r", AM62D_PORT_AUDIO_PCM, AM62D_DIR_OUT, { .pcm = { 1 } } },
};

AM62D_SPA_PLUGIN_DEFINE(am62d_passthrough, "passthrough", ports, 4,
                         plugin_init, plugin_destroy, plugin_process,
                         AM62D_EXEC_A53);
