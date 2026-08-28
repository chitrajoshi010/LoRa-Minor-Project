#pragma once

/*
 * fire_scoring.h - multisensor fire-risk score (project report Eq. 3.4).
 *
 * FireRisk = 0.51*dCO2 + 0.37*dTEMP + 0.12*dHUMIDITY, where each term is the
 * z-score deviation of the live reading from a calibrated baseline. A fire is
 * flagged when the score exceeds FIRE_ALERT_THRESHOLD.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Report threshold: fire likely when the composite score exceeds 3.0.
#define FIRE_ALERT_THRESHOLD 3.0f

/**
 * Compute the composite fire-risk score.
 * @param gas_valid  False when the gas reading must be ignored (e.g. the
 *                    sleep-gate MOSFET has the MQ-135/3.3V rail powered off,
 *                    or the ADC read is pinned near the rail — see
 *                    mq135_read()). The gas term is then dropped (treated as
 *                    baseline, zero deviation) instead of using a bogus
 *                    floating-rail voltage.
 * @param gas_mv     MQ-135 output in millivolts (CO2 proxy); ignored unless
 *                    gas_valid is true.
 * @param env_valid  False when temp_c/humidity must be ignored (e.g. the
 *                    sleep-gate MOSFET has the DHT22's 3.3V rail powered off
 *                    — dht22_read() already returns false in that case since
 *                    the sensor never acks; pass that same result through
 *                    here rather than substituting 0/0). Both env terms are
 *                    dropped together since one DHT22 read yields both.
 * @param temp_c     Temperature in degrees Celsius; ignored unless env_valid.
 * @param humidity   Relative humidity in percent; ignored unless env_valid.
 */
float fire_score_compute(bool gas_valid, float gas_mv, bool env_valid, float temp_c, float humidity);

#ifdef __cplusplus
}
#endif
