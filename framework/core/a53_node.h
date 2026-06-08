#ifndef A53_NODE_H
#define A53_NODE_H

#include <pipewire/pipewire.h>

#include "am62d_plugin.h"

#define MAX_PORTS 8

struct port_data {
	/* intentionally empty */
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
};

struct a53_node *a53_node_create(struct pw_core *core,
				 const struct am62d_plugin *plugin,
				 const char *config_json);
void a53_node_destroy(struct a53_node *node);

#endif
