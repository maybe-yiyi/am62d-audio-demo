#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <spa/support/plugin.h>

#define MAX_PLUGINS 16
#define SPA_HANDLE_FACTORY_ENUM_FUNC_NAME "spa_handle_factory_enum"

static struct {
	void *handle;
	const struct spa_handle_factory *factory;
} cache[MAX_PLUGINS];

static int cache_count = 0;
static char plugin_dir[PATH_MAX];

int registry_init(const char *dir)
{
	if (!dir)
		return -1;

	snprintf(plugin_dir, sizeof(plugin_dir), "%s", dir);
	cache_count = 0;
	return 0;
}

const struct spa_handle_factory *registry_get(const char *name)
{
	for (int i = 0; i < cache_count; i++)
		if (strcmp(cache[i].factory->name, name) == 0)
			return cache[i].factory;

	char path[PATH_MAX];
	int n = snprintf(path, sizeof(path), "%s/libam62d_%s.so", plugin_dir, name);
	if (n >= (int)sizeof(path)) {
		fprintf(stderr, "registry: path too long for plugin '%s'\n", name);
		goto exit;
	}

	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		fprintf(stderr, "registry: dlopen %s: %s\n", path, dlerror());
		goto exit;
	}

	spa_handle_factory_enum_func_t enum_func = dlsym(handle, SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
	if (!enum_func) {
		fprintf(stderr, "registry: %s is not a valid SPA plugin (no %s symbol)\n",
			name, SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
		goto close_handle;
	}

	const struct spa_handle_factory *factory = NULL;
	uint32_t index = 0;
	while (enum_func(&factory, &index) > 0) {
		if (strcmp(factory->name, name) == 0) {
			if (cache_count >= MAX_PLUGINS) {
				fprintf(stderr, "registry: plugin cache full\n");
				goto close_handle;
			}

			cache[cache_count].handle = handle;
			cache[cache_count].factory = factory;
			cache_count++;
			return factory;
		}
	}

	fprintf(stderr, "registry: factory '%s' not found in plugin\n", name);

close_handle:
	dlclose(handle);
exit:
	return NULL;
}

void registry_destroy(void)
{
	for (int i = 0; i < cache_count; i++) {
		dlclose(cache[i].handle);
		cache[i].handle = NULL;
		cache[i].factory = NULL;
	}
	cache_count = 0;
}
