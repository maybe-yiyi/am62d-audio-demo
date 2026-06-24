#ifndef AM62D_SPA_H
#define AM62D_SPA_H

#include "am62d_plugin.h"
#include <spa/support/plugin.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/node/utils.h>
#include <spa/param/param.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/iter.h>
#include <spa/utils/hook.h>
#include <spa/utils/type.h>
#include <spa/utils/string.h>
#include <spa/support/log.h>
#include <spa/control/control.h>
#include <spa/param/props.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define AM62D_MAX_PORTS 16
#define AM62D_MAX_PARAMS 32

/* Encode am62d_param array into a SPA_CONTROL_Properties event in a sequence.
 * The params are encoded using SPA_PROP_params (Struct of (String,Pod) pairs).
 * buf/buf_size must be large enough to hold the pod sequence; writes from offset 0. */
static inline int am62d_params_encode(uint8_t *buf, size_t buf_size,
                                       const struct am62d_param *params, int n_params)
{
	struct spa_pod_builder b;
	struct spa_pod_frame f_seq, f_obj, f_struct;

	spa_pod_builder_init(&b, buf, buf_size);
	spa_pod_builder_push_sequence(&b, &f_seq, 0);
	spa_pod_builder_control(&b, 0, SPA_CONTROL_Properties);
	spa_pod_builder_push_object(&b, &f_obj, SPA_TYPE_OBJECT_Props, 0);
	spa_pod_builder_prop(&b, SPA_PROP_params, 0);
	spa_pod_builder_push_struct(&b, &f_struct);

	for (int i = 0; i < n_params; i++) {
		spa_pod_builder_string(&b, params[i].key);
		switch (params[i].type) {
		case AM62D_PARAM_FLOAT:
			spa_pod_builder_float(&b, params[i].v.f);
			break;
		case AM62D_PARAM_INT:
			spa_pod_builder_int(&b, params[i].v.i);
			break;
		case AM62D_PARAM_INT64:
			spa_pod_builder_long(&b, params[i].v.i64);
			break;
		case AM62D_PARAM_STRING:
			spa_pod_builder_string(&b, params[i].v.s ? params[i].v.s : "");
			break;
		}
	}

	spa_pod_builder_pop(&b, &f_struct);
	spa_pod_builder_pop(&b, &f_obj);
	spa_pod_builder_pop(&b, &f_seq);

	if (b.state.offset > b.size)
		return -ENOSPC;

	return (int)b.state.offset;
}

/* Decode SPA_PROP_params from a spa_io_sequence into am62d_param array.
 * Returns number of params decoded, or negative on error. */
static inline int am62d_params_decode(const struct spa_io_sequence *seq,
                                       struct am62d_param *params, int max_params)
{
	int n = 0;
	if (!seq)
		return 0;

	struct spa_pod_control *c;
	SPA_POD_SEQUENCE_FOREACH(&seq->sequence, c) {
		if (c->type != SPA_CONTROL_Properties)
			continue;

		const struct spa_pod_object *obj = (const struct spa_pod_object *)&c->value;
		const struct spa_pod_prop *prop;
		SPA_POD_OBJECT_FOREACH(obj, prop) {
			if (prop->key != SPA_PROP_params)
				continue;

			struct spa_pod_parser p;
			struct spa_pod_frame f;
			spa_pod_parser_pod(&p, &prop->value);

			if (spa_pod_parser_push_struct(&p, &f) < 0)
				continue;

			while (n < max_params) {
				const char *key = NULL;
				struct spa_pod *val_pod = NULL;

				if (spa_pod_parser_get_string(&p, &key) < 0)
					break;
				if (spa_pod_parser_get_pod(&p, &val_pod) < 0)
					break;

				params[n].key = key;

				if (spa_pod_is_float(val_pod)) {
					params[n].type = AM62D_PARAM_FLOAT;
					spa_pod_get_float(val_pod, &params[n].v.f);
				} else if (spa_pod_is_int(val_pod)) {
					params[n].type = AM62D_PARAM_INT;
					spa_pod_get_int(val_pod, &params[n].v.i);
				} else if (spa_pod_is_long(val_pod)) {
					params[n].type = AM62D_PARAM_INT64;
					spa_pod_get_long(val_pod, &params[n].v.i64);
				} else if (spa_pod_is_string(val_pod)) {
					params[n].type = AM62D_PARAM_STRING;
					spa_pod_get_string(val_pod, &params[n].v.s);
				} else {
					continue;
				}
				n++;
			}
			spa_pod_parser_pop(&p, &f);
			break;
		}
	}
	return n;
}

#define AM62D_SPA_PLUGIN_DEFINE(factory_id, ports_array, ports_count, \
                                 init_callback, destroy_callback, process_callback, \
                                 executor_type) \
\
struct am62d_impl_##factory_id { \
	struct spa_handle handle; \
	struct spa_node node; \
	struct spa_log *log; \
	void *priv; \
	\
	float *buf[AM62D_MAX_PORTS]; \
	struct spa_io_buffers *io[AM62D_MAX_PORTS]; \
	\
	struct spa_io_sequence *notify_out; \
	struct spa_io_sequence *control_in; \
	struct spa_io_position *position; \
	\
	struct spa_hook_list hooks; \
	bool started; \
	\
	const struct am62d_port_desc *port_descs; \
	uint32_t n_ports; \
}; \
\
static int am62d_node_enum_params_##factory_id(void *object, int seq, \
                                                uint32_t id, uint32_t start, uint32_t num, \
                                                const struct spa_pod *filter) \
{ \
	return 0; \
} \
\
static int am62d_node_set_param_##factory_id(void *object, uint32_t id, uint32_t flags, \
                                              const struct spa_pod *param) \
{ \
	return -ENOTSUP; \
} \
\
static int am62d_node_set_io_##factory_id(void *object, uint32_t id, void *data, size_t size) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	if (id == SPA_IO_Position) { \
		impl->position = data; \
		return 0; \
	} \
	return -ENOENT; \
} \
\
static int am62d_node_send_command_##factory_id(void *object, const struct spa_command *command) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	switch (SPA_NODE_COMMAND_ID(command)) { \
	case SPA_NODE_COMMAND_Start: \
		impl->started = true; \
		break; \
	case SPA_NODE_COMMAND_Pause: \
	case SPA_NODE_COMMAND_Suspend: \
		impl->started = false; \
		break; \
	default: \
		return -ENOTSUP; \
	} \
	return 0; \
} \
\
static int am62d_node_add_listener_##factory_id(void *object, \
                                                 struct spa_hook *listener, \
                                                 const struct spa_node_events *events, \
                                                 void *data) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	spa_hook_list_append(&impl->hooks, listener, events, data); \
	return 0; \
} \
\
static int am62d_node_set_callbacks_##factory_id(void *object, \
                                                  const struct spa_node_callbacks *callbacks, \
                                                  void *data) \
{ \
	return 0; \
} \
\
static int am62d_node_add_port_##factory_id(void *object, enum spa_direction direction, \
                                             uint32_t port_id, const struct spa_dict *props) \
{ \
	return -ENOTSUP; \
} \
\
static int am62d_node_remove_port_##factory_id(void *object, enum spa_direction direction, \
                                                uint32_t port_id) \
{ \
	return -ENOTSUP; \
} \
\
static int am62d_node_port_enum_params_##factory_id(void *object, int seq, \
                                                     enum spa_direction direction, uint32_t port_id, \
                                                     uint32_t id, uint32_t start, uint32_t num, \
                                                     const struct spa_pod *filter) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	uint8_t buffer[512]; \
	struct spa_pod_builder b; \
	struct spa_pod *param; \
	struct spa_result_node_params result; \
	\
	if (port_id >= impl->n_ports || start > 0 || num == 0) \
		return 0; \
	\
	spa_pod_builder_init(&b, buffer, sizeof(buffer)); \
	\
	const struct am62d_port_desc *desc = &impl->port_descs[port_id]; \
	\
	if (id != SPA_PARAM_EnumFormat) \
		return 0; \
	\
	if (desc->type == AM62D_PORT_AUDIO_PCM || desc->type == AM62D_PORT_AUDIO_SPECTRUM) { \
		struct spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT( \
			.format = SPA_AUDIO_FORMAT_F32P, \
			.rate = 48000, \
			.channels = desc->u.pcm.n_channels); \
		param = spa_format_audio_raw_build(&b, id, &info); \
	} else if (desc->type == AM62D_PORT_CONTROL) { \
		struct spa_pod_frame f; \
		spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_ParamIO, id); \
		spa_pod_builder_prop(&b, SPA_PARAM_IO_id, 0); \
		spa_pod_builder_id(&b, desc->dir == AM62D_DIR_IN ? SPA_IO_Control : SPA_IO_Notify); \
		param = spa_pod_builder_pop(&b, &f); \
	} else { \
		return 0; \
	} \
	\
	result.id = id; \
	result.index = 0; \
	result.next = 1; \
	result.param = param; \
	\
	spa_node_emit_result(&impl->hooks, seq, 0, SPA_RESULT_TYPE_NODE_PARAMS, &result); \
	return 1; \
} \
\
static int am62d_node_port_set_param_##factory_id(void *object, \
                                                   enum spa_direction direction, uint32_t port_id, \
                                                   uint32_t id, uint32_t flags, \
                                                   const struct spa_pod *param) \
{ \
	return 0; \
} \
\
static int am62d_node_port_use_buffers_##factory_id(void *object, \
                                                     enum spa_direction direction, uint32_t port_id, \
                                                     uint32_t flags, \
                                                     struct spa_buffer **buffers, uint32_t n_buffers) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	if (port_id >= impl->n_ports) \
		return -EINVAL; \
	impl->buf[port_id] = (n_buffers > 0 && buffers[0]) ? buffers[0]->datas[0].data : NULL; \
	return 0; \
} \
\
static int am62d_node_port_set_io_##factory_id(void *object, \
                                                enum spa_direction direction, uint32_t port_id, \
                                                uint32_t id, void *data, size_t size) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	if (port_id >= impl->n_ports) \
		return -EINVAL; \
	const struct am62d_port_desc *desc = &impl->port_descs[port_id]; \
	if (id == SPA_IO_Buffers && \
	    (desc->type == AM62D_PORT_AUDIO_PCM || desc->type == AM62D_PORT_AUDIO_SPECTRUM)) { \
		impl->io[port_id] = data; \
	} else if (id == SPA_IO_Control && desc->type == AM62D_PORT_CONTROL && \
	           desc->dir == AM62D_DIR_IN) { \
		impl->control_in = data; \
	} else if (id == SPA_IO_Notify && desc->type == AM62D_PORT_CONTROL && \
	           desc->dir == AM62D_DIR_OUT) { \
		impl->notify_out = data; \
	} else { \
		return -ENOENT; \
	} \
	return 0; \
} \
\
static int am62d_node_port_reuse_buffer_##factory_id(void *object, uint32_t port_id, uint32_t buffer_id) \
{ \
	return 0; \
} \
\
static int am62d_node_process_##factory_id(void *object) \
{ \
	struct am62d_impl_##factory_id *impl = object; \
	if (!impl->started) \
		return SPA_STATUS_OK; \
	\
	uint32_t n_frames = 1024; \
	if (impl->position) \
		n_frames = impl->position->clock.duration; \
	\
	const float *in_bufs[AM62D_MAX_PORTS]; \
	float *out_bufs[AM62D_MAX_PORTS]; \
	int in_idx = 0, out_idx = 0; \
	memset(in_bufs, 0, sizeof(in_bufs)); \
	memset(out_bufs, 0, sizeof(out_bufs)); \
	\
	for (uint32_t i = 0; i < impl->n_ports; i++) { \
		const struct am62d_port_desc *desc = &impl->port_descs[i]; \
		if (desc->type == AM62D_PORT_AUDIO_PCM || desc->type == AM62D_PORT_AUDIO_SPECTRUM) { \
			if (desc->dir == AM62D_DIR_IN) { \
				in_bufs[in_idx++] = impl->buf[i]; \
				if (impl->io[i]) \
					impl->io[i]->status = SPA_STATUS_NEED_DATA; \
			} else { \
				out_bufs[out_idx++] = impl->buf[i]; \
			} \
		} \
	} \
	\
	struct am62d_param in_params[AM62D_MAX_PARAMS]; \
	struct am62d_param out_params[AM62D_MAX_PARAMS]; \
	int n_in_params = 0, n_out_params = 0; \
	\
	n_in_params = am62d_params_decode(impl->control_in, in_params, AM62D_MAX_PARAMS); \
	if (n_in_params < 0) n_in_params = 0; \
	\
	int ret = process_callback(impl->priv, in_bufs, out_bufs, n_frames, \
	                            in_params, n_in_params, out_params, &n_out_params); \
	\
	if (impl->notify_out && n_out_params > 0) { \
		uint8_t encode_buf[4096]; \
		int enc_size = am62d_params_encode(encode_buf, sizeof(encode_buf), \
		                                    out_params, n_out_params); \
		if (enc_size > 0 && (size_t)enc_size <= sizeof(encode_buf)) \
			memcpy(impl->notify_out, encode_buf, enc_size); \
	} \
	\
	for (uint32_t i = 0; i < impl->n_ports; i++) { \
		const struct am62d_port_desc *desc = &impl->port_descs[i]; \
		if ((desc->type == AM62D_PORT_AUDIO_PCM || desc->type == AM62D_PORT_AUDIO_SPECTRUM) && \
		    desc->dir == AM62D_DIR_OUT && impl->io[i]) { \
			impl->io[i]->status = SPA_STATUS_HAVE_DATA; \
			impl->io[i]->buffer_id = 0; \
		} \
	} \
	\
	return ret < 0 ? SPA_STATUS_STOPPED : SPA_STATUS_HAVE_DATA; \
} \
\
static const struct spa_node_methods am62d_node_methods_##factory_id = { \
	SPA_VERSION_NODE_METHODS, \
	.add_listener = am62d_node_add_listener_##factory_id, \
	.set_callbacks = am62d_node_set_callbacks_##factory_id, \
	.enum_params = am62d_node_enum_params_##factory_id, \
	.set_param = am62d_node_set_param_##factory_id, \
	.set_io = am62d_node_set_io_##factory_id, \
	.send_command = am62d_node_send_command_##factory_id, \
	.add_port = am62d_node_add_port_##factory_id, \
	.remove_port = am62d_node_remove_port_##factory_id, \
	.port_enum_params = am62d_node_port_enum_params_##factory_id, \
	.port_set_param = am62d_node_port_set_param_##factory_id, \
	.port_use_buffers = am62d_node_port_use_buffers_##factory_id, \
	.port_set_io = am62d_node_port_set_io_##factory_id, \
	.port_reuse_buffer = am62d_node_port_reuse_buffer_##factory_id, \
	.process = am62d_node_process_##factory_id, \
}; \
\
static int am62d_get_interface_##factory_id(struct spa_handle *handle, const char *type, void **iface) \
{ \
	struct am62d_impl_##factory_id *impl = (struct am62d_impl_##factory_id *)handle; \
	if (spa_streq(type, SPA_TYPE_INTERFACE_Node)) { \
		*iface = &impl->node; \
		return 0; \
	} \
	return -ENOENT; \
} \
\
static int am62d_clear_##factory_id(struct spa_handle *handle) \
{ \
	struct am62d_impl_##factory_id *impl = (struct am62d_impl_##factory_id *)handle; \
	destroy_callback(impl->priv); \
	return 0; \
} \
\
static size_t am62d_get_size_##factory_id(const struct spa_handle_factory *factory, \
                                           const struct spa_dict *params) \
{ \
	return sizeof(struct am62d_impl_##factory_id); \
} \
\
static int am62d_factory_init_##factory_id(const struct spa_handle_factory *factory, \
                                            struct spa_handle *handle, \
                                            const struct spa_dict *info, \
                                            const struct spa_support *support, \
                                            uint32_t n_support) \
{ \
	struct am62d_impl_##factory_id *impl = (struct am62d_impl_##factory_id *)handle; \
	memset(impl, 0, sizeof(*impl)); \
	\
	impl->handle.get_interface = am62d_get_interface_##factory_id; \
	impl->handle.clear = am62d_clear_##factory_id; \
	impl->node.iface = SPA_INTERFACE_INIT( \
		SPA_TYPE_INTERFACE_Node, SPA_VERSION_NODE, \
		&am62d_node_methods_##factory_id, impl); \
	\
	impl->port_descs = ports_array; \
	impl->n_ports = ports_count; \
	\
	for (uint32_t i = 0; i < n_support; i++) { \
		if (spa_streq(support[i].type, SPA_TYPE_INTERFACE_Log)) \
			impl->log = support[i].data; \
	} \
	\
	spa_hook_list_init(&impl->hooks); \
	\
	return init_callback(&impl->priv, NULL, 0); \
} \
\
static int am62d_enum_interface_info_##factory_id(const struct spa_handle_factory *factory, \
                                                   const struct spa_interface_info **info, \
                                                   uint32_t *index) \
{ \
	static const struct spa_interface_info infos[] = { \
		{ SPA_TYPE_INTERFACE_Node, }, \
	}; \
	if (*index >= SPA_N_ELEMENTS(infos)) \
		return 0; \
	*info = &infos[(*index)++]; \
	return 1; \
} \
\
static const struct spa_handle_factory am62d_factory_##factory_id = { \
	SPA_VERSION_HANDLE_FACTORY, \
	.name = #factory_id, \
	.get_size = am62d_get_size_##factory_id, \
	.init = am62d_factory_init_##factory_id, \
	.enum_interface_info = am62d_enum_interface_info_##factory_id, \
}; \
\
AM62D_PLUGIN_EXPORT \
int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index) \
{ \
	if (*index != 0) return 0; \
	*factory = &am62d_factory_##factory_id; \
	(*index)++; \
	return 1; \
}

#endif /* AM62D_SPA_H */
