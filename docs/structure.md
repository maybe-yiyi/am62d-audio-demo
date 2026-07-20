# Project Structure

## Table of Contents

1. [Framework](#framework)
1. [Plugins](#plugins)
1. [Configs](#configs)
1. [Demos](#demos)
1. [Tests](#tests)
1. [Third Party](#third-party)

## Framework

Framework code is in `framework/core/`. The core components are:

- `a53_node` is the LV2-to-PipeWire bridge. It wraps a single LV2 plugin
  instance in a `pw_filter`, wires DSP buffers to LV2 ports each cycle, and
  calls the plugin's `run` callback.
- `pipeline` is the central coordinator. Owns the PipeWire connection,
  instantiates plugin nodes, and wires them together according to the JSON
  config.
- `config` is the JSON config loader. Parses the pipeline topology from a JSON
  file.
- `publish` is the FIFO data stream publisher. Provides a non-blocking channel
  for plugins to emit JSON results to external consumers.

For detailed behavioral documentation, see [Core Components Overview](architecture/core.md).

## Plugins

Plugin code is found in `plugins/`.

## Configs

This folder (`configs/`) contains the JSON files for pipelines. For the config
JSON spec, see [How to write a new config](how-to/new-config.md).

## Demos

In `demos/`, applications related to the pipeline, but not necessarily dependent
on the core pipeline. Technically, these don't even need to be in the project,
but they're here for demonstration purposes.

## Tests

Tests are located in the `tests/` folder. Currently, only tests for plugins
exist, but more tests can be added.

## Third Party

The `third_party/` folder contains the cJSON source code. Since the library is
lightweight, it's much easier to include the library natively than require it as
a dependency.
