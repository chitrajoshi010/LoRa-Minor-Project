/*
 * relay.cpp - LDSE relay role (layer 1), ESP-IDF port.
 *
 * Learns layer 1 from the gateway, forwards the sync beacon to the node,
 * listens for node data on Prc1 and forwards it to the gateway on Puc with
 * CAD + backoff and congestion control. Additionally samples its own MQ-135 +
 * DHT22 + INMP441 mic (via the background acoustic classifier task) each
 * epoch and raises an alert to the gateway (carrying the latest acoustic
 * class) when either its fire-risk score exceeds FIRE_ALERT_THRESHOLD or the
 * classifier reports a threat class (Axe/Chainsaw/Gunshot/Handsaw) with
 * confidence >= ACOUSTIC_ALERT_THRESHOLD (see classifier_is_threat()).
 */

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "ldse/LdseConfig.h"
#include "ldse/LdseEpoch.h"
#include "ldse/LdseEnergy.h"
#include "ldse/LdseForwarder.h"
#include "ldse/LdsePacket.h"
#include "ldse/LdseRadio.h"
#include "ldse/LdseRouting.h"
#include "ldse/LdseSync.h"
#include "payload.h"

#include "classifier.h"
#include "mq135.h"
#include "dht22.h"
#include "fire_scoring.h"

using namespace ldse;

#define RELAY_DHT_GPIO 12

static LdseRadio g_radio;
static LdseSync g_sync;
static LdseRouting g_routing;
static LdseForwarder g_forwarder;
static LdseEnergy g_energy;

static uint8_t g_layer = 0;
static uint8_t g_lastWindow = WIN_SLEEP;
static bool g_synced = false;
static int32_t g_phaseOffsetMs = 0;
static uint32_t g_lastSyncFwdMs = 0;
static uint32_t g_rxFromNode = 0;
static uint32_t g_forwardedToGateway = 0;
static uint32_t g_lastFireEpochMs = 0;

static void OnSync(const LdsePacket& pkt)
{
    uint32_t rxLocalUs = micros();
    bool firstSync = !g_synced;
    g_sync.OnReceiveSync(pkt.timestampUs, rxLocalUs);
    g_sync.ApplyCorrection();
    g_sync.SetHopCount(pkt.hopCount + 1);
    g_synced = true;
    g_phaseOffsetMs = -(int32_t)(g_sync.GetOffsetUs() / 1000);
    g_energy.Wake();
    printf("[REL] SYNC offset=%ld us, hops=%u\n", (long)g_sync.GetOffsetUs(), g_sync.GetHopCount());

    g_routing.UpdateParent(pkt.srcId, pkt.layer, pkt.rssiDbm, pkt.energyPct);

    if (!firstSync && (uint32_t)(millis() - g_lastSyncFwdMs) < LDSE_SYNC_FWD_INTERVAL_MS)
    {
        return;
    }
    g_lastSyncFwdMs = millis();

    LdsePacket fwd = pkt;
    fwd.srcId = LDSE_RELAY_ID;
    fwd.originId = LDSE_GATEWAY_ID;
    fwd.hopCount = pkt.hopCount + 1;
    fwd.layer = g_layer;
    fwd.timestampUs = g_sync.LocalTimeUs();
    g_radio.SetChannel(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL);
    g_radio.Send(fwd);
    g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
}

static void OnLayerInit(const LdsePacket& pkt)
{
    g_layer = pkt.layer;
    g_routing.UpdateParent(pkt.srcId, 0, pkt.rssiDbm, pkt.energyPct);
    printf("[REL] Layer = %u, parent = gateway(%u)\n", g_layer, pkt.srcId);

    bool forward = !g_synced || (uint32_t)(millis() - g_lastSyncFwdMs) >= LDSE_SYNC_FWD_INTERVAL_MS;
    if (!forward)
    {
        return;
    }
    g_lastSyncFwdMs = millis();

    LdsePacket fwd = pkt;
    fwd.srcId = LDSE_RELAY_ID;
    fwd.originId = LDSE_GATEWAY_ID;
    fwd.hopCount = pkt.hopCount + 1;
    fwd.layer = g_layer + 1;
    g_radio.SetChannel(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL);
    g_radio.Send(fwd);
    g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
}

static void OnDataFromNode(const LdsePacket& pkt)
{
    g_rxFromNode++;
    if (!g_forwarder.Enqueue(pkt))
    {
        printf("[REL] Queue full: dropped packet\n");
        return;
    }
    LdsePacket ack;
    ack.type = MSG_ACK;
    ack.srcId = LDSE_RELAY_ID;
    ack.dstId = pkt.srcId;
    ack.originId = LDSE_RELAY_ID;
    ack.seq = pkt.seq;
    g_radio.Send(ack);
}

static void DrainQueue()
{
    LdsePacket pkt;
    while (g_forwarder.Dequeue(pkt))
    {
        uint8_t sf = g_forwarder.GetDataSpreadingFactor();
        g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, sf);
        g_radio.Standby();
        if (g_forwarder.TryTransmit(g_radio, pkt, LDSE_GATEWAY_ID))
        {
            g_forwardedToGateway++;
            printf("[REL] Forwarded seq=%u sf=%u bypass=%d\n",
                   pkt.seq, sf, g_forwarder.ShouldBypassToGateway());
        }
        else
        {
            printf("[REL] Forward failed seq=%u\n", pkt.seq);
        }
    }
    if (g_forwarder.RouteRefreshRequested())
    {
        g_forwarder.ClearRouteRefresh();
    }
}

// Sample local sensors (gas + temp/humidity) and the acoustic classifier,
// then raise an alert to the gateway when either the fire-risk score or the
// acoustic threat confidence exceeds its threshold (see
// classifier_is_threat()). Background/low-confidence epochs with no fire
// risk send nothing.
static void RelayAlertCheck()
{
    AcousticResult ar;
    bool haveAc = classifier_get_latest(&ar);
    float temp = 0.0f, hum = 0.0f;
    dht22_read(&temp, &hum);
    float gas = mq135_read_mv();
    float score = fire_score_compute(gas, temp, hum);

    bool fire = score >= FIRE_ALERT_THRESHOLD;
    bool acousticAlert = haveAc && classifier_is_threat(&ar);
    if (!fire && !acousticAlert)
    {
        return;
    }

    NodePayload np = {};
    if (haveAc)
    {
        np.classIdx = ar.classIdx;
        memcpy(np.confidence, ar.confidence, sizeof(np.confidence));
    }
    else
    {
        np.classIdx = (uint8_t)(LDSE_ACOUSTIC_CLASSES - 1); // Background
    }
    np.temperature = temp;
    np.humidity = hum;
    np.gas = gas;
    np.fireScore = score;

    LdsePacket pkt;
    pkt.type = fire ? MSG_FIRE_ALERT : MSG_DATA;
    pkt.srcId = LDSE_RELAY_ID;
    pkt.dstId = LDSE_GATEWAY_ID;
    pkt.originId = LDSE_RELAY_ID;
    pkt.hopCount = 1;
    pkt.layer = g_layer;
    pkt.energyPct = (uint8_t)(g_energy.GetBatteryMouth() / LDSE_BATTERY_CAPACITY_MAH * 100.0f);
    memcpy(pkt.payload, &np, sizeof(NodePayload));
    pkt.payloadLen = sizeof(NodePayload);

    g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, g_forwarder.GetDataSpreadingFactor());
    g_radio.Standby();
    g_forwarder.TryTransmit(g_radio, pkt, LDSE_GATEWAY_ID);
    printf("[REL] %s alert score=%.3f temp=%.1f gas=%.0f class=%s\n",
           fire ? "FIRE" : "ACOUSTIC", score, temp, gas, haveAc ? ACOUSTIC_LABELS[np.classIdx] : "n/a");
}

void ldse_relay_main()
{
    printf("[LDSE] Relay\n");

    g_sync.Begin(LDSE_DRIFT_PPM);
    g_energy.Begin();
    g_forwarder.SetGatewayId(LDSE_GATEWAY_ID);
    g_forwarder.SetRelayCapacity(LDSE_RELAY_QUEUE_CAPACITY);

    mq135_init();
    dht22_init(RELAY_DHT_GPIO);
    if (!classifier_start())
    {
        printf("[REL] classifier_start() failed\n");
    }

    if (!g_radio.Begin(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL))
    {
        printf("[LDSE] Radio init FAILED\n");
        for (;;)
        {
            delay(1000);
        }
    }
    g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
    printf("[LDSE] Relay listening on Puc\n");

    for (;;)
    {
        if (!g_synced)
        {
            LdsePacket pkt;
            if (g_radio.Receive(pkt, 20))
            {
                if (pkt.type == MSG_SYNC)
                {
                    OnSync(pkt);
                }
                else if (pkt.type == MSG_LAYER_INIT)
                {
                    OnLayerInit(pkt);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint32_t nowMs = millis();
        uint8_t win = LdseEpoch::GetWindow(nowMs, g_phaseOffsetMs);

        if (win != g_lastWindow)
        {
            if (win == WIN_SYNC)
            {
                g_energy.Wake();
                g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, LDSE_SF_NORMAL);
            }
            else if (win == WIN_DATA)
            {
                g_energy.Wake();
                g_radio.SetChannel(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL);
            }
            else // WIN_SLEEP
            {
                g_energy.EnterSleep();
                g_radio.Sleep();
                printf("[REL] Sleep: energy=%.3f J battery=%.1f mAh\n",
                       g_energy.GetEnergyConsumedJ(), g_energy.GetBatteryMouth());
            }
            g_lastWindow = win;
        }

        if (win == WIN_SYNC)
        {
            LdsePacket pkt;
            if (g_radio.Receive(pkt, 20))
            {
                if (pkt.type == MSG_SYNC)
                {
                    OnSync(pkt);
                }
                else if (pkt.type == MSG_LAYER_INIT)
                {
                    OnLayerInit(pkt);
                }
            }
        }
        else if (win == WIN_DATA)
        {
            // Phase A: collect node data on Prc1.
            uint32_t listenDeadline = nowMs + LDSE_RELAY_LISTEN_MS;
            while (millis() < listenDeadline)
            {
                LdsePacket pkt;
                if (g_radio.Receive(pkt, 20))
                {
                    if (pkt.type == MSG_DATA || pkt.type == MSG_FIRE_ALERT)
                    {
                        OnDataFromNode(pkt);
                    }
                }
            }
            // Phase B: forward the queue to the gateway on the Puc.
            DrainQueue();

            // Phase C: local alert check - fire risk + acoustic threat (once per epoch).
            if ((uint32_t)(nowMs - g_lastFireEpochMs) >= LDSE_EPOCH_MS)
            {
                g_lastFireEpochMs = nowMs;
                RelayAlertCheck();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // yield to the idle task (WDT)
    }
}
