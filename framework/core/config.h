#ifndef CONFIG_H
#define CONFIG_H

#define MAX_NODES 16
#define MAX_LINKS 32
#define MAX_CTRL_LINKS 32
#define MAX_CHANNELS 8
#define MAX_DATA_STREAMS 8

/**
 * struct node_config - configuration for a single node
 * @id: node identifier from configuration
 * @plugin: LV2 plugin URI to load for this node
 */
struct node_config {
	char id[64];
	char plugin[128];
};

/**
 * struct link_config - configuration for a link between nodes
 * @from: source port identifier (format: "node_id.port_name")
 * @to: destination port identifier (format: "node_id.port_name")
 */
struct link_config {
	char from[128];
	char to[128];
};

/**
 * struct control_link_config - configuration for a control link
 * @from: source parameter identifier (format: "node_id.parameter_name")
 * @to: destination parameter identifier (format: "node_id.parameter_name")
 * @param: LV2 parameter property URI
 */
struct control_link_config {
	char from[128];
	char to[64];
	char param[64];
};

/**
 * struct pipeline_config - complete pipeline configuration
 * @name: name of the pipeline
 * @nodes: array of node configurations
 * @n_nodes: number of nodes in configuration
 * @links: array of link configurations
 * @n_links: number of links in configuration
 * @ctrl_links: array of control link configurations
 * @n_ctrl_links: number of control links in configuration
 * @data_streams: array of data stream identifiers
 * @n_data_streams: number of data streams
 * @json: raw JSON configuration (for reference/debugging)
 */
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

/**
 * config_load() - load pipeline configuration from JSON file
 * @path: path to JSON configuration file
 *
 * Loads and parses a JSON configuration file defining the pipeline
 * structure including nodes, links, and control links.
 *
 * Return: pointer to pipeline_config structure, or NULL on failure
 */
struct pipeline_config *config_load(const char *path);

/**
 * config_free() - free pipeline configuration resources
 * @conf: pointer to pipeline_config structure to free
 *
 * Frees all memory associated with the pipeline configuration,
 * including the underlying JSON structure.
 *
 * Return: None
 */
void config_free(struct pipeline_config *conf);

#endif
