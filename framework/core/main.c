#include <getopt.h>
#include <pthread.h>
#include <signal.h>

#include "pipeline.h"

/**
 * main() - pipeline executable entry point
 * @argc: argument count
 * @argv: argument vector
 *
 * Parses command line options to get config path and plugin directory.
 * Blocks SIGINT and SIGTERM in main thread (handled by pipeline_run).
 * Creates, runs, and destroys the pipeline.
 *
 * Return: 0 on success, 1 on failure
 */
int main(int argc, char *argv[]) {
	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &ss, NULL);

	const char *config_path = NULL;
	const char *plugin_dir = NULL;

	static const struct option opts[] = {
		{ "config", required_argument, NULL, 'c' },
		{ "plugin-dir", required_argument, NULL, 'p' },
		{ NULL, 0, NULL, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
		switch (c) {
		case 'c':
			config_path = optarg;
			break;
		case 'p':
			plugin_dir = optarg;
			break;
		default:
			fprintf(stderr, "Usage: %s [--config=PATH] [--plugin-dir=DIR]\n", argv[0]);
			return 1;
		}
	}

	if (!config_path || !plugin_dir) {
		fprintf(stderr, "Usage: %s [--config=PATH] [--plugin-dir=DIR]\n", argv[0]);
		return 1;
	}

	struct pipeline *pl = pipeline_create(config_path, plugin_dir);
	if (!pl) {
		fprintf(stderr, "Failed to create pipeline!\n");
		return 1;
	}
	pipeline_run(pl);
	pipeline_destroy(pl);
	return 0;
}
