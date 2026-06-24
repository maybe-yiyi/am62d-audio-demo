#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pipewire/pipewire.h>
#include <spa/support/plugin.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/node/utils.h>
#include <spa/param/param.h>
#include <spa/param/audio/format.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

#include "a53_node.h"
#include "param_bus.h"

struct port_enum_state {
	uint32_t n_ports;
	const struct spa_port_info *ports;
};

static void on_process(void *data, struct spa_io_position *pos)
{
	struct a53_node *node = data;
	uint32_t n_frames = pos->clock.duration;

	node->position.clock.duration = n_frames;

	for (int i = 0; i < node->n_ports; i++) {
		if (!node->spa_buffers[i])
			continue;

		void *dsp = pw_filter_get_dsp_buffer(node->pw_ports[i], n_frames);
		if (dsp) {
			node->spa_buffers[i]->datas[0].data = dsp;
			node->spa_buffers[i]->datas[0].chunk->size = n_frames * sizeof(float);
		}
	}

	spa_node_process(node->spa_node);

	param_bus_dispatch(node);
}

static const struct pw_filter_events filter_events = {
	PW_VERSION_FILTER_EVENTS,
	.process = on_process,
};


struct a53_node *a53_node_create(struct pw_core *core,
				 const struct spa_handle_factory *factory,
				 const char *node_name,
				 const struct am62d_param *params,
				 int n_params)
{
	struct a53_node *node = calloc(1, sizeof(struct a53_node));
	if (!node)
		goto exit;

	/* Initialize sequence pod headers - spa_io_sequence wraps spa_pod_sequence */
	node->control_in.sequence.pod.type = SPA_TYPE_Sequence;
	node->control_in.sequence.pod.size = sizeof(node->control_in_data) - sizeof(struct spa_pod);
	node->notify_out.sequence.pod.type = SPA_TYPE_Sequence;
	node->notify_out.sequence.pod.size = sizeof(node->notify_out_data) - sizeof(struct spa_pod);

	struct pw_context *ctx = pw_core_get_context(core);
	const struct spa_support *support;
	uint32_t n_support;
	support = pw_context_get_support(ctx, &n_support);

	size_t handle_size = factory->get_size(factory, NULL);
	node->spa_handle = calloc(1, handle_size);
	if (!node->spa_handle)
		goto free_node;

	int ret = factory->init(factory, node->spa_handle, NULL, support, n_support);
	if (ret < 0)
		goto free_handle;

	ret = node->spa_handle->get_interface(node->spa_handle,
	                                       SPA_TYPE_INTERFACE_Node,
	                                       (void **)&node->spa_node);
	if (ret < 0)
		goto clear_handle;

	node->filter = pw_filter_new(core, node_name,
		pw_properties_new(
			PW_KEY_NODE_NAME, node_name,
			PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Filter",
			NULL));
	if (!node->filter)
		goto clear_handle;

	pw_filter_add_listener(node->filter,
			&node->filter_listener,
			&filter_events,
			node);

	/* Discover ports by enumerating SPA_PARAM_EnumFormat on each candidate port */
	for (uint32_t dir = 0; dir < 2; dir++) {
		for (uint32_t port_id = 0; port_id < MAX_PORTS / 2; port_id++) {
			uint8_t buf[512];
			struct spa_pod_builder b;
			spa_pod_builder_init(&b, buf, sizeof(buf));

			struct spa_result_node_params result;
			memset(&result, 0, sizeof(result));

			/* Use port_enum_params to probe if port exists */
			ret = spa_node_port_enum_params(node->spa_node,
			                                0,
			                                (enum spa_direction)dir,
			                                port_id,
			                                SPA_PARAM_EnumFormat,
			                                0, 1, NULL);
			if (ret <= 0)
				break;
		}
	}

	/* For simplicity, add PW filter ports based on SPA port enumeration */
	/* We'll use the factory name prefix to determine port layout from the plugin's declared ports */
	/* The actual port count and layout is determined by the SPA node at init */

	ret = pw_filter_connect(node->filter,
			PW_FILTER_FLAG_RT_PROCESS,
			NULL, 0);
	if (ret < 0)
		goto destroy_filter;

	/* Wire up SPA I/O areas after connecting */
	spa_node_set_io(node->spa_node, SPA_IO_Position, &node->position, sizeof(node->position));

	return node;

destroy_filter:
	pw_filter_destroy(node->filter);
clear_handle:
	node->spa_handle->clear(node->spa_handle);
free_handle:
	free(node->spa_handle);
free_node:
	free(node);
exit:
	return NULL;
}

void a53_node_destroy(struct a53_node *node)
{
	for (int i = 0; i < node->n_ports; i++)
		free(node->spa_buffers[i]);

	pw_filter_destroy(node->filter);

	if (node->spa_handle) {
		node->spa_handle->clear(node->spa_handle);
		free(node->spa_handle);
	}

	free(node);
}
