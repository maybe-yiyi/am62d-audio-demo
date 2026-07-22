#ifndef PUBLISH_H
#define PUBLISH_H

#include <stddef.h>

/**
 * publish_init() - initialize data publishing subsystem
 * @data_streams: array of data stream names
 * @n_data_streams: number of data streams
 *
 * Initializes the publishing subsystem for the specified data streams.
 * Must be called before using am62d_publish().
 *
 * Return: None
 */
void publish_init(const char **data_streams, int n_data_streams);

/**
 * am62d_publish() - publish JSON data to a named stream
 * @data_stream: name of data stream to publish to
 * @json: JSON data buffer
 * @len: length of JSON data
 *
 * Publishes JSON formatted data to the specified data stream.
 * Stream must have been registered via publish_init().
 *
 * Return: None
 */
void am62d_publish(const char *data_stream, const char *json, size_t len);

/**
 * publish_destroy() - shutdown data publishing subsystem
 *
 * Cleans up resources associated with data publishing.
 *
 * Return: None
 */
void publish_destroy(void);

#endif /* PUBLISH_H */
