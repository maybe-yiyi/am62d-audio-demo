#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#include "am62d_plugin.h"

#define MAX_NODES 16
#define MAX_LINKS 32
#define MAX_CTRL_LINKS 32
#define MAX_NODE_PARAMS 64

struct node_config {
	char id[64];
	char plugin[64];
	char plugin_path[256];
	struct am62d_param typed_params[MAX_NODE_PARAMS];
	int n_params;
};

struct io_config {
	char id[64];
	char driver[32];
	char device[128];
	uint32_t channels;
	uint32_t rate;
};

struct link_config {
	char from[128];
	char to[128];
};

struct control_link_config {
	char from[128];
	char to[64];
	char param[64];
};

struct pipeline_config {
	char name[64];

	struct io_config sources[MAX_NODES];
	int n_sources;

	struct io_config sinks[MAX_NODES];
	int n_sinks;

	struct node_config plugins[MAX_NODES];
	int n_plugins;

	struct link_config links[MAX_LINKS];
	int n_links;

	struct control_link_config ctrl_links[MAX_CTRL_LINKS];
	int n_ctrl_links;

	struct cJSON *json;
};

struct pipeline_config *config_load(const char *path);
void config_free(struct pipeline_config *conf);

#endif /* CONFIG_H */
