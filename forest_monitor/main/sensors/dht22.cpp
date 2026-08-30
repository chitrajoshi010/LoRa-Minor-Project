/*
 * dht22.cpp - DHT22 (AM2302) single-wire bit-banged driver.
 *
 * Vendored directly against the ESP-IDF GPIO driver instead of a managed
 * component: the previously declared "chmorgan/esp-dht" dependency does not
 * exist (no such GitHub repo, not in the Component Registry) and broke
 * `idf.py set-target`/`build` dependency resolution for every role. This
 * file is the single adapter point (per AGENTS.md) - the public API
 * (dht22_init/dht22_read) is unchanged, so callers (mq135/fire_scoring
 * callers in roles/relay.cpp, roles/node.cpp) need no changes.
 *
 * Protocol (AM2302/DHT22 datasheet):
 *   1. Host pulls the line low for >= 1 ms, then releases it (open-drain,
 *      internal pull-up) and switches to input.
 *   2. Sensor acknowledges: pulls low ~80 us, then high ~80 us.
 *   3. Sensor clocks out 40 bits (humidity[16] + temperature[16] +
 *      checksum[8]); each bit is a ~50 us low pulse followed by a high
 *      pulse whose length encodes the value: ~26-28 us = 0, ~70 us = 1.
 *
 * The whole 40-bit read (~5 ms) runs inside a FreeRTOS critical section:
 * scheduler preemption jitter of even a few hundred microseconds would
 * corrupt the edge timing, and this is short enough not to trip the task
 * watchdog (default 5 s) - it is only called once per epoch from the
 * relay/node DATA-window sensor read, same as the existing mq135 read.
 */

#include "dht22.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "dht22";

static gpio_num_t s_gpio = GPIO_NUM_NC;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline void SetOutputLow()
{
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_gpio, 0);
}

static inline void SetInput()
{
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);
}

// Busy-waits for the line to reach `level`. Returns the number of
// microseconds waited (>= 0), or -1 on timeout.
static int WaitForLevel(int level, int timeoutUs)
{
    int waited = 0;
    while (gpio_get_level(s_gpio) != level)
    {
        if (waited >= timeoutUs)
        {
            return -1;
        }
        esp_rom_delay_us(1);
        waited++;
    }
    return waited;
}

void dht22_init(int gpio)
{
    s_gpio = (gpio_num_t)gpio;

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << s_gpio;
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(s_gpio, 1); // idle high between reads

    ESP_LOGI(TAG, "DHT22 initialized on GPIO%d", gpio);
}

void dht22_set_sleeping(bool asleep)
{
    if (s_gpio == GPIO_NUM_NC)
    {
        return;
    }

    if (asleep)
    {
        // Peripheral rail is being/has been cut: drop the pull-up (which
        // would otherwise back-feed current into the now-unpowered DHT22
        // through its data-pin ESD clamp diode - "phantom powering") and
        // pull the line down instead so it settles near 0V, matching the
        // sensor's own de-energised state.
        gpio_set_direction(s_gpio, GPIO_MODE_INPUT);
        gpio_set_pull_mode(s_gpio, GPIO_PULLDOWN_ONLY);
    }
    else
    {
        // Rail re-powered: restore the normal open-drain + pull-up idle-high
        // configuration the protocol needs before a read can succeed (same
        // as dht22_init()).
        gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT_OD);
        gpio_set_pull_mode(s_gpio, GPIO_PULLUP_ONLY);
        gpio_set_level(s_gpio, 1);
    }
}

bool dht22_read(float* out_temp_c, float* out_humidity)
{
    if (s_gpio == GPIO_NUM_NC)
    {
        ESP_LOGE(TAG, "dht22_read() called before dht22_init()");
        return false;
    }

    uint8_t data[5] = {0, 0, 0, 0, 0};

    // ---- host start signal: pull low >= 1 ms, then release ----
    SetOutputLow();
    vTaskDelay(pdMS_TO_TICKS(2));

    portENTER_CRITICAL(&s_mux);
    gpio_set_level(s_gpio, 1);
    esp_rom_delay_us(30); // release; sensor pulls low to ack within ~20-40us
    SetInput();

    // ---- sensor ack: ~80us low, ~80us high, then first bit's leading low ----
    if (WaitForLevel(0, 100) < 0 || WaitForLevel(1, 100) < 0 || WaitForLevel(0, 100) < 0)
    {
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGW(TAG, "no ack from sensor (check wiring/pull-up on GPIO)");
        return false;
    }

    // ---- 40 data bits: each is a high pulse whose width says 0 or 1 ----
    for (int i = 0; i < 40; i++)
    {
        if (WaitForLevel(1, 100) < 0)
        {
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGW(TAG, "timeout waiting for bit %d start", i);
            return false;
        }
        int highUs = WaitForLevel(0, 100);
        if (highUs < 0)
        {
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGW(TAG, "timeout waiting for bit %d end", i);
            return false;
        }
        data[i / 8] <<= 1;
        if (highUs > 40) // ~70us pulse = 1, ~26-28us pulse = 0
        {
            data[i / 8] |= 1;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4])
    {
        ESP_LOGW(TAG, "checksum mismatch: got 0x%02x want 0x%02x", checksum, data[4]);
        return false;
    }

    float humidity = ((data[0] << 8) | data[1]) / 10.0f;
    int16_t rawTempMag = ((data[2] & 0x7F) << 8) | data[3];
    float temp = rawTempMag / 10.0f;
    if (data[2] & 0x80) // sign bit: negative temperature
    {
        temp = -temp;
    }

    if (out_humidity)
    {
        *out_humidity = humidity;
    }
    if (out_temp_c)
    {
        *out_temp_c = temp;
    }
    return true;
}
