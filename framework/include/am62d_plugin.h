#ifndef AM62D_PLUGIN_H
#define AM62D_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

#define AM62D_PLUGIN_EXPORT __attribute__((visibility("default")))

enum am62d_param_type {
	AM62D_PARAM_INT,
	AM62D_PARAM_FLOAT,
	AM62D_PARAM_INT64,
	AM62D_PARAM_STRING,
};

struct am62d_param {
	const char *key;
	enum am62d_param_type type;
	union {
		int32_t i;
		float f;
		int64_t i64;
		const char *s;
	} v;
};

enum am62d_executor {
	AM62D_EXEC_A53,
	AM62D_EXEC_C7X,
	AM62D_EXEC_MMA
};

enum am62d_port_type {
	AM62D_PORT_AUDIO_PCM,
	AM62D_PORT_AUDIO_SPECTRUM,
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
		} pcm;
	} u;
};

typedef int (*am62d_init_fn)(void **priv,
                             const struct am62d_param *params, int n_params);
typedef void (*am62d_destroy_fn)(void *priv);
typedef int (*am62d_process_fn)(void *priv,
                                const float **in, float **out, uint32_t n_frames,
                                const struct am62d_param *in_params, int n_in_params,
                                struct am62d_param *out_params, int *n_out_params);
typedef int (*am62d_set_control_fn)(void *priv, const char *key, float value);

#endif /* AM62D_PLUGIN_H */
