#ifndef AM62D_PLUGIN_H
#define AM62D_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

#define AM62D_ABI_MAGIC	0x41363244
#define AM62D_ABI_MAJOR	1
#define AM62D_ABI_MINOR	0

#define AM62D_PLUGIN_EXPORT __attribute__((visibility("default")))

enum am62d_param_type {
	AM62D_PARAM_INT,
	AM62D_PARAM_FLOAT,
	AM62D_PARAM_STRING,
};

struct am62d_param {
	const char *key;
	enum am62d_param_type type;
	union {
		int32_t i;
		float f;
		const char *s;
	} v;
};

#define AM62D_DATA_VAD_RESULT		0x01

struct am62d_data_buf {
	uint64_t seq;
	uint32_t type_tag;
	uint32_t payload_size;
	uint8_t payload[];
};

static inline void am62d_buf_commit(struct am62d_data_buf *buf)
{
	__atomic_fetch_add(&buf->seq, 1, __ATOMIC_RELEASE);
}

static inline uint64_t am62d_buf_seq(const struct am62d_data_buf *buf)
{
	return __atomic_load_n(&buf->seq, __ATOMIC_ACQUIRE);
}

struct am62d_vad_result {
	float voice_probability;
	uint8_t is_voiced;
	uint8_t _pad[3];
	uint64_t frame_idx;
};

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
		struct {
			uint32_t payload_size;
			uint32_t type_tag;
		} meta;
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

	int (*init)(void **priv, const struct am62d_param *params, int n_params);
	void (*destroy)(void *priv);
	int (*process)(void *priv, const float **in, float **out,
		       uint32_t n_frames,
		       struct am62d_data_buf *const *in_meta,
		       struct am62d_data_buf **out_meta,
		       float *out_ctrl);
	int (*set_control)(void *priv, const char *key, float value);
};

extern const struct am62d_plugin AM62D_PLUGIN_ENTRY;

#endif /* AM62D_PLUGIN_H */
