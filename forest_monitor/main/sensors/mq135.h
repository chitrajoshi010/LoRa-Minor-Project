#pragma once

/*
 * mq135.h - MQ-135 air-quality (CO2 proxy) analog reading.
 *
 * The MQ-135 analog output is wired to GPIO2 (ADC1 channel 1 on the ESP32-S3)
 * and sampled with the esp_adc oneshot driver.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Initialize ADC1 oneshot for the MQ-135 on GPIO2. */
esp_err_t mq135_init(void);

/** Read the MQ-135 output as a calibrated voltage in millivolts. */
float mq135_read_mv(void);

#ifdef __cplusplus
}
#endif
