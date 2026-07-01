#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "../../plugins/yamnet_dsp.h"

int main(void)
{
	/* --- Downsampler --- */

	/* Test 1: output rate is 1/3 of input rate */
	ds_state_t s = {0};
	int outputs = 0;
	for (int i = 0; i < 30; i++) {
		float y = ds_tick(&s, 1.0f, 1.0f);
		if (!isnan(y))
			outputs++;
	}
	assert(outputs == 10);  /* 30 / 3 = 10 */

	/* Test 2: DC input → DC output (FIR coeff sum approx. 1.0) */
	ds_state_t s2 = {0};
	float last_y = 0.0f;
	for (int i = 0; i < 60; i++) {
		float y = ds_tick(&s2, 1.0f, 1.0f);
		if (!isnan(y))
			last_y = y;
	}
	assert(fabsf(last_y - 1.0f) < 0.01f);

	/* Test 3: L/R average — mono = (L+R)/2 in DC case */
	ds_state_t s3 = {0};
	float last_y3 = 0.0f;
	for (int i = 0; i < 60; i++) {
		float y = ds_tick(&s3, 0.5f, 1.5f);  /* avg = 1.0 */
		if (!isnan(y))
			last_y3 = y;
	}
	assert(fabsf(last_y3 - 1.0f) < 0.01f);

	/* --- Ring buffer --- */

	/* Test 4: ring_get_patch returns false until PATCH_SAMPLES written */
	ring_t r = {0};
	float patch[PATCH_SAMPLES];
	assert(!ring_get_patch(&r, patch));

	for (uint32_t i = 0; i < PATCH_SAMPLES; i++)
		ring_push(&r, (float)i);

	assert(ring_get_patch(&r, patch));
	assert(patch[0] == 0.0f);
	assert(patch[PATCH_SAMPLES - 1] == (float)(PATCH_SAMPLES - 1));

	/* Test 5: hop_pending counter */
	ring_t r2 = {0};
	for (uint32_t i = 0; i < HOP_SAMPLES; i++)
		ring_push(&r2, 0.5f);
	assert(r2.hop_pending == (uint32_t)HOP_SAMPLES);

	/* Test 6: ring_reset_hop clears hop_pending */
	ring_reset_hop(&r2);
	assert(r2.hop_pending == 0);

	/* Test 7: ring_get_patch exercises the split path (wrap in middle of patch) */
	ring_t r3 = {0};
	/* Push RING_CAP + (RING_CAP - PATCH_SAMPLES) + 1 samples so that
	* write % RING_CAP = RING_CAP - PATCH_SAMPLES + 1 = 15601 > 15600,
	* forcing the two-segment copy path in ring_get_patch */
	uint32_t split_push = RING_CAP + (RING_CAP - PATCH_SAMPLES) + 1;
	for (uint32_t i = 0; i < split_push; i++)
		ring_push(&r3, (float)(i % 100));
	assert(r3.write % RING_CAP > RING_CAP - PATCH_SAMPLES);  /* verify split path is hit */
	assert(ring_get_patch(&r3, patch));
	/* Verify first and last samples of the patch are correct */
	/* start = write - PATCH_SAMPLES = 46801 - 15600 = 31201 */
	/* s0 = 31201 % 31200 = 1 */
	/* patch[0] should be ring.buf[1] = value at push index (RING_CAP+1) % 100 = 1 */
	assert(patch[0] == (float)(((uint64_t)RING_CAP + 1) % 100));

	/* --- Energy gate --- */

	/* Test 8: silence is below floor */
	float silence[PATCH_SAMPLES];
	memset(silence, 0, sizeof(silence));
	assert(patch_energy(silence, PATCH_SAMPLES) < ENERGY_FLOOR);

	/* Test 9: loud signal is above floor */
	float loud[PATCH_SAMPLES];
	for (uint32_t i = 0; i < PATCH_SAMPLES; i++)
		loud[i] = 0.1f;
	assert(patch_energy(loud, PATCH_SAMPLES) >= ENERGY_FLOOR);

	printf("PASS: yamnet_dsp\n");
	return 0;
}
