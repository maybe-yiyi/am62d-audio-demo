#ifndef AM62D_REGISTRY_H
#define AM62D_REGISTRY_H

#include <spa/support/plugin.h>

int registry_init(const char *dir);
const struct spa_handle_factory *registry_get(const char *name);
void registry_destroy(void);

#endif
