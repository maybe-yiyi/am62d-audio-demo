#include <stdio.h>

#include "param_bus.h"

void param_bus_dispatch(struct a53_node *node)
{
	(void)node;
}

int param_bus_register(struct a53_node *src, const char *port_name,
		       struct a53_node *target, const char *param_key)
{
	(void)src; (void)port_name; (void)target; (void)param_key;
	fprintf(stderr, "param_bus: control routing not yet implemented for LV2\n");
	return -1;
}
