#pragma once

/*
 * fire_scoring.h - multisensor fire-risk score (project report Eq. 3.4).
 *
 * FireRisk = 0.51*dCO2 + 0.37*dTEMP + 0.12*dHUMIDITY, where each term is the
 * z-score deviation of the live reading from a calibrated baseline. A fire is
 * flagged when the combined score exceeds FIRE_ALERT_THRESHOLD.
 *
 * Independent single-sensor triggers: the combined weights (0.51 gas / 0.49
 * env) mean a spike confined to only one sensor group can never reach the
 * combined threshold on its own. To still catch "DHT22-only" (temp+humidity)
 * or "MQ-135-only" (gas) fire signatures, the corresponding sub-terms are
 * renormalized back up to the same 0..3-ish scale as the combined score and
 * compared against their own threshold:
 *
 *   envScore = (0.37*dTEMP + 0.12*dHUMIDITY) / 0.49
 *   gasScore = (0.51*dCO2) / 0.51 = dCO2
 *
 * A fire is reported when EITHER the combined score, envScore, or gasScore
 * crosses its threshold. The wire packet (ldse::NodePayload) still carries a
 * single `fireScore` float; it is populated with whichever of the three
 * scores is currently highest (see fire_score_evaluate()).
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Report threshold: fire likely when the composite score exceeds 3.0.
#define FIRE_ALERT_THRESHOLD 3.0f

// Independent single-sensor thresholds, on the same renormalized 0..3-ish
// scale as FIRE_ALERT_THRESHOLD (see file header comment).
#define ENV_ALERT_THRESHOLD 3.0f
#define GAS_ALERT_THRESHOLD 3.0f

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

// Result of the full independent-trigger evaluation (see fire_score_evaluate).
typedef struct
{
    float combined;  // classic 3-term weighted score (same as fire_score_compute)
    float envScore;  // renormalized DHT22-only (temp+humidity) score; 0 if !env_valid
    float gasScore;  // renormalized MQ-135-only (gas) score; 0 if !gas_valid
    float reported;  // max(combined, valid envScore, valid gasScore) — goes in NodePayload.fireScore
    bool fire;       // true if combined, envScore, or gasScore crosses its threshold
} FireScoreResult;

/**
 * Evaluate the composite score plus the two independent single-sensor
 * sub-scores, and decide whether a fire should be flagged. This is the
 * function callers (node/relay roles) should use to decide MSG_FIRE_ALERT
 * and to fill NodePayload.fireScore (via .reported) — the packet layout
 * itself is unchanged, it still carries a single float.
 *
 * A fire is flagged when ANY of the following holds:
 *   - combined >= FIRE_ALERT_THRESHOLD
 *   - env_valid && envScore >= ENV_ALERT_THRESHOLD   (DHT22-only trigger)
 *   - gas_valid && gasScore >= GAS_ALERT_THRESHOLD   (MQ-135-only trigger)
 *
 * @see fire_score_compute() for parameter semantics (gas_valid/env_valid).
 */
FireScoreResult fire_score_evaluate(bool gas_valid, float gas_mv, bool env_valid, float temp_c, float humidity);

#ifdef __cplusplus
}
#endif
