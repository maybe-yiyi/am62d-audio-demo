#ifndef AM62D_PLUGIN_H
#define AM62D_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

#define AM62D_ABI_MAGIC	0x41363244
#define AM62D_ABI_MAJOR	0
#define AM62D_ABI_MINOR	1

#define AM62D_PLUGIN_EXPORT __attribute__((visibility("default")))

enum am62d_executor {
	AM62D_EXEC_A53,
	AM62D_EXEC_C7X,
	AM62D_EXEC_MMA
};

enum am62d_port_type {
	AM62D_PORT_AUDIO_PCM,
	AM62D_PORT_AUDIO_SPECTRUM,
	AM62D_PORT_METADATA,
	AM62D_PORT_CONTROL
};

enum am62d_port_dir {
	AM62D_DIR_IN,
	AM62D_DIR_OUT
};

struct am62d_port_desc {
	const char *name;
	enum am62d_port_type type;
	enum am62d_port_dir dir;
	union {
		struct {
			uint32_t n_channels;
		} pcm; /* pcm and spectrum */
		/* metadata and control, no params */
	} u;
};

struct am62d_plugin {
	uint32_t abi_magic;
	uint32_t abi_major;
	uint32_t abi_minor;
	const char *name;

	enum am62d_executor executor;
	const struct am62d_port_desc *ports;
	uint32_t n_ports;

	int (*init)(void **priv, const struct cJSON *config_json);
	void (*destroy)(void *priv);
	int (*process)(void *priv, const float **in, float **out,
		       uint32_t n_frames);
	int (*set_param)(void *priv, const char *key, const struct cJSON *value_json);
	int (*get_param)(void *priv, const char *key, struct cJSON *out_json,
			 uint32_t out_len);
};

extern const struct am62d_plugin AM62D_PLUGIN_ENTRY;

#endif /* AM62D_PLUGIN_H */
