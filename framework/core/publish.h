#ifndef PUBLISH_H
#define PUBLISH_H

#include <stddef.h>

void publish_init(const char **channels, int n_channels);
void am62d_publish(const char *channel, const char *json, size_t len);
void publish_destroy(void);

#endif /* PUBLISH_H */
