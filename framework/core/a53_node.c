#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/core/lv2.h>
#include <pipewire/pipewire.h>

#include "a53_node.h"
#include "param_bus.h"

static void a53_publish_text(void *handle, const char *key,
			     const char *text, float confidence)
{
	struct a53_node *node = handle;
	if (!node || !key || !text)
		return;

	pthread_mutex_lock(&node->text_lock);
	snprintf(node->outbox_key, sizeof(node->outbox_key), "%s", key);
	snprintf(node->outbox_text, sizeof(node->outbox_text), "%s", text);
	node->outbox_confidence = confidence;
	node->outbox_seq++;
	pthread_mutex_unlock(&node->text_lock);
}

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

static bool port_is_linked(const char *sym, const char **linked_ports, int n_linked_ports)
{
	for (int i = 0; i < n_linked_ports; i++)
		if (strcmp(sym, linked_ports[i]) == 0)
			return true;
	return false;
}

struct a53_node *a53_node_create(struct pw_core *core,
				 LilvWorld *world,
				 const LilvPlugin *plugin,
				 const char *node_name,
				 const char **linked_ports,
				 int n_linked_ports)
{
	LilvNode *audio_class = lilv_new_uri(world, LV2_CORE__AudioPort);
	LilvNode *control_class = lilv_new_uri(world, LV2_CORE__ControlPort);
	LilvNode *input_class = lilv_new_uri(world, LV2_CORE__InputPort);
	LilvNode *optional_class = lilv_new_uri(world, LV2_CORE__connectionOptional);

	struct a53_node *node = calloc(1, sizeof(struct a53_node));
	if (!node)
		goto exit;

	node->plugin = plugin;
	pthread_mutex_init(&node->text_lock, NULL);

	node->params.handle = node;
	node->params.publish_text = a53_publish_text;
	node->params_feature.URI = AM62D_PARAMS_URI;
	node->params_feature.data = &node->params;
	node->features[0] = &node->params_feature;
	node->features[1] = NULL;

	node->instance = lilv_plugin_instantiate(plugin, 48000.0, node->features);
	if (!node->instance) {
		fprintf(stderr, "a53_node: failed to instantiate plugin\n");
		goto free_node;
	}

	const char *plugin_uri = lilv_node_as_uri(lilv_plugin_get_uri(plugin));

	node->filter = pw_filter_new(core, plugin_uri,
		pw_properties_new(
			PW_KEY_NODE_NAME, node_name,
			PW_KEY_MEDIA_TYPE, "AUDIO",
			PW_KEY_MEDIA_CATEGORY, "FILTER",
			NULL));
	if (!node->filter)
		goto free_instance;

	pw_filter_add_listener(node->filter, &node->filter_listener,
			&filter_events, node);

	uint32_t n_ports = lilv_plugin_get_num_ports(plugin);
	for (uint32_t i = 0; i < n_ports; i++) {
		const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
		bool is_audio = lilv_port_is_a(plugin, port, audio_class);
		bool is_control = lilv_port_is_a(plugin, port, control_class);
		bool is_input = lilv_port_is_a(plugin, port, input_class);
		bool is_optional = lilv_port_has_property(plugin, port, optional_class);
		const char *sym = lilv_node_as_string(lilv_port_get_symbol(plugin, port));

		if (is_audio) {
			if (is_optional && !port_is_linked(sym, linked_ports, n_linked_ports))
				continue;

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
			if (node->n_ctrl >= MAX_CTRL_PORTS)
				goto destroy_filter;

			int buf_index = node->n_ctrl;
			node->ctrl_bufs[buf_index] = 0.0f;
			snprintf(node->ctrl_ports[buf_index].symbol,
				 sizeof(node->ctrl_ports[buf_index].symbol),
				 "%s", sym);
			node->ctrl_ports[buf_index].lv2_index = i;
			node->ctrl_ports[buf_index].buf_index = buf_index;
			node->ctrl_ports[buf_index].is_input = is_input;

			lilv_instance_connect_port(node->instance, i,
					&node->ctrl_bufs[buf_index]);
			node->n_ctrl++;
		}
	}

	int ret = pw_filter_connect(node->filter,
			PW_FILTER_FLAG_RT_PROCESS,
			NULL, 0);
	if (ret < 0)
		goto destroy_filter;

	lilv_instance_activate(node->instance);

	lilv_node_free(audio_class);
	lilv_node_free(control_class);
	lilv_node_free(input_class);
	lilv_node_free(optional_class);
	return node;

destroy_filter:
	pw_filter_destroy(node->filter);
free_instance:
	lilv_instance_free(node->instance);
free_node:
	pthread_mutex_destroy(&node->text_lock);
	free(node);
exit:
	lilv_node_free(audio_class);
	lilv_node_free(control_class);
	lilv_node_free(input_class);
	lilv_node_free(optional_class);
	return NULL;
}

void a53_node_destroy(struct a53_node *node)
{
	lilv_instance_deactivate(node->instance);
	pw_filter_destroy(node->filter);
	lilv_instance_free(node->instance);
	pthread_mutex_destroy(&node->text_lock);
	free(node);
}
