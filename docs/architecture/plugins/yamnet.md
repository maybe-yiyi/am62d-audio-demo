# yamnet plugin

URI: `urn:am62d:yamnet`

The YAMNet plugin classifies audio events in real time using Google's [YAMNet](https://tfhub.dev/google/yamnet/1) model running under TensorFlow Lite. It accepts mono audio at 48 kHz, downsamples to 16 kHz internally, and writes per-bucket confidence scores to 10 control output ports. It also publishes the same scores as JSON to the `"yamnet"` data stream.

## Port layout

| Index | Symbol | Direction | Type | Notes |
|---|---|---|---|---|
| 0 | `in` | Input | Audio | mono, 48 kHz |
| 1 | `speech` | Output | Control | score 0.0–1.0 |
| 2 | `alert` | Output | Control | score 0.0–1.0 |
| 3 | `laughter` | Output | Control | score 0.0–1.0 |
| 4 | `crowd` | Output | Control | score 0.0–1.0 |
| 5 | `hvac` | Output | Control | score 0.0–1.0 |
| 6 | `ext_noise` | Output | Control | score 0.0–1.0 |
| 7 | `door` | Output | Control | score 0.0–1.0 |
| 8 | `music` | Output | Control | score 0.0–1.0 |
| 9 | `typing` | Output | Control | score 0.0–1.0 |
| 10 | `applause` | Output | Control | score 0.0–1.0 |

## Signal path

The signal processing flow follows these steps:
1. Input: 48 kHz mono audio
2. Apply ds_tick(): mono mix + 5-tap FIR + 3:1 decimation
3. Output: 16 kHz samples → ring buffer (31200-sample capacity)
4. Every HOP_SAMPLES = 7800 samples (0.4875 s):
   - Extract last PATCH_SAMPLES = 15600 samples (0.975 s)
   - Apply energy gate (RMS < 1e-6 → drop)
5. Send patch to inference thread via patch_queue_t (2-slot SPSC)
6. Inference thread processes with TFLite: 521-class scores
7. Apply bucket mapping: max per bucket
8. Store result in result_queue_t (2-slot SPSC)
9. Audio thread retrieves result from result_queue_t
10. Copy scores to ctrl_buf
11. Publish JSON via am62d_publish("yamnet", json)
12. Write ctrl_buf to control output ports (unconditional — runs every run() call)

### Downsampling

Each 48 kHz sample is mixed to mono and passed through `ds_tick`. The downsampler uses a 5-tap symmetric FIR filter (Hamming window, fc = 7.2 kHz) followed by 3:1 decimation. For the 2 out of every 3 input samples that fall between decimation steps, `ds_tick` returns `NaN`. Only non-`NaN` outputs are pushed to the ring buffer.

The FIR coefficients are `{0.0201, 0.2309, 0.4980, 0.2309, 0.0201}` (derived from `scipy.signal.firwin(5, 7200, fs=48000, window='hamming')`).

### Patch extraction and energy gate

The ring buffer has capacity `RING_CAP = 2 × PATCH_SAMPLES = 31200` samples. The plugin tracks how many new samples have accumulated since the last hop (`hop_pending`). When `hop_pending >= HOP_SAMPLES = 7800`, it extracts the most recent 15600 samples as a patch candidate (50% overlap between consecutive patches).

Patches with RMS below `ENERGY_FLOOR = 1e-6` are discarded without inference. This avoids wasting compute on silence.

### SPSC queues

Two lock-free single-producer/single-consumer queues (capacity 2) connect the audio thread to the inference thread:

- `patch_queue_t`: audio thread pushes patches, inference thread pops them.
- `result_queue_t`: inference thread pushes results, audio thread pops them.

If `patch_queue_t` is full when the audio thread attempts a push, the patch is dropped. If `result_queue_t` is full when the inference thread attempts a push, the result is dropped. Neither operation blocks.

### Inference

The inference thread waits on a POSIX semaphore. Each `sem_post` from the audio thread corresponds to one patch push. The thread pops the patch, copies it into the TFLite input tensor, invokes the interpreter, and reads all 521 output scores.

521 AudioSet class scores are mapped to 10 named buckets by a static lookup table initialized in `yamnet_init_class_buckets`. The bucket score is the maximum raw score among all member classes. Classes not assigned to any bucket (table entry `0xFF`) are ignored.

The 10 buckets and a representative sample of their member AudioSet classes:

| JSON label | LV2 port symbol | Example classes |
|---|---|---|
| `speech` | `speech` | Speech, Conversation, Whispering |
| `alert` | `alert` | Alarm, Siren, Doorbell, Smoke detector |
| `laugh` | `laughter` | Laughter, Giggle, Belly laugh |
| `crowd` | `crowd` | Crowd, Chatter, Hubbub |
| `hvac` | `hvac` | Air conditioning, Mechanical fan, White noise |
| `noise` | `ext_noise` | Rain, Traffic, Engine, Static |
| `door` | `door` | Door, Sliding door, Slam |
| `music` | `music` | Music, Background music, Theme music |
| `typing` | `typing` | Typing, Computer keyboard, Clicking |
| `applause` | `applause` | Applause, Clapping, Cheering |

### Result delivery

In the `run()` call after inference completes, `result_try_pop` returns a `result_t`. The plugin:

1. Copies all 10 bucket scores into `ctrl_buf`.
2. Publishes a JSON payload to the `"yamnet"` data stream:

```json
{"labels":["speech","alert","laugh","crowd","hvac","noise","door","music","typing","applause"],
 "scores":[0.842,0.001,0.000,0.003,0.012,0.034,0.000,0.001,0.000,0.000]}
```

The labels array is always present and always in bucket-index order. Consumers may parse by index rather than by name lookup.

3. Writes all scores from `ctrl_buf` to their corresponding control output ports. This happens on every `run()` call (not only when a new result is available), so ports always reflect the most recent inference result.

## Model file

The TFLite model is loaded from `<bundle_path>/yamnet.tflite` at `instantiate` time, where `bundle_path` is the plugin directory passed by the framework. The plugin fails instantiation if:

- The file does not exist or cannot be read.
- The interpreter cannot be created.
- The model's input tensor last dimension does not equal `PATCH_SAMPLES = 15600`.

## Timing

At 48 kHz with a 512-sample PipeWire block:

- Each `run()` processes 512 samples, producing ~171 downsampled output samples.
- A hop of 7800 downsampled samples accumulates over approximately 45 `run()` calls (~23 ms).
- Inference runs on every hop (subject to the energy gate), so the maximum result rate is approximately 43 classifications per second.
- Inference latency from the inference thread is one hop window behind the audio thread. Result values lag by up to one hop (~23 ms) beyond the audio that triggered them.

## Real-time safety

All audio-thread operations are lock-free. The `run()` function:

- Does not allocate memory.
- Does not take any mutex.
- Uses only `patch_try_push`, `result_try_pop`, `sem_post` (non-blocking), and `am62d_publish` (non-blocking write to a FIFO).

The inference thread is the only thread that calls TFLite and is the only consumer of `patch_queue_t`.
