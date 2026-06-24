#ifndef A53_NODE_H
#define A53_NODE_H

#include <pipewire/pipewire.h>
#include <spa/support/plugin.h>
#include <spa/node/node.h>
#include <spa/node/io.h>

#include "am62d_plugin.h"

#define MAX_PORTS 16

struct port_data {
	/* intentionally empty */
};

struct a53_node {
	struct pw_filter *filter;
	struct spa_hook filter_listener;

	struct spa_handle *spa_handle;
	struct spa_node *spa_node;

	struct port_data *pw_ports[MAX_PORTS];
	struct spa_buffer *spa_buffers[MAX_PORTS];
	struct spa_io_buffers io_buffers[MAX_PORTS];

	struct spa_io_sequence control_in;
	struct spa_io_sequence notify_out;
	struct spa_io_position position;

	uint8_t control_in_data[4096];
	uint8_t notify_out_data[4096];

	int n_ports;
};

struct a53_node *a53_node_create(struct pw_core *core,
				 const struct spa_handle_factory *factory,
				 const char *node_id,
				 const struct am62d_param *params,
				 int n_params);
void a53_node_destroy(struct a53_node *node);

#endif
