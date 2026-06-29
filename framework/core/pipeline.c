#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/utils/dict.h>

#include "config.h"
#include "dataloop.h"
#include "driver.h"
#include "pipeline.h"
#include "plugin.h"

static int ready_cb(void *data, int status)
{
	struct pipeline *pl = data;
	(void)status;

	for (int i = 0; i < pl->n_sources; i++)
		spa_node_process(pl->sources[i]->node);
	for (int i = 0; i < pl->n_nodes; i++)
		spa_node_process(pl->nodes[i]->node);
	for (int i = 0; i < pl->n_sinks; i++)
		spa_node_process(pl->sinks[i]->node);

	return 0;
}

static const struct spa_node_callbacks source_cbs = {
	SPA_VERSION_NODE_CALLBACKS,
	.ready = ready_cb,
};

static struct spa_node_wrapper *find_node_by_id(const struct pipeline *pl, const char *id)
{
	for (int i = 0; i < pl->n_sources; i++)
		if (strcmp(pl->config->sources[i].id, id) == 0) return pl->sources[i];
	for (int i = 0; i < pl->n_nodes; i++)
		if (strcmp(pl->config->plugins[i].id, id) == 0) return pl->nodes[i];
	for (int i = 0; i < pl->n_sinks; i++)
		if (strcmp(pl->config->sinks[i].id, id) == 0) return pl->sinks[i];
	return NULL;
}

static void pipeline_wire_buffers(struct pipeline *pl)
{
	for (int i = 0; i < pl->config->n_links; i++) {
		const struct link_config *lc = &pl->config->links[i];

		char from_node[64], from_port[64];
		char to_node[64], to_port[64];

		if (sscanf(lc->from, "%63[^:]:%63s", from_node, from_port) != 2 ||
		    sscanf(lc->to, "%63[^:]:%63s", to_node, to_port) != 2) {
			fprintf(stderr, "pipeline: malformed link '%s' -> '%s'\n",
				lc->from, lc->to);
			continue;
		}

		struct spa_node_wrapper *src = find_node_by_id(pl, from_node);
		struct spa_node_wrapper *dst = find_node_by_id(pl, to_node);

		if (!src || !dst) {
			fprintf(stderr, "pipeline: link '%s' -> '%s': node not found\n",
				lc->from, lc->to);
			continue;
		}

		uint32_t src_port = 0, dst_port = 0;
		sscanf(from_port, "%*[^0-9]%u", &src_port);
		sscanf(to_port, "%*[^0-9]%u", &dst_port);

		if (src_port >= src->n_output_ports || dst_port >= dst->n_input_ports) {
			fprintf(stderr, "pipeline: link '%s' -> '%s': port index out of range\n",
				lc->from, lc->to);
			continue;
		}

		dst->input_buffers[dst_port] = src->output_buffers[src_port];
		spa_node_port_set_io(dst->node, SPA_DIRECTION_INPUT, dst_port,
				     SPA_IO_Buffers,
				     &src->out_io[src_port],
				     sizeof(src->out_io[src_port]));

		printf("Wired '%s' -> '%s'\n", lc->from, lc->to);
	}
}

static const char *plugin_path(const struct node_config *nc, char *buf, size_t len)
{
	if (nc->plugin_path[0] != '\0')
		return nc->plugin_path;
	snprintf(buf, len, "/usr/lib/am62d/plugins/libam62d_%s.so", nc->plugin);
	return buf;
}

struct pipeline *pipeline_create(const char *config_path)
{
	struct pipeline *pl = calloc(1, sizeof(*pl));
	if (!pl)
		return NULL;

	pl->config = config_load(config_path);
	if (!pl->config) {
		free(pl);
		return NULL;
	}

	pl->sample_rate = PIPELINE_SAMPLE_RATE;
	pl->quantum = PIPELINE_QUANTUM;

	printf("Loading pipeline '%s'\n", pl->config->name);

	if (pl->config->n_sources > 0 || pl->config->n_sinks > 0) {
		pl->dataloop = spa_dataloop_create();
		if (!pl->dataloop) {
			fprintf(stderr, "pipeline: failed to create data loop\n");
			goto fail;
		}
	}

	uint32_t n_support = 0;
	const struct spa_support *support = pl->dataloop
		? spa_dataloop_support(pl->dataloop, &n_support)
		: NULL;

	for (int i = 0; i < pl->config->n_sources; i++) {
		const struct io_config *ic = &pl->config->sources[i];
		const struct driver_info *drv = driver_lookup(ic->driver);
		if (!drv) {
			fprintf(stderr, "pipeline: unknown driver '%s'\n", ic->driver);
			goto fail;
		}

		char ch_str[16], rate_str[16];
		snprintf(ch_str, sizeof(ch_str), "%u", ic->channels);
		snprintf(rate_str, sizeof(rate_str), "%u", ic->rate);
		struct spa_dict_item items[] = {
			{ "api.alsa.path", ic->device },
			{ "audio.channels", ch_str },
			{ "audio.rate", rate_str },
		};
		struct spa_dict info = SPA_DICT_INIT(items, 3);

		printf("Loading source '%s' (%s, device=%s)\n",
			ic->id, drv->source_factory, ic->device);

		struct spa_node_wrapper *wrap = spa_plugin_load(
			drv->lib_path, drv->source_factory, &info, support, n_support);
		if (!wrap) {
			fprintf(stderr, "pipeline: failed to load source '%s'\n", ic->id);
			goto fail;
		}
		wrap->n_output_ports = ic->channels;
		wrap->n_input_ports = 0;

		if (spa_node_setup_buffers(wrap, pl->quantum) < 0) {
			fprintf(stderr, "pipeline: failed to setup buffers for source '%s'\n", ic->id);
			spa_plugin_unload(wrap);
			goto fail;
		}

		pl->sources[pl->n_sources++] = wrap;
	}

	for (int i = 0; i < pl->config->n_plugins; i++) {
		const struct node_config *nc = &pl->config->plugins[i];
		char path_buf[256];
		const char *path = plugin_path(nc, path_buf, sizeof(path_buf));

		struct spa_dict_item items[nc->n_params];
		char val_bufs[MAX_NODE_PARAMS][32];
		for (int j = 0; j < nc->n_params; j++) {
			items[j].key = nc->typed_params[j].key;
			switch (nc->typed_params[j].type) {
			case AM62D_PARAM_INT:
				snprintf(val_bufs[j], sizeof(val_bufs[j]), "%d", nc->typed_params[j].v.i);
				items[j].value = val_bufs[j]; break;
			case AM62D_PARAM_FLOAT:
				snprintf(val_bufs[j], sizeof(val_bufs[j]), "%.6g", nc->typed_params[j].v.f);
				items[j].value = val_bufs[j]; break;
			case AM62D_PARAM_STRING:
				items[j].value = nc->typed_params[j].v.s; break;
			}
		}
		struct spa_dict info = SPA_DICT_INIT(items, nc->n_params);

		printf("Loading plugin '%s' from %s\n", nc->id, path);

		struct spa_node_wrapper *wrap = spa_plugin_load(path, nc->plugin, &info, NULL, 0);
		if (!wrap) {
			fprintf(stderr, "pipeline: failed to load plugin '%s'\n", nc->id);
			goto fail;
		}
		if (spa_node_configure_ports(wrap, pl->sample_rate) < 0 ||
			spa_node_setup_buffers(wrap, pl->quantum) < 0) {
			fprintf(stderr, "pipeline: failed to configure plugin '%s'\n", nc->id);
			spa_plugin_unload(wrap);
			goto fail;
		}
		pl->nodes[pl->n_nodes++] = wrap;
	}

	for (int i = 0; i < pl->config->n_sinks; i++) {
		const struct io_config *ic = &pl->config->sinks[i];
		const struct driver_info *drv = driver_lookup(ic->driver);
		if (!drv) {
			fprintf(stderr, "pipeline: unknown driver '%s'\n", ic->driver);
			goto fail;
		}

		char ch_str[16], rate_str[16];
		snprintf(ch_str, sizeof(ch_str), "%u", ic->channels);
		snprintf(rate_str, sizeof(rate_str), "%u", ic->rate);
		struct spa_dict_item items[] = {
			{ "api.alsa.path", ic->device },
			{ "audio.channels", ch_str },
			{ "audio.rate", rate_str },
		};
		struct spa_dict info = SPA_DICT_INIT(items, 3);

		printf("Loading sink '%s' (%s, device=%s)\n",
			ic->id, drv->sink_factory, ic->device);

		struct spa_node_wrapper *wrap = spa_plugin_load(
			drv->lib_path, drv->sink_factory, &info, support, n_support);
		if (!wrap) {
			fprintf(stderr, "pipeline: failed to load sink '%s'\n", ic->id);
			goto fail;
		}
		wrap->n_input_ports = ic->channels;
		wrap->n_output_ports = 0;

		if (spa_node_setup_buffers(wrap, pl->quantum) < 0) {
			fprintf(stderr, "pipeline: failed to setup buffers for sink '%s'\n", ic->id);
			spa_plugin_unload(wrap);
			goto fail;
		}

		pl->sinks[pl->n_sinks++] = wrap;
	}

	pl->position.state = SPA_IO_POSITION_STATE_RUNNING;
	pl->position.clock.rate.num = 1;
	pl->position.clock.rate.denom = pl->sample_rate;
	pl->position.clock.duration = pl->quantum;

	for (int i = 0; i < pl->n_sources; i++)
		spa_node_set_clock(pl->sources[i], &pl->clock, &pl->position);
	for (int i = 0; i < pl->n_nodes; i++)
		spa_node_set_clock(pl->nodes[i], &pl->clock, &pl->position);
	for (int i = 0; i < pl->n_sinks; i++)
		spa_node_set_clock(pl->sinks[i], &pl->clock, &pl->position);

	pipeline_wire_buffers(pl);

	for (int i = 0; i < pl->n_sinks; i++) spa_node_start(pl->sinks[i]);
	for (int i = 0; i < pl->n_nodes; i++) spa_node_start(pl->nodes[i]);
	for (int i = 0; i < pl->n_sources; i++) spa_node_start(pl->sources[i]);

	for (int i = 0; i < pl->n_sources; i++)
		spa_node_set_callbacks(pl->sources[i]->node, &source_cbs, pl);

	return pl;

fail:
	pipeline_destroy(pl);
	return NULL;
}

void pipeline_run(struct pipeline *pl)
{
	printf("Running pipeline (Ctrl-C to stop)...\n");

	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGTERM);

	int sig = 0;
	sigwait(&ss, &sig);

	printf("Caught signal %d, shutting down\n", sig);
}

void pipeline_destroy(struct pipeline *pl)
{
	if (!pl)
		return;

	for (int i = 0; i < pl->n_sources; i++) {
		if (pl->sources[i]) {
			spa_node_stop(pl->sources[i]);
			spa_plugin_unload(pl->sources[i]);
		}
	}
	for (int i = 0; i < pl->n_nodes; i++) {
		if (pl->nodes[i]) {
			spa_node_stop(pl->nodes[i]);
			spa_plugin_unload(pl->nodes[i]);
		}
	}
	for (int i = 0; i < pl->n_sinks; i++) {
		if (pl->sinks[i]) {
			spa_node_stop(pl->sinks[i]);
			spa_plugin_unload(pl->sinks[i]);
		}
	}

	if (pl->dataloop)
		spa_dataloop_destroy(pl->dataloop);

	config_free(pl->config);
	free(pl);
}
