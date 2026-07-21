#ifndef A53_NODE_H
#define A53_NODE_H

#include <pipewire/pipewire.h>
#include <lilv/lilv.h>

#define MAX_PORTS 8
#define MAX_CTRL_PORTS 16

struct port_data {
	/* intentionally empty */
};

struct a53_node {
	struct pw_filter *filter;
	struct spa_hook filter_listener;
	const LilvPlugin *plugin;
	LilvInstance *instance;

	struct port_data *in_ports[MAX_PORTS];
	struct port_data *out_ports[MAX_PORTS];
	uint32_t in_port_indices[MAX_PORTS];
	uint32_t out_port_indices[MAX_PORTS];
	int n_in;
	int n_out;

	float ctrl_bufs[MAX_CTRL_PORTS];
	int n_ctrl;
};

struct a53_node *a53_node_create(struct pw_core *core,
				 LilvWorld *world,
				 const char *plugin_uri,
				 const char *node_name,
				 const char **linked_ports,
				 int n_linked_ports);
void a53_node_destroy(struct a53_node *node);

#endif
