#ifndef YAMNET_DSP_H
#define YAMNET_DSP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

/* Timing constants */
#define SAMPLE_RATE_IN 48000
#define SAMPLE_RATE_OUT 16000
#define DECIMATE_FACTOR 3

/* YAMNet patch parameters */
#define PATCH_SAMPLES 15600u
#define HOP_SAMPLES 7800u
#define RING_CAP (2u * PATCH_SAMPLES)

/* Silence gate - skip inference below this RMS */
#define ENERGY_FLOOR 1e-6f

/** Downsampler:
 * 48 kHz stereo --> 16 kHz mono
 * 5-tap symmetric FIR, fc = 7.2 kHz (Hamming), followed by 3:1 decimate
 * coeffients from scipy, firwin(5, 7200, fs=48000, window='hamming')
 */
static const float FIR5[5] = { 0.0201f, 0.2309f, 0.4980f, 0.2309f, 0.0201f };

typedef struct {
	float fir_buf[5];
	int phase; /* 0 .. DECIMATE_FACTOR-1 */
} ds_state_t;

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

	float acc = 0.0f;
	for (int i = 0; i < 5; i++)
		acc += FIR5[i] * s->fir_buf[i];
	return acc;
}

/* Ring buffer */
typedef struct {
	float buf[RING_CAP];
	uint64_t write;
	uint32_t hop_pending;
} ring_t;

static inline void ring_push(ring_t *r, float sample)
{
	r->buf[r->write % RING_CAP] = sample;
	r->write++;
	r->hop_pending++;
}

static inline void ring_reset_hop(ring_t *r)
{
    r->hop_pending = 0;
}

static inline bool ring_get_patch(const ring_t *r, float *dst)
{
	if (r->write < PATCH_SAMPLES)
		return false;

	uint64_t start = r->write - PATCH_SAMPLES;
	uint32_t s0 = (uint32_t)(start % RING_CAP);

	if (s0 + PATCH_SAMPLES <= RING_CAP) {
		/* Contiguous */
		memcpy(dst, r->buf + s0, PATCH_SAMPLES * sizeof(float));
	} else {
		/* Split across ring end */
		uint32_t first = RING_CAP - s0;
		memcpy(dst, r->buf + s0, first * sizeof(float));
		memcpy(dst + first, r->buf, (PATCH_SAMPLES - first) * sizeof(float));
	}

	return true;
}

/* Energy gate */
static inline float patch_energy(const float *data, uint32_t n)
{
	float sum = 0.0f;
	for (uint32_t i = 0; i < n; i++)
		sum += data[i] * data[i];
	return sqrtf(sum / (float)n);
}

#endif /* YAMNET_DSP_H */
