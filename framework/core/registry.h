#ifndef AM62D_REGISTRY_H
#define AM62D_REGISTRY_H

#include <lilv/lilv.h>

int registry_init(const char *dir);
LilvInstance *registry_get(const char *uri);
void registry_destroy(void);

#endif
