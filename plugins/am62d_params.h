#ifndef AM62D_PARAMS_H
#define AM62D_PARAMS_H

/* Optional LV2 feature: plugins publish text events to the host param_bus. */
#define AM62D_PARAMS_URI "urn:am62d:params"

struct am62d_params {
	void *handle;
	void (*publish_text)(void *handle, const char *key,
			     const char *text, float confidence);
};

#endif /* AM62D_PARAMS_H */
