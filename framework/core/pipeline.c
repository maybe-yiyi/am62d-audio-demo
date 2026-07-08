#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/pipewire.h>

#include "a53_node.h"
#include "config.h"
#include "param_bus.h"
#include "pipeline.h"
#include "registry.h"

struct pipeline *pipewire_setup()
{
	struct pipeline *pl = calloc(1, sizeof(struct pipeline));

	pw_init(NULL, NULL);

	pl->loop = pw_main_loop_new(NULL);
	pl->context = pw_context_new(pw_main_loop_get_loop(pl->loop), NULL, 0);
	pl->core = pw_context_connect(pl->context, NULL, 0);
	if (!pl->core) {
		fprintf(stderr, "pipeline: failed to connect to PipeWire daemon\n");
		pw_context_destroy(pl->context);
		pw_main_loop_destroy(pl->loop);
		free(pl);
		return NULL;
	}

	return pl;
}

static uint32_t find_node_id(struct pipeline *pl, const char *config_id)
{
	for (int i = 0; i < pl->n_node_ids; i++)
		if (strcmp(pl->node_ids[i].config_id, config_id) == 0)
			return pl->node_ids[i].pw_node_id;
	return SPA_ID_INVALID;
}

static uint32_t find_port_id(struct pipeline *pl, uint32_t pw_node_id, const char *port_name)
{
	for (int i = 0; i < pl->n_port_ids; i++)
		if (pl->port_ids[i].pw_node_id == pw_node_id &&
			strcmp(pl->port_ids[i].port_name, port_name) == 0)
			return pl->port_ids[i].pw_port_id;
	return SPA_ID_INVALID;
}

static struct a53_node *pipeline_find_node(struct pipeline *pl, const char *config_id)
{
	for (int i = 0; i < pl->n_nodes; i++)
		if (strcmp(pl->config->nodes[i].id, config_id) == 0)
			return pl->nodes[i];
	return NULL;
}

static void pipeline_wire_control_links(struct pipeline *pl)
{
	for (int i = 0; i < pl->config->n_ctrl_links; i++) {
		const struct control_link_config *clc = &pl->config->ctrl_links[i];

		char src_node_id[128];
		char src_port_name[128];
		if (sscanf(clc->from, "%127[^:]:%127s", src_node_id, src_port_name) != 2) {
			fprintf(stderr, "pipeline: malformed control link 'from': %s\n",
				clc->from);
			continue;
		}

		struct a53_node *src = pipeline_find_node(pl, src_node_id);
		struct a53_node *dst = pipeline_find_node(pl, clc->to);
		if (!src || !dst) {
			fprintf(stderr, "pipeline: control link failed: %s -> %s\n",
				clc->from, clc->to);
			continue;
		}

		if (param_bus_register(src, src_port_name, dst, clc->param) < 0) {
			fprintf(stderr, "pipeline: control link failed: %s -> %s.%s\n",
				clc->from, clc->to, clc->param);
			continue;
		}
		printf("Registered control %s -> %s.%s\n", clc->from, clc->to, clc->param);
	}
}

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

static void pipeline_record_node_id(struct pipeline *pl, const char *config_id, uint32_t pw_id)
{
	if (pl->n_node_ids >= MAX_NODES)
		return;

	snprintf(pl->node_ids[pl->n_node_ids].config_id,
		sizeof(pl->node_ids[0].config_id), "%s", config_id);

	pl->node_ids[pl->n_node_ids].pw_node_id = pw_id;
	pl->n_node_ids++;
}

static void pipeline_record_port_id(struct pipeline *pl, uint32_t pw_node_id,
	const char *port_name, uint32_t pw_port_id)
{
	if (pl->n_port_ids >= MAX_NODE_PORTS)
		return;

	snprintf(pl->port_ids[pl->n_port_ids].port_name,
		sizeof(pl->port_ids[0].port_name), "%s", port_name);

	pl->port_ids[pl->n_port_ids].pw_node_id = pw_node_id;
	pl->port_ids[pl->n_port_ids].pw_port_id = pw_port_id;
	pl->n_port_ids++;
}

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
		printf("- Recieved port %s\n", port_name);
	}
}

static const struct pw_registry_events registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = on_global,
};

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
			pipeline_wire_control_links(pl);
			pipeline_create_links(pl);
			break;
		default:
			break;
		}
	}
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = on_core_done,
};

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

struct pipeline *pipeline_create(const char *config_path, const char *plugin_dir)
{
	struct pipeline *pl = pipewire_setup();
	if (!pl)
		return NULL;

	pl->config = config_load(config_path);
	printf("Loading configuration %s\n", pl->config->name);

	registry_init(plugin_dir);

	for (int i = 0; i < pl->config->n_nodes; i++) {
		struct node_config node_conf = pl->config->nodes[i];
		printf("Loading node %s\n", node_conf.id);

		LilvInstance *instance = registry_get(node_conf.plugin);
		const LilvPlugin *plugin = registry_get_plugin(node_conf.plugin);

		const char *linked_ports[MAX_LINKS * 2];
		int n_linked_ports = collect_linked_ports(pl->config, node_conf.id,
						linked_ports, MAX_LINKS * 2);

		struct a53_node *a53_node = a53_node_create(pl->core, registry_world(),
						plugin, instance, node_conf.id,
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

static void on_signal(void *data, int sig)
{
	struct pw_main_loop *loop = data;
	pw_main_loop_quit(loop);
}

void pipeline_run(struct pipeline *pl)
{
	printf("Running main loop...\n");
	pw_loop_add_signal(pw_main_loop_get_loop(pl->loop), SIGINT, on_signal, pl->loop);
	pw_loop_add_signal(pw_main_loop_get_loop(pl->loop), SIGTERM, on_signal, pl->loop);

	pw_main_loop_run(pl->loop);
}

void pipeline_destroy(struct pipeline *pl)
{
	for (int i = 0; i < pl->n_nodes; i++)
		a53_node_destroy(pl->nodes[i]);

	registry_destroy();

	pw_proxy_destroy((struct pw_proxy *)pl->registry);
	pw_core_disconnect(pl->core);
	pw_context_destroy(pl->context);
	pw_main_loop_destroy(pl->loop);
	pw_deinit();

	config_free(pl->config);

	free(pl);
}
