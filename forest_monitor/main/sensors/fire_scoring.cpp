/*
 * fire_scoring.cpp - weighted multisensor fire-risk score (report Eq. 3.4).
 *
 * Each sensor term is normalised to a z-score against a calibrated baseline
 * (mean + standard deviation). The baselines below are placeholders and MUST
 * be re-calibrated per deployment site (record the node's stable readings and
 * their spread, then update these constants).
 *
 * Weights are the correlation-derived coefficients from the report:
 *   CO2 0.51 (corr 0.4842), Temperature 0.37 (0.3477), Humidity 0.12 (0.10).
 * Humidity contributes positively as it DROPS during a fire, so its deviation
 * is taken as (baseline - live).
 */

#include "fire_scoring.h"

// ---- Calibrated baselines (placeholder; calibrate in field) ----
#define CAL_GAS_MEAN_MV 800.0f
#define CAL_GAS_STD_MV  150.0f
#define CAL_TEMP_MEAN_C 25.0f
#define CAL_TEMP_STD_C  3.0f
#define CAL_HUM_MEAN    60.0f
#define CAL_HUM_STD     10.0f

// ---- Report Eq. 3.4 weights ----
#define W_GAS  0.51f
#define W_TEMP 0.37f
#define W_HUM  0.12f

float fire_score_compute(bool gas_valid, float gas_mv, bool env_valid, float temp_c, float humidity)
{
    float d_gas = gas_valid ? (gas_mv - CAL_GAS_MEAN_MV) / CAL_GAS_STD_MV : 0.0f;
    float d_temp = env_valid ? (temp_c - CAL_TEMP_MEAN_C) / CAL_TEMP_STD_C : 0.0f;
    float d_hum = env_valid ? (CAL_HUM_MEAN - humidity) / CAL_HUM_STD : 0.0f; // drop raises risk
    return W_GAS * d_gas + W_TEMP * d_temp + W_HUM * d_hum;
}
