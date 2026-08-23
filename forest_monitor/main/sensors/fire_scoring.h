#pragma once

/*
 * fire_scoring.h - multisensor fire-risk score (project report Eq. 3.4).
 *
 * FireRisk = 0.51*dCO2 + 0.37*dTEMP + 0.12*dHUMIDITY, where each term is the
 * z-score deviation of the live reading from a calibrated baseline. A fire is
 * flagged when the score exceeds FIRE_ALERT_THRESHOLD.
 */

#ifdef __cplusplus
extern "C"
{
#endif

// Report threshold: fire likely when the composite score exceeds 3.0.
#define FIRE_ALERT_THRESHOLD 3.0f

/**
 * Compute the composite fire-risk score.
 * @param gas_mv    MQ-135 output in millivolts (CO2 proxy).
 * @param temp_c    Temperature in degrees Celsius.
 * @param humidity  Relative humidity in percent.
 */
float fire_score_compute(float gas_mv, float temp_c, float humidity);

#ifdef __cplusplus
}
#endif
