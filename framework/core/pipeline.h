#ifndef PIPELINE_H
#define PIPELINE_H

#include <pipewire/pipewire.h>

#include "a53_node.h"

#define MAX_NODES 16
#define MAX_NODE_PORTS 64
#define MAX_LINKS 32

struct node_id_entry {
	char config_id[64];
	uint32_t pw_node_id;
};

struct port_id_entry {
	uint32_t pw_node_id;
	char port_name[64];
	uint32_t pw_port_id;
};

struct link_desc {
	char from[128];
	char to[128];
};

struct pipeline {
	struct pw_main_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_registry *registry;
	struct spa_hook registry_listener;
	struct spa_hook core_listener;
	int sync_seq;
	int sync_phase;

	struct a53_node *nodes[MAX_NODES];
	int n_nodes;
	struct link_desc links[MAX_LINKS];
	int n_links;

	struct node_id_entry node_ids[MAX_NODES];
	int n_node_ids;
	struct port_id_entry port_ids[MAX_NODE_PORTS];
	int n_port_ids;
};

struct pipeline *pipeline_create(const char *config_path);
void pipeline_run(struct pipeline *pl);
void pipeline_destroy(struct pipeline *pl);

#endif /* PIPELINE_H */
