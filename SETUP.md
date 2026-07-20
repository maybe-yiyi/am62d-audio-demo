# Setup

This project cross-compiles for the AM62D (AArch64). You need the TI Processor
SDK for the cross-compiler toolchain and a populated sysroot (e.g., from Yocto)
for target libraries and headers.

## Prerequisites

**Host machine (x86-64 Linux)**

- [Meson](https://mesonbuild.com/) >= 0.57, Ninja
- TI Processor SDK for AM62xx
  (`ti-processor-sdk-linux-am62xx-evm-12.00.00.07.04`) provides the
  cross-compiler (`aarch64-oe-linux-gcc`, etc.)
- A sysroot at `~/rootfs/` (or similar) containing the target libraries listed
  below (typically built via Yocto) containing:

**Required libraries in the sysroot**

| Library | pkg-config name | Used by |
|---|---|---|
| PipeWire | `libpipewire-0.3` | `am62d-pipeline` binary |
| Lilv | `lilv-0` | `am62d-pipeline` binary |

**Optional libraries (plugins are skipped if absent)**

| Library | pkg-config name | Plugin |
|---|---|---|
| WebRTC Audio Processing | `webrtc-audio-processing-2` | `webrtc` |
| TensorFlow Lite | `tensorflow2-lite` | `yamnet` |

Meson skips a plugin automatically when its dependency is not found. No manual
intervention needed.

## Configure

An example `arm.txt.example` Meson cross-file is already present and configured
for the TI Processor SDK and sysroot at the paths shown below.

To configure, copy `arm.txt.example` to `arm.txt` and replace `<SDK>` and
`<SYSROOT>` with your absolute paths.

Then setup `build/`:

```sh
meson setup build --cross-file arm.txt
```

**Common options**

```sh
# Build only specific plugins (default: all)
meson setup build --cross-file arm.txt -Dplugins=passthrough,webrtc,downmix

# Bundle the YAMNet model file into the install tree
meson setup build --cross-file arm.txt -Dyamnet_model=yamnet.tflite

# Override the maximum channel count for webrtc/downmix (default: 8)
meson setup build --cross-file arm.txt -Dmax_channels=4
```

## Build

In `build/`:

```sh
meson compile
```

## Deploy

Install into a local staging directory, then copy to the board:

```sh
DESTDIR=/tmp/staging meson install
scp -r /tmp/staging/usr root@<board-ip>:/
```

Files are at `/usr/lib/am62d/` on the board.

## Run

On the board, start PipeWire if it is not already running, then launch the pipeline with a config file:

```sh
./am62d-pipeline --config /usr/lib/am62d/configs/demo.json --plugin-dir /usr/lib/am62d/plugins
```

To monitor pipeline output (YAMNet classification scores and WebRTC noise metrics), run `am62d-term` in a second terminal on the board:

```sh
./am62d-term
```

Both `"webrtc"` and `"yamnet"` must be listed in the `data_streams` array of the active config for `am62d-term` to receive events. The tool opens both FIFOs unconditionally and blocks until the pipeline creates them.
