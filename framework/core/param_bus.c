#include <string.h>

#include <spa/node/io.h>
#include <spa/pod/pod.h>
#include <spa/pod/iter.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/control/control.h>
#include <spa/param/props.h>

#include "param_bus.h"
#include "am62d_spa.h"

#define MAX_ROUTES 32

struct param_route {
	struct a53_node *src;
	const char *src_param;
	struct a53_node *dst;
	const char *dst_param;
};

static struct param_route routes[MAX_ROUTES];
static int n_routes = 0;

void param_bus_dispatch(struct a53_node *node)
{
	for (int i = 0; i < n_routes; i++) {
		struct param_route *r = &routes[i];
		if (r->src != node)
			continue;

		/* Decode all params from src notify_out */
		struct am62d_param src_params[AM62D_MAX_PARAMS];
		int n_src = am62d_params_decode(&node->notify_out, src_params, AM62D_MAX_PARAMS);
		if (n_src <= 0)
			continue;

		/* Find matching param by key */
		for (int j = 0; j < n_src; j++) {
			if (strcmp(src_params[j].key, r->src_param) != 0)
				continue;

			/* Re-encode with the destination key into dst control_in */
			struct am62d_param dst_param = src_params[j];
			dst_param.key = r->dst_param;

			uint8_t buf[512];
			int enc_size = am62d_params_encode(buf, sizeof(buf), &dst_param, 1);
			if (enc_size > 0)
				memcpy(&r->dst->control_in, buf, enc_size);

			break;
		}
	}
}

int param_bus_register(struct a53_node *src, const char *src_param,
		       struct a53_node *target, const char *dst_param)
{
	if (n_routes >= MAX_ROUTES)
		return -1;

	routes[n_routes].src = src;
	routes[n_routes].src_param = src_param;
	routes[n_routes].dst = target;
	routes[n_routes].dst_param = dst_param;
	n_routes++;

	return 0;
}
