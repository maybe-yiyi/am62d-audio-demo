#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <pipewire/pipewire.h>
#include <spa/support/plugin.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/node/utils.h>
#include <spa/param/param.h>
#include <spa/param/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/buffer/buffer.h>
#include <spa/utils/type.h>

#include "a53_node.h"
#include "param_bus.h"

/* -------------------------------------------------------------------------
 * Port discovery: temporary listener installed during a53_node_create to
 * receive results from spa_node_port_enum_params synchronously.
 * ---------------------------------------------------------------------- */

struct discovery_ctx {
	struct a53_node *node;
	/* populated per call */
	enum spa_direction cur_dir;
	uint32_t cur_port_id;
	bool got_result;
	/* audio info from the last result */
	uint32_t n_channels;
	bool is_audio;
};

static void discovery_result(void *data, int seq, int res,
                              uint32_t type, const void *result)
{
	struct discovery_ctx *ctx = (struct discovery_ctx *)data;

	if (type != SPA_RESULT_TYPE_NODE_PARAMS)
		return;

	const struct spa_result_node_params *r =
		(const struct spa_result_node_params *)result;

	if (!r->param)
		return;

	ctx->got_result = true;
	ctx->is_audio = false;
	ctx->n_channels = 1;

	/* Try to parse as audio format */
	uint32_t media_type = 0, media_subtype = 0;
	if (spa_format_parse(r->param, &media_type, &media_subtype) >= 0 &&
	    media_type == SPA_MEDIA_TYPE_audio &&
	    media_subtype == SPA_MEDIA_SUBTYPE_raw) {
		struct spa_audio_info_raw info = { 0 };
		spa_format_audio_raw_parse(r->param, &info);
		ctx->is_audio = true;
		ctx->n_channels = info.channels ? info.channels : 1;
	}
	/* If not audio format, treat as control (IO param) - is_audio stays false */
}

static const struct spa_node_events discovery_events = {
	SPA_VERSION_NODE_EVENTS,
	.result = discovery_result,
};

/* -------------------------------------------------------------------------
 * on_process: called every PipeWire graph cycle
 * ---------------------------------------------------------------------- */

static void on_process(void *data, struct spa_io_position *pos)
{
	struct a53_node *node = (struct a53_node *)data;

	node->position.clock = pos->clock;

	/* Update audio buffer pointers and io status for each port */
	for (int i = 0; i < node->n_ports; i++) {
		struct a53_port *p = &node->ports[i];
		if (!p->is_audio || !p->pw_port)
			continue;

		void *dsp = pw_filter_get_dsp_buffer(p->pw_port,
		                                     pos->clock.duration);
		p->spa_data.data = dsp;
		p->spa_chunk.size = pos->clock.duration * sizeof(float);
		p->spa_chunk.offset = 0;
		p->spa_chunk.stride = sizeof(float);

		if (p->dir == SPA_DIRECTION_INPUT) {
			p->io_buf.status = SPA_STATUS_HAVE_DATA;
			p->io_buf.buffer_id = 0;
		} else {
			p->io_buf.status = SPA_STATUS_NEED_DATA;
			p->io_buf.buffer_id = 0;
		}
	}

	spa_node_process(node->spa_node);

	param_bus_dispatch(node);
}

/* -------------------------------------------------------------------------
 * state_changed: wire SPA node buffers and start it when PW reaches PAUSED
 * ---------------------------------------------------------------------- */

static void on_state_changed(void *data,
                              enum pw_filter_state old,
                              enum pw_filter_state state,
                              const char *error)
{
	struct a53_node *node = (struct a53_node *)data;

	if (state != PW_FILTER_STATE_PAUSED)
		return;

	for (int i = 0; i < node->n_ports; i++) {
		struct a53_port *p = &node->ports[i];
		if (!p->is_audio || !p->pw_port)
			continue;

		/* Set up a minimal spa_buffer wrapping the DSP memory.
		 * data pointer is updated per-cycle in on_process. */
		memset(&p->spa_data, 0, sizeof(p->spa_data));
		memset(&p->spa_chunk, 0, sizeof(p->spa_chunk));
		p->spa_data.type = SPA_DATA_MemPtr;
		p->spa_data.flags = SPA_DATA_FLAG_READWRITE;
		p->spa_data.maxsize = 8192 * sizeof(float);
		p->spa_data.data = NULL;
		p->spa_data.chunk = &p->spa_chunk;

		memset(&p->spa_buf, 0, sizeof(p->spa_buf));
		p->spa_buf.n_metas = 0;
		p->spa_buf.metas = NULL;
		p->spa_buf.n_datas = 1;
		p->spa_buf.datas = &p->spa_data;
		p->spa_buf_ptr = &p->spa_buf;

		spa_node_port_use_buffers(node->spa_node,
		                          p->dir, p->spa_port_id,
		                          0,
		                          &p->spa_buf_ptr, 1);

		memset(&p->io_buf, 0, sizeof(p->io_buf));
		p->io_buf.status = SPA_STATUS_NEED_DATA;
		p->io_buf.buffer_id = 0;

		spa_node_port_set_io(node->spa_node,
		                     p->dir, p->spa_port_id,
		                     SPA_IO_Buffers,
		                     &p->io_buf, sizeof(p->io_buf));
	}

	spa_node_set_io(node->spa_node, SPA_IO_Position,
	                &node->position, sizeof(node->position));

	struct spa_command cmd = SPA_NODE_COMMAND_INIT(SPA_NODE_COMMAND_Start);
	spa_node_send_command(node->spa_node, &cmd);
}

static const struct pw_filter_events filter_events = {
	PW_VERSION_FILTER_EVENTS,
	.process = on_process,
	.state_changed = on_state_changed,
};

/* -------------------------------------------------------------------------
 * a53_node_create
 * ---------------------------------------------------------------------- */

struct a53_node *a53_node_create(struct pw_core *core,
				 const struct spa_handle_factory *factory,
				 const char *node_name,
				 const struct am62d_param *params,
				 int n_params)
{
	struct a53_node *node = (struct a53_node *)calloc(1, sizeof(*node));
	if (!node)
		return NULL;

	/* Create and initialise the SPA handle */
	struct pw_context *ctx = pw_core_get_context(core);
	const struct spa_support *support;
	uint32_t n_support;
	support = pw_context_get_support(ctx, &n_support);

	size_t handle_size = factory->get_size(factory, NULL);
	node->spa_handle = (struct spa_handle *)calloc(1, handle_size);
	if (!node->spa_handle)
		goto free_node;

	if (factory->init(factory, node->spa_handle, NULL, support, n_support) < 0)
		goto free_handle;

	if (node->spa_handle->get_interface(node->spa_handle,
	                                    SPA_TYPE_INTERFACE_Node,
	                                    (void **)&node->spa_node) < 0)
		goto clear_handle;

	/* Install a temporary listener so port_enum_params results come back
	 * synchronously in discovery_result(). */
	struct discovery_ctx dctx = { .node = node };
	spa_node_add_listener(node->spa_node, &node->spa_listener,
	                      &discovery_events, &dctx);

	/* Create the PipeWire filter */
	node->filter = pw_filter_new(core, node_name,
		pw_properties_new(
			PW_KEY_NODE_NAME, node_name,
			PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Filter",
			NULL));
	if (!node->filter)
		goto remove_listener;

	pw_filter_add_listener(node->filter, &node->filter_listener,
	                       &filter_events, node);

	/* Enumerate ports from the SPA node and register pw_filter ports */
	for (uint32_t dir = 0; dir <= 1; dir++) {
		for (uint32_t port_id = 0; port_id < MAX_PORTS; port_id++) {
			dctx.got_result = false;

			int r = spa_node_port_enum_params(node->spa_node,
			                                  0,
			                                  (enum spa_direction)dir,
			                                  port_id,
			                                  SPA_PARAM_EnumFormat,
			                                  0, 1, NULL);
			if (r <= 0)
				break;

			if (!dctx.got_result)
				break;

			if (node->n_ports >= MAX_PORTS) {
				fprintf(stderr, "a53_node: too many ports\n");
				break;
			}

			struct a53_port *p = &node->ports[node->n_ports];
			p->spa_port_id = port_id;
			p->dir = (enum spa_direction)dir;
			p->is_audio = dctx.is_audio;
			p->n_channels = dctx.n_channels;

			if (p->is_audio) {
				char fmt[64];
				if (p->n_channels == 1)
					snprintf(fmt, sizeof(fmt),
					         "32 bit float mono audio");
				else
					snprintf(fmt, sizeof(fmt),
					         "32 bit %u channel audio",
					         p->n_channels);

				p->pw_port = (struct port_data *)pw_filter_add_port(
					node->filter,
					(enum pw_direction)dir,
					PW_FILTER_PORT_FLAG_MAP_BUFFERS,
					sizeof(struct port_data),
					pw_properties_new(
						PW_KEY_FORMAT_DSP, fmt,
						PW_KEY_PORT_NAME,
						  (dir == SPA_DIRECTION_INPUT
						   ? "input" : "output"),
						NULL),
					NULL, 0);

				if (!p->pw_port) {
					fprintf(stderr,
					        "a53_node: failed to add port\n");
					goto destroy_filter;
				}
			}

			node->n_ports++;
		}
	}

	/* Remove the temporary discovery listener */
	spa_hook_remove(&node->spa_listener);

	if (pw_filter_connect(node->filter, PW_FILTER_FLAG_RT_PROCESS,
	                      NULL, 0) < 0)
		goto destroy_filter;

	return node;

destroy_filter:
	pw_filter_destroy(node->filter);
remove_listener:
	spa_hook_remove(&node->spa_listener);
clear_handle:
	node->spa_handle->clear(node->spa_handle);
free_handle:
	free(node->spa_handle);
free_node:
	free(node);
	return NULL;
}

void a53_node_destroy(struct a53_node *node)
{
	pw_filter_destroy(node->filter);
	node->spa_handle->clear(node->spa_handle);
	free(node->spa_handle);
	free(node);
}
