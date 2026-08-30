#pragma once

/*
 * mq135.h - MQ-135 air-quality (CO2 proxy) analog reading.
 *
 * The MQ-135 analog output is wired to GPIO2 (ADC1 channel 1 on the ESP32-S3)
 * and sampled with the esp_adc oneshot driver.
 *
 * Hardware note: the gas sensor's supply and the 3.3V regulator that feeds it
 * are switched by the sleep-gate MOSFET (CONFIG_LDSE_PIN_SLEEP_GATE), while
 * the ESP32-S3 and LoRa radio stay powered directly off the rail. When that
 * MOSFET is off, GPIO2 is not driven by the sensor and floats up to the 3.3V
 * rail through the sensor's own load resistor, i.e. it reads near full-scale
 * rather than 0V. mq135_read() therefore takes the caller's sleep-gate
 * "powered" state and also rejects any reading pinned near the ADC rail, so a
 * de-energised sensor is never mistaken for a real high-CO2 reading.
 */

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Initialize ADC1 oneshot for the MQ-135 on GPIO2. */
esp_err_t mq135_init(void);

/**
 * Reconfigure the ADC pin's pull resistor to match the sleep-gate MOSFET
 * state (see LdseSleepGate.h). Call this immediately alongside
 * LdseSleepGate::Sleep()/Wake() - not automatic, the caller must invoke it
 * each transition.
 *
 * @param asleep  true when the peripheral rail is about to be/was just cut
 *                (MOSFET off): enables an internal pull-DOWN on the ADC pin
 *                so it settles near 0V instead of floating up toward the
 *                3.3V rail through the sensor's own (now unpowered) load
 *                resistor network. false when the rail is powered again:
 *                disables the pull (back to the default no-pull ADC input)
 *                before any mq135_read() call, so the resistor never biases
 *                a genuine reading.
 */
void mq135_set_sleeping(bool asleep);

/**
 * Read the MQ-135 output as a calibrated voltage in millivolts.
 * @param peripheral_rail_powered  Current sleep-gate MOSFET state (true when
 *                                 the gas sensor / 3.3V peripheral rail is
 *                                 energised, e.g. LdseSleepGate::IsAwake()).
 * @param out_mv                   Set to the last sampled reading (whether
 *                                 valid or not); callers must not use it as
 *                                 real gas data unless this function returns
 *                                 true.
 * @return true if out_mv is a genuine sensor reading; false if the rail is
 *         off or the ADC read is pinned near the 3.3V rail (sensor
 *         unpowered/floating), in which case out_mv must be ignored.
 */
bool mq135_read(bool peripheral_rail_powered, float* out_mv);

#ifdef __cplusplus
}
#endif
