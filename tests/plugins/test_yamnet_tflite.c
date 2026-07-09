#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../../plugins/yamnet.c"

#ifndef YAMNET_BUNDLE_PATH
#define YAMNET_BUNDLE_PATH ""
#endif

int main(void)
{
	const LV2_Descriptor *d = lv2_descriptor(0);
	assert(d);

	LV2_Handle h = d->instantiate(d, 48000.0, YAMNET_BUNDLE_PATH, NULL);
	if (!h) {
		fprintf(stderr, "SKIP: could not load model from %s\n", YAMNET_BUNDLE_PATH);
		return 77;
	}

	float in[256];
	float out[256];
	float ctrl[NUM_BUCKETS] = {0};

	d->connect_port(h, PORT_IN, in);
	d->connect_port(h, PORT_OUT, out);
	for (int i = 0; i < NUM_BUCKETS; i++)
		d->connect_port(h, PORT_SPEECH + i, &ctrl[i]);

	d->activate(h);

	/* Feed 440 Hz tone for 250 quanta (enough for ~2 patch dispatches) */
	for (int q = 0; q < 250; q++) {
		for (int i = 0; i < 256; i++) {
			float t = (float)(q * 256 + i) / 48000.0f;
			in[i] = 0.3f * sinf(2.0f * (float)M_PI * 440.0f * t);
		}
		d->run(h, 256);
	}

	/* Poll for inference result — TFLite on AM62D A53 can take several seconds.
	* Run the plugin every 100ms so it can drain the result queue as soon as
	* the inference thread finishes, up to a 30s hard cap. */
	float total = 0.0f;
	for (int i = 0; i < 300; i++) {
		struct timespec ts = {0, 100000000};  /* 100 ms */
		nanosleep(&ts, NULL);
		d->run(h, 256);
		total = 0.0f;
		for (int j = 0; j < NUM_BUCKETS; j++)
			total += ctrl[j];
		if (total > 0.0f)
			break;
	}

	/* All scores must be in [0, 1] */
	for (int i = 0; i < NUM_BUCKETS; i++) {
		assert(ctrl[i] >= 0.0f);
		assert(ctrl[i] <= 1.0f);
	}

	/* At least one bucket must have a non-zero score */
	assert(total > 0.0f);

	printf("Bucket scores:\n");
	for (int i = 0; i < NUM_BUCKETS; i++)
		printf("  %-10s: %.3f\n", yamnet_bucket_names[i], ctrl[i]);

	d->deactivate(h);
	d->cleanup(h);

	printf("PASS: yamnet_tflite\n");
	return 0;
}
