#ifndef A53_NODE_H
#define A53_NODE_H

#include <pipewire/pipewire.h>
#include <lilv/lilv.h>

#define MAX_PORTS 8
#define MAX_CTRL_PORTS 16

/**
 * struct port_data - port data placeholder
 *
 * Placeholder structure for port data, currently empty but reserved
 * for future extension to hold port-specific data buffers or metadata.
 */
struct port_data {
	/* intentionally empty */
};

/**
 * struct a53_node - LV2 audio node wrapper
 * @filter: pipewire filter representing the LV2 plugin
 * @filter_listener: filter event listener
 * @plugin: LV2 plugin descriptor
 * @instance: LV2 plugin instance
 * @in_ports: array of input port data
 * @out_ports: array of output port data
 * @in_port_indices: indices of connected input ports
 * @out_port_indices: indices of connected output ports
 * @n_in: number of input ports
 * @n_out: number of output ports
 * @ctrl_bufs: buffer for control port values
 * @n_ctrl: number of control ports
 */
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

/**
 * a53_node_create() - create and initialize an LV2 audio node
 * @core: pipewire core instance
 * @world: LV2 world instance
 * @plugin_uri: URI of the LV2 plugin to load
 * @node_name: desired name for the pipewire node
 * @linked_ports: array of port names to link (format: "input_port,output_port")
 * @n_linked_ports: number of linked port pairs
 *
 * Return: pointer to initialized a53_node structure, or NULL on failure
 */
struct a53_node *a53_node_create(struct pw_core *core,
				 LilvWorld *world,
				 const char *plugin_uri,
				 const char *node_name,
				 const char **linked_ports,
				 int n_linked_ports);

/**
 * a53_node_destroy() - destroy LV2 audio node and free resources
 * @node: pointer to a53_node structure to destroy
 *
 * Return: None
 */
void a53_node_destroy(struct a53_node *node);

#endif
