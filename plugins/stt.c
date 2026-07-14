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

#include "stt_dsp.h"
#include "spsc_queue.h"
#include "stt_labels.h"
#include "am62d_params.h"

#include "tensorflow/lite/c/c_api.h"

#define STT_URI "urn:am62d:stt"

enum {
	PORT_IN = 0,
	PORT_OUT = 1,
	PORT_CONFIDENCE = 2,
	PORT_KEYWORD_ID = 3,
	PORT_HIT = 4,
	NUM_PORTS
};

typedef struct {
	float data[STT_PATCH_SAMPLES];
} stt_patch_t;

typedef struct {
	float confidence;
	float keyword_id;
	float hit;
	char label[64];
} stt_result_t;

SPSC_DEFINE(stt_patch, stt_patch_t, 2)
SPSC_DEFINE(stt_result, stt_result_t, 2)

struct priv {
	const float *in;
	float *out;
	float *confidence;
	float *keyword_id;
	float *hit;

	stt_ds_state_t ds;
	stt_ring_t ring;
	float patch_staging[STT_PATCH_SAMPLES];

	stt_patch_queue_t inference_queue;
	stt_result_queue_t result_queue;

	pthread_t thread;
	sem_t patch_sem;
	atomic_bool stop;

	float confidence_buf;
	float keyword_id_buf;
	float hit_buf;
	int hit_hold;

	TfLiteModel *model;
	TfLiteInterpreterOptions *opts;
	TfLiteInterpreter *interpreter;
	int n_classes;

	struct am62d_params *params;
};

static void *inference_thread(void *arg);

static struct am62d_params *find_params(const LV2_Feature *const *features)
{
	if (!features)
		return NULL;
	for (int i = 0; features[i]; i++) {
		if (features[i]->URI &&
		    strcmp(features[i]->URI, AM62D_PARAMS_URI) == 0)
			return (struct am62d_params *)features[i]->data;
	}
	return NULL;
}

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
			      double sample_rate,
			      const char *bundle_path,
			      const LV2_Feature *const *features)
{
	(void)descriptor;
	(void)sample_rate;

	struct priv *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	p->params = find_params(features);
	p->n_classes = STT_NUM_LABELS;

	char model_path[512];
	size_t blen = strlen(bundle_path);
	snprintf(model_path, sizeof(model_path), "%s%sstt.tflite",
		 bundle_path,
		 (blen > 0 && bundle_path[blen - 1] == '/') ? "" : "/");

	p->model = TfLiteModelCreateFromFile(model_path);
	if (!p->model) {
		fprintf(stderr, "stt: failed to load model from %s\n", model_path);
		goto free_p;
	}

	p->opts = TfLiteInterpreterOptionsCreate();
	if (!p->opts) {
		fprintf(stderr, "stt: failed to create interpreter options\n");
		goto del_model;
	}
	TfLiteInterpreterOptionsSetNumThreads(p->opts, 1);

	p->interpreter = TfLiteInterpreterCreate(p->model, p->opts);
	if (!p->interpreter) {
		fprintf(stderr, "stt: failed to create interpreter\n");
		goto del_opts;
	}

	if (TfLiteInterpreterAllocateTensors(p->interpreter) != kTfLiteOk) {
		fprintf(stderr, "stt: AllocateTensors failed\n");
		goto del_interp;
	}

	TfLiteTensor *in_t = TfLiteInterpreterGetInputTensor(p->interpreter, 0);
	int32_t n_dims = TfLiteTensorNumDims(in_t);
	int32_t last_dim = TfLiteTensorDim(in_t, n_dims - 1);
	if ((int32_t)STT_PATCH_SAMPLES != last_dim) {
		fprintf(stderr, "stt: unexpected input shape — last dim %d, want %u\n",
			last_dim, STT_PATCH_SAMPLES);
		goto del_interp;
	}

	const TfLiteTensor *out_t = TfLiteInterpreterGetOutputTensor(p->interpreter, 0);
	int32_t out_dims = TfLiteTensorNumDims(out_t);
	int32_t out_last = TfLiteTensorDim(out_t, out_dims - 1);
	if (out_last > 0 && out_last <= STT_NUM_LABELS)
		p->n_classes = out_last;

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
	case PORT_CONFIDENCE:
		p->confidence = data;
		break;
	case PORT_KEYWORD_ID:
		p->keyword_id = data;
		break;
	case PORT_HIT:
		p->hit = data;
		break;
	default:
		break;
	}
}

static void activate(LV2_Handle instance)
{
	struct priv *p = instance;
	memset(&p->ds, 0, sizeof(p->ds));
	memset(&p->ring, 0, sizeof(p->ring));
	memset(&p->inference_queue, 0, sizeof(p->inference_queue));
	memset(&p->result_queue, 0, sizeof(p->result_queue));
	p->confidence_buf = 0.0f;
	p->keyword_id_buf = 0.0f;
	p->hit_buf = 0.0f;
	p->hit_hold = 0;
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
		float y = stt_ds_tick(&p->ds, s);
		if (!isnan(y))
			stt_ring_push(&p->ring, y);
	}

	if (p->ring.hop_pending >= STT_HOP_SAMPLES) {
		if (stt_ring_get_patch(&p->ring, p->patch_staging)) {
			if (stt_patch_energy(p->patch_staging, STT_PATCH_SAMPLES) >= STT_ENERGY_FLOOR) {
				stt_patch_t pkt;
				memcpy(pkt.data, p->patch_staging, sizeof(pkt.data));
				if (stt_patch_try_push(&p->inference_queue, &pkt))
					sem_post(&p->patch_sem);
			}
		}
		stt_ring_reset_hop(&p->ring);
	}

	stt_result_t res;
	if (stt_result_try_pop(&p->result_queue, &res)) {
		p->confidence_buf = res.confidence;
		p->keyword_id_buf = res.keyword_id;
		p->hit_buf = res.hit;
		if (res.hit >= 1.0f)
			p->hit_hold = 3; /* brief latch for control observers */

		fprintf(stderr, "stt: %s conf=%.2f id=%.0f\n",
			res.label, res.confidence, res.keyword_id);

		if (p->params && p->params->publish_text && res.hit >= 1.0f &&
		    res.label[0] != '\0') {
			p->params->publish_text(p->params->handle, "command",
						res.label, res.confidence);
		}
	} else if (p->hit_hold > 0) {
		p->hit_hold--;
		if (p->hit_hold == 0)
			p->hit_buf = 0.0f;
	}

	if (p->confidence)
		*p->confidence = p->confidence_buf;
	if (p->keyword_id)
		*p->keyword_id = p->keyword_id_buf;
	if (p->hit)
		*p->hit = p->hit_buf;
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
	stt_patch_t patch;
	float *scores = NULL;

	scores = calloc((size_t)p->n_classes, sizeof(float));
	if (!scores)
		return NULL;

	while (true) {
		sem_wait(&p->patch_sem);
		if (atomic_load(&p->stop))
			break;
		if (!stt_patch_try_pop(&p->inference_queue, &patch))
			continue;

		TfLiteTensor *in_t = TfLiteInterpreterGetInputTensor(p->interpreter, 0);
		TfLiteTensorCopyFromBuffer(in_t, patch.data,
					   STT_PATCH_SAMPLES * sizeof(float));
		if (TfLiteInterpreterInvoke(p->interpreter) != kTfLiteOk)
			continue;

		const TfLiteTensor *out_t =
			TfLiteInterpreterGetOutputTensor(p->interpreter, 0);
		memcpy(scores, TfLiteTensorData(out_t),
		       (size_t)p->n_classes * sizeof(float));

		int best = 0;
		float best_score = scores[0];
		for (int i = 1; i < p->n_classes; i++) {
			if (scores[i] > best_score) {
				best_score = scores[i];
				best = i;
			}
		}

		stt_result_t result = {0};
		result.confidence = best_score;
		result.keyword_id = (float)best;
		result.hit = (best_score >= STT_HIT_THRESHOLD &&
			      best >= 2 /* skip silence/unknown */)
				     ? 1.0f
				     : 0.0f;

		if (best >= 0 && best < STT_NUM_LABELS)
			snprintf(result.label, sizeof(result.label), "%s",
				 stt_labels[best]);
		else
			snprintf(result.label, sizeof(result.label), "class_%d", best);

		stt_result_try_push(&p->result_queue, &result);
	}

	free(scores);
	return NULL;
}

static const LV2_Descriptor descriptor = {
	.URI = STT_URI,
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
