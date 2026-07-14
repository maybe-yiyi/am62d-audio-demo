#ifndef A53_NODE_H
#define A53_NODE_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <lv2/core/lv2.h>
#include <pipewire/pipewire.h>
#include <lilv/lilv.h>

#include "plugins/am62d_params.h"

#define MAX_PORTS 8
#define MAX_CTRL_PORTS 16
#define MAX_TEXT_PARAMS 8
#define CTRL_SYMBOL_LEN 64
#define TEXT_PARAM_KEY_LEN 64
#define TEXT_PARAM_VAL_LEN 128

struct port_data {
	/* intentionally empty */
};

struct ctrl_port_info {
	char symbol[CTRL_SYMBOL_LEN];
	uint32_t lv2_index;
	int buf_index;
	bool is_input;
};

struct a53_node {
	struct pw_filter *filter;
	struct spa_hook filter_listener;
	const LilvPlugin *plugin;
	LilvInstance *instance;

	struct port_data *in_ports[MAX_PORTS];
	struct port_data *out_ports[MAX_PORTS];
	uint32_t in_port_indices[MAX_PORTS];
	uint32_t out_port_indices[MAX_PORTS];
	int n_in;
	int n_out;

	float ctrl_bufs[MAX_CTRL_PORTS];
	struct ctrl_port_info ctrl_ports[MAX_CTRL_PORTS];
	int n_ctrl;

	char text_keys[MAX_TEXT_PARAMS][TEXT_PARAM_KEY_LEN];
	char text_values[MAX_TEXT_PARAMS][TEXT_PARAM_VAL_LEN];
	float text_confidence[MAX_TEXT_PARAMS];
	int n_text_params;

	/* Thread-safe outbox for text published by the plugin worker. */
	pthread_mutex_t text_lock;
	char outbox_key[TEXT_PARAM_KEY_LEN];
	char outbox_text[TEXT_PARAM_VAL_LEN];
	float outbox_confidence;
	uint32_t outbox_seq;
	uint32_t last_text_seq;

	struct am62d_params params;
	LV2_Feature params_feature;
	const LV2_Feature *features[2];
};

struct a53_node *a53_node_create(struct pw_core *core,
				 LilvWorld *world,
				 const LilvPlugin *plugin,
				 const char *node_name,
				 const char **linked_ports,
				 int n_linked_ports);
void a53_node_destroy(struct a53_node *node);

/* Returns buf_index of a control port with the given symbol, or -1. */
int a53_node_find_ctrl(const struct a53_node *node, const char *symbol,
		       bool want_input);

/* Set or update a host-side text param slot on the node. */
int a53_node_set_text(struct a53_node *node, const char *key,
		      const char *text, float confidence);

#endif
