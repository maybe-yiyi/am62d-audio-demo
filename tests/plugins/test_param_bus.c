#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../framework/core/a53_node.h"
#include "../../framework/core/param_bus.h"

static void setup_out_ctrl(struct a53_node *n, const char *sym, int buf)
{
	snprintf(n->ctrl_ports[buf].symbol, sizeof(n->ctrl_ports[buf].symbol),
		 "%s", sym);
	n->ctrl_ports[buf].buf_index = buf;
	n->ctrl_ports[buf].is_input = false;
	n->ctrl_bufs[buf] = 0.0f;
	if (buf + 1 > n->n_ctrl)
		n->n_ctrl = buf + 1;
}

static void setup_in_ctrl(struct a53_node *n, const char *sym, int buf)
{
	snprintf(n->ctrl_ports[buf].symbol, sizeof(n->ctrl_ports[buf].symbol),
		 "%s", sym);
	n->ctrl_ports[buf].buf_index = buf;
	n->ctrl_ports[buf].is_input = true;
	n->ctrl_bufs[buf] = 0.0f;
	if (buf + 1 > n->n_ctrl)
		n->n_ctrl = buf + 1;
}

int main(void)
{
	struct a53_node src;
	struct a53_node dst;

	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	pthread_mutex_init(&src.text_lock, NULL);
	pthread_mutex_init(&dst.text_lock, NULL);

	setup_out_ctrl(&src, "confidence", 0);
	setup_out_ctrl(&src, "hit", 1);
	setup_in_ctrl(&dst, "ns_level", 0);

	param_bus_reset();

	assert(param_bus_register(&src, "confidence", &dst, "ns_level") == 0);
	assert(param_bus_register(&src, "hit", &dst, "command") == 0);

	src.ctrl_bufs[0] = 0.75f;
	param_bus_dispatch(&src);
	assert(fabsf(dst.ctrl_bufs[0] - 0.75f) < 1e-6f);

	/* Unchanged value should not thrash (still equal). */
	param_bus_dispatch(&src);
	assert(fabsf(dst.ctrl_bufs[0] - 0.75f) < 1e-6f);

	/* Publish text through the same path a53_node uses. */
	pthread_mutex_lock(&src.text_lock);
	snprintf(src.outbox_key, sizeof(src.outbox_key), "%s", "command");
	snprintf(src.outbox_text, sizeof(src.outbox_text), "%s", "yes");
	src.outbox_confidence = 0.91f;
	src.outbox_seq++;
	pthread_mutex_unlock(&src.text_lock);

	param_bus_dispatch(&src);

	assert(dst.n_text_params == 1);
	assert(strcmp(dst.text_keys[0], "command") == 0);
	assert(strcmp(dst.text_values[0], "yes") == 0);
	assert(fabsf(dst.text_confidence[0] - 0.91f) < 1e-6f);

	pthread_mutex_destroy(&src.text_lock);
	pthread_mutex_destroy(&dst.text_lock);

	printf("PASS: param_bus\n");
	return 0;
}
