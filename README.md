# AM62D Pipewire Pipeline Framework & Demo

## Background

Currently, embedded audio products are split primarily into two categories. The
first is audio effects, such as compression, reverb, noise reduction, etc. The
second is audio intelligence, such as speaker location detection, voice
recognition, etc.

### Assumptions about the reader

This documentation assumes that the reader is familiar with C, Linux systems
programming, and audio DSP basics. Standard C idioms (`calloc`,
`pthread_create`, `memset`) are not explained. Domain-specific concepts such as
the LV2 plugin lifecycle, PipeWire graph topology, and real-time safe
programming are explained on first use.

## Objective

This project aims to showcase [Pipewire](https://pipewire.org/), an audio server
that greatly improves upon ALSA, reducing latency and adding the ability to
multiprocess audio.

This project sits on top of Pipewire, but also takes advantage of exposed ports
to connect apps, the pipeline, and the hardware.

```
+------------------------------------------------------------------------------+
|                                                                              |
|  APPLICATION                                                                 |
|  - Browser, music player, etc.                                               |
|                                                                              |
|------------------------------------------------------------------------------|
|                                                                              |
|  USERSPACE LAYER                                                             |
|                                                                              |
|  - AM62D Framework (this project):                                           |
|    - WebRTC (NS/HPF/TS)                                                      |
|    - YAMNet (Audio Classification)                                           |
|                                                                              |
|  --------------------------------------------------------------------------  |
|                                                                              |
|  - PipeWire Server                                                           |
|                                                                              |
|------------------------------------------------------------------------------|
|                                                                              |
|  KERNEL SPACE                                                                |
|  - ALSA Drivers                                                              |
|                                                                              |
|------------------------------------------------------------------------------|
|                                                                              |
|  HARDWARE                                                                    |
|  - Codec & McASP                                                             |
|                                                                              |
+------------------------------------------------------------------------------+
```
