#ifndef STT_DSP_H
#define STT_DSP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#define STT_SAMPLE_RATE_IN 48000
#define STT_SAMPLE_RATE_OUT 16000
#define STT_DECIMATE_FACTOR 3

/* 1.0 s @ 16 kHz — speech_commands-style window */
#define STT_PATCH_SAMPLES 16000u
#define STT_HOP_SAMPLES 8000u
#define STT_RING_CAP (2u * STT_PATCH_SAMPLES)

#define STT_ENERGY_FLOOR 1e-6f
#define STT_HIT_THRESHOLD 0.5f

/* Same 5-tap FIR as YAMNet path (fc = 7.2 kHz @ 48 kHz). */
static const float STT_FIR5[5] = { 0.0201f, 0.2309f, 0.4980f, 0.2309f, 0.0201f };

typedef struct {
	float fir_buf[5];
	int phase;
} stt_ds_state_t;

static inline float stt_ds_tick(stt_ds_state_t *s, float sample)
{
	s->fir_buf[4] = s->fir_buf[3];
	s->fir_buf[3] = s->fir_buf[2];
	s->fir_buf[2] = s->fir_buf[1];
	s->fir_buf[1] = s->fir_buf[0];
	s->fir_buf[0] = sample;

	if (++s->phase < STT_DECIMATE_FACTOR)
		return (float)NAN;

	s->phase = 0;

	float acc = 0.0f;
	for (int i = 0; i < 5; i++)
		acc += STT_FIR5[i] * s->fir_buf[i];
	return acc;
}

typedef struct {
	float buf[STT_RING_CAP];
	uint64_t write;
	uint32_t hop_pending;
} stt_ring_t;

static inline void stt_ring_push(stt_ring_t *r, float sample)
{
	r->buf[r->write % STT_RING_CAP] = sample;
	r->write++;
	r->hop_pending++;
}

static inline void stt_ring_reset_hop(stt_ring_t *r)
{
	r->hop_pending = 0;
}

static inline bool stt_ring_get_patch(const stt_ring_t *r, float *dst)
{
	if (r->write < STT_PATCH_SAMPLES)
		return false;

	uint64_t start = r->write - STT_PATCH_SAMPLES;
	uint32_t s0 = (uint32_t)(start % STT_RING_CAP);

	if (s0 + STT_PATCH_SAMPLES <= STT_RING_CAP) {
		memcpy(dst, r->buf + s0, STT_PATCH_SAMPLES * sizeof(float));
	} else {
		uint32_t first = STT_RING_CAP - s0;
		memcpy(dst, r->buf + s0, first * sizeof(float));
		memcpy(dst + first, r->buf, (STT_PATCH_SAMPLES - first) * sizeof(float));
	}

	return true;
}

static inline float stt_patch_energy(const float *data, uint32_t n)
{
	float sum = 0.0f;
	for (uint32_t i = 0; i < n; i++)
		sum += data[i] * data[i];
	return sqrtf(sum / (float)n);
}

#endif /* STT_DSP_H */
