#ifndef A53_NODE_H
#define A53_NODE_H

#include <pipewire/pipewire.h>

#include "am62d_plugin.h"

#define MAX_PORTS 8
#define MAX_CTRL_ROUTES 8

struct port_data {
	/* intentionally empty */
};

struct ctrl_route {
	int ctrl_out_idx;
	struct a53_node *target;
	char param_key[64];
};

struct a53_node {
	struct pw_filter *filter;
	struct spa_hook filter_listener;
	const struct am62d_plugin *plugin;
	void *priv;
	struct port_data *in_ports[MAX_PORTS];
	struct port_data *out_ports[MAX_PORTS];
	int n_in;
	int n_out;

	struct am62d_data_buf *meta_in[MAX_PORTS];
	struct am62d_data_buf *meta_out[MAX_PORTS];
	int n_meta_in;
	int n_meta_out;

	struct ctrl_route ctrl_in_routes[MAX_CTRL_ROUTES];
	float ctrl_out_vals[MAX_PORTS];
	int n_ctrl_in;
	int n_ctrl_out;
};

struct a53_node *a53_node_create(struct pw_core *core,
				 const struct am62d_plugin *plugin,
				 const char *node_id,
				 const struct am62d_param *params,
				 int n_params);
void a53_node_destroy(struct a53_node *node);

#endif
