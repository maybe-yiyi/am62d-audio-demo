#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spa/node/command.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/buffer/buffer.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>
#include <spa/pod/command.h>
#include <spa/support/log.h>
#include <spa/support/plugin.h>
#include <spa/utils/dict.h>
#include <spa/utils/names.h>

#include "plugin.h"

static void default_logv(void *object, enum spa_log_level level,
			const char *file, int line,
			const char *func, const char *fmt, va_list args)
{
	(void)object; (void)file; (void)line; (void)func;
	if (level <= SPA_LOG_LEVEL_WARN) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, "\n");
	}
}

static void default_log(void *object, enum spa_log_level level,
			 const char *file, int line,
			 const char *func, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	default_logv(object, level, file, line, func, fmt, args);
	va_end(args);
}

static const struct spa_log_methods default_log_methods = {
	SPA_VERSION_LOG_METHODS,
	.log = default_log,
	.logv = default_logv,
};

static struct spa_log default_log_obj;
static bool default_log_inited = false;

struct spa_node_wrapper *spa_plugin_load(const char *path,
					 const char *factory_name,
					 const struct spa_dict *info,
					 const struct spa_support *support,
					 uint32_t n_support)
{
	if (!default_log_inited) {
		default_log_obj.iface = SPA_INTERFACE_INIT(SPA_TYPE_INTERFACE_Log,
							SPA_VERSION_LOG,
							&default_log_methods, NULL);
		default_log_obj.level = SPA_LOG_LEVEL_WARN;
		default_log_inited = true;
	}

	struct spa_support default_support =
		SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_Log, &default_log_obj);
	if (!support) {
		support = &default_support;
		n_support = 1;
	}

	struct spa_node_wrapper *wrap = calloc(1, sizeof(*wrap));
	if (!wrap)
		return NULL;

	wrap->dl_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!wrap->dl_handle) {
		fprintf(stderr, "plugin: dlopen(%s) failed: %s\n", path, dlerror());
		goto fail_open;
	}

	spa_handle_factory_enum_func_t enum_fn =
		(spa_handle_factory_enum_func_t)dlsym(wrap->dl_handle,
							SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
	if (!enum_fn) {
		fprintf(stderr, "plugin: %s: missing %s\n", path,
			SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
		goto fail_sym;
	}

	const struct spa_handle_factory *factory = NULL;
	uint32_t idx = 0;
	while (enum_fn(&factory, &idx) > 0) {
		if (strcmp(factory->name, factory_name) == 0)
			break;
		factory = NULL;
	}
	if (!factory) {
		fprintf(stderr, "plugin: factory '%s' not found in %s\n", factory_name, path);
		goto fail_sym;
	}

	snprintf(wrap->factory_name, sizeof(wrap->factory_name), "%s", factory_name);

	size_t handle_size = spa_handle_factory_get_size(factory, info);
	wrap->handle = calloc(1, handle_size);
	if (!wrap->handle)
		goto fail_sym;

	int res = spa_handle_factory_init(factory, wrap->handle, info, support, n_support);
	if (res < 0) {
		fprintf(stderr, "plugin: factory '%s' init failed: %s\n",
			factory_name, strerror(-res));
		goto fail_init;
	}

	res = spa_handle_get_interface(wrap->handle, SPA_TYPE_INTERFACE_Node,
					(void **)&wrap->node);
	if (res < 0) {
		fprintf(stderr, "plugin: '%s' has no Node interface\n", factory_name);
		goto fail_iface;
	}

	return wrap;

fail_iface:
	if (wrap->handle->clear)
		wrap->handle->clear(wrap->handle);
fail_init:
	free(wrap->handle);
fail_sym:
	dlclose(wrap->dl_handle);
fail_open:
	free(wrap);
	return NULL;
}

int spa_node_configure_ports(struct spa_node_wrapper *wrap, uint32_t sample_rate)
{
	uint8_t buf[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));

	struct spa_audio_info_raw info = {
		.format = SPA_AUDIO_FORMAT_F32P,
		.rate = sample_rate,
		.channels = 1,
	};

	struct spa_pod *param = spa_format_audio_raw_build(&b, SPA_PARAM_Format, &info);

	int res;
	for (uint32_t i = 0; i < wrap->n_input_ports; i++) {
		res = spa_node_port_set_param(wrap->node, SPA_DIRECTION_INPUT, i,
						SPA_PARAM_Format, 0, param);
		if (res < 0 && res != -ENOENT) {
			fprintf(stderr, "plugin: %s: set_param(Format) input %u failed: %s\n",
				wrap->factory_name, i, strerror(-res));
			return res;
		}
	}

	for (uint32_t i = 0; i < wrap->n_output_ports; i++) {
		res = spa_node_port_set_param(wrap->node, SPA_DIRECTION_OUTPUT, i,
						SPA_PARAM_Format, 0, param);
		if (res < 0 && res != -ENOENT) {
			fprintf(stderr, "plugin: %s: set_param(Format) output %u failed: %s\n",
				wrap->factory_name, i, strerror(-res));
			return res;
		}
	}

	return 0;
}

static struct spa_buffer *alloc_buffer(uint32_t quantum)
{
	size_t data_size = quantum * sizeof(float);
	size_t alloc_size = sizeof(struct spa_buffer)
			+ sizeof(struct spa_data)
			+ sizeof(struct spa_chunk)
			+ data_size;

	uint8_t *mem = calloc(1, alloc_size);
	if (!mem)
		return NULL;

	struct spa_buffer *buf = (struct spa_buffer *)mem;
	struct spa_data *d = (struct spa_data *)(mem + sizeof(struct spa_buffer));
	struct spa_chunk *c = (struct spa_chunk *)(mem + sizeof(struct spa_buffer)
						+ sizeof(struct spa_data));
	void *pcm = mem + sizeof(struct spa_buffer)
			+ sizeof(struct spa_data)
			+ sizeof(struct spa_chunk);

	buf->n_metas = 0;
	buf->metas = NULL;
	buf->n_datas = 1;
	buf->datas = d;

	d->type = SPA_DATA_MemPtr;
	d->flags = SPA_DATA_FLAG_READWRITE;
	d->fd = -1;
	d->maxsize = (uint32_t)data_size;
	d->data = pcm;
	d->chunk = c;

	c->offset = 0;
	c->size = (uint32_t)data_size;
	c->stride = (int32_t)sizeof(float);

	return buf;
}

int spa_node_setup_buffers(struct spa_node_wrapper *wrap, uint32_t quantum)
{
	int res;

	for (uint32_t i = 0; i < wrap->n_input_ports; i++) {
		if (!wrap->input_buffers[i]) {
			wrap->input_buffers[i] = alloc_buffer(quantum);
			if (!wrap->input_buffers[i])
				return -ENOMEM;
		}

		res = spa_node_port_use_buffers(wrap->node, SPA_DIRECTION_INPUT, i,
						0, &wrap->input_buffers[i], 1);
		if (res < 0 && res != -ENOENT) {
			fprintf(stderr, "plugin: %s: port_use_buffers input %u failed: %s\n",
				wrap->factory_name, i, strerror(-res));
			return res;
		}

		wrap->in_io[i] = SPA_IO_BUFFERS_INIT;
		spa_node_port_set_io(wrap->node, SPA_DIRECTION_INPUT, i,
				SPA_IO_Buffers,
				&wrap->in_io[i], sizeof(wrap->in_io[i]));
	}

	for (uint32_t i = 0; i < wrap->n_output_ports; i++) {
		if (!wrap->output_buffers[i]) {
			wrap->output_buffers[i] = alloc_buffer(quantum);
			if (!wrap->output_buffers[i])
				return -ENOMEM;
		}

		res = spa_node_port_use_buffers(wrap->node, SPA_DIRECTION_OUTPUT, i,
						0, &wrap->output_buffers[i], 1);
		if (res < 0 && res != -ENOENT) {
			fprintf(stderr, "plugin: %s: port_use_buffers output %u failed: %s\n",
				wrap->factory_name, i, strerror(-res));
			return res;
		}

		wrap->out_io[i] = SPA_IO_BUFFERS_INIT;
		spa_node_port_set_io(wrap->node, SPA_DIRECTION_OUTPUT, i,
				SPA_IO_Buffers,
				&wrap->out_io[i], sizeof(wrap->out_io[i]));
	}

	return 0;
}

/* ---------- spa_node_set_clock ---------- */

int spa_node_set_clock(struct spa_node_wrapper *wrap,
			struct spa_io_clock *clock,
			struct spa_io_position *position)
{
	int res;

	res = spa_node_set_io(wrap->node, SPA_IO_Clock, clock, sizeof(*clock));
	if (res < 0 && res != -ENOENT) {
		fprintf(stderr, "plugin: %s: set_io(Clock) failed: %s\n",
			wrap->factory_name, strerror(-res));
		return res;
	}

	res = spa_node_set_io(wrap->node, SPA_IO_Position, position, sizeof(*position));
	if (res < 0 && res != -ENOENT) {
		fprintf(stderr, "plugin: %s: set_io(Position) failed: %s\n",
			wrap->factory_name, strerror(-res));
		return res;
	}

	return 0;
}

int spa_node_start(struct spa_node_wrapper *wrap)
{
	struct spa_command cmd = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Start);
	int res = spa_node_send_command(wrap->node, &cmd);
	if (res < 0)
		fprintf(stderr, "plugin: %s: start failed: %s\n",
			wrap->factory_name, strerror(-res));
	return res;
}

int spa_node_stop(struct spa_node_wrapper *wrap)
{
	struct spa_command cmd = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Pause);
	int res = spa_node_send_command(wrap->node, &cmd);
	if (res < 0)
		fprintf(stderr, "plugin: %s: stop failed: %s\n",
			wrap->factory_name, strerror(-res));
	return res;
}

void spa_plugin_unload(struct spa_node_wrapper *wrap)
{
	if (!wrap)
		return;

	for (uint32_t i = 0; i < wrap->n_input_ports; i++)
		free(wrap->input_buffers[i]);
	for (uint32_t i = 0; i < wrap->n_output_ports; i++)
		free(wrap->output_buffers[i]);

	if (wrap->handle && wrap->handle->clear)
		wrap->handle->clear(wrap->handle);
	free(wrap->handle);

	dlclose(wrap->dl_handle);
	free(wrap);
}
