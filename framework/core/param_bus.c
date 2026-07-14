#include <math.h>
#include <stdio.h>
#include <string.h>

#include "param_bus.h"

int a53_node_find_ctrl(const struct a53_node *node, const char *symbol,
		       bool want_input)
{
	if (!node || !symbol)
		return -1;

	for (int i = 0; i < node->n_ctrl; i++) {
		if (node->ctrl_ports[i].is_input != want_input)
			continue;
		if (strcmp(node->ctrl_ports[i].symbol, symbol) == 0)
			return node->ctrl_ports[i].buf_index;
	}
	return -1;
}

int a53_node_set_text(struct a53_node *node, const char *key,
		      const char *text, float confidence)
{
	if (!node || !key || !text)
		return -1;

	for (int i = 0; i < node->n_text_params; i++) {
		if (strcmp(node->text_keys[i], key) == 0) {
			snprintf(node->text_values[i], sizeof(node->text_values[i]),
				 "%s", text);
			node->text_confidence[i] = confidence;
			return 0;
		}
	}

	if (node->n_text_params >= MAX_TEXT_PARAMS)
		return -1;

	int i = node->n_text_params++;
	snprintf(node->text_keys[i], sizeof(node->text_keys[i]), "%s", key);
	snprintf(node->text_values[i], sizeof(node->text_values[i]), "%s", text);
	node->text_confidence[i] = confidence;
	return 0;
}

struct float_link {
	struct a53_node *src;
	int src_buf;
	struct a53_node *dst;
	int dst_buf;
	float last_value;
	bool active;
};

struct text_link {
	struct a53_node *src;
	struct a53_node *dst;
	char param_key[TEXT_PARAM_KEY_LEN];
	bool active;
};

static struct {
	struct float_link floats[MAX_PARAM_LINKS];
	int n_floats;
	struct text_link texts[MAX_PARAM_LINKS];
	int n_texts;
} bus;

void param_bus_init(void)
{
	memset(&bus, 0, sizeof(bus));
}

void param_bus_reset(void)
{
	param_bus_init();
}

int param_bus_register(struct a53_node *src, const char *port_name,
		       struct a53_node *target, const char *param_key)
{
	if (!src || !target || !port_name || !param_key)
		return -1;

	bool registered = false;

	int src_buf = a53_node_find_ctrl(src, port_name, false);
	int dst_buf = a53_node_find_ctrl(target, param_key, true);

	if (src_buf >= 0 && dst_buf >= 0) {
		if (bus.n_floats >= MAX_PARAM_LINKS) {
			fprintf(stderr, "param_bus: float link table full\n");
			return -1;
		}
		struct float_link *fl = &bus.floats[bus.n_floats++];
		fl->src = src;
		fl->src_buf = src_buf;
		fl->dst = target;
		fl->dst_buf = dst_buf;
		fl->last_value = NAN;
		fl->active = true;
		registered = true;
	}

	if (bus.n_texts < MAX_PARAM_LINKS) {
		struct text_link *tl = &bus.texts[bus.n_texts++];
		tl->src = src;
		tl->dst = target;
		snprintf(tl->param_key, sizeof(tl->param_key), "%s", param_key);
		tl->active = true;
		registered = true;
	} else if (!registered) {
		fprintf(stderr, "param_bus: text link table full\n");
		return -1;
	}

	return 0;
}

void param_bus_dispatch(struct a53_node *node)
{
	if (!node)
		return;

	for (int i = 0; i < bus.n_floats; i++) {
		struct float_link *fl = &bus.floats[i];
		if (!fl->active || fl->src != node)
			continue;

		float v = node->ctrl_bufs[fl->src_buf];
		if (!isnan(fl->last_value) && v == fl->last_value)
			continue;

		fl->dst->ctrl_bufs[fl->dst_buf] = v;
		fl->last_value = v;
	}

	uint32_t seq;
	char key[TEXT_PARAM_KEY_LEN];
	char text[TEXT_PARAM_VAL_LEN];
	float confidence;

	pthread_mutex_lock(&node->text_lock);
	seq = node->outbox_seq;
	if (seq != node->last_text_seq) {
		snprintf(key, sizeof(key), "%s", node->outbox_key);
		snprintf(text, sizeof(text), "%s", node->outbox_text);
		confidence = node->outbox_confidence;
		node->last_text_seq = seq;
	} else {
		seq = node->last_text_seq;
		key[0] = '\0';
		text[0] = '\0';
		confidence = 0.0f;
	}
	pthread_mutex_unlock(&node->text_lock);

	if (key[0] == '\0')
		return;

	for (int i = 0; i < bus.n_texts; i++) {
		struct text_link *tl = &bus.texts[i];
		if (!tl->active || tl->src != node)
			continue;

		/*
		 * Deliver when the published key matches the control_links
		 * param, or when the published key is empty.
		 */
		if (key[0] && strcmp(key, tl->param_key) != 0)
			continue;

		if (a53_node_set_text(tl->dst, tl->param_key, text, confidence) == 0) {
			fprintf(stderr, "param_bus: %s -> %s = \"%s\" (%.2f)\n",
				key[0] ? key : tl->param_key, tl->param_key,
				text, confidence);
		}
	}
}
