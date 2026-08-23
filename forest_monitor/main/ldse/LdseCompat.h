#pragma once

/*
 * LdseCompat.h - Arduino -> ESP-IDF compatibility shims.
 *
 * The LDSE protocol logic was written against the Arduino core. This header
 * provides the handful of Arduino primitives the ported code relies on
 * (millis/micros/delay/constrain/min) implemented on top of the ESP-IDF /
 * FreeRTOS APIs, so the protocol source stays byte-for-byte identical.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/** Milliseconds since boot (Arduino millis()). */
static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

/** Microseconds since boot (Arduino micros()). */
static inline uint32_t micros(void)
{
    return (uint32_t)esp_timer_get_time();
}

/** Busy-yield for the given milliseconds (Arduino delay()). */
static inline void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/** Clamp helper (replaces Arduino constrain(); avoids a global macro that
 *  would collide with RadioLib's std::min/std::max usage). */
static inline float ldse_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** uint8_t minimum helper (replaces Arduino min() in the packet codec). */
static inline uint8_t ldse_min_u8(uint8_t a, uint8_t b)
{
    return a < b ? a : b;
}
