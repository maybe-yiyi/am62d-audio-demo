#include <string.h>

#include "param_bus.h"

static int ctrl_port_idx(const struct am62d_plugin *plugin, const char *name)
{
	int idx = 0;
	for (int i = 0; i < plugin->n_ports; i++) {
		const struct am62d_port_desc *pd = &plugin->ports[i];
		if (pd->type != AM62D_PORT_CONTROL || pd->dir != AM62D_DIR_OUT)
			continue;
		if (strcmp(pd->name, name) == 0)
			return idx;
		idx++;
	}
	return -1;
}

void param_bus_dispatch(struct a53_node *node)
{
	int n = __atomic_load_n(&node->n_ctrl_in, __ATOMIC_ACQUIRE);
	for (int i = 0; i < n; i++) {
		struct ctrl_route *r = &node->ctrl_in_routes[i];
		r->target->plugin->set_control(r->target->priv, r->param_key,
					       node->ctrl_out_vals[r->ctrl_out_idx]);
	}
}

int param_bus_register(struct a53_node *src, const char *port_name,
		       struct a53_node *target, const char *param_key)
{
	int idx = ctrl_port_idx(src->plugin, port_name);
	if (idx < 0)
		return -1;

	struct ctrl_route *r = &src->ctrl_in_routes[src->n_ctrl_in];
	r->ctrl_out_idx = idx;
	r->target = target;
	snprintf(r->param_key, sizeof(r->param_key), "%s", param_key);
	__atomic_store_n(&src->n_ctrl_in, src->n_ctrl_in + 1, __ATOMIC_RELEASE);
	return 0;
}
