#pragma once

/*
 * payload.h - shared node DATA / FIRE_ALERT payload layout.
 *
 * This header is intentionally dependency-free (no sensors, no TFLite) so the
 * gateway role can decode the payload for CSV logging without compiling the
 * classifier or sensor modules.
 *
 * Carried in the LDSE frame payload (LDSE_PAYLOAD_MAX = 48 bytes).
 */

#include <stdint.h>

#define LDSE_ACOUSTIC_CLASSES 5

namespace ldse
{

// Packed so sizeof() is the exact wire size (37 bytes, <= 48).
struct __attribute__((packed)) NodePayload
{
    uint8_t classIdx;                          // acoustic argmax (0..4)
    float confidence[LDSE_ACOUSTIC_CLASSES];   // per-class probabilities
    float temperature;                         // deg C (DHT22)
    float humidity;                            // % RH (DHT22)
    float gas;                                 // MQ-135 (mV, proxy for CO2)
    float fireScore;                           // weighted fire-risk score
};

static_assert(sizeof(NodePayload) <= 48, "NodePayload exceeds LDSE_PAYLOAD_MAX");

} // namespace ldse
