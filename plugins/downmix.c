#include <lv2/core/lv2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DOWNMIX_URI "urn:am62d:downmix"

#ifndef AM62D_MAX_CHANNELS
#define AM62D_MAX_CHANNELS 8
#endif

#define PORT_OUT AM62D_MAX_CHANNELS

struct priv {
	const float *in_bufs[AM62D_MAX_CHANNELS];
	float *out;
	int n_active;
};

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
			double sample_rate,
			const char *bundle_path,
			const LV2_Feature *const *features)
{
	(void)descriptor; (void)sample_rate; (void)bundle_path; (void)features;
	return calloc(1, sizeof(struct priv));
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	struct priv *p = instance;
	if (port < (uint32_t)AM62D_MAX_CHANNELS)
		p->in_bufs[port] = data;
	else if (port == PORT_OUT)
		p->out = data;
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
	struct priv *p = instance;

	if (!p->out)
		return;

	int n = 0;
	while (n < AM62D_MAX_CHANNELS && p->in_bufs[n])
		n++;
	p->n_active = n;

	if (p->n_active <= 0) {
		memset(p->out, 0, n_samples * sizeof(float));
		return;
	}

	for (uint32_t s = 0; s < n_samples; s++) {
		float sum = 0.0f;
		for (int ch = 0; ch < p->n_active; ch++)
			sum += p->in_bufs[ch][s];
		p->out[s] = sum / (float)p->n_active;
	}
}

static void cleanup(LV2_Handle instance)
{
	free(instance);
}

static const LV2_Descriptor descriptor = {
	.URI = DOWNMIX_URI,
	.instantiate = instantiate,
	.connect_port = connect_port,
	.activate = NULL,
	.run = run,
	.deactivate = NULL,
	.cleanup = cleanup,
	.extension_data = NULL,
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
	return index == 0 ? &descriptor : NULL;
}
