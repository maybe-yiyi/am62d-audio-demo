#include <stdlib.h>

#include <lv2/core/lv2.h>
#include <pipewire/pipewire.h>

#include "a53_node.h"
#include "param_bus.h"

static void on_process(void *data, struct spa_io_position *pos)
{
	struct a53_node *node = data;
	uint32_t n_frames = pos->clock.duration;

	for (int i = 0; i < node->n_in; i++)
		lilv_instance_connect_port(node->instance,
				node->in_port_indices[i],
				pw_filter_get_dsp_buffer(node->in_ports[i], n_frames));

	for (int i = 0; i < node->n_out; i++)
		lilv_instance_connect_port(node->instance,
				node->out_port_indices[i],
				pw_filter_get_dsp_buffer(node->out_ports[i], n_frames));

	lilv_instance_run(node->instance, n_frames);
	param_bus_dispatch(node);
}

static const struct pw_filter_events filter_events = {
	PW_VERSION_FILTER_EVENTS,
	.process = on_process,
};

struct a53_node *a53_node_create(struct pw_core *core,
				 LilvWorld *world,
				 const LilvPlugin *plugin,
				 LilvInstance *instance,
				 const char *node_name)
{
	LilvNode *audio_class = lilv_new_uri(world, LV2_CORE__AudioPort);
	LilvNode *control_class = lilv_new_uri(world, LV2_CORE__ControlPort);
	LilvNode *input_class = lilv_new_uri(world, LV2_CORE__InputPort);

	struct a53_node *node = calloc(1, sizeof(struct a53_node));
	if (!node)
		goto exit;

	node->plugin = plugin;
	node->instance = instance;

	const char *plugin_uri = lilv_node_as_uri(lilv_plugin_get_uri(plugin));

	node->filter = pw_filter_new(core, plugin_uri,
		pw_properties_new(
			PW_KEY_NODE_NAME, node_name,
			PW_KEY_MEDIA_TYPE, "AUDIO",
			PW_KEY_MEDIA_CATEGORY, "FILTER",
			NULL));
	if (!node->filter)
		goto free_node;

	pw_filter_add_listener(node->filter, &node->filter_listener,
			&filter_events, node);

	uint32_t n_ports = lilv_plugin_get_num_ports(plugin);
	for (uint32_t i = 0; i < n_ports; i++) {
		const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
		bool is_audio = lilv_port_is_a(plugin, port, audio_class);
		bool is_control = lilv_port_is_a(plugin, port, control_class);
		bool is_input = lilv_port_is_a(plugin, port, input_class);
		const char *sym = lilv_node_as_string(lilv_port_get_symbol(plugin, port));

		if (is_audio) {
			if (is_input && node->n_in >= MAX_PORTS)
				goto destroy_filter;
			if (!is_input && node->n_out >= MAX_PORTS)
				goto destroy_filter;

			struct port_data *pd = pw_filter_add_port(node->filter,
					is_input ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT,
					PW_FILTER_PORT_FLAG_MAP_BUFFERS,
					sizeof(struct port_data),
					pw_properties_new(
						PW_KEY_FORMAT_DSP, "32 bit float mono audio",
						PW_KEY_PORT_NAME, sym,
						NULL),
					NULL, 0);
			if (!pd)
				goto destroy_filter;

			if (is_input) {
				node->in_ports[node->n_in] = pd;
				node->in_port_indices[node->n_in] = i;
				node->n_in++;
			} else {
				node->out_ports[node->n_out] = pd;
				node->out_port_indices[node->n_out] = i;
				node->n_out++;
			}
		} else if (is_control) {
			if (node->n_ctrl >= MAX_PORTS)
				goto destroy_filter;
			node->ctrl_bufs[node->n_ctrl] = 0.0f;
			lilv_instance_connect_port(instance, i,
					&node->ctrl_bufs[node->n_ctrl]);
			node->n_ctrl++;
		}
	}

	int ret = pw_filter_connect(node->filter,
			PW_FILTER_FLAG_RT_PROCESS,
			NULL, 0);
	if (ret < 0)
		goto destroy_filter;

	lilv_instance_activate(instance);

	lilv_node_free(audio_class);
	lilv_node_free(control_class);
	lilv_node_free(input_class);
	return node;

destroy_filter:
	pw_filter_destroy(node->filter);
free_node:
	free(node);
exit:
	lilv_node_free(audio_class);
	lilv_node_free(control_class);
	lilv_node_free(input_class);
	return NULL;
}

void a53_node_destroy(struct a53_node *node)
{
	lilv_instance_deactivate(node->instance);
	pw_filter_destroy(node->filter);
	free(node);
}
