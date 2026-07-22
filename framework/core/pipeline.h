#ifndef PIPELINE_H
#define PIPELINE_H

#include <pipewire/pipewire.h>
#include <lilv/lilv.h>

#include "a53_node.h"
#include "config.h"

#define MAX_NODE_PORTS 256

/**
 * struct node_id_entry - joins the config declared id with pipewire's reference
 * @config_id: node id from configuration file
 * @pw_node_id: pipewire assigned node id
 */
struct node_id_entry {
	char config_id[128];
	uint32_t pw_node_id;
};

/**
 * struct port_id_entry - joins node and port names with pipewire port ids
 * @pw_node_id: pipewire node id
 * @port_name: port name from configuration
 * @pw_port_id: pipewire assigned port id
 */
struct port_id_entry {
	uint32_t pw_node_id;
	char port_name[128];
	uint32_t pw_port_id;
};

/**
 * enum sync_phase - pipeline synchronization phases
 * @SYNC_PHASE_WAIT_REGISTRY: waiting for pipewire registry to populate
 * @SYNC_PHASE_CREATE_LINKS: creating links between nodes
 */
enum sync_phase {
	SYNC_PHASE_WAIT_REGISTRY = 0,
	SYNC_PHASE_CREATE_LINKS = 1,
};

/**
 * struct pipeline - main pipeline context
 * @loop: pipewire main loop
 * @context: pipewire context
 * @core: pipewire core
 * @registry: pipewire registry
 * @registry_listener: registry event listener
 * @core_listener: core event listener
 * @sync_seq: synchronization sequence number
 * @sync_phase: current synchronization phase
 * @config: pipeline configuration
 * @nodes: array of audio nodes
 * @n_nodes: number of nodes in pipeline
 * @node_ids: mapping of config ids to pipewire node ids
 * @n_node_ids: number of node id mappings
 * @port_ids: mapping of node/port names to pipewire port ids
 * @n_port_ids: number of port id mappings
 * @lv2_world: LV2 plugin world
 */
struct pipeline {
	struct pw_main_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_registry *registry;
	struct spa_hook registry_listener;
	struct spa_hook core_listener;
	int sync_seq;
	enum sync_phase sync_phase;

	struct pipeline_config *config;

	struct a53_node *nodes[MAX_NODES];
	int n_nodes;

	struct node_id_entry node_ids[MAX_NODES];
	int n_node_ids;
	struct port_id_entry port_ids[MAX_NODE_PORTS];
	int n_port_ids;

	LilvWorld *lv2_world;
};

/**
 * pipeline_create() - create a new pipeline instance
 * @config_path: path to pipeline configuration file
 * @plugin_dir: directory containing LV2 plugins
 *
 * Return: pointer to new pipeline instance, or NULL on failure
 */
struct pipeline *pipeline_create(const char *config_path, const char *plugin_dir);

/**
 * pipeline_run() - run the pipeline main loop
 * @pl: pipeline instance
 *
 * Return: None
 */
void pipeline_run(struct pipeline *pl);

/**
 * pipeline_destroy() - destroy pipeline instance and free resources
 * @pl: pipeline instance
 *
 * Return: None
 */
void pipeline_destroy(struct pipeline *pl);

#endif /* PIPELINE_H */
