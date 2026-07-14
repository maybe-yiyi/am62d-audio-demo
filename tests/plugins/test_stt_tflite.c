#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../../plugins/stt.c"

#ifndef STT_BUNDLE_PATH
#define STT_BUNDLE_PATH ""
#endif

int main(void)
{
	const LV2_Descriptor *d = lv2_descriptor(0);
	assert(d);

	LV2_Handle h = d->instantiate(d, 48000.0, STT_BUNDLE_PATH, NULL);
	if (!h) {
		fprintf(stderr, "SKIP: could not load model from %s\n", STT_BUNDLE_PATH);
		return 77;
	}

	float in[256];
	float out[256];
	float confidence = 0.0f;
	float keyword_id = 0.0f;
	float hit = 0.0f;

	d->connect_port(h, PORT_IN, in);
	d->connect_port(h, PORT_OUT, out);
	d->connect_port(h, PORT_CONFIDENCE, &confidence);
	d->connect_port(h, PORT_KEYWORD_ID, &keyword_id);
	d->connect_port(h, PORT_HIT, &hit);

	d->activate(h);

	for (int q = 0; q < 400; q++) {
		for (int i = 0; i < 256; i++) {
			float t = (float)(q * 256 + i) / 48000.0f;
			in[i] = 0.3f * sinf(2.0f * (float)M_PI * 440.0f * t);
		}
		d->run(h, 256);
	}

	for (int i = 0; i < 300; i++) {
		struct timespec ts = {0, 100000000};
		nanosleep(&ts, NULL);
		d->run(h, 256);
		if (confidence > 0.0f)
			break;
	}

	assert(confidence >= 0.0f);
	assert(confidence <= 1.0f);
	assert(keyword_id >= 0.0f);

	printf("stt: confidence=%.3f keyword_id=%.0f hit=%.0f\n",
	       confidence, keyword_id, hit);

	d->deactivate(h);
	d->cleanup(h);

	printf("PASS: stt_tflite\n");
	return 0;
}
