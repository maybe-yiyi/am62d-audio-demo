#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

/* Timing constants */
#define SAMPLE_RATE_IN   48000
#define SAMPLE_RATE_OUT  16000
#define DECIMATE_FACTOR  3

/* YAMNet patch parameters */
#define PATCH_SAMPLES   15600u   /* 0.975 s × 16 kHz (yamnet_audio_classification.tflite) */
#define HOP_SAMPLES      7800u   /* 0.4875 s × 16 kHz (50 % overlap) */
#define RING_CAP        (2u * PATCH_SAMPLES)   /* 31200 samples */

/* Silence gate — skip inference below this RMS */
#define ENERGY_FLOOR    1e-6f

/* ------------------------------------------------------------------ */
/* Downsampler: 48 kHz stereo → 16 kHz mono                           */
/* 5-tap symmetric FIR, fc ≈ 7.2 kHz (Hamming), followed by 3:1 decimate */
/* ------------------------------------------------------------------ */
static const float FIR5[5] = { 0.0607f, 0.2428f, 0.3930f, 0.2428f, 0.0607f };

typedef struct {
    float fir_buf[5];
    int   phase;   /* 0 .. DECIMATE_FACTOR-1 */
} ds_state_t;

/*
 * Process one 48 kHz stereo sample pair.
 * Returns a 16 kHz mono sample when phase rolls over, NAN otherwise.
 */
static inline float ds_tick(ds_state_t *s, float l, float r)
{
    float mono = (l + r) * 0.5f;

    s->fir_buf[4] = s->fir_buf[3];
    s->fir_buf[3] = s->fir_buf[2];
    s->fir_buf[2] = s->fir_buf[1];
    s->fir_buf[1] = s->fir_buf[0];
    s->fir_buf[0] = mono;

    if (++s->phase < DECIMATE_FACTOR)
        return (float)NAN;

    s->phase = 0;
    return FIR5[0]*s->fir_buf[0] + FIR5[1]*s->fir_buf[1]
         + FIR5[2]*s->fir_buf[2] + FIR5[3]*s->fir_buf[3]
         + FIR5[4]*s->fir_buf[4];
}

/* ------------------------------------------------------------------ */
/* Ring buffer — single-owner (RT thread), no atomics                  */
/* ------------------------------------------------------------------ */
typedef struct {
    float    buf[RING_CAP];
    uint64_t write;        /* absolute write cursor (never wraps in practice) */
    uint32_t hop_pending;  /* 16 kHz samples since last patch dispatch */
} ring_t;

static inline void ring_push(ring_t *r, float sample)
{
    r->buf[r->write % RING_CAP] = sample;
    r->write++;
    r->hop_pending++;
}

/* Clear hop counter after dispatching a patch. */
static inline void ring_reset_hop(ring_t *r)
{
    r->hop_pending = 0;
}

/*
 * Copy the most recent PATCH_SAMPLES into dst[PATCH_SAMPLES].
 * Returns false if fewer than PATCH_SAMPLES have been written.
 * Handles ring wrap via two-segment copy.
 */
static inline bool ring_get_patch(const ring_t *r, float *dst)
{
    if (r->write < PATCH_SAMPLES)
        return false;

    uint64_t start = r->write - PATCH_SAMPLES;
    uint32_t s0    = (uint32_t)(start % RING_CAP);

    if (s0 + PATCH_SAMPLES <= RING_CAP) {
        /* Contiguous */
        memcpy(dst, r->buf + s0, PATCH_SAMPLES * sizeof(float));
    } else {
        /* Split across ring end */
        uint32_t first = RING_CAP - s0;
        memcpy(dst,         r->buf + s0, first * sizeof(float));
        memcpy(dst + first, r->buf,      (PATCH_SAMPLES - first) * sizeof(float));
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Energy gate                                                         */
/* ------------------------------------------------------------------ */
static inline float patch_energy(const float *data, uint32_t n)
{
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; i++)
        sum += data[i] * data[i];
    return sqrtf(sum / (float)n);
}
