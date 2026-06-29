#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdint.h>
#include <spa/support/plugin.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/buffer/buffer.h>
#include <spa/utils/dict.h>

#define MAX_PORTS_PER_NODE 8

struct spa_node_wrapper {
	struct spa_handle *handle;
	struct spa_node *node;
	void *dl_handle;
	char factory_name[64];

	uint32_t n_input_ports;
	uint32_t n_output_ports;
	struct spa_buffer *input_buffers[MAX_PORTS_PER_NODE];
	struct spa_buffer *output_buffers[MAX_PORTS_PER_NODE];

	struct spa_io_buffers in_io[MAX_PORTS_PER_NODE];
	struct spa_io_buffers out_io[MAX_PORTS_PER_NODE];
};

struct spa_node_wrapper *spa_plugin_load(const char *path,
					 const char *factory_name,
					 const struct spa_dict *info,
					 const struct spa_support *support,
					 uint32_t n_support);

int spa_node_configure_ports(struct spa_node_wrapper *wrap, uint32_t sample_rate);
int spa_node_setup_buffers(struct spa_node_wrapper *wrap, uint32_t quantum);

int spa_node_set_clock(struct spa_node_wrapper *wrap,
		       struct spa_io_clock *clock,
		       struct spa_io_position *position);

int spa_node_start(struct spa_node_wrapper *wrap);
int spa_node_stop(struct spa_node_wrapper *wrap);

void spa_plugin_unload(struct spa_node_wrapper *wrap);

#endif /* PLUGIN_H */
