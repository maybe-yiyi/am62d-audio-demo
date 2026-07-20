/* no-op stub; tests link this instead of framework/core/publish.c */

#include <stddef.h>

void am62d_publish(const char *data_stream, const char *json, size_t len)
{
	(void)data_stream;
	(void)json;
	(void)len;
}
