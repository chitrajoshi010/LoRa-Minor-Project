/*
 * audio_capture.cpp
 *
 * I2S driver for INMP441 MEMS microphone on ESP32-S3.
 * Pins: BCK=GPIO17, WS=GPIO15, DIN=GPIO16
 * Config: 16 kHz, 16-bit, mono (L/R pin must be tied to GND)
 */

#include "audio_capture.h"
#include "spectrogram_params.h"

#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "audio_capture";

i2s_chan_handle_t s_rx_chan = NULL;

esp_err_t audio_init(void)
{
    // Channel configuration — receive only
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    // Standard (Philips) mode config for INMP441
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,   // INMP441 sends 24-bit in 32-bit frame
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_17,
            .ws   = GPIO_NUM_15,
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_16,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    ESP_LOGI(TAG, "I2S microphone initialized (GPIO BCK=17, WS=15, DIN=16)");
    return ESP_OK;
}

esp_err_t audio_capture(int16_t *buffer, size_t num_samples)
{
    // INMP441 outputs 24-bit data in a 32-bit frame — read as int32_t, shift to int16_t
    size_t bytes_to_read = num_samples * sizeof(int32_t);
    int32_t *raw = (int32_t *)malloc(bytes_to_read);
    if (raw == NULL) {
        ESP_LOGE(TAG, "malloc failed for raw I2S buffer");
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(s_rx_chan, raw, bytes_to_read, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        free(raw);
        ESP_LOGE(TAG, "i2s_channel_read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // INMP441 Philips 32-bit slot: data in bits [31:8] (MSB-justified), bits[7:0]=0.
    // Shift right 16 to extract the top 16 bits as a signed int16_t.
    size_t samples_read = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < samples_read && i < num_samples; i++) {
        buffer[i] = (int16_t)(raw[i] >> 16);
    }

    free(raw);
    return ESP_OK;
}
