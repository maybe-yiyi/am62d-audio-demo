#include <stdlib.h>
#include <string.h>

#include <lv2/core/lv2.h>

#define PASSTHROUGH_URI "urn:am62d:passthrough"

enum {
	PORT_IN_L = 0,
	PORT_OUT_L = 1,
	PORT_IN_R = 2,
	PORT_OUT_R = 3,
};

struct priv {
	const float *in_l;
	float *out_l;
	const float *in_r;
	float *out_r;
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
	switch (port) {
	case PORT_IN_L:
		p->in_l = data;
		break;
	case PORT_OUT_L:
		p->out_l = data;
		break;
	case PORT_IN_R:
		p->in_r = data;
		break;
	case PORT_OUT_R:
		p->out_r = data;
		break;
	}
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
	struct priv *p = instance;
	if (p->in_l && p->out_l)
		memcpy(p->out_l, p->in_l, n_samples * sizeof(float));
	if (p->in_r && p->out_r)
		memcpy(p->out_r, p->in_r, n_samples * sizeof(float));
}

static void cleanup(LV2_Handle instance)
{
	free(instance);
}

static const LV2_Descriptor descriptor = {
	.URI = PASSTHROUGH_URI,
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
