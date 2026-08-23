#pragma once

/*
 * firebase_uploader.h - gateway-only Firebase RTDB upload (CONFIG_LDSE_ROLE=0).
 *
 * Pushes decoded MSG_DATA / MSG_FIRE_ALERT packets to Firebase Realtime
 * Database using the mesh schema documented in CLAUDE.md / the dashboard:
 *
 *   /nodes/{nodeId}/latest         <- overwritten every cycle (PUT)
 *   /nodes/{nodeId}/history/{key}  <- append-only (POST)
 *
 * This runs entirely on a dedicated background FreeRTOS task fed by a
 * queue - firebase_uploader_enqueue() is called from the LDSE receive loop
 * (gateway.cpp) and must never block on the network. If the queue is full
 * or Wi-Fi isn't connected, the item is dropped (CSV/serial logging is the
 * source of truth; Firebase is best-effort onward delivery), matching the
 * same "don't stall the LDSE loop" rule the Wi-Fi skill applies to
 * wifi_manager.
 */

#include <stdint.h>
#include <stdbool.h>

#include "ldse/LdsePacket.h"
#include "payload.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Create the upload queue and background task. Call once, after
 * wifi_manager_init(), before entering the LDSE main loop. Safe to call
 * even if CONFIG_FIREBASE_UPLOAD_ENABLE is off (becomes a no-op).
 */
void firebase_uploader_init(void);

/**
 * Queue one decoded packet for upload. Non-blocking: returns immediately
 * whether or not the item was actually accepted (queue full -> dropped).
 * `pkt` must be a MSG_DATA or MSG_FIRE_ALERT packet whose payload decodes
 * to a ldse::NodePayload (37 bytes); `nowEpochSec` is wall-clock seconds
 * since epoch if available, else any monotonically-useful timestamp.
 */
void firebase_uploader_enqueue(const ldse::LdsePacket& pkt, uint32_t nowEpochSec, uint32_t seqCounter);

#ifdef __cplusplus
}
#endif
