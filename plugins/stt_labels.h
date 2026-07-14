#ifndef STT_LABELS_H
#define STT_LABELS_H

/* TensorFlow Speech Commands v0.02 label order (12-class classifiers). */
#define STT_NUM_LABELS 12

static const char *const stt_labels[STT_NUM_LABELS] = {
	"silence",
	"unknown",
	"yes",
	"no",
	"up",
	"down",
	"left",
	"right",
	"on",
	"off",
	"stop",
	"go",
};

#endif /* STT_LABELS_H */
