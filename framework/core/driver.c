#include <string.h>
#include "driver.h"

static const struct driver_info drivers[] = {
	{
		.name           = "alsa",
		.lib_path       = SPA_PLUGIN_DIR "/alsa/libspa-alsa.so",
		.source_factory = "api.alsa.pcm.source",
		.sink_factory   = "api.alsa.pcm.sink",
	},
};

const struct driver_info *driver_lookup(const char *name)
{
	for (size_t i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
		if (strcmp(drivers[i].name, name) == 0)
			return &drivers[i];
	}
	return NULL;
}
