#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spa/support/log.h>
#include <spa/support/loop.h>
#include <spa/support/plugin.h>
#include <spa/support/system.h>
#include <spa/utils/names.h>

#include "dataloop.h"

#define SUPPORT_LIB SPA_PLUGIN_DIR "/support/libspa-support.so"

static void stderr_logv(void *object, enum spa_log_level level,
			const char *file, int line,
			const char *func, const char *fmt, va_list args)
{
	(void)object; (void)file; (void)line; (void)func;
	if (level <= SPA_LOG_LEVEL_WARN) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, "\n");
	}
}

static void stderr_log(void *object, enum spa_log_level level,
			const char *file, int line,
			const char *func, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	stderr_logv(object, level, file, line, func, fmt, args);
	va_end(args);
}

static const struct spa_log_methods log_methods = {
	SPA_VERSION_LOG_METHODS,
	.log = stderr_log,
	.logv = stderr_logv,
};

static struct spa_log g_log;
static bool g_log_inited = false;

static struct spa_log *get_log(void)
{
	if (!g_log_inited) {
		g_log.iface = SPA_INTERFACE_INIT(SPA_TYPE_INTERFACE_Log, SPA_VERSION_LOG,
						&log_methods, NULL);
		g_log.level = SPA_LOG_LEVEL_WARN;
		g_log_inited = true;
	}
	return &g_log;
}

static const struct spa_handle_factory *find_factory(
	spa_handle_factory_enum_func_t enum_fn, const char *name)
{
	const struct spa_handle_factory *f = NULL;
	uint32_t idx = 0;
	while (enum_fn(&f, &idx) > 0) {
		if (strcmp(f->name, name) == 0)
			return f;
		f = NULL;
	}
	return NULL;
}

static void *loop_thread_fn(void *data)
{
	struct spa_dataloop *dl = data;
	spa_loop_control_enter(dl->control);
	while (!dl->quit)
		spa_loop_control_iterate(dl->control, -1);
	spa_loop_control_leave(dl->control);
	return NULL;
}

static int stop_loop_cb(struct spa_loop *loop, bool async,
			uint32_t seq, const void *data,
			size_t size, void *user_data)
{
	struct spa_dataloop *dl = user_data;
	dl->quit = 1;
	(void)loop; (void)async; (void)seq; (void)data; (void)size;
	return 0;
}

struct spa_dataloop *spa_dataloop_create(void)
{
	struct spa_dataloop *dl = calloc(1, sizeof(*dl));
	if (!dl)
		return NULL;

	dl->dl_handle = dlopen(SUPPORT_LIB, RTLD_NOW | RTLD_LOCAL);
	if (!dl->dl_handle) {
		fprintf(stderr, "dataloop: dlopen(%s) failed: %s\n", SUPPORT_LIB, dlerror());
		goto fail_open;
	}

	spa_handle_factory_enum_func_t enum_fn =
		(spa_handle_factory_enum_func_t)dlsym(dl->dl_handle,
						SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
	if (!enum_fn) {
		fprintf(stderr, "dataloop: missing %s\n", SPA_HANDLE_FACTORY_ENUM_FUNC_NAME);
		goto fail_sym;
	}

	struct spa_log *log = get_log();
	struct spa_support init_support[1] = {
		SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_Log, log),
	};

	const struct spa_handle_factory *sys_factory = find_factory(enum_fn, SPA_NAME_SUPPORT_SYSTEM);
	if (!sys_factory) {
		fprintf(stderr, "dataloop: support.system factory not found\n");
		goto fail_sym;
	}

	dl->system_data = calloc(1, spa_handle_factory_get_size(sys_factory, NULL));
	if (!dl->system_data)
		goto fail_sym;
	dl->system_handle = dl->system_data;

	if (spa_handle_factory_init(sys_factory, dl->system_handle, NULL, init_support, 1) < 0) {
		fprintf(stderr, "dataloop: support.system init failed\n");
		goto fail_system_init;
	}

	if (spa_handle_get_interface(dl->system_handle,
					SPA_TYPE_INTERFACE_DataSystem,
					&dl->system_iface) < 0) {
		if (spa_handle_get_interface(dl->system_handle,
					SPA_TYPE_INTERFACE_System,
					&dl->system_iface) < 0) {
			fprintf(stderr, "dataloop: support.system has no System interface\n");
			goto fail_system_iface;
		}
	}

	struct spa_support loop_support[2] = {
		SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_Log, log),
		SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_System, dl->system_iface),
	};

	const struct spa_handle_factory *loop_factory = find_factory(enum_fn, SPA_NAME_SUPPORT_LOOP);
	if (!loop_factory) {
		fprintf(stderr, "dataloop: support.loop factory not found\n");
		goto fail_system_iface;
	}

	dl->loop_data = calloc(1, spa_handle_factory_get_size(loop_factory, NULL));
	if (!dl->loop_data)
		goto fail_system_iface;
	dl->loop_handle = dl->loop_data;

	if (spa_handle_factory_init(loop_factory, dl->loop_handle, NULL, loop_support, 2) < 0) {
		fprintf(stderr, "dataloop: support.loop init failed\n");
		goto fail_loop_init;
	}

	if (spa_handle_get_interface(dl->loop_handle,
					SPA_TYPE_INTERFACE_DataLoop,
					(void **)&dl->loop) < 0) {
		if (spa_handle_get_interface(dl->loop_handle,
					SPA_TYPE_INTERFACE_Loop,
					(void **)&dl->loop) < 0) {
			fprintf(stderr, "dataloop: no Loop interface\n");
			goto fail_loop_iface;
		}
	}

	if (spa_handle_get_interface(dl->loop_handle,
					SPA_TYPE_INTERFACE_LoopControl,
					(void **)&dl->control) < 0) {
		fprintf(stderr, "dataloop: no LoopControl interface\n");
		goto fail_loop_iface;
	}

	dl->support[0] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_Log, log);
	dl->support[1] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_System, dl->system_iface);
	dl->support[2] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_DataSystem, dl->system_iface);
	dl->support[3] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_Loop, dl->loop);
	dl->support[4] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_DataLoop, dl->loop);
	dl->support[5] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_LoopControl, dl->control);
	dl->n_support = 6;

	if (pthread_create(&dl->thread, NULL, loop_thread_fn, dl) != 0) {
		fprintf(stderr, "dataloop: pthread_create failed\n");
		goto fail_loop_iface;
	}

	return dl;

fail_loop_iface:
	if (dl->loop_handle->clear)
		dl->loop_handle->clear(dl->loop_handle);
fail_loop_init:
	free(dl->loop_data);
fail_system_iface:
	if (dl->system_handle->clear)
		dl->system_handle->clear(dl->system_handle);
fail_system_init:
	free(dl->system_data);
fail_sym:
	dlclose(dl->dl_handle);
fail_open:
	free(dl);
	return NULL;
}

void spa_dataloop_destroy(struct spa_dataloop *dl)
{
	if (!dl)
		return;

	spa_loop_invoke(dl->loop, stop_loop_cb, 0, NULL, 0, false, dl);
	pthread_join(dl->thread, NULL);

	if (dl->loop_handle->clear)
		dl->loop_handle->clear(dl->loop_handle);
	free(dl->loop_data);

	if (dl->system_handle->clear)
		dl->system_handle->clear(dl->system_handle);
	free(dl->system_data);

	dlclose(dl->dl_handle);
	free(dl);
}

const struct spa_support *spa_dataloop_support(struct spa_dataloop *dl, uint32_t *n_support)
{
	*n_support = dl->n_support;
	return dl->support;
}
