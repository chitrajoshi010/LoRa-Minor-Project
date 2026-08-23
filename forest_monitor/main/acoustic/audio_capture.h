#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

// Exposed so spectrogram.cpp can call i2s_channel_read directly in streaming loop
extern i2s_chan_handle_t s_rx_chan;

/**
 * @brief Initialize I2S for INMP441. GPIO17=BCK, GPIO15=WS, GPIO16=DIN.
 *        16 kHz, 32-bit slot (INMP441 sends 24-bit MSB-justified), mono.
 */
esp_err_t audio_init(void);

/**
 * @brief Read num_samples PCM samples from I2S (blocking).
 *        Converts 32-bit I2S frames → int16_t by shifting >> 16.
 */
esp_err_t audio_capture(int16_t *buffer, size_t num_samples);

