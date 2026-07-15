#include <assert.h>
#include <stdio.h>

#include "../../plugins/downmix.c"

int main(void)
{
	const LV2_Descriptor *d = lv2_descriptor(0);
	assert(d);

	/* Connect only channels 0..2 (3 of AM62D_MAX_CHANNELS); leave the rest
	 * unconnected, matching how a53_node.c will behave for optional ports
	 * that aren't referenced in a pipeline config's links[]. */
	float ch0[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float ch1[4] = {3.0f, 3.0f, 3.0f, 3.0f};
	float ch2[4] = {5.0f, 5.0f, 5.0f, 5.0f};
	float out[4];

	LV2_Handle h = d->instantiate(d, 48000.0, "/tmp", NULL);
	assert(h);

	d->connect_port(h, 0, ch0);
	d->connect_port(h, 1, ch1);
	d->connect_port(h, 2, ch2);
	d->connect_port(h, PORT_OUT, out);

	d->run(h, 4);

	/* Average of 1.0, 3.0, 5.0 = 3.0 */
	for (int i = 0; i < 4; i++)
		assert(out[i] == 3.0f);

	d->cleanup(h);

	printf("PASS: downmix_averages_connected_channels\n");
	return 0;
}
