# downmix plugin

URI: `urn:am62d:downmix`

The downmix plugin averages up to 8 mono input channels into a single mono output channel. It has no internal state, no control ports, and produces no data stream output.

## Port layout

| Index | Symbol | Direction | Type | Notes |
|---|---|---|---|---|
| 0 | `in_ch0` | Input | Audio | required |
| 1–7 | `in_ch1` … `in_ch7` | Input | Audio | `connectionOptional` |
| 8 | `out` | Output | Audio | required |

Only ports present in the pipeline config `links` are activated by the framework. Unlinked optional ports receive `NULL` from `connect_port`.

## Processing

The active channel count `n_active` is determined at the start of each `run()` call by counting consecutive non-NULL input buffer pointers:

```c
int n = 0;
while (n < AM62D_MAX_CHANNELS && p->in_bufs[n])
    n++;
p->n_active = n;
```

For each sample position, the plugin sums all active channels and divides by `n_active`:

```c
for (uint32_t s = 0; s < n_samples; s++) {
    float sum = 0.0f;
    for (int ch = 0; ch < p->n_active; ch++)
        sum += p->in_bufs[ch][s];
    p->out[s] = sum / (float)p->n_active;
}
```

If no inputs are connected (`n_active == 0`), the output buffer is zeroed.

## Constraints

- The plugin does not normalize by a fixed channel count. Mixing two channels produces a signal at the same level as one channel, not 6 dB quieter. If level preservation is required, apply a gain stage downstream.
- `n_active` is recomputed every `run()`. A sudden change in connected ports between calls produces a discontinuity in the output signal.
