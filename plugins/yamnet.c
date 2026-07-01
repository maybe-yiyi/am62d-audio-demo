#include <lv2/core/lv2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>

#include "yamnet_dsp.h"
#include "spsc_queue.h"
#include "yamnet_classes.h"

#include "tensorflow/lite/c/c_api.h"

#define YAMNET_URI "urn:am62d:yamnet"

/* Port indices must match yamnet.ttl lv2:index values. */
enum {
	PORT_IN = 0,
	PORT_OUT = 1,
	PORT_CTRL_BASE = 2,
	PORT_SPEECH = PORT_CTRL_BASE + BUCKET_SPEECH,
	PORT_ALERT = PORT_CTRL_BASE + BUCKET_ALERT,
	PORT_LAUGH = PORT_CTRL_BASE + BUCKET_LAUGH,
	PORT_CROWD = PORT_CTRL_BASE + BUCKET_CROWD,
	PORT_HVAC = PORT_CTRL_BASE + BUCKET_HVAC,
	PORT_NOISE = PORT_CTRL_BASE + BUCKET_NOISE,
	PORT_DOOR = PORT_CTRL_BASE + BUCKET_DOOR,
	PORT_MUSIC = PORT_CTRL_BASE + BUCKET_MUSIC,
	PORT_TYPING = PORT_CTRL_BASE + BUCKET_TYPING,
	PORT_APPLAUSE = PORT_CTRL_BASE + BUCKET_APPLAUSE,
	NUM_PORTS = PORT_CTRL_BASE + NUM_BUCKETS,
};

typedef struct {
	float data[PATCH_SAMPLES];
} patch_t;
typedef struct {
	float scores[NUM_BUCKETS];
} result_t;

SPSC_DEFINE(patch, patch_t, 2)
SPSC_DEFINE(result, result_t, 2)

struct priv {
	const float *in;
	float *out;
	float *ctrl[NUM_BUCKETS];

	ds_state_t ds;
	ring_t ring;
	float patch_staging[PATCH_SAMPLES];

	patch_queue_t inference_queue;
	result_queue_t result_queue;

	pthread_t thread;
	sem_t patch_sem;
	atomic_bool stop;

	float ctrl_buf[NUM_BUCKETS];

	uint8_t class_bucket[AUDIOSET_CLASSES];

	TfLiteModel *model;
	TfLiteInterpreterOptions *opts;
	TfLiteInterpreter *interpreter;
};

static void *inference_thread(void *arg);

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
				double sample_rate,
				const char *bundle_path,
				const LV2_Feature *const *features)
{
	(void)descriptor; (void)sample_rate; (void)features;

	struct priv *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	yamnet_init_class_buckets(p->class_bucket);

	char model_path[512];
	size_t blen = strlen(bundle_path);
	snprintf(model_path, sizeof(model_path), "%s%syamnet.tflite",
		bundle_path,
		(blen > 0 && bundle_path[blen - 1] == '/') ? "" : "/");

	p->model = TfLiteModelCreateFromFile(model_path);
	if (!p->model) {
		fprintf(stderr, "yamnet: failed to load model from %s\n", model_path);
		goto free_p;
	}

	p->opts = TfLiteInterpreterOptionsCreate();
	if (!p->opts) {
		fprintf(stderr, "yamnet: failed to create interpreter options\n");
		goto del_model;
	}
	TfLiteInterpreterOptionsSetNumThreads(p->opts, 1);

	p->interpreter = TfLiteInterpreterCreate(p->model, p->opts);
	if (!p->interpreter) {
		fprintf(stderr, "yamnet: failed to create interpreter\n");
		goto del_opts;
	}

	if (TfLiteInterpreterAllocateTensors(p->interpreter) != kTfLiteOk) {
		fprintf(stderr, "yamnet: AllocateTensors failed\n");
		goto del_interp;
	}

	TfLiteTensor *in_t = TfLiteInterpreterGetInputTensor(p->interpreter, 0);
	int32_t n_dims = TfLiteTensorNumDims(in_t);
	int32_t last_dim = TfLiteTensorDim(in_t, n_dims - 1);
	if ((int32_t)PATCH_SAMPLES != last_dim) {
		fprintf(stderr, "yamnet: unexpected input shape — last dim %d, want %u\n",
				last_dim, PATCH_SAMPLES);
		goto del_interp;
	}

	return p;

del_interp:
	TfLiteInterpreterDelete(p->interpreter);
del_opts:
	TfLiteInterpreterOptionsDelete(p->opts);
del_model:
	TfLiteModelDelete(p->model);
free_p:
	free(p);
	return NULL;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	struct priv *p = instance;
	switch (port) {
	case PORT_IN:
		p->in = data;
		break;
	case PORT_OUT:
		p->out = data;
		break;
	default:
		if (port >= PORT_SPEECH && port < (uint32_t)(PORT_SPEECH + NUM_BUCKETS))
			p->ctrl[port - PORT_SPEECH] = data;
		break;
	}
}

static void activate(LV2_Handle instance)
{
	struct priv *p = instance;
	memset(&p->ds, 0, sizeof(p->ds));
	memset(&p->ring, 0, sizeof(p->ring));
	memset(p->ctrl_buf, 0, sizeof(p->ctrl_buf));
	memset(&p->inference_queue, 0, sizeof(p->inference_queue));
	memset(&p->result_queue, 0, sizeof(p->result_queue));
	atomic_store(&p->stop, false);
	sem_init(&p->patch_sem, 0, 0);
	pthread_create(&p->thread, NULL, inference_thread, p);
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
	struct priv *p = instance;

	if (p->in && p->out)
		memcpy(p->out, p->in, n_samples * sizeof(float));

	const float *in = p->in;
	for (uint32_t i = 0; i < n_samples; i++) {
		float s = in ? in[i] : 0.0f;
		float y = ds_tick(&p->ds, s, s);
		if (!isnan(y))
			ring_push(&p->ring, y);
	}

	if (p->ring.hop_pending >= HOP_SAMPLES) {
		if (ring_get_patch(&p->ring, p->patch_staging)) {
			if (patch_energy(p->patch_staging, PATCH_SAMPLES) >= ENERGY_FLOOR) {
				patch_t pkt;
				memcpy(pkt.data, p->patch_staging, sizeof(pkt.data));
				if (patch_try_push(&p->inference_queue, &pkt))
					sem_post(&p->patch_sem);
			}
		}
		ring_reset_hop(&p->ring);
	}

	result_t res;
	if (result_try_pop(&p->result_queue, &res)) {
		memcpy(p->ctrl_buf, res.scores, sizeof(p->ctrl_buf));

		fprintf(stderr, "yamnet:");
		for (int i = 0; i < NUM_BUCKETS; i++)
			fprintf(stderr, " %s=%.2f", yamnet_bucket_names[i], p->ctrl_buf[i]);
		fprintf(stderr, "\n");
	}

	for (int i = 0; i < NUM_BUCKETS; i++)
		if (p->ctrl[i])
			*p->ctrl[i] = p->ctrl_buf[i];
}

static void deactivate(LV2_Handle instance)
{
	struct priv *p = instance;
	atomic_store(&p->stop, true);
	sem_post(&p->patch_sem);
	pthread_join(p->thread, NULL);
	sem_destroy(&p->patch_sem);
}

static void cleanup(LV2_Handle instance)
{
	struct priv *p = instance;
	if (p->interpreter)
		TfLiteInterpreterDelete(p->interpreter);
	if (p->opts)
		TfLiteInterpreterOptionsDelete(p->opts);
	if (p->model)
		TfLiteModelDelete(p->model);
	free(p);
}

static void *inference_thread(void *arg)
{
	struct priv *p = arg;
	patch_t patch;

	while (true) {
		sem_wait(&p->patch_sem);
		if (atomic_load(&p->stop))
			break;
		if (!patch_try_pop(&p->inference_queue, &patch))
			continue;

		float raw_scores[AUDIOSET_CLASSES] = {0};

		TfLiteTensor *in_t = TfLiteInterpreterGetInputTensor(p->interpreter, 0);
		TfLiteTensorCopyFromBuffer(in_t, patch.data, PATCH_SAMPLES * sizeof(float));
		TfLiteInterpreterInvoke(p->interpreter);
		const TfLiteTensor *out_t = TfLiteInterpreterGetOutputTensor(p->interpreter, 0);
		memcpy(raw_scores, TfLiteTensorData(out_t), AUDIOSET_CLASSES * sizeof(float));

		result_t result = {{0}};
		for (int i = 0; i < AUDIOSET_CLASSES; i++) {
			uint8_t b = p->class_bucket[i];
			if (b != 0xFF && raw_scores[i] > result.scores[b])
				result.scores[b] = raw_scores[i];
		}

		result_try_push(&p->result_queue, &result);
	}

	return NULL;
}

static const LV2_Descriptor descriptor = {
	.URI = YAMNET_URI,
	.instantiate = instantiate,
	.connect_port = connect_port,
	.activate = activate,
	.run = run,
	.deactivate = deactivate,
	.cleanup = cleanup,
	.extension_data = NULL,
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
	if (index == 0)
		return &descriptor;
	return NULL;
}
