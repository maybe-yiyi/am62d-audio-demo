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

#ifdef HAVE_TFLITE
#include "tensorflow/lite/c/c_api.h"
#endif

#define YAMNET_URI "urn:am62d:yamnet"

/* Port indices — must match yamnet.ttl lv2:index values */
enum {
	PORT_IN_L   = 0,
	PORT_IN_R   = 1,
	PORT_OUT_L  = 2,
	PORT_OUT_R  = 3,
	PORT_SPEECH = 4,
	PORT_ALERT  = 5,
	PORT_LAUGH  = 6,
	PORT_CROWD  = 7,
	PORT_HVAC   = 8,
	PORT_NOISE  = 9,
	PORT_DOOR   = 10,
	PORT_MUSIC  = 11,
	PORT_TYPING = 12,
	NUM_PORTS   = 13,
};

/* Data types crossing the thread boundary */
typedef struct { float data[PATCH_SAMPLES]; } patch_t;
typedef struct { float scores[NUM_BUCKETS]; } result_t;

SPSC_DEFINE(patch,  patch_t,  2)
SPSC_DEFINE(result, result_t, 2)

struct priv {
	/* LV2 port buffer pointers — set by connect_port() */
	const float *in_l;
	const float *in_r;
	float       *out_l;
	float       *out_r;
	float       *ctrl[NUM_BUCKETS];   /* ctrl[0]=speech … ctrl[8]=typing */

	/* Downsampler + ring buffer (RT thread only) */
	ds_state_t  ds;
	ring_t      ring;
	float       patch_staging[PATCH_SAMPLES];

	/* Cross-thread queues */
	patch_queue_t  inference_queue;
	result_queue_t result_queue;

	/* Inference thread */
	pthread_t       thread;
	sem_t           patch_sem;
	atomic_bool     stop;

	/* Latest results (RT thread reads, updated from result_queue) */
	float           ctrl_buf[NUM_BUCKETS];

	/* AudioSet class → bucket mapping */
	uint8_t         class_bucket[521];

#ifdef HAVE_TFLITE
	TfLiteModel              *model;
	TfLiteInterpreterOptions *opts;
	TfLiteInterpreter        *interpreter;
#endif
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

#ifdef HAVE_TFLITE
	char model_path[512];
	size_t blen = strlen(bundle_path);
	snprintf(model_path, sizeof(model_path), "%s%syamnet.tflite",
	         bundle_path,
	         (blen > 0 && bundle_path[blen - 1] == '/') ? "" : "/");

	p->model = TfLiteModelCreateFromFile(model_path);
	if (!p->model) {
		fprintf(stderr, "yamnet: failed to load model from %s\n", model_path);
		free(p);
		return NULL;
	}

	p->opts = TfLiteInterpreterOptionsCreate();
	if (!p->opts) {
		fprintf(stderr, "yamnet: failed to create interpreter options\n");
		TfLiteModelDelete(p->model);
		free(p);
		return NULL;
	}
	TfLiteInterpreterOptionsSetNumThreads(p->opts, 1);

	p->interpreter = TfLiteInterpreterCreate(p->model, p->opts);
	if (!p->interpreter) {
		fprintf(stderr, "yamnet: failed to create interpreter\n");
		TfLiteInterpreterOptionsDelete(p->opts);
		TfLiteModelDelete(p->model);
		free(p);
		return NULL;
	}

	if (TfLiteInterpreterAllocateTensors(p->interpreter) != kTfLiteOk) {
		fprintf(stderr, "yamnet: AllocateTensors failed\n");
		TfLiteInterpreterDelete(p->interpreter);
		TfLiteInterpreterOptionsDelete(p->opts);
		TfLiteModelDelete(p->model);
		free(p);
		return NULL;
	}

	/* Validate input tensor: last dimension must match PATCH_SAMPLES */
	TfLiteTensor *in_t = TfLiteInterpreterGetInputTensor(p->interpreter, 0);
	int32_t n_dims = TfLiteTensorNumDims(in_t);
	int32_t last_dim = TfLiteTensorDim(in_t, n_dims - 1);
	if ((int32_t)PATCH_SAMPLES != last_dim) {
		fprintf(stderr, "yamnet: unexpected input shape — last dim %d, want %u\n",
				last_dim, PATCH_SAMPLES);
		TfLiteInterpreterDelete(p->interpreter);
		TfLiteInterpreterOptionsDelete(p->opts);
		TfLiteModelDelete(p->model);
		free(p);
		return NULL;
	}
#else
	(void)bundle_path;
#endif

	return p;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	struct priv *p = instance;
	switch (port) {
	case PORT_IN_L:  p->in_l  = data; break;
	case PORT_IN_R:  p->in_r  = data; break;
	case PORT_OUT_L: p->out_l = data; break;
	case PORT_OUT_R: p->out_r = data; break;
	default:
		if (port >= PORT_SPEECH && port < (uint32_t)(PORT_SPEECH + NUM_BUCKETS))
			p->ctrl[port - PORT_SPEECH] = data;
		break;
	}
}

static void activate(LV2_Handle instance)
{
	struct priv *p = instance;
	memset(&p->ds,   0, sizeof(p->ds));
	memset(&p->ring, 0, sizeof(p->ring));
	memset(p->ctrl_buf, 0, sizeof(p->ctrl_buf));
	atomic_store(&p->stop, false);
	sem_init(&p->patch_sem, 0, 0);
	pthread_create(&p->thread, NULL, inference_thread, p);
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
	struct priv *p = instance;

	/* Passthrough audio */
	if (p->in_l && p->out_l)
		memcpy(p->out_l, p->in_l, n_samples * sizeof(float));
	if (p->in_r && p->out_r)
		memcpy(p->out_r, p->in_r, n_samples * sizeof(float));

	/* Downsample and accumulate */
	const float *il = p->in_l;
	const float *ir = p->in_r;
	for (uint32_t i = 0; i < n_samples; i++) {
		float y = ds_tick(&p->ds,
				  il ? il[i] : 0.0f,
				  ir ? ir[i] : 0.0f);
		if (!isnan(y))
			ring_push(&p->ring, y);
	}

	/* Dispatch patch when hop complete */
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

	/* Consume latest result */
	result_t res;
	if (result_try_pop(&p->result_queue, &res))
		memcpy(p->ctrl_buf, res.scores, sizeof(p->ctrl_buf));

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
#ifdef HAVE_TFLITE
	if (p->interpreter) TfLiteInterpreterDelete(p->interpreter);
	if (p->opts)        TfLiteInterpreterOptionsDelete(p->opts);
	if (p->model)       TfLiteModelDelete(p->model);
#endif
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

		float raw_scores[521] = {0};

#ifdef HAVE_TFLITE
		TfLiteTensor *in_t = TfLiteInterpreterGetInputTensor(p->interpreter, 0);
		TfLiteTensorCopyFromBuffer(in_t, patch.data, PATCH_SAMPLES * sizeof(float));
		TfLiteInterpreterInvoke(p->interpreter);
		const TfLiteTensor *out_t = TfLiteInterpreterGetOutputTensor(p->interpreter, 0);
		memcpy(raw_scores, TfLiteTensorData(out_t), 521 * sizeof(float));
#else
		/* Mock: always return speech=0.9 for testing without TFLite */
		raw_scores[0] = 0.9f;
#endif

		/* Map 521 AudioSet classes → 9 bucket maxima */
		result_t result = {{0}};
		for (int i = 0; i < 521; i++) {
			uint8_t b = p->class_bucket[i];
			if (b != 0xFF && raw_scores[i] > result.scores[b])
				result.scores[b] = raw_scores[i];
		}

		result_try_push(&p->result_queue, &result);
	}

	return NULL;
}

static const LV2_Descriptor descriptor = {
	.URI            = YAMNET_URI,
	.instantiate    = instantiate,
	.connect_port   = connect_port,
	.activate       = activate,
	.run            = run,
	.deactivate     = deactivate,
	.cleanup        = cleanup,
	.extension_data = NULL,
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
	return index == 0 ? &descriptor : NULL;
}
