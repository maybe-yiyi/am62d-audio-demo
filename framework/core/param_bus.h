#ifndef PARAM_BUS_H
#define PARAM_BUS_H

#include "a53_node.h"

#define MAX_PARAM_LINKS 32

void param_bus_init(void);
void param_bus_reset(void);

void param_bus_dispatch(struct a53_node *node);
int param_bus_register(struct a53_node *src, const char *port_name,
		       struct a53_node *target, const char *param_key);

#endif /* PARAM_BUS_H */
