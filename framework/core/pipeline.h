#ifndef PIPELINE_H
#define PIPELINE_H

#include <spa/node/io.h>
#include "config.h"
#include "dataloop.h"
#include "plugin.h"

#define PIPELINE_SAMPLE_RATE 48000
#define PIPELINE_QUANTUM  1024

struct pipeline {
	struct pipeline_config *config;
	struct spa_dataloop *dataloop;

	struct spa_node_wrapper *sources[MAX_NODES];
	int n_sources;

	struct spa_node_wrapper *nodes[MAX_NODES];
	int n_nodes;

	struct spa_node_wrapper *sinks[MAX_NODES];
	int n_sinks;

	struct spa_io_clock clock;
	struct spa_io_position position;

	uint32_t sample_rate;
	uint32_t quantum;
};

struct pipeline *pipeline_create(const char *config_path);
void pipeline_run(struct pipeline *pl);
void pipeline_destroy(struct pipeline *pl);

#endif /* PIPELINE_H */
