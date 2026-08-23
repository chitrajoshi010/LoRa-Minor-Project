#pragma once

/*
 * dht22.h - DHT22 (AM2302) temperature/humidity read wrapper.
 *
 * Backed by the chmorgan/esp-dht component (bit-banged single-wire driver).
 * Data line on GPIO12.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Configure the DHT22 data GPIO (call once at startup). */
void dht22_init(int gpio);

/**
 * Read temperature (deg C) and relative humidity (%).
 * @return true on a successful, checksum-valid read.
 */
bool dht22_read(float* out_temp_c, float* out_humidity);

#ifdef __cplusplus
}
#endif
