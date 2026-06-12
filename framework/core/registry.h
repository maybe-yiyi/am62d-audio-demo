#ifndef AM62D_REGISTRY_H
#define AM62D_REGISTRY_H

#include "am62d_plugin.h"

int registry_init(const char *dir);
const struct am62d_plugin *registry_get(const char *name);
void registry_destroy(void);

#endif
