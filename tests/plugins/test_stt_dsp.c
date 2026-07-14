#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../../plugins/stt_dsp.h"

int main(void)
{
	stt_ds_state_t ds = {0};
	stt_ring_t ring = {0};
	float patch[STT_PATCH_SAMPLES];
	int produced = 0;

	/* 48000 samples (~1s) of a unit tone, downsampled → 16000. */
	for (int i = 0; i < 48000; i++) {
		float y = stt_ds_tick(&ds, 0.25f);
		if (!isnan(y)) {
			stt_ring_push(&ring, y);
			produced++;
		}
	}

	assert(produced == 16000);
	assert(stt_ring_get_patch(&ring, patch));
	assert(stt_patch_energy(patch, STT_PATCH_SAMPLES) > STT_ENERGY_FLOOR);

	printf("PASS: stt_dsp\n");
	return 0;
}
