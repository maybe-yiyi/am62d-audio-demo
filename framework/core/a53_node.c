#include <stdlib.h>

#include <pipewire/pipewire.h>

#include "a53_node.h"
#include "cJSON.h"

static enum pw_direction to_pw_direction(enum am62d_port_dir dir)
{
	switch (dir) {
	case AM62D_DIR_IN:
		return PW_DIRECTION_INPUT;
	case AM62D_DIR_OUT:
		return PW_DIRECTION_OUTPUT;
	default:
		fprintf(stderr, "a53_node: unknown port direction %d\n", dir);
		return PW_DIRECTION_INPUT;
	}
}

static void on_process(void *data, struct spa_io_position *pos)
{
	struct a53_node *node = data;
	const float *in[MAX_PORTS];
	float *out[MAX_PORTS];
	uint32_t n_frames = pos->clock.duration;

	for (int i = 0; i < node->n_in; i++)
		in[i] = pw_filter_get_dsp_buffer(node->in_ports[i], n_frames);
	for (int i = 0; i < node->n_out; i++)
		out[i] = pw_filter_get_dsp_buffer(node->out_ports[i], n_frames);

	node->plugin->process(node->priv, in, out, n_frames);
}

static const struct pw_filter_events filter_events = {
	PW_VERSION_FILTER_EVENTS,
	.process = on_process,
};

struct a53_node *a53_node_create(struct pw_core *core,
				 const struct am62d_plugin *plugin,
				 const char *node_id,
				 const struct cJSON *config_json)
{
	struct a53_node *node = calloc(1, sizeof(struct a53_node));
	if (!node)
		goto exit;

	node->plugin = plugin;

	int ret = plugin->init(&node->priv, config_json);
	if (ret < 0)
		goto free_node;

	node->filter = pw_filter_new(core, plugin->name,
		pw_properties_new(
			PW_KEY_MEDIA_TYPE, "AUDIO",
			PW_KEY_MEDIA_CATEGORY, "FILTER",
			"am62d.node.id", node_id,
			NULL));
	if (!node->filter)
		goto destroy_plugin;

	pw_filter_add_listener(node->filter,
			&node->filter_listener,
			&filter_events,
			node);

	for (int i = 0; i < plugin->n_ports; i++) {
		if (plugin->ports[i].type == AM62D_PORT_METADATA ||
			plugin->ports[i].type == AM62D_PORT_CONTROL)
			continue;

		char dsp_format[64];
		uint32_t ch = plugin->ports[i].u.pcm.n_channels;
		if (ch == 1)
			snprintf(dsp_format, sizeof(dsp_format), "32 bit float mono audio");
		else
			snprintf(dsp_format, sizeof(dsp_format), "32 bit %u channel audio", ch);

		struct port_data *pd = pw_filter_add_port(node->filter,
				to_pw_direction(plugin->ports[i].dir),
				PW_FILTER_PORT_FLAG_MAP_BUFFERS,
				sizeof(struct port_data),
				pw_properties_new(
					PW_KEY_FORMAT_DSP, dsp_format,
					PW_KEY_PORT_NAME, plugin->ports[i].name,
					NULL),
				NULL, 0);
		if (!pd)
			goto destroy_filter;

		struct port_data **port_arr = (plugin->ports[i].dir == AM62D_DIR_IN)
			? node->in_ports : node->out_ports;
		int *n = (plugin->ports[i].dir == AM62D_DIR_IN)
			? &node->n_in : &node->n_out;

		if (*n >= MAX_PORTS)
			goto destroy_filter;

		port_arr[*n] = pd;
		(*n)++;
	}

	ret = pw_filter_connect(node->filter,
			PW_FILTER_FLAG_RT_PROCESS,
			NULL, 0);
	if (ret < 0)
		goto destroy_filter;

	return node;

destroy_filter:
	pw_filter_destroy(node->filter);
destroy_plugin:
	plugin->destroy(node->priv);
free_node:
	free(node);
exit:
	return NULL;
}

void a53_node_destroy(struct a53_node *node)
{
	pw_filter_destroy(node->filter);
	node->plugin->destroy(node->priv);
	free(node);
}
