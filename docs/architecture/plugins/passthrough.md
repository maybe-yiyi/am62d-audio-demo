# passthrough plugin

URI: `urn:am62d:passthrough`

The passthrough plugin copies two stereo channels (L and R) from input to output without modification. It has no internal state, no control ports, and produces no data stream output. Its primary use is as a minimal test fixture to verify PipeWire connectivity.

## Port layout

| Index | Symbol | Direction | Type |
|---|---|---|---|
| 0 | `in_l` | Input | Audio |
| 1 | `out_l` | Output | Audio |
| 2 | `in_r` | Input | Audio |
| 3 | `out_r` | Output | Audio |

All four ports are required (none are `connectionOptional`).

## Processing

Each channel is copied with `memcpy`. If either port in a channel pair is `NULL`, that channel is skipped silently:

```c
if (p->in_l && p->out_l)
    memcpy(p->out_l, p->in_l, n_samples * sizeof(float));
if (p->in_r && p->out_r)
    memcpy(p->out_r, p->in_r, n_samples * sizeof(float));
```

## Constraints

- The plugin does not support more than two channels. Use multiple `passthrough` instances or a different plugin for higher channel counts.
- L and R channels are processed independently. Connecting only one pair and leaving the other unlinked is valid; the unconnected channel is silently skipped.
