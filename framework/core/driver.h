#ifndef DRIVER_H
#define DRIVER_H

struct driver_info {
	const char *name;
	const char *lib_path;
	const char *source_factory;
	const char *sink_factory;
};

const struct driver_info *driver_lookup(const char *name);

#endif /* DRIVER_H */
