#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <pipewire/pipewire.h>

#include "a53_node.h"
#include "cJSON.h"
#include "pipeline.h"
#include "registry.h"

struct pipeline *pipewire_setup()
{
	struct pipeline *pl = calloc(1, sizeof(struct pipeline));

	pw_init(NULL, NULL);

	pl->loop = pw_main_loop_new(NULL);
	pl->context = pw_context_new(pw_main_loop_get_loop(pl->loop), NULL, 0);
	pl->core = pw_context_connect(pl->context, NULL, 0);

	return pl;
}

const struct cJSON *load_config(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);

	char *buf = malloc(len + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	fread(buf, 1, len, f);
	fclose(f);
	buf[len] = '\0';

	const struct cJSON *config = cJSON_Parse(buf);
	free(buf);

	return config;
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

static void pipeline_create_links(struct pipeline *pl)
{
	for (int i = 0; i < pl->n_links; i++) {
		char from_node[64];
		char from_port[64];
		char to_node[64];
		char to_port[64];

		if (sscanf(pl->links[i].from, "%63[^:]:%63s", from_node, from_port) != 2 ||
			sscanf(pl->links[i].to, "%63[^:]:%63s", to_node, to_port) != 2) {
			fprintf(stderr, "pipeline: malformed link '%s' -> '%s'\n",
				pl->links[i].from, pl->links[i].to);
			continue;
		}

		uint32_t out_node = find_node_id(pl, from_node);
		uint32_t out_port = find_port_id(pl, out_node, from_port);
		uint32_t in_node = find_node_id(pl, to_node);
		uint32_t in_port = find_port_id(pl, in_node, to_port);

		if (out_node == SPA_ID_INVALID || out_port == SPA_ID_INVALID ||
			in_node == SPA_ID_INVALID || in_port == SPA_ID_INVALID) {
			fprintf(stderr, "pipeline: could not resolve link '%s' -> '%s'\n",
				pl->links[i].from, pl->links[i].to);
			continue;
		}

		char s_out_node[16], s_out_port[16], s_in_node[16], s_in_port[16];
		snprintf(s_out_node, sizeof(s_out_node), "%u", out_node);
		snprintf(s_out_port, sizeof(s_out_port), "%u", out_port);
		snprintf(s_in_node,  sizeof(s_in_node),  "%u", in_node);
		snprintf(s_in_port,  sizeof(s_in_port),  "%u", in_port);

		printf("Linking '%s' -> '%s'\n", pl->links[i].from, pl->links[i].to);
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
		const char *am62d_id = spa_dict_lookup(props, "am62d.node.id");
		if (am62d_id)
			pipeline_record_node_id(pl, am62d_id, id);
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
		case 0:
			pl->sync_seq = pw_core_sync(pl->core, PW_ID_CORE, 1);
			pl->sync_phase++;
			break;
		case 1:
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

struct pipeline *pipeline_create(const char *config_path, const char *plugin_dir)
{
	const struct cJSON *config = load_config(config_path);

	const struct cJSON *name = cJSON_GetObjectItemCaseSensitive(config, "name");
	if (!cJSON_IsString(name))
		return NULL;
	printf("Loading configuration %s\n", name->valuestring);

	struct pipeline *pl = pipewire_setup();

	registry_init(plugin_dir);

	const struct cJSON *nodes = cJSON_GetObjectItemCaseSensitive(config, "nodes");
	const struct cJSON *node;
	cJSON_ArrayForEach(node, nodes) {
		const struct cJSON *id = cJSON_GetObjectItemCaseSensitive(node, "id");
		if (!cJSON_IsString(id))
			return NULL;
		printf("Loading node %s\n", id->valuestring);

		const struct cJSON *pname = cJSON_GetObjectItemCaseSensitive(node, "plugin");
		if (!cJSON_IsString(pname))
			return NULL;

		printf("- Loading plugin %s\n", pname->valuestring);
		const struct am62d_plugin *plugin = registry_get(pname->valuestring);

		const struct cJSON *node_config = cJSON_GetObjectItemCaseSensitive(node, "config");
		struct a53_node *a53_node = a53_node_create(pl->core, plugin, id->valuestring, node_config);
		if (!a53_node)
			return NULL;
		pl->nodes[pl->n_nodes++] = a53_node;
	}

	const struct cJSON *links = cJSON_GetObjectItemCaseSensitive(config, "links");
	const struct cJSON *link;
	cJSON_ArrayForEach(link, links) {
		const struct cJSON *from = cJSON_GetObjectItemCaseSensitive(link, "from");
		if (!cJSON_IsString(from))
			return NULL;

		const struct cJSON *to = cJSON_GetObjectItemCaseSensitive(link, "to");
		if (!cJSON_IsString(to))
			return NULL;

		snprintf(pl->links[pl->n_links].from, sizeof(pl->links[0].from), "%s", from->valuestring);
		snprintf(pl->links[pl->n_links].to, sizeof(pl->links[0].to), "%s", to->valuestring);
		pl->n_links++;
	}

	struct pw_registry *registry = pw_core_get_registry(pl->core, PW_VERSION_REGISTRY, 0);

	pw_registry_add_listener(registry, &pl->registry_listener, &registry_events, pl);

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
	registry_destroy();

	for (int i = 0; i < pl->n_nodes; i++)
		a53_node_destroy(pl->nodes[i]);

	pw_proxy_destroy((struct pw_proxy *)pl->registry);
	pw_core_disconnect(pl->core);
	pw_context_destroy(pl->context);
	pw_main_loop_destroy(pl->loop);

	free(pl);
}
