#include <assert.h>
#include <stdio.h>
#include <math.h>

#include "../../plugins/webrtc.cpp"

int main(void)
{
	const LV2_Descriptor *d = lv2_descriptor(0);
	assert(d);

	LV2_Handle h = d->instantiate(d, 48000.0, "/tmp", NULL);
	assert(h);

	/* Connect only 2 of AM62D_MAX_CHANNELS in/out pairs. */
	float in0[WEBRTC_FRAMES], in1[WEBRTC_FRAMES];
	float out0[WEBRTC_FRAMES], out1[WEBRTC_FRAMES];
	float ns_level = 1.0f;

	d->connect_port(h, 0, in0);
	d->connect_port(h, 1, in1);
	d->connect_port(h, AM62D_MAX_CHANNELS + 0, out0);
	d->connect_port(h, AM62D_MAX_CHANNELS + 1, out1);
	d->connect_port(h, PORT_NS_LEVEL, &ns_level);

	d->activate(h);

	for (int i = 0; i < WEBRTC_FRAMES; i++) {
		in0[i] = 0.1f * sinf(2.0f * (float)M_PI * 440.0f * (float)i / 48000.0f);
		in1[i] = in0[i];
	}
	d->run(h, WEBRTC_FRAMES);

	/* n_active and out_avail are settled after the first run(), not at activate(),
	 * because connect_port() is valid between activate() and run() per LV2 spec. */
	priv *p = static_cast<priv *>(h);
	assert(p->n_active == 2);
	assert(p->out_avail == WEBRTC_FRAMES);

	d->deactivate(h);
	d->cleanup(h);

	printf("PASS: webrtc_n_active_from_contiguous_ports\n");
	return 0;
}
