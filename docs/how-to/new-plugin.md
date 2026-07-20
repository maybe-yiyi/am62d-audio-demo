# How to write a new plugin

For references, see `yamnet.c` or `webrtc.cpp` for C and C++ plugins
respectively, or the [LV2 Core
Documentation](https://lv2plug.in/c/html/group__lv2core.html) for more
information.

## LV2 API spec

Plugins are written using the LV2 API spec.

### Descriptor

The core of the plugin is the descriptor. The descriptor contains all of the
necessary information for LV2 to run the plugin.

In general, the descriptor will take the following form (snippet taken from
`yamnet.c`):

```c
static const LV2_Descriptor descriptor = {
    .URI = PLUGIN_URI,
    .instantiate = instantiate, // create a plugin instance
    .connect_port = connect_port, // connect ports to memory buffers
    .activate = activate, // start processing
    .run = run, // process audio blocks
    .deactivate = deactivate, // stop processing
    .cleanup = cleanup, // destroy plugin instance
    .extension_data = NULL,
}
```

In addition, plugins need an `lv2_descriptor` function, since this is how [LV2
discovers
functions](https://gitlab.com/lv2/lv2/-/blob/main/include/lv2/core/lv2.h#L372).

In [short](https://gitlab.com/lv2/lv2/-/blob/main/include/lv2/core/lv2.h#L378),

> When it is time to load a plugin (designated by its URI), the host loads the
plugin's library, gets the lv2_descriptor() function from it, and uses this
function to find the LV2_Descriptor for the desired plugin.  Plugins are
accessed by index using values from 0 upwards. This function MUST return NULL
for out of range indices, so the host can enumerate plugins by increasing
`index` until NULL is returned.

Such an `lv2_descriptor()` will typically take on the following form (except
when including multiple plugins in a library):

```c
LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    if (index == 0)
        return &descriptor;
    return NULL;
}
```

## Ports

Every audio path and every parameter value crosses through a port. Ports are
declared twice: in a `.ttl` metadata file that LV2 reads at load time, and in
`connect_port` where the framework links audio buffers to the plugin's internal
pointers.

### Port types

LV2 ports have a direction and a media type:

| Direction | Media | What the buffer is |
|-----------|-------|--------------------|
| `lv2:InputPort` | `lv2:AudioPort` | `const float[]` of length `n_samples` |
| `lv2:OutputPort` | `lv2:AudioPort` | `float[]` of length `n_samples` |
| `lv2:InputPort` | `lv2:ControlPort` | `const float *` to one float |
| `lv2:OutputPort` | `lv2:ControlPort` | `float *` to one float |

### Declaring ports in the TTL file

For every plugin, a `.ttl` file is needed (e.g. `myplugin.ttl`). Each port is a
bracketed node with four required predicates: type classes, `lv2:index`,
`lv2:symbol`, and `lv2:name`. A minimal audio in/out pair:

```turtle
@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<urn:am62d:myplugin>
    a lv2:Plugin ;
    lv2:port [
        a lv2:AudioPort, lv2:InputPort ;
        lv2:index 0 ;
        lv2:symbol "in" ;
        lv2:name "Input"
    ] , [
        a lv2:AudioPort, lv2:OutputPort ;
        lv2:index 1 ;
        lv2:symbol "out" ;
        lv2:name "Output"
    ] .
```

`lv2:symbol` is the machine-readable port name used in config `links` entries
(e.g. `"myplugin:in"`). `lv2:index` is zero-based and must be continuously
indexed. Every index declared in the TTL must have a case in `connect_port`.

### Indices must match the C enum

Define an enum in the `.c` file for all port indices and keep it in sync with
the TTL. If they are different, `connect_port` will bind to the wrong buffer.

```c
enum {
    PORT_IN  = 0,
    PORT_OUT = 1,
};
```

### Optional ports

Ports marked `lv2:connectionOptional` may be left unconnected; the framework
passes `NULL` for them in `connect_port`. Guard against NULL in `run()` before
reading or writing.

Adding `lv2:property lv2:connectionOptional` on ports after the first
input/output port can be useful when you are unsure how many connections will be
added.

```turtle
lv2:port [
    a lv2:AudioPort, lv2:InputPort ;
    lv2:index 1 ;
    lv2:symbol "in_ch1" ;
    lv2:name "Input Ch 1" ;
    lv2:portProperty lv2:connectionOptional
] .
```

```c
static void run(LV2_Handle instance, uint32_t n_samples)
{
    struct priv *p = instance;
    if (p->in_ch1)
        /* process channel 1 */;
}
```

The `downmix` and `webrtc` plugins use this to support a variable number of
channels up to `AM62D_MAX_CHANNELS` without separate plugin variants.

### Control port metadata

Control ports declare `lv2:default`, `lv2:minimum`, and `lv2:maximum`. The
framework uses these to pre-fill control buffers at startup.

```turtle
lv2:port [
    a lv2:ControlPort, lv2:InputPort ;
    lv2:index 2 ;
    lv2:symbol "ns_level" ;
    lv2:name "Noise Suppression Level" ;
    lv2:default 3 ;
    lv2:minimum 0 ;
    lv2:maximum 3
] .
```

Output control ports (such as scores and measurements) also declare these even
though the plugin overwrites them each `run()`:

```turtle
lv2:port [
    a lv2:ControlPort, lv2:OutputPort ;
    lv2:index 1 ;
    lv2:symbol "speech" ;
    lv2:name "Speech Score" ;
    lv2:default 0.0 ; lv2:minimum 0.0 ; lv2:maximum 1.0
] .
```

### Implementing connect_port

Store the pointer directly in the private struct, not the buffer contents. LV2
may call `connect_port` again between `activate` and `run` to rebind buffers.

```c
struct priv {
    const float *in;
    float *out;
    const float *ns_level;
};

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    struct priv *p = instance;
    switch (port) {
    case PORT_IN:
        p->in = data;
        break;
    case PORT_OUT:
        p->out = data;
        break;
    case PORT_NS_LEVEL:
        p->ns_level = data;
        break;
    }
}
```

## Plugin lifecycle

The framework calls the descriptor callbacks in a fixed order. Each callback
has a defined responsibility; doing work outside its intended phase causes bugs
that are hard to reproduce.

1. `instantiate`
1. `connect_port`
1. `activate`
1. `run`
1. `deactivate`
1. `cleanup`

### instantiate

Allocates the private struct and load any external assets (models, config
files). The `bundle_path` argument is the filesystem path to the plugin's
install directory, use it to find files installed alongside the `.so`.

```c
static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
                              double sample_rate,
                              const char *bundle_path,
                              const LV2_Feature *const *features)
{
    struct priv *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;

    char model_path[512];
    snprintf(model_path, sizeof(model_path), "%s/mymodel.bin", bundle_path);
    p->model = load_model(model_path);
    if (!p->model) {
        free(p);
        return NULL;
    }

    return p;
}
```

Return `NULL` on any allocation or load failure. The framework will not call
any other callback if `instantiate` returns `NULL`.

Use `calloc` (or zero-initialize with `new priv{}` in C++) so unconnected
optional port pointers start as `NULL`.

### activate

Initialize runtime state and start background threads. Separate from
`instantiate` because the plugin may be stopped and restarted without being
destroyed: `activate`/`deactivate` can cycle while the struct persists.

```c
static void activate(LV2_Handle instance)
{
    struct priv *p = instance;
    memset(&p->state, 0, sizeof(p->state));
    atomic_store(&p->stop, false);
    sem_init(&p->sem, 0, 0);
    pthread_create(&p->thread, NULL, worker_thread, p);
}
```

`activate` and `deactivate` may be `NULL` in the descriptor if your plugin has
no runtime state to initialize. `passthrough` and `downmix` do this.

### run

Process one block of audio. `n_samples` is the number of frames in each
connected audio buffer this call.

> [!WARNING]
> `run` executes on the audio thread and must be real-time safe. Never
> allocate memory, call `malloc`/`free`, take a mutex, or do any blocking I/O
> inside `run`. Use lock-free structures (SPSC queues, atomics) to communicate
> with background threads.

```c
static void run(LV2_Handle instance, uint32_t n_samples)
{
    struct priv *p = instance;
    if (!p->in || !p->out)
        return;

    for (uint32_t i = 0; i < n_samples; i++)
        p->out[i] = process(p->in[i]);

    /* write scalar result to control output port */
    if (p->result_port)
        *p->result_port = p->last_result;
}
```

For plugins with heavier processing (inference, DSP with internal block sizes),
push data into a lock-free queue and let a background thread consume it, as
`yamnet` does.

### deactivate

Signal background threads to stop and join them. Must leave the struct intact,
since `instantiate` is not called again on restart, only `activate`.

```c
static void deactivate(LV2_Handle instance)
{
    struct priv *p = instance;
    atomic_store(&p->stop, true);
    sem_post(&p->sem);
    pthread_join(p->thread, NULL);
    sem_destroy(&p->sem);
}
```

For C++ plugins that hold RAII objects, reset them here rather than in
`cleanup`:

```cpp
static void deactivate(LV2_Handle instance)
{
    priv *p = static_cast<priv *>(instance);
    p->apm = nullptr; /* releases the scoped_refptr */
}
```

### cleanup

Free the private struct and any resources not released in `deactivate`.

```c
static void cleanup(LV2_Handle instance)
{
    struct priv *p = instance;
    free_model(p->model);
    free(p);
}
```

For C++ plugins use `delete`:

```cpp
static void cleanup(LV2_Handle instance)
{
    delete static_cast<priv *>(instance);
}
```

## Publishing data streams

Plugins that produce structured results (such as scores, metrics, events) can
push JSON to a named data stream. Consumers such as `am62d-term` receive these
messages over a socket.

The API is declared in `framework/core/publish.h`:

```c
void am62d_publish(const char *data_stream, const char *json, size_t len);
```

Call it from `run()` whenever there is a result to report. The call is
non-blocking, because it writes to a named FIFO and returns immediately.

```c
#include "../framework/core/publish.h"

static void run(LV2_Handle instance, uint32_t n_samples)
{
    /* ... process audio ... */

    char json[128];
    int len = snprintf(json, sizeof(json),
        "{\"score\":%.3f}", p->last_score);
    am62d_publish("myplugin", json, (size_t)len);
}
```

The first argument must match a name in the `data_streams` array of the
pipeline config. The framework only subscribes to streams listed there; calls
to undeclared stream names are silently dropped.

```json
{
  "name": "example",
  "data_streams": ["myplugin"],
  ...
}
```

## Building and registering

### TTL manifest

Add two entries to `plugins/manifest.ttl`: one pointing at your shared library
and one pointing at your plugin-specific TTL.

```turtle
<urn:am62d:myplugin>
    a lv2:Plugin ;
    lv2:binary <libam62d_myplugin.so> ;
    rdfs:seeAlso <myplugin.ttl> .
```

The binary name must match what Meson builds (`am62d_<name>` -> `libam62d_<name>.so`).

### meson.build

Add an entry to the `plugin_configs` dict in the top-level `meson.build`. The
`deps` list must contain every `dependency()` the plugin needs. If a
dependency is optional, declare it with `required: false` and Meson will skip
the plugin rather than failing the build.

```meson
'myplugin': {
    'sources': ['plugins/myplugin.c'],
    'deps': [],
},
```

For a plugin with an optional external library:

```meson
mylib_dep = dependency('mylib', required: false)

...

'myplugin': {
    'sources': ['plugins/myplugin.c'],
    'deps': [mylib_dep],
},
```

Also add the plugin TTL file to the `install_data` call near the bottom of
`meson.build`:

```meson
install_data('plugins/manifest.ttl', ..., 'plugins/myplugin.ttl',
  install_dir: '/usr/lib/am62d/plugins')
```

If your plugin installs additional data files (models, lookup tables), use a
separate `install_data` with `install_dir: '/usr/lib/am62d/plugins'` so they
end up in the same bundle directory that `bundle_path` points to at runtime.

### Adding to the plugins option

The `plugins` build option controls which plugins are compiled. Add your
plugin name to the default list in `meson_options.txt` or pass it explicitly
at configure time:

```sh
meson setup build -Dplugins=passthrough,webrtc,downmix,yamnet,myplugin
```

## Testing

Tests live in `tests/plugins/`. The pattern is to `#include` the plugin
source directly rather than linking the shared module, so the test binary
contains the plugin code and can inspect internal state.

```c
#include "../../plugins/myplugin.c" /* pulls in the full implementation */

int main(void)
{
    const LV2_Descriptor *d = lv2_descriptor(0);
    assert(d);

    LV2_Handle h = d->instantiate(d, 48000.0, "/tmp", NULL);
    assert(h);

    float in[64] = {0};
    float out[64];
    d->connect_port(h, PORT_IN, in);
    d->connect_port(h, PORT_OUT, out);

    d->run(h, 64);

    /* assert expected output */

    d->cleanup(h);
    return 0;
}
```

If the plugin calls `am62d_publish`, link `tests/plugins/publish_stub.c`
instead of the real publish implementation; the stub accepts any call and
does nothing, keeping tests self-contained.

Register the test in `tests/plugins/meson.build`:

```meson
test('myplugin',
    executable('test_myplugin', 'test_myplugin.c',
        include_directories: plugin_inc,
        c_args: ['-std=c11']))
```

For plugins with optional dependencies, guard the test the same way the build
is guarded:

```meson
if mylib_dep.found()
    test('myplugin',
        executable('test_myplugin', ['test_myplugin.c', publish_stub],
            include_directories: plugin_inc,
            dependencies: [mylib_dep],
            c_args: ['-std=c11']))
endif
```
