#include <stdio.h>
#include <string.h>

#include <lilv/lilv.h>

#include "registry.h"

#define MAX_PLUGINS 16

static const LilvPlugin *cache[MAX_PLUGINS];
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

LilvWorld *registry_world(void)
{
	return world;
}

const LilvPlugin *registry_get_plugin(const char *uri)
{
	for (int i = 0; i < cache_count; i++) {
		const char *cached = lilv_node_as_uri(
			lilv_plugin_get_uri(cache[i]));
		if (strcmp(cached, uri) == 0)
			return cache[i];
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

	cache[cache_count++] = plugin;
	return plugin;
}

void registry_destroy(void)
{
	cache_count = 0;

	if (world) {
		lilv_world_free(world);
		world = NULL;
	}
}
