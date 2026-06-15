#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "am62d_plugin.h"

#define MAX_PLUGINS 16

static struct {
	void *handle;
	const struct am62d_plugin *plugin;
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

const struct am62d_plugin *registry_get(const char *name)
{
	for (int i = 0; i < cache_count; i++)
		if (strcmp(cache[i].plugin->name, name) == 0)
			return cache[i].plugin;

	char path[PATH_MAX];
	int n = snprintf(path, sizeof(path), "%s/libam62d_%s.so", plugin_dir, name);
	if (n >= (int)sizeof(path)) {
		fprintf(stderr, "registry: path too long for plugin '%s'\n",
			name);
		goto exit;
	}

	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		fprintf(stderr, "registry: dlopen %s: %s\n", path, dlerror());
		goto exit;
	}

	const struct am62d_plugin *p = dlsym(handle, "AM62D_PLUGIN_ENTRY");
	if (!p)
		goto close_handle;

	if (cache_count >= MAX_PLUGINS) {
		fprintf(stderr, "registry: plugin cache full\n");
		goto close_handle;
	}

	if (p->abi_magic != AM62D_ABI_MAGIC) {
		fprintf(stderr, "registry: %s is not an am62d plugin\n", name);
		goto close_handle;
	}
	if (p->abi_major != AM62D_ABI_MAJOR) {
		fprintf(stderr, "registry: %s major ABI version mismatch: got %u, expected %u\n",
			name, p->abi_major, AM62D_ABI_MAJOR);
		goto close_handle;
	}
	if (p->abi_minor > AM62D_ABI_MINOR) {
		fprintf(stderr, "registry: %s minor ABI version mismatch: got %u, expected %u\n",
			name, p->abi_minor, AM62D_ABI_MINOR);
		goto close_handle;
	}

	if (strcmp(p->name, name) != 0) {
		fprintf(stderr, "registry: expected '%s', plugin declares '%s'\n",
			name, p->name);
		goto close_handle;
	}

	cache[cache_count].handle = handle;
	cache[cache_count].plugin = p;
	cache_count++;
	return p;

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
		cache[i].plugin = NULL;
	}
	cache_count = 0;
}
