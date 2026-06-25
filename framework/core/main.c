#include <getopt.h>
#include <pthread.h>
#include <signal.h>

#include "pipeline.h"

int main(int argc, char *argv[]) {
	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &ss, NULL);

	const char *config_path = NULL;

	static const struct option opts[] = {
		{ "config", required_argument, NULL, 'c' },
		{ NULL, 0, NULL, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
		switch (c) {
		case 'c':
			config_path = optarg;
			break;
		default:
			fprintf(stderr, "Usage: %s --config=PATH\n", argv[0]);
			return 1;
		}
	}

	if (!config_path) {
		fprintf(stderr, "Usage: %s --config=PATH\n", argv[0]);
		return 1;
	}

	struct pipeline *pl = pipeline_create(config_path);
	if (!pl) {
		fprintf(stderr, "Failed to create pipeline!\n");
		return 1;
	}
	pipeline_run(pl);
	pipeline_destroy(pl);
	return 0;
}
