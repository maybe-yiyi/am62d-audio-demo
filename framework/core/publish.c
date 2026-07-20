#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "publish.h"

#define FIFO_PREFIX "/tmp/am62d_"

struct data_stream {
	char path[64];
	int fd;
};

static struct data_stream data_streams[MAX_DATA_STREAMS];
static int n_data_streams = 0;

void publish_init(const char **names, int n)
{
	/* close any fds left open from a previous call */
	for (int i = 0; i < n_data_streams; i++) {
		if (data_streams[i].fd >= 0) {
			close(data_streams[i].fd);
			data_streams[i].fd = -1;
		}
	}

	signal(SIGPIPE, SIG_IGN);
	n_data_streams = n < MAX_DATA_STREAMS ? n : MAX_DATA_STREAMS;
	for (int i = 0; i < n_data_streams; i++) {
		snprintf(data_streams[i].path, sizeof(data_streams[i].path),
			FIFO_PREFIX "%s", names[i]);
		data_streams[i].fd = -1;

		if (mkfifo(data_streams[i].path, 0666) < 0 && errno != EEXIST)
			fprintf(stderr, "publish: mkfifo %s: %s\n",
				data_streams[i].path, strerror(errno));
	}
}

void am62d_publish(const char *channel, const char *json, size_t len)
{
	for (int i = 0; i < n_data_streams; i++) {
		if (strcmp(data_streams[i].path + sizeof(FIFO_PREFIX) - 1, channel) != 0)
			continue;

		if (data_streams[i].fd < 0) {
			data_streams[i].fd = open(data_streams[i].path, O_WRONLY | O_NONBLOCK);
			if (data_streams[i].fd < 0)
				return;
		}

		if (len > 4095)
			return;

		char buf[4096];
		memcpy(buf, json, len);
		buf[len] = '\n';

		ssize_t written = write(data_streams[i].fd, buf, len + 1);
		if (written < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				close(data_streams[i].fd);
				data_streams[i].fd = -1;
			}
		} else if ((size_t)written < len + 1) {
			/* partial write: FIFO buffer full; close so next call retries open */
			close(data_streams[i].fd);
			data_streams[i].fd = -1;
		}
		return;
	}

	static char warned[MAX_DATA_STREAMS][32];
	static int n_warned = 0;
	for (int i = 0; i < n_warned; i++)
		if (strncmp(warned[i], channel, 31) == 0)
			return;
	if (n_warned < MAX_DATA_STREAMS) {
		strncpy(warned[n_warned], channel, 31);
		warned[n_warned++][31] = '\0';
		fprintf(stderr, "publish: data stream '%s' not registered (missing from config)\n", channel);
	}
}

void publish_destroy(void)
{
	for (int i = 0; i < n_data_streams; i++) {
		if (data_streams[i].fd >= 0) {
			close(data_streams[i].fd);
			data_streams[i].fd = -1;
		}
		unlink(data_streams[i].path);
	}
	n_data_streams = 0;
}
