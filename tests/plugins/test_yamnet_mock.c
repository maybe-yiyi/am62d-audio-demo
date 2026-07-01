/*
 * Integration test for yamnet.c compiled with mock TFLite.
 * Drives the LV2 descriptor directly — no PipeWire or lilv needed.
 */
#define _GNU_SOURCE   /* M_PI, nanosleep */
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* Include the plugin source directly. HAVE_TFLITE is NOT defined,
 * so the mock inference path (raw_scores[0] = 0.9) is used. */
#include "../../plugins/yamnet.c"

#define N_SAMPLES 256

static void connect_audio_and_ctrl(const LV2_Descriptor *d, LV2_Handle h,
    float *in_l, float *in_r, float *out_l, float *out_r,
    float ctrl[NUM_BUCKETS])
{
    d->connect_port(h, PORT_IN_L,  in_l);
    d->connect_port(h, PORT_IN_R,  in_r);
    d->connect_port(h, PORT_OUT_L, out_l);
    d->connect_port(h, PORT_OUT_R, out_r);
    for (int i = 0; i < NUM_BUCKETS; i++)
        d->connect_port(h, PORT_SPEECH + i, &ctrl[i]);
}

int main(void)
{
    const LV2_Descriptor *d = lv2_descriptor(0);
    assert(d);
    assert(strcmp(d->URI, YAMNET_URI) == 0);

    /* instantiate — empty bundle path, no model file needed in mock mode */
    LV2_Handle h = d->instantiate(d, 48000.0, "/tmp", NULL);
    assert(h);

    float in_l[N_SAMPLES], in_r[N_SAMPLES];
    float out_l[N_SAMPLES], out_r[N_SAMPLES];
    float ctrl[NUM_BUCKETS] = {0};

    connect_audio_and_ctrl(d, h, in_l, in_r, out_l, out_r, ctrl);
    d->activate(h);

    /* Fill with a 440 Hz sine on L and 880 Hz on R */
    for (int i = 0; i < N_SAMPLES; i++) {
        in_l[i] = sinf(2.0f * (float)M_PI * 440.0f * i / 48000.0f);
        in_r[i] = sinf(2.0f * (float)M_PI * 880.0f * i / 48000.0f);
    }

    /* --- Test 1: audio passthrough --- */
    d->run(h, N_SAMPLES);
    for (int i = 0; i < N_SAMPLES; i++) {
        assert(out_l[i] == in_l[i]);
        assert(out_r[i] == in_r[i]);
    }
    printf("PASS: passthrough\n");

    /* --- Test 2: mock inference scores appear on control ports ---
     * ring_get_patch requires ring.write >= PATCH_SAMPLES = 15360 (16kHz samples).
     * First successful patch dispatch happens after 2 hops: 2*7680=15360 16kHz
     * samples = 46080 input samples at 48kHz.
     * At N_SAMPLES=256 per run: ceil(46080/256) = 180 calls minimum.
     * Feed 200 quanta to be safe.
     */
    for (int q = 0; q < 200; q++)
        d->run(h, N_SAMPLES);

    /* Give inference thread time to process and post result */
    struct timespec ts = {0, 100000000};  /* 100 ms */
    nanosleep(&ts, NULL);

    /* One more run() to drain result_queue into ctrl_buf */
    d->run(h, N_SAMPLES);

    /* Mock always returns raw_scores[0]=0.9 (Speech class 0 → BUCKET_SPEECH)
     * so ctrl[BUCKET_SPEECH] must be > 0.5 */
    assert(ctrl[BUCKET_SPEECH] > 0.5f);

    printf("PASS: mock inference scores on control ports\n");

    d->deactivate(h);
    d->cleanup(h);
    return 0;
}
