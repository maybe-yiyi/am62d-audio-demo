#ifndef CONFIG_H
#define CONFIG_H

#include "am62d_plugin.h"

#define MAX_NODES 16
#define MAX_LINKS 32
#define MAX_NODE_PARAMS 64

struct node_config {
	char id[64];
	char plugin[64];
	struct cJSON *params;
	struct am62d_param typed_params[MAX_NODE_PARAMS];
	int n_params;
};

struct link_config {
	char from[128];
	char to[128];
};

struct pipeline_config {
	char name[64];
	struct node_config nodes[MAX_NODES];
	int n_nodes;
	struct link_config links[MAX_LINKS];
	int n_links;
	struct cJSON *json;
};

struct pipeline_config *config_load(const char *path);
void config_free(struct pipeline_config *conf);

#endif
