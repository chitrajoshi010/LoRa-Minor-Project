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

// Last class index (4, "Background") is the only non-threat class; every
// other class (Axe/Chainsaw/Gunshot/Handsaw) is a threat class.
#define ACOUSTIC_BACKGROUND_CLASS (ACOUSTIC_NUM_CLASSES - 1)

// Minimum confidence the model must report on a threat class before we
// consider it a real detection. Below this, treat it the same as
// Background/uncertain: no alert. See classifier_is_threat().
#define ACOUSTIC_ALERT_THRESHOLD 0.70f

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

/**
 * Notify the classifier task that the mic's power rail changed state. The
 * INMP441 mic shares the sleep-gate MOSFET rail with the MQ-135/DHT22 (see
 * AGENTS.md power topology), so call this from the same place the node/relay
 * calls LdseSleepGate::Wake()/Sleep() (true on Wake, false on Sleep). While
 * powered==false the task skips capture/inference (I2S DIN floats when
 * unpowered, which would otherwise feed garbage samples into the model and
 * could produce a spurious high-confidence "threat") and any stale result is
 * cleared so classifier_get_latest() returns false until a fresh, real
 * capture completes after the rail is re-powered.
 */
void classifier_set_mic_powered(bool powered);

/** Copy the most recent inference result. @return false if none yet. */
bool classifier_get_latest(AcousticResult* out);

/**
 * Decide whether the latest inference is worth alerting on:
 * argmax class is a threat class (not Background) AND its confidence is
 * >= ACOUSTIC_ALERT_THRESHOLD. Background, or an uncertain threat call below
 * threshold, is treated as "nothing to report".
 */
bool classifier_is_threat(const AcousticResult* r);

#ifdef __cplusplus
}
#endif
