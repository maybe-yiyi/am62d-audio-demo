/* no-op stub; tests link this instead of framework/core/publish.c */

#include <stddef.h>

void am62d_publish(const char *channel, const char *json, size_t len)
{
	(void)channel;
	(void)json;
	(void)len;
}
