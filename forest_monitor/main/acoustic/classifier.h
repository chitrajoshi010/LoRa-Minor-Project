#pragma once

/*
 * classifier.h - acoustic threat classifier running as a background task.
 *
 * Starts a FreeRTOS task that continuously captures audio, computes the
 * mel-spectrogram and runs the quantized TFLite Micro CNN. The latest result
 * (argmax class + per-class confidences) is published behind a mutex so the
 * node's DATA-window code can read it without blocking on inference.
 */

#include <stdint.h>
#include <stdbool.h>

#define ACOUSTIC_NUM_CLASSES 5

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint8_t classIdx;                       // argmax (0..4)
    float confidence[ACOUSTIC_NUM_CLASSES]; // per-class probabilities
} AcousticResult;

// Class label strings, index-aligned with the model output.
extern const char* const ACOUSTIC_LABELS[ACOUSTIC_NUM_CLASSES];

/** Initialise mic + model and start the inference task. @return true on success. */
bool classifier_start(void);

/** Copy the most recent inference result. @return false if none yet. */
bool classifier_get_latest(AcousticResult* out);

#ifdef __cplusplus
}
#endif
