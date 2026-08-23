/*
 * gateway.cpp - LDSE gateway role (layer 0), ESP-IDF port.
 *
 * Announces layer 1, sends FTSP sync beacons during the SYNC window, and logs
 * received node data / fire alerts to serial as CSV. Stays awake as the
 * mains-powered network sink. No sensors or classifier are compiled here.
 */

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "ldse/LdseConfig.h"
#include "ldse/LdseEpoch.h"
#include "ldse/LdsePacket.h"
#include "ldse/LdseRadio.h"
#include "ldse/LdseSync.h"
#include "payload.h"

using namespace ldse;

static const char* TAG = "GW";

static LdseRadio g_radio;
static LdseSync g_sync;
static uint16_t g_seq = 0;
static uint8_t g_lastWindow = WIN_SLEEP;
static uint32_t g_nextBeaconMs = 0;
static uint32_t g_dataPackets = 0;

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
}

void ldse_gateway_main()
{
    printf("[LDSE] Gateway (layer 0)\n");

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
