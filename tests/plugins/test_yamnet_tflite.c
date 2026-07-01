#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define HAVE_TFLITE
#include "../../plugins/yamnet.c"

#define MODEL_PATH "/tmp/am62d-plugins/yamnet.tflite"

int main(void)
{
    const LV2_Descriptor *d = lv2_descriptor(0);
    assert(d);

    /* instantiate with real model */
    LV2_Handle h = d->instantiate(d, 48000.0, "/tmp/am62d-plugins", NULL);
    if (!h) {
        fprintf(stderr, "SKIP: could not load %s\n", MODEL_PATH);
        return 77;  /* meson skip code */
    }

    float in_l[256], in_r[256];
    float out_l[256], out_r[256];
    float ctrl[NUM_BUCKETS] = {0};

    d->connect_port(h, PORT_IN_L,  in_l);
    d->connect_port(h, PORT_IN_R,  in_r);
    d->connect_port(h, PORT_OUT_L, out_l);
    d->connect_port(h, PORT_OUT_R, out_r);
    for (int i = 0; i < NUM_BUCKETS; i++)
        d->connect_port(h, PORT_SPEECH + i, &ctrl[i]);

    d->activate(h);

    /* Feed 440 Hz tone for 250 quanta (enough for ~2 patch dispatches) */
    for (int q = 0; q < 250; q++) {
        for (int i = 0; i < 256; i++) {
            float t = (float)(q * 256 + i) / 48000.0f;
            in_l[i] = 0.3f * sinf(2.0f * (float)M_PI * 440.0f * t);
            in_r[i] = in_l[i];
        }
        d->run(h, 256);
    }

    /* Wait for inference thread */
    struct timespec ts = {0, 500000000};  /* 500 ms */
    nanosleep(&ts, NULL);
    d->run(h, 256);  /* collect result */

    /* All scores must be in [0, 1] */
    for (int i = 0; i < NUM_BUCKETS; i++) {
        assert(ctrl[i] >= 0.0f);
        assert(ctrl[i] <= 1.0f);
    }

    /* At least one bucket must have a non-zero score */
    float total = 0.0f;
    for (int i = 0; i < NUM_BUCKETS; i++)
        total += ctrl[i];
    assert(total > 0.0f);

    printf("Bucket scores:\n");
    const char *names[NUM_BUCKETS] = {
        "speech", "alert", "laugh", "crowd",
        "hvac", "ext_noise", "door", "music", "typing"
    };
    for (int i = 0; i < NUM_BUCKETS; i++)
        printf("  %-10s: %.3f\n", names[i], ctrl[i]);

    d->deactivate(h);
    d->cleanup(h);

    printf("PASS: yamnet_tflite\n");
    return 0;
}
