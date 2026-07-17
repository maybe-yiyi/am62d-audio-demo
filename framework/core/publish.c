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

struct channel {
	char path[64];
	int fd;
};

static struct channel channels[MAX_CHANNELS];
static int n_channels = 0;

void publish_init(const char **names, int n)
{
	/* close any fds left open from a previous call */
	for (int i = 0; i < n_channels; i++) {
		if (channels[i].fd >= 0) {
			close(channels[i].fd);
			channels[i].fd = -1;
		}
	}

	signal(SIGPIPE, SIG_IGN);
	n_channels = n < MAX_CHANNELS ? n : MAX_CHANNELS;
	for (int i = 0; i < n_channels; i++) {
		snprintf(channels[i].path, sizeof(channels[i].path),
			FIFO_PREFIX "%s", names[i]);
		channels[i].fd = -1;

		if (mkfifo(channels[i].path, 0666) < 0 && errno != EEXIST)
			fprintf(stderr, "publish: mkfifo %s: %s\n",
				channels[i].path, strerror(errno));
	}
}

void am62d_publish(const char *channel, const char *json, size_t len)
{
	for (int i = 0; i < n_channels; i++) {
		if (strcmp(channels[i].path + sizeof(FIFO_PREFIX) - 1, channel) != 0)
			continue;

		if (channels[i].fd < 0) {
			channels[i].fd = open(channels[i].path, O_WRONLY | O_NONBLOCK);
			if (channels[i].fd < 0)
				return;
		}

		if (len > 4095)
			return;

		char buf[4096];
		memcpy(buf, json, len);
		buf[len] = '\n';

		ssize_t written = write(channels[i].fd, buf, len + 1);
		if (written < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				close(channels[i].fd);
				channels[i].fd = -1;
			}
		} else if ((size_t)written < len + 1) {
			/* partial write: FIFO buffer full; close so next call retries open */
			close(channels[i].fd);
			channels[i].fd = -1;
		}
		return;
	}

	static char warned[MAX_CHANNELS][32];
	static int n_warned = 0;
	for (int i = 0; i < n_warned; i++)
		if (strncmp(warned[i], channel, 31) == 0)
			return;
	if (n_warned < MAX_CHANNELS) {
		strncpy(warned[n_warned], channel, 31);
		warned[n_warned++][31] = '\0';
		fprintf(stderr, "publish: channel '%s' not registered (missing from config)\n", channel);
	}
}

void publish_destroy(void)
{
	for (int i = 0; i < n_channels; i++) {
		if (channels[i].fd >= 0) {
			close(channels[i].fd);
			channels[i].fd = -1;
		}
		unlink(channels[i].path);
	}
	n_channels = 0;
}
