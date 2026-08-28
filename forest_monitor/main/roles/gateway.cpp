/*
 * gateway.cpp - LDSE gateway role (layer 0), ESP-IDF port.
 *
 * Announces layer 1, sends FTSP sync beacons during the SYNC window, and logs
 * received node data / fire alerts to serial as CSV. Stays awake as the
 * mains-powered network sink. No sensors or classifier are compiled here.
 *
 * Also brings up Wi-Fi station mode (net/wifi_manager) for onward delivery
 * of the CSV/DATA|FIRE stream (e.g. to Firebase). wifi_manager_init() is
 * non-blocking - it starts connecting and returns immediately so SYNC
 * beacons keep firing on schedule with or without an IP yet.
 */

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

#include "ldse/LdseConfig.h"
#include "ldse/LdseEpoch.h"
#include "ldse/LdsePacket.h"
#include "ldse/LdseRadio.h"
#include "ldse/LdseSync.h"
#include "payload.h"

#include "net/wifi_manager.h"
#include "net/firebase_uploader.h"

using namespace ldse;

static const char* TAG = "GW";

static LdseRadio g_radio;
static LdseSync g_sync;
static uint16_t g_seq = 0;
static uint8_t g_lastWindow = WIN_SLEEP;
static uint32_t g_nextBeaconMs = 0;
static uint32_t g_dataPackets = 0;

// Display-only labels mirroring main/acoustic/classifier.cpp's
// ACOUSTIC_LABELS. Duplicated (not #included) so the gateway keeps decoding
// NodePayload without pulling in the classifier/TFLite build (payload.h must
// stay dependency-free - see AGENTS.md).
static const char* const GW_ACOUSTIC_LABELS[LDSE_ACOUSTIC_CLASSES] = {
    "Axe", "Chainsaw", "Gunshot", "Handsaw", "Background",
};

static const char* MsgTypeName(uint8_t type)
{
    switch (type)
    {
        case MSG_LAYER_INIT: return "LAYER_INIT";
        case MSG_SYNC: return "SYNC";
        case MSG_RREQ: return "RREQ";
        case MSG_RREP: return "RREP";
        case MSG_HANDSHAKE: return "HANDSHAKE";
        case MSG_DATA: return "DATA";
        case MSG_ACK: return "ACK";
        case MSG_FIRE_ALERT: return "FIRE_ALERT";
        default: return "UNKNOWN";
    }
}

// Dump every LDSE header field and (if present) the fully decoded
// NodePayload/hex bytes to the serial console. Called before any Wi-Fi/
// Firebase handoff so the raw packet is always visible on the monitor even
// if the network upload is queued, dropped, or Wi-Fi is down.
static void DumpPacketFull(const LdsePacket& pkt, const NodePayload* np, bool haveNp)
{
    printf("---- RX PACKET ----\n");
    printf("  type=0x%02X (%s) srcId=%u dstId=%u originId=%u hopCount=%u layer=%u\n",
           pkt.type, MsgTypeName(pkt.type), pkt.srcId, pkt.dstId, pkt.originId,
           pkt.hopCount, pkt.layer);
    printf("  timestampUs=%lu seq=%u energyPct=%u%% rssiDbm=%d payloadLen=%u\n",
           (unsigned long)pkt.timestampUs, pkt.seq, pkt.energyPct, pkt.rssiDbm,
           pkt.payloadLen);

    printf("  payload (hex):");
    for (uint8_t i = 0; i < pkt.payloadLen; i++)
    {
        printf(" %02X", pkt.payload[i]);
    }
    printf("\n");

    if (haveNp && np != nullptr)
    {
        const char* label = (np->classIdx < LDSE_ACOUSTIC_CLASSES)
                                 ? GW_ACOUSTIC_LABELS[np->classIdx]
                                 : "?";
        printf("  payload (decoded NodePayload):\n");
        printf("    classIdx=%u (%s) confidence=[%.3f, %.3f, %.3f, %.3f, %.3f]\n",
               np->classIdx, label, np->confidence[0], np->confidence[1],
               np->confidence[2], np->confidence[3], np->confidence[4]);
        printf("    temperature=%.2f C  humidity=%.2f %%  gas=%.1f mV  fireScore=%.3f\n",
               np->temperature, np->humidity, np->gas, np->fireScore);
    }
    else
    {
        printf("  payload: not a NodePayload (len %u != %u expected)\n",
               pkt.payloadLen, (unsigned)sizeof(NodePayload));
    }
    printf("--------------------\n");
}

static void BroadcastLayerInit()
{
    LdsePacket pkt;
    pkt.type = MSG_LAYER_INIT;
    pkt.srcId = LDSE_GATEWAY_ID;
    pkt.dstId = LDSE_BROADCAST;
    pkt.originId = LDSE_GATEWAY_ID;
    pkt.hopCount = 0;
    pkt.layer = 1; // advertised layer for direct children
    pkt.seq = g_seq++;
    g_radio.Send(pkt);
}

static void SendSync()
{
    LdsePacket pkt;
    pkt.type = MSG_SYNC;
    pkt.srcId = LDSE_GATEWAY_ID;
    pkt.dstId = LDSE_BROADCAST;
    pkt.originId = LDSE_GATEWAY_ID;
    pkt.hopCount = 0;
    pkt.layer = 0;
    pkt.timestampUs = g_sync.LocalTimeUs();
    pkt.seq = g_seq++;
    g_radio.Send(pkt);
}

static void SendAck(const LdsePacket& rx)
{
    LdsePacket ack;
    ack.type = MSG_ACK;
    ack.srcId = LDSE_GATEWAY_ID;
    ack.dstId = rx.originId;
    ack.originId = LDSE_GATEWAY_ID;
    ack.seq = rx.seq;
    g_radio.Send(ack);
}

static void LogPacket(const LdsePacket& pkt, uint32_t nowMs)
{
    NodePayload np = {};
    bool haveNp = (pkt.payloadLen == sizeof(NodePayload));
    if (haveNp)
    {
        memcpy(&np, pkt.payload, sizeof(NodePayload));
    }

    // Full raw + decoded packet dump, shown on the monitor before any
    // CSV/Wi-Fi/Firebase handoff below.
    DumpPacketFull(pkt, &np, haveNp);

    // CSV: kind,epoch_ms,origin,src,hops,seq,rssi,layer,class,temp,hum,gas,fire,count
    const char* kind = (pkt.type == MSG_FIRE_ALERT) ? "FIRE" : "DATA";
    printf("%s,%lu,%u,%u,%u,%u,%d,%u,%d,%.2f,%.2f,%.1f,%.3f,%lu\n",
           kind,
           (unsigned long)(nowMs % LDSE_EPOCH_MS),
           pkt.originId, pkt.srcId, pkt.hopCount, pkt.seq,
           pkt.rssiDbm, pkt.layer,
           haveNp ? np.classIdx : -1,
           haveNp ? np.temperature : 0.0f,
           haveNp ? np.humidity : 0.0f,
           haveNp ? np.gas : 0.0f,
           haveNp ? np.fireScore : 0.0f,
           (unsigned long)g_dataPackets);

    if (haveNp)
    {
        // Best-effort onward delivery to Firebase (mesh schema). Never
        // blocks: enqueue() drops the item if Wi-Fi is down or the small
        // upload queue is full. Clock may not be NTP-synced yet (SNTP is
        // opportunistic in wifi_manager) - treat anything before roughly
        // 2020-01-01 as "no wall clock yet" and send 0 rather than a
        // meaningless small number.
        time_t nowEpoch = time(nullptr);
        uint32_t epochSec = (nowEpoch > 1577836800) ? (uint32_t)nowEpoch : 0;
        firebase_uploader_enqueue(pkt, epochSec, g_dataPackets);
    }
}

void ldse_gateway_main()
{
    printf("[LDSE] Gateway (layer 0)\n");

    // Non-blocking: starts connecting and returns immediately. Must come
    // before the LDSE radio/loop init below so SYNC beacons are never
    // delayed waiting on Wi-Fi (see .claude/wifi/SKILL.md constraint #1).
    wifi_manager_init();

    // Starts the background upload task/queue. Also non-blocking - the
    // task itself gates every upload on wifi_manager_is_connected().
    firebase_uploader_init();

    g_sync.Begin(0.0f); // gateway is the reference clock
    g_sync.SetHopCount(0);

    if (!g_radio.Begin(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL))
    {
        ESP_LOGE(TAG, "Radio init FAILED");
        for (;;)
        {
            delay(1000);
        }
    }
    g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
    printf("[LDSE] Radio on Puc ready\n");

    for (;;)
    {
        uint32_t nowMs = millis();
        uint8_t win = LdseEpoch::GetWindow(nowMs);

        if (win != g_lastWindow)
        {
            if (win == WIN_SYNC)
            {
                g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
                g_nextBeaconMs = nowMs;
            }
            else if (win == WIN_DATA)
            {
                g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
            }
            g_lastWindow = win;
        }

        // Beacon burst across the SYNC window so unsynced children cold-start.
        if (win == WIN_SYNC && (int32_t)(nowMs - g_nextBeaconMs) >= 0)
        {
            BroadcastLayerInit();
            SendSync();
            g_nextBeaconMs = nowMs + LDSE_SYNC_BEACON_PERIOD_MS;
        }

        LdsePacket pkt;
        if (g_radio.Receive(pkt, 10))
        {
            if (pkt.type == MSG_DATA || pkt.type == MSG_FIRE_ALERT)
            {
                g_dataPackets++;
                LogPacket(pkt, nowMs);
                SendAck(pkt);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // yield to the idle task (WDT)
    }
}
