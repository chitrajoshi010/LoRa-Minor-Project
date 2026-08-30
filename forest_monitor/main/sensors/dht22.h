#pragma once

/*
 * dht22.h - DHT22 (AM2302) temperature/humidity read wrapper.
 *
 * Self-contained bit-banged single-wire driver (no external managed
 * component - see dht22.cpp for why). Data line on GPIO12.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Configure the DHT22 data GPIO (call once at startup). */
void dht22_init(int gpio);

/**
 * Reconfigure the data GPIO's pull resistor to match the sleep-gate MOSFET
 * state (see LdseSleepGate.h). Call this immediately alongside
 * LdseSleepGate::Sleep()/Wake() - not automatic, the caller must invoke it
 * each transition.
 *
 * @param asleep  true when the peripheral rail is about to be/was just cut
 *                (MOSFET off): switches the pin to input + pull-DOWN so the
 *                line settles near 0V instead of staying pulled up toward
 *                3.3V while the sensor itself is unpowered (which would
 *                otherwise back-feed current into the DHT22 through its
 *                data-pin ESD clamp diode - "phantom powering"). false when
 *                the rail is powered again: restores the normal open-drain +
 *                pull-UP idle-high configuration dht22_init() sets up, which
 *                the protocol needs before a read can succeed.
 */
void dht22_set_sleeping(bool asleep);

/**
 * Read temperature (deg C) and relative humidity (%).
 * @return true on a successful, checksum-valid read.
 */
bool dht22_read(float* out_temp_c, float* out_humidity);

#ifdef __cplusplus
}
#endif
