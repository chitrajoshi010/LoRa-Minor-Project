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

FireScoreResult fire_score_evaluate(bool gas_valid, float gas_mv, bool env_valid, float temp_c, float humidity)
{
    float d_gas = gas_valid ? (gas_mv - CAL_GAS_MEAN_MV) / CAL_GAS_STD_MV : 0.0f;
    float d_temp = env_valid ? (temp_c - CAL_TEMP_MEAN_C) / CAL_TEMP_STD_C : 0.0f;
    float d_hum = env_valid ? (CAL_HUM_MEAN - humidity) / CAL_HUM_STD : 0.0f; // drop raises risk

    FireScoreResult r;
    r.combined = W_GAS * d_gas + W_TEMP * d_temp + W_HUM * d_hum;
    // Renormalized single-sensor sub-scores (see fire_scoring.h): each is
    // scaled back up so it can independently reach the same threshold that
    // the combined score uses, letting a DHT22-only or MQ-135-only spike
    // trigger a fire even though it can't move the combined score alone.
    r.envScore = env_valid ? (W_TEMP * d_temp + W_HUM * d_hum) / (W_TEMP + W_HUM) : 0.0f;
    r.gasScore = gas_valid ? (W_GAS * d_gas) / W_GAS : 0.0f; // == d_gas

    r.fire = (r.combined >= FIRE_ALERT_THRESHOLD) ||
             (env_valid && r.envScore >= ENV_ALERT_THRESHOLD) ||
             (gas_valid && r.gasScore >= GAS_ALERT_THRESHOLD);

    r.reported = r.combined;
    if (env_valid && r.envScore > r.reported) r.reported = r.envScore;
    if (gas_valid && r.gasScore > r.reported) r.reported = r.gasScore;

    return r;
}
