#ifndef A53_NODE_H
#define A53_NODE_H

#include <pipewire/pipewire.h>
#include <spa/support/plugin.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/buffer/buffer.h>

#include "am62d_plugin.h"

#define MAX_PORTS 16

struct port_data {
	/* intentionally empty */
};

/* Per-port state for one SPA port */
struct a53_port {
	uint32_t spa_port_id;
	enum spa_direction dir;
	bool is_audio;           /* false = control/notify */

	/* audio ports only */
	uint32_t n_channels;
	struct port_data *pw_port;
	struct spa_data spa_data;
	struct spa_chunk spa_chunk;
	struct spa_buffer spa_buf;
	struct spa_buffer *spa_buf_ptr; /* points to spa_buf above */
	struct spa_io_buffers io_buf;
};

struct a53_node {
	struct pw_filter *filter;
	struct spa_hook filter_listener;

	struct spa_handle *spa_handle;
	struct spa_node *spa_node;
	struct spa_hook spa_listener;

	struct a53_port ports[MAX_PORTS];
	int n_ports;

	struct spa_io_sequence *control_in;  /* pointer into PW-provided area */
	struct spa_io_sequence *notify_out;  /* pointer into PW-provided area */
	struct spa_io_position position;
};

struct a53_node *a53_node_create(struct pw_core *core,
				 const struct spa_handle_factory *factory,
				 const char *node_id,
				 const struct am62d_param *params,
				 int n_params);
void a53_node_destroy(struct a53_node *node);

#endif
