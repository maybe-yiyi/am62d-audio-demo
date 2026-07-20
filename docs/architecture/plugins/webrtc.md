# webrtc plugin

URI: `urn:am62d:webrtc`

The WebRTC plugin applies noise suppression, high-pass filtering, and transient suppression to up to 8 audio channels using the [WebRTC APM](https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_processing/) library. It also applies a post-processing noise gate and publishes per-block audio metrics to the `"webrtc"` data stream.

## Port layout

| Index | Symbol | Direction | Type | Notes |
|---|---|---|---|---|
| 0 | `in_ch0` | Input | Audio | required |
| 1–7 | `in_ch1` … `in_ch7` | Input | Audio | `connectionOptional` |
| 8 | `out_ch0` | Output | Audio | required |
| 9–15 | `out_ch1` … `out_ch7` | Output | Audio | `connectionOptional` |
| 16 | `ns_level` | Input | Control | default 3, range 0–3 |

Only ports that appear in the pipeline config `links` are activated. The active channel count is the number of contiguous non-NULL input/output buffer pairs present at the start of each `run()` call.

## APM configuration

The APM is created with the following settings fixed at build time:

- Noise suppression: enabled, level configurable via `ns_level`
- High-pass filter: enabled
- Transient suppression: enabled
- Echo cancellation (AEC): disabled

The `ns_level` control port maps to WebRTC's `NoiseSuppression::Level` enum:

| `ns_level` value | WebRTC level |
|---|---|
| 0 | `kLow` |
| 1 | `kModerate` |
| 2 | `kHigh` |
| 3 (default) | `kVeryHigh` |

The APM is recreated whenever the active channel count changes between `run()` calls. Recreating the APM resets all internal state, including noise model estimates.

## 480-frame staging buffer

WebRTC APM requires exactly 480 samples per `ProcessStream` call (10 ms at 48 kHz). PipeWire delivers variable block sizes. The plugin maintains per-channel staging buffers `in_stage` and `out_stage` of 480 floats each and implements a consume-produce loop that handles PipeWire blocks of any size:

```
while consumed < n_samples:
    if output is available in out_stage:
        drain to out_bufs up to remaining output demand
    else:
        copy from in_bufs into in_stage until in_stage is full
        when in_stage reaches 480 samples:
            apm->ProcessStream(in_stage, cfg, cfg, out_stage)
            gate_frame(out_stage)
            publish_metrics()
            out_avail = 480
```

This means a single PipeWire `run()` call may invoke `ProcessStream` zero, one, or multiple times depending on block size.

## Noise gate

After each `ProcessStream` call, a per-channel smoothed noise gate is applied to `out_stage`. The gate uses an envelope follower with separate attack and release time constants:

- Threshold: 0.00316 linear amplitude (-50 dBFS)
- Attack coefficient: 0.90 (fast open)
- Release coefficient: 0.05 (slow close)

The gain ramp is applied sample-by-sample within each frame to avoid clicks at gate transitions.

## Metrics

Metrics are published to the `"webrtc"` data stream every `THROTTLE_DIV = 5` APM blocks (~50 ms at 48 kHz / 480 frames). The JSON payload contains raw (pre-APM) and processed (post-APM, post-gate) statistics:

```json
{"raw":  {"rms": -18.2, "peak": -12.1, "floor": -42.0, "snr": 23.8},
 "proc": {"rms": -24.1, "peak": -18.3, "floor": -48.0, "snr": 23.9}}
```

- `rms`: dBFS RMS over the current 480-sample block
- `peak`: dBFS peak over the current block
- `floor`: running minimum RMS over the past `FLOOR_BLOCKS = 192` blocks (~3.84 s); tracked with an O(1) ring-buffer min tracker that rescans only when the minimum slot is overwritten
- `snr`: `rms - floor` in dB (0.0 if rms ≤ floor)

The floor tracker uses separate ring buffers for raw and processed signals. This means the reported SNR reflects the noise suppression gain independently for both paths.

## Real-time safety

The plugin calls `am62d_publish` from inside `run()`. The publish call is non-blocking (see [publish](../core/publish.md)); it drops the write if the FIFO buffer is full rather than stalling the audio thread.

> [!WARNING]
> The APM is heap-allocated via `rtc::scoped_refptr`. APM recreation (on channel-count change) calls the allocator from the audio thread. Avoid frequent channel-count changes at runtime to prevent audio thread stalls.
