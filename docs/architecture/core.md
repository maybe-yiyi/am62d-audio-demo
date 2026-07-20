# AM62D Audio Pipeline Framework - Core Components Overview

This document provides essential context and usage information for the core components that is not evident from the code-level documentation.

## a53_node - LV2 to PipeWire Bridge

**Purpose**: Bridges a single LV2 plugin instance to the PipeWire graph by wrapping a pw_filter, connecting DSP buffers to LV2 ports each process cycle, and invoking the plugin's run callback.

**Key Behavioral Details**:
- **Optional Port Handling**: LV2 ports marked `lv2:connectionOptional` may be left unconnected. If a port symbol does not appear in `linked_ports`, the port is connected to NULL and no PipeWire port is added. If present, a PipeWire port is added normally.
- **Control Port Initialization**: Reads `lv2:default` from TTL for each control port, stores it in `ctrl_bufs[]`, and immediately connects the port to this buffer. Values persist across run() calls.
- **Process Callback** (runs on audio thread, must be real-time safe):
  1. For each input port: get buffer, connect port (use silence_buf if NULL)
  2. For each output port: get buffer, connect port (use scratch_buf if NULL)
  3. Run LV2 instance
- **NULL Buffer Handling**: If pw_filter_get_dsp_buffer returns NULL, plugin receives read-only zero buffer (silence_buf) for inputs and writable discard buffer (scratch_buf) for outputs (both 8192-sample static arrays).
- **Destruction**: Deactivates LV2 instance, destroys PipeWire filter, frees node struct.

**Constraints**:
- MAX_PORTS = 8 (max audio input/output ports per node)
- MAX_CTRL_PORTS = 16 (max control ports per node)
- struct port_data is an opaque handle for pw_filter_get_dsp_buffer
- PipeWire node.name must be unique within graph (uses config id as name)

## Pipeline - Central Coordinator

**Purpose**: Owns PipeWire connection, instantiates plugin nodes, and wires them together according to JSON config.

**Creation Sequence**:
1. pipewire_setup() - connect to PipeWire daemon
2. config_load() - parse JSON config from disk
3. publish_init() - create named FIFOs for data streams
4. For each node: instantiate plugin and create pw_filter
5. pw_core_get_registry() - subscribe to PipeWire graph events
6. pw_registry_add_listener(on_global)
7. pw_core_add_listener(on_core_done)
8. pw_core_sync() - kick off two-phase sync

**Two-Phase Sync**:
- SYNC_PHASE_WAIT_REGISTRY: on_global fires for every Node/Port (records name→ID mappings)
- SYNC_PHASE_CREATE_LINKS: after core.done match, create audio links between nodes

**Node/Port ID Recording**:
- on_global stores mapping from node.name to PipeWire node ID
- For ports: maps (node_id, port_name) to PipeWire port ID
- Tables consulted during link creation to resolve "node_id:port_symbol" strings

**Link Creation**:
- Each link parsed as "from_node:from_port" → "to_node:to_port"
- Looks up numeric IDs, calls pw_core_create_object with link-factory type
- Malformed or unresolved links are skipped with error message

**Running**:
- pipeline_run installs SIGINT/SIGTERM handlers via pw_loop_add_signal
- Blocks until signal received (pw_main_loop_run)
- Signals blocked on main thread with pthread_sigmask(SIG_BLOCK) before pipeline_create
- PipeWire delivers signals safely on event loop thread

**Destruction Order** (reverse of creation):
1. a53_node_destroy for each node
2. lilv_world_free
3. pw_proxy_destroy on registry proxy
4. pw_core_disconnect
5. pw_context_destroy
6. pw_main_loop_destroy
7. pw_deinit
8. publish_destroy
9. config_free
10. free(pl)

**Constraints**:
- MAX_NODES = 16 nodes per pipeline
- MAX_LINKS = 32 audio links per pipeline
- MAX_NODE_PORTS = 256 total tracked port entries
- Link strings must be exactly "node_id:port_symbol" format
- Hardware source/sink nodes (e.g., alsa_audio_source) must exist in PipeWire graph before link creation

## Publish Subsystem - JSON Data Streaming

**Purpose**: Provides non-blocking channel for plugins to emit JSON results to external consumers via POSIX named FIFOs under /tmp/.

**Initialization** (publish_init):
- Creates named FIFOs: /tmp/am62d_<name> for each data stream
- Ignores SIGPIPE globally (prevents termination on writer-with-no-reader)
- File descriptors start at -1; actual opens deferred to first publish call
- If FIFO exists (EEXIST), error ignored

**Publishing** (am62d_publish):
- Writes JSON data + newline to named FIFO (self-delimited line)
- Total write (payload + newline) must not exceed 4096 bytes
- Opens FIFO lazily with O_WRONLY | O_NONBLOCK on first use per channel
- O_NONBLOCK prevents open from blocking if no reader

**Write Error Handling**:
| errno after write | Action |
|-------------------|--------|
| EAGAIN or EWOULDBLOCK | Drop silently (FIFO buffer full) |
| Any other error | Close fd, reset to -1; retry open on next call |
| Partial write | Close fd, reset to -1; retry open on next call |

**Destruction** (publish_destroy):
- Closes all open FIFOs
- Unlinks FIFO paths from filesystem
- Resets internal state

**Constraints**:
- MAX_DATA_STREAMS = 8 named data streams
- FIFO paths: /tmp/am62d_<name> (name max 31 chars; longer names truncated at config load)
- Not thread-safe for simultaneous publish_init calls
- Plugins must call am62d_publish only from run() or audio-thread backgrounds (if plugin ensures channel name constant and no concurrent init/destroy)

**Real-Time Safety Note**: Dropping on EAGAIN means plugin's run() never blocks waiting for slow consumer. Consumer that can't keep up causes dropped metrics, not audio thread stall.