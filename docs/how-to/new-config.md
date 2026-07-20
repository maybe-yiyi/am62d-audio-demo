# How to write a new config

Config files are JSON documents that describe the pipeline topology: which
plugins to instantiate, how to connect them, and which data streams to publish.

## Required fields

Every config must contain:

- `name` — identifier for this configuration (e.g., "demo", "basic")
- `data_streams` — array of data stream names to subscribe to for external publishing
- `nodes` — array of plugin instances, each with:
  - `id` — node identifier used in link specifications and PipeWire graph
  - `plugin` — LV2 plugin URI (defined in the plugin's descriptor)
- `links` — array of connections, each with:
  - `from` — source in `node_id:port_symbol` format
  - `to` — sink in `node_id:port_symbol` format

The `id` field in each node is arbitrary but must be unique within the config.
The `plugin` URI must match an installed LV2 plugin's `LV2_Descriptor.URI`.

Port symbols in link specifications come from the `lv2:symbol` field declared in
the plugin's `.ttl` file. The framework will fail to start if a link references
an undefined node or port.

## Example: peak dBFS monitor

A pipeline that computes the peak dBFS over a five-second sliding window using a
hypothetical `urn:am62d:peak` plugin (mono input, publishes to a data stream):

```json
{
  "name": "example",
  "data_streams": ["peak"],
  "nodes": [
    {
      "id": "peak",
      "plugin": "urn:am62d:peak"
    }
  ],
  "links": [
    {
      "from": "mic:capture",
      "to": "peak:in"
    }
  ]
}
```

The node ID `"peak"` is arbitrary. The plugin URI `"urn:am62d:peak"` must match
the installed plugin. The link assumes `mic:capture` is the hardware input node
created by the framework and that the plugin declares an `lv2:symbol "in"` port.
