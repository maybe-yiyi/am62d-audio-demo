#ifndef PUBLISH_H
#define PUBLISH_H

#include <stddef.h>

void publish_init(const char **data_streams, int n_data_streams);
void am62d_publish(const char *data_stream, const char *json, size_t len);
void publish_destroy(void);

#endif /* PUBLISH_H */
