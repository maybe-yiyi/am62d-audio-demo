#ifndef PIPELINE_H
#define PIPELINE_H

#include <pipewire/pipewire.h>
#include <lilv/lilv.h>

#include "a53_node.h"
#include "config.h"

#define MAX_NODE_PORTS 256

struct node_id_entry {
	char config_id[128];
	uint32_t pw_node_id;
};

struct port_id_entry {
	uint32_t pw_node_id;
	char port_name[128];
	uint32_t pw_port_id;
};

enum sync_phase {
	SYNC_PHASE_WAIT_REGISTRY = 0,
	SYNC_PHASE_CREATE_LINKS = 1,
};

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

struct pipeline *pipeline_create(const char *config_path, const char *plugin_dir);
void pipeline_run(struct pipeline *pl);
void pipeline_destroy(struct pipeline *pl);

#endif /* PIPELINE_H */
