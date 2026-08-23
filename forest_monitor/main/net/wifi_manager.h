#pragma once

/*
 * wifi_manager.h - gateway-only Wi-Fi station connectivity (CONFIG_LDSE_ROLE=0).
 *
 * Event-driven STA connect with capped exponential backoff on disconnect.
 * wifi_manager_init() is non-blocking: it starts the connection attempt and
 * returns immediately so the gateway's LDSE SYNC beacon (every 250 ms during
 * the SYNC window) is never delayed waiting on Wi-Fi. Callers must gate any
 * network I/O (HTTP/Firebase push, NTP, etc.) on wifi_manager_is_connected()
 * and keep functioning without it (CSV logging over serial, LDSE forwarding
 * do not depend on Wi-Fi).
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Bring up Wi-Fi station mode using CONFIG_GATEWAY_WIFI_SSID /
 * CONFIG_GATEWAY_WIFI_PASSWORD and start connecting in the background.
 * Does NOT block on WIFI_CONNECTED_BIT - safe to call before entering the
 * LDSE main loop. Reconnects automatically (capped exponential backoff, see
 * wifi_manager.cpp) if the connection drops.
 */
void wifi_manager_init(void);

/** @return true once an IP address has been obtained; false otherwise. */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
