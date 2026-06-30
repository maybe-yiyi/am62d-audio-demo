#include <stdio.h>
#include <string.h>

#include <lilv/lilv.h>

#define MAX_PLUGINS 16

static struct {
	LilvInstance *instance;
	const LilvPlugin *plugin;
} cache[MAX_PLUGINS];

static int cache_count = 0;
static LilvWorld *world = NULL;
static char plugin_bundle_path[512];

int registry_init(const char *dir)
{
	if (!dir)
		return -1;

	snprintf(plugin_bundle_path, sizeof(plugin_bundle_path), "file://%s/", dir);

	world = lilv_world_new();
	if (!world)
		return -1;

	LilvNode *bundle_uri = lilv_new_uri(world, plugin_bundle_path);
	lilv_world_load_bundle(world, bundle_uri);
	lilv_node_free(bundle_uri);

	cache_count = 0;
	return 0;
}

LilvInstance *registry_get(const char *uri)
{
	for (int i = 0; i < cache_count; i++) {
		const char *cached_uri = lilv_node_as_uri(
			lilv_plugin_get_uri(cache[i].plugin));
		if (strcmp(cached_uri, uri) == 0)
			return cache[i].instance;
	}

	if (cache_count >= MAX_PLUGINS) {
		fprintf(stderr, "registry: plugin cache full\n");
		return NULL;
	}

	LilvNode *uri_node = lilv_new_uri(world, uri);
	const LilvPlugins *plugins = lilv_world_get_all_plugins(world);
	const LilvPlugin *plugin = lilv_plugins_get_by_uri(plugins, uri_node);
	lilv_node_free(uri_node);

	if (!plugin) {
		fprintf(stderr, "registry: plugin '%s' not found\n", uri);
		return NULL;
	}

	LilvInstance *instance = lilv_plugin_instantiate(plugin, 48000.0, NULL);
	if (!instance) {
		fprintf(stderr, "registry: failed to instantiate '%s'\n", uri);
		return NULL;
	}

	cache[cache_count].instance = instance;
	cache[cache_count].plugin = plugin;
	cache_count++;

	return instance;
}

void registry_destroy(void)
{
	for (int i = 0; i < cache_count; i++) {
		lilv_instance_free(cache[i].instance);
		cache[i].instance = NULL;
		cache[i].plugin = NULL;
	}
	cache_count = 0;

	if (world) {
		lilv_world_free(world);
		world = NULL;
	}
}
