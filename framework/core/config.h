#ifndef CONFIG_H
#define CONFIG_H

#define MAX_NODES 16
#define MAX_LINKS 32
#define MAX_CTRL_LINKS 32
#define MAX_CHANNELS 8
#define MAX_DATA_STREAMS 8

struct node_config {
	char id[64];
	char plugin[128];
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
	struct node_config nodes[MAX_NODES];
	int n_nodes;
	struct link_config links[MAX_LINKS];
	int n_links;
	struct control_link_config ctrl_links[MAX_CTRL_LINKS];
	int n_ctrl_links;
	char data_streams[MAX_DATA_STREAMS][32];
	int n_data_streams;
	struct cJSON *json;
};

struct pipeline_config *config_load(const char *path);
void config_free(struct pipeline_config *conf);

#endif
