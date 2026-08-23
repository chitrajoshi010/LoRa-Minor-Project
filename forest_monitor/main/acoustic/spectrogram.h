#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "spectrogram_params.h"

/**
 * @brief Initialize the spectrogram engine (allocates heap buffers, inits ESP-DSP FFT).
 *        Call once after audio_init().
 * @return ESP_OK on success
 */
esp_err_t spectrogram_init(void);

/**
 * @brief Capture 2.5 s of audio and compute the 64×251 INT8 log-mel spectrogram.
 *        Result is written directly into out_tensor (N_MELS × N_FRAMES int8 values,
 *        row-major, ready to feed into the TFLite input tensor).
 * @param out_tensor  Pointer to the model's input tensor data buffer (int8_t*)
 * @return ESP_OK on success
 */
esp_err_t spectrogram_compute(int8_t *out_tensor);

/**
 * @brief Free heap buffers allocated by spectrogram_init().
 */
void spectrogram_deinit(void);
