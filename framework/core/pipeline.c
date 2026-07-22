#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <lilv/lilv.h>

#include "a53_node.h"
#include "config.h"
#include "pipeline.h"
#include "publish.h"

/**
 * pipewire_setup() - initialize pipewire context and lv2 world
 *
 * Allocates and initializes pipeline structure with pipewire context,
 * core connection, and lv2 world.
 *
 * Return: initialized pipeline structure or NULL on failure
 */
static struct pipeline *pipewire_setup(void)
{
	struct pipeline *pl = calloc(1, sizeof(struct pipeline));

	pw_init(NULL, NULL);

	pl->loop = pw_main_loop_new(NULL);
	pl->context = pw_context_new(pw_main_loop_get_loop(pl->loop), NULL, 0);
	pl->core = pw_context_connect(pl->context, NULL, 0);
	if (!pl->core) {
		fprintf(stderr, "pipeline: failed to connect to PipeWire daemon\n");
		goto err;
	}

	pl->lv2_world = lilv_world_new();
	if (!pl->lv2_world) {
		fprintf(stderr, "pipeline: failed to create LV2 world\n");
		goto err;
	}

	return pl;

err:
	pw_context_destroy(pl->context);
	pw_main_loop_destroy(pl->loop);
	free(pl);
	return NULL;
}

/**
 * find_node_id() - find pipewire node id by config id
 * @pl: pipeline instance
 * @config_id: node identifier from configuration
 *
 * Searches node id table for matching config id and returns
 * corresponding pipewire node id.
 *
 * Return: pipewire node id or SPA_ID_INVALID if not found
 */
static uint32_t find_node_id(struct pipeline *pl, const char *config_id)
{
	for (int i = 0; i < pl->n_node_ids; i++)
		if (strcmp(pl->node_ids[i].config_id, config_id) == 0)
			return pl->node_ids[i].pw_node_id;
	return SPA_ID_INVALID;
}

/**
 * find_port_id() - find pipewire port id by node and port name
 * @pl: pipeline instance
 * @pw_node_id: pipewire node id
 * @port_name: port name from configuration
 *
 * Searches port id table for matching node and port name combination
 * and returns corresponding pipewire port id.
 *
 * Return: pipewire port id or SPA_ID_INVALID if not found
 */
static uint32_t find_port_id(struct pipeline *pl, uint32_t pw_node_id, const char *port_name)
{
	for (int i = 0; i < pl->n_port_ids; i++)
		if (pl->port_ids[i].pw_node_id == pw_node_id &&
			strcmp(pl->port_ids[i].port_name, port_name) == 0)
			return pl->port_ids[i].pw_port_id;
	return SPA_ID_INVALID;
}

/**
 * pipeline_create_links() - create links between node ports
 * @pl: pipeline instance
 *
 * Iterates through link configuration and creates pipewire links
 * between output ports of source nodes and input ports of destination nodes.
 *
 * Return: None
 */
static void pipeline_create_links(struct pipeline *pl)
{
	for (int i = 0; i < pl->config->n_links; i++) {
		struct link_config link = pl->config->links[i];

		char from_node[128];
		char from_port[128];
		char to_node[128];
		char to_port[128];

		if (sscanf(link.from, "%127[^:]:%127s", from_node, from_port) != 2 ||
			sscanf(link.to, "%127[^:]:%127s", to_node, to_port) != 2) {
			fprintf(stderr, "pipeline: malformed link '%s' -> '%s'\n",
				link.from, link.to);
			continue;
		}

		uint32_t out_node = find_node_id(pl, from_node);
		uint32_t out_port = find_port_id(pl, out_node, from_port);
		uint32_t in_node = find_node_id(pl, to_node);
		uint32_t in_port = find_port_id(pl, in_node, to_port);

		if (out_node == SPA_ID_INVALID || out_port == SPA_ID_INVALID ||
			in_node == SPA_ID_INVALID || in_port == SPA_ID_INVALID) {
			fprintf(stderr, "pipeline: could not resolve link '%s' -> '%s'\n",
				link.from, link.to);
			continue;
		}

		char s_out_node[16], s_out_port[16], s_in_node[16], s_in_port[16];
		snprintf(s_out_node, sizeof(s_out_node), "%u", out_node);
		snprintf(s_out_port, sizeof(s_out_port), "%u", out_port);
		snprintf(s_in_node,  sizeof(s_in_node),  "%u", in_node);
		snprintf(s_in_port,  sizeof(s_in_port),  "%u", in_port);

		printf("Linking '%s' -> '%s'\n", link.from, link.to);
		pw_core_create_object(pl->core, "link-factory",
			PW_TYPE_INTERFACE_Link, PW_VERSION_LINK,
			&SPA_DICT_INIT_ARRAY(((struct spa_dict_item[4]) {
				SPA_DICT_ITEM_INIT(PW_KEY_LINK_OUTPUT_NODE, s_out_node),
				SPA_DICT_ITEM_INIT(PW_KEY_LINK_OUTPUT_PORT, s_out_port),
				SPA_DICT_ITEM_INIT(PW_KEY_LINK_INPUT_NODE,  s_in_node),
				SPA_DICT_ITEM_INIT(PW_KEY_LINK_INPUT_PORT,  s_in_port),
			})),
			0);
	}
}

/**
 * pipeline_record_node_id() - store node id mapping
 * @pl: pipeline instance
 * @config_id: node identifier from configuration
 * @pw_id: pipewire node id
 *
 * Stores mapping between config node id and pipewire node id
 * in the node ids table for later reference.
 *
 * Return: None
 */
static void pipeline_record_node_id(struct pipeline *pl, const char *config_id, uint32_t pw_id)
{
	if (pl->n_node_ids >= MAX_NODES)
		return;

	snprintf(pl->node_ids[pl->n_node_ids].config_id,
		sizeof(pl->node_ids[0].config_id), "%s", config_id);

	pl->node_ids[pl->n_node_ids].pw_node_id = pw_id;
	pl->n_node_ids++;
}

/**
 * pipeline_record_port_id() - store port id mapping
 * @pl: pipeline instance
 * @pw_node_id: pipewire node id
 * @port_name: port name from configuration
 * @pw_port_id: pipewire port id
 *
 * Stores mapping between node/port names and pipewire port id
 * in the port ids table for later reference.
 *
 * Return: None
 */
static void pipeline_record_port_id(struct pipeline *pl, uint32_t pw_node_id,
	const char *port_name, uint32_t pw_port_id)
{
	if (pl->n_port_ids >= MAX_NODE_PORTS) {
		fprintf(stderr, "pipeline: port table full, dropping port '%s'\n", port_name);
		return;
	}

	snprintf(pl->port_ids[pl->n_port_ids].port_name,
		sizeof(pl->port_ids[0].port_name), "%s", port_name);

	pl->port_ids[pl->n_port_ids].pw_node_id = pw_node_id;
	pl->port_ids[pl->n_port_ids].pw_port_id = pw_port_id;
	pl->n_port_ids++;
}

/**
 * on_global() - pipewire registry global callback
 * @data: pipeline instance
 * @id: object id
 * @permissions: object permissions
 * @type: object type
 * @version: object version
 * @props: object properties
 *
 * Called by pipewire when global objects (nodes, ports) appear.
 * Records node and port ids for later use in linking.
 *
 * Return: None
 */
static void on_global(void *data, uint32_t id, uint32_t permissions,
		const char *type, uint32_t version,
		const struct spa_dict *props)
{
	struct pipeline *pl = data;

	if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
		const char *node_name = spa_dict_lookup(props, "node.name");
		if (node_name)
			pipeline_record_node_id(pl, node_name, id);
	}

	if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
		const char *node_id = spa_dict_lookup(props, PW_KEY_NODE_ID);
		const char *port_name = spa_dict_lookup(props, PW_KEY_PORT_NAME);
		if (node_id && port_name)
			pipeline_record_port_id(pl, atoi(node_id), port_name, id);
		printf("- Received port %s\n", port_name);
	}
}

/**
 * registry_events - pipewire registry event handlers
 * @global: global object callback
 *
 * Static initialization of pipewire registry event handlers.
 */
static const struct pw_registry_events registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = on_global,
};

/**
 * on_core_done() - pipewire core sync callback
 * @data: pipeline instance
 * @id: object id
 * @seq: sequence number
 *
 * Called by pipewire when sync completes. Advances synchronization
 * phase from waiting for registry to creating links.
 *
 * Return: None
 */
static void on_core_done(void *data, uint32_t id, int seq)
{
	struct pipeline *pl = data;

	if (seq == pl->sync_seq) {
		printf("Received sync %d\n", pl->sync_phase);
		switch (pl->sync_phase) {
		case SYNC_PHASE_WAIT_REGISTRY:
			pl->sync_seq = pw_core_sync(pl->core, PW_ID_CORE, 1);
			pl->sync_phase = SYNC_PHASE_CREATE_LINKS;
			break;
		case SYNC_PHASE_CREATE_LINKS:
			pipeline_create_links(pl);
			break;
		default:
			break;
		}
	}
}

/**
 * core_events - pipewire core event handlers
 * @done: sync completion callback
 *
 * Static initialization of pipewire core event handlers.
 */
static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = on_core_done,
};

/**
 * collect_linked_ports() - collect port names to link for a node
 * @config: pipeline configuration
 * @node_id: node identifier
 * @out: output array for port names
 * @max_out: maximum number of ports to collect
 *
 * Scans link configuration to find all ports that should be
 * linked for the specified node.
 *
 * Return: number of ports collected
 */
static int collect_linked_ports(struct pipeline_config *config, const char *node_id,
		const char *out[], int max_out)
{
	int n = 0;
	for (int i = 0; i < config->n_links; i++) {
		char link_node[128];
		char link_port[128];

		if (n < max_out &&
			sscanf(config->links[i].from, "%127[^:]:%127s", link_node, link_port) == 2 &&
			strcmp(link_node, node_id) == 0) {
			out[n++] = config->links[i].from + strlen(link_node) + 1;
		}

		if (n < max_out &&
			sscanf(config->links[i].to, "%127[^:]:%127s", link_node, link_port) == 2 &&
			strcmp(link_node, node_id) == 0) {
			out[n++] = config->links[i].to + strlen(link_node) + 1;
		}
	}
	return n;
}

/**
 * pipeline_create() - create and initialize pipeline
 * @config_path: path to configuration file
 * @plugin_dir: directory containing lv2 plugins
 *
 * Creates a new pipeline instance by:
 * - Setting up pipewire context and lv2 world
 * - Loading configuration from JSON file
 * - Initializing data stream publishing
 * - Loading lv2 plugins
 * - Creating audio nodes
 * - Setting up registry and core listeners
 *
 * Return: initialized pipeline structure or NULL on failure
 */
struct pipeline *pipeline_create(const char *config_path, const char *plugin_dir)
{
	struct pipeline *pl = pipewire_setup();
	if (!pl)
		return NULL;

	pl->config = config_load(config_path);
	printf("Loading configuration %s\n", pl->config->name);

	const char *ds_names[MAX_DATA_STREAMS];
	for (int i = 0; i < pl->config->n_data_streams; i++)
		ds_names[i] = pl->config->data_streams[i];
	publish_init(ds_names, pl->config->n_data_streams);

	lilv_world_load_all(pl->lv2_world);
	if (plugin_dir) {
		LilvNode *bundle_uri = lilv_new_file_uri(pl->lv2_world, NULL, plugin_dir);
		lilv_world_load_bundle(pl->lv2_world, bundle_uri);
		lilv_node_free(bundle_uri);
	}

	for (int i = 0; i < pl->config->n_nodes; i++) {
		struct node_config node_conf = pl->config->nodes[i];
		printf("Loading node %s\n", node_conf.id);

		const char *linked_ports[MAX_LINKS * 2];
		int n_linked_ports = collect_linked_ports(pl->config, node_conf.id,
						linked_ports, MAX_LINKS * 2);

		struct a53_node *a53_node = a53_node_create(pl->core, pl->lv2_world,
						node_conf.plugin, node_conf.id,
						linked_ports, n_linked_ports);
		if (!a53_node)
			return NULL;
		pl->nodes[pl->n_nodes++] = a53_node;
	}

	pl->registry = pw_core_get_registry(pl->core, PW_VERSION_REGISTRY, 0);

	pw_registry_add_listener(pl->registry, &pl->registry_listener, &registry_events, pl);

	pw_core_add_listener(pl->core, &pl->core_listener, &core_events, pl);
	pl->sync_seq = pw_core_sync(pl->core, PW_ID_CORE, pl->sync_seq);

	return pl;
}

/**
 * on_signal() - signal handler for pipeline shutdown
 * @data: pipewire main loop
 * @sig: signal number
 *
 * Handles SIGINT and SIGTERM by quitting the pipewire main loop.
 *
 * Return: None
 */
static void on_signal(void *data, int sig)
{
	struct pw_main_loop *loop = data;
	pw_main_loop_quit(loop);
}

/**
 * pipeline_run() - run pipeline main loop
 * @pl: pipeline instance
 *
 * Sets up signal handlers and runs the pipewire main loop
 * until interrupted by SIGINT or SIGTERM.
 *
 * Return: None
 */
void pipeline_run(struct pipeline *pl)
{
	printf("Running main loop...\n");
	pw_loop_add_signal(pw_main_loop_get_loop(pl->loop), SIGINT, on_signal, pl->loop);
	pw_loop_add_signal(pw_main_loop_get_loop(pl->loop), SIGTERM, on_signal, pl->loop);

	pw_main_loop_run(pl->loop);
}

/**
 * pipeline_destroy() - destroy pipeline and free resources
 * @pl: pipeline instance
 *
 * Cleans up all resources associated with pipeline:
 * - Destroys all audio nodes
 * - Frees lv2 world
 * - Destroys pipewire objects (registry, core, context, loop)
 * - Deinitializes pipewire
 * - Destroys publishing resources
 * - Frees configuration
 * - Frees pipeline structure
 *
 * Return: None
 */
void pipeline_destroy(struct pipeline *pl)
{
	for (int i = 0; i < pl->n_nodes; i++)
		a53_node_destroy(pl->nodes[i]);

	lilv_world_free(pl->lv2_world);

	pw_proxy_destroy((struct pw_proxy *)pl->registry);
	pw_core_disconnect(pl->core);
	pw_context_destroy(pl->context);
	pw_main_loop_destroy(pl->loop);
	pw_deinit();

	publish_destroy();

	config_free(pl->config);

	free(pl);
}
