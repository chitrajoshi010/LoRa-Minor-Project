/*
 * node.cpp - LDSE end-node role (layer 2), ESP-IDF port.
 *
 * Runs the acoustic classifier in a background task and, each DATA window,
 * builds a NodePayload from the latest inference plus the MQ-135 / DHT22
 * readings and its fire-risk score. A packet is only sent when there is
 * something to report: fire score >= FIRE_ALERT_THRESHOLD, or the acoustic
 * classifier's argmax is a threat class (Axe/Chainsaw/Gunshot/Handsaw) with
 * confidence >= ACOUSTIC_ALERT_THRESHOLD (see classifier_is_threat()).
 * Background / low-confidence epochs send nothing. Sends MSG_DATA to the
 * relay on Prc1; if the fire score exceeds the threshold it sends
 * MSG_FIRE_ALERT instead, and if the relay is unreachable/congested it
 * bypasses directly to the gateway on Puc (LdseForwarder::ShouldBypassToGateway).
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
#include "ldse/LdseSleepGate.h"
#include "ldse/LdseSync.h"
#include "payload.h"

#include "classifier.h"
#include "mq135.h"
#include "dht22.h"
#include "fire_scoring.h"

using namespace ldse;

#define NODE_DHT_GPIO 12

static LdseRadio g_radio;
static LdseSync g_sync;
static LdseRouting g_routing;
static LdseForwarder g_forwarder;
static LdseEnergy g_energy;
static LdseSleepGate g_sleepGate;

static uint8_t g_layer = 0;
static uint8_t g_lastWindow = WIN_SLEEP;
static uint16_t g_seq = 0;
static uint32_t g_txSuccess = 0;
static uint32_t g_txFail = 0;
static bool g_synced = false;
static int32_t g_phaseOffsetMs = 0;

static void OnSync(const LdsePacket& pkt)
{
    uint32_t rxLocalUs = micros();
    g_sync.OnReceiveSync(pkt.timestampUs, rxLocalUs);
    g_sync.ApplyCorrection();
    g_sync.SetHopCount(pkt.hopCount + 1);
    g_synced = true;
    g_phaseOffsetMs = -(int32_t)(g_sync.GetOffsetUs() / 1000);
    g_energy.Wake();
    printf("[NODE] SYNC offset=%ld us, hops=%u\n", (long)g_sync.GetOffsetUs(), g_sync.GetHopCount());
    g_routing.UpdateParent(pkt.srcId, g_layer ? g_layer - 1 : pkt.layer, pkt.rssiDbm, pkt.energyPct);
}

static void OnLayerInit(const LdsePacket& pkt)
{
    g_layer = pkt.layer;
    g_routing.UpdateParent(pkt.srcId, g_layer - 1, pkt.rssiDbm, pkt.energyPct);
    printf("[NODE] Layer = %u, parent = relay(%u)\n", g_layer, pkt.srcId);
}

// Wait for a matching ACK (from relay or gateway) within the timeout.
static bool WaitForAck(uint16_t seq)
{
    uint32_t deadline = millis() + LDSE_ACK_TIMEOUT_MS;
    while (millis() < deadline)
    {
        LdsePacket ack;
        if (g_radio.Receive(ack, 20) && ack.type == MSG_ACK && ack.seq == seq)
        {
            return true;
        }
    }
    return false;
}

static void SendData()
{
    if (!g_routing.HasParent())
    {
        printf("[NODE] No parent yet: waiting for LAYER_INIT/SYNC\n");
        return;
    }

    // ---- gather payload ----
    AcousticResult ar;
    bool haveAc = classifier_get_latest(&ar);
    bool railAwake = g_sleepGate.IsAwake();
    float temp = 0.0f, hum = 0.0f;
    bool envValid = railAwake && dht22_read(&temp, &hum);
    float gas = 0.0f;
    bool gasValid = railAwake && mq135_read(railAwake, &gas);
    float score = fire_score_compute(gasValid, gas, envValid, temp, hum);

    bool fire = score >= FIRE_ALERT_THRESHOLD;
    bool acousticAlert = haveAc && classifier_is_threat(&ar);

    // Bench-test visibility: log the raw sensor readings every DATA epoch,
    // regardless of whether they're alert-worthy (no radio TX happens below
    // for a no-alert epoch, so this is the only place these values surface).
    printf("[NODE] sensors T=%.1f%s H=%.1f%s gas=%.0f%s class=%s(%.2f) fire_score=%.3f\n",
           envValid ? temp : 0.0f, envValid ? "" : " (n/a)",
           envValid ? hum : 0.0f, envValid ? "" : " (n/a)",
           gasValid ? gas : 0.0f, gasValid ? "" : " (n/a, rail unpowered)",
           haveAc ? ACOUSTIC_LABELS[ar.classIdx] : "n/a",
           haveAc ? ar.confidence[ar.classIdx] : 0.0f, score);

    if (!fire && !acousticAlert)
    {
        // Background, or a threat call below ACOUSTIC_ALERT_THRESHOLD, and
        // no fire risk this epoch: nothing worth spending airtime/energy on.
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
    np.temperature = envValid ? temp : 0.0f;
    np.humidity = envValid ? hum : 0.0f;
    np.gas = gasValid ? gas : 0.0f; // 0 = "no valid reading this epoch"
    np.fireScore = score;

    uint8_t parent = g_routing.SelectBestParent();
    uint8_t sf = g_forwarder.GetDataSpreadingFactor();
    bool bypass = g_forwarder.ShouldBypassToGateway();

    LdsePacket pkt;
    pkt.type = fire ? MSG_FIRE_ALERT : MSG_DATA;
    pkt.srcId = LDSE_NODE_ID;
    pkt.originId = LDSE_NODE_ID;
    pkt.hopCount = 1;
    pkt.layer = g_layer;
    pkt.seq = g_seq++;
    pkt.energyPct = (uint8_t)(g_energy.GetBatteryMouth() / LDSE_BATTERY_CAPACITY_MAH * 100.0f);
    memcpy(pkt.payload, &np, sizeof(NodePayload));
    pkt.payloadLen = sizeof(NodePayload);

    // Normal path: relay on Prc1. Congestion bypass: gateway on Puc.
    uint8_t dst = bypass ? LDSE_GATEWAY_ID : parent;
    float channel = bypass ? LDSE_FREQ_PUC_MHZ : LDSE_FREQ_PRC1_MHZ;
    pkt.dstId = dst;

    g_radio.SetChannel(channel, sf);
    g_radio.Standby();

    if (!g_forwarder.TryTransmit(g_radio, pkt, dst))
    {
        g_txFail++;
        // Relay unreachable: fall back to a direct gateway hop on Puc.
        if (dst != LDSE_GATEWAY_ID)
        {
            printf("[NODE] Relay unreachable: bypassing to gateway\n");
            pkt.dstId = LDSE_GATEWAY_ID;
            g_radio.SetChannel(LDSE_FREQ_PUC_MHZ, sf);
            g_radio.Standby();
            if (!g_forwarder.TryTransmit(g_radio, pkt, LDSE_GATEWAY_ID))
            {
                g_routing.RemoveParent(parent);
                return;
            }
        }
        else
        {
            g_routing.RemoveParent(parent);
            return;
        }
    }

    if (WaitForAck(pkt.seq))
    {
        g_txSuccess++;
        printf("[NODE] %s seq=%u acked class=%s T=%.1f%s H=%.1f gas=%.0f%s fire=%.3f sf=%u\n",
               fire ? "FIRE" : "DATA", pkt.seq, ACOUSTIC_LABELS[np.classIdx],
               envValid ? temp : 0.0f, envValid ? "" : " (n/a)", envValid ? hum : 0.0f,
               gasValid ? gas : 0.0f, gasValid ? "" : " (n/a, rail unpowered)", score, sf);
    }
    else
    {
        g_txFail++;
        printf("[NODE] No ACK seq=%u: route refresh\n", pkt.seq);
        g_forwarder.OnParentFailure(parent);
        g_routing.RemoveParent(parent);
    }
}

void ldse_node_main()
{
    printf("[LDSE] End device (node)\n");

    g_sync.Begin(LDSE_DRIFT_PPM);
    g_energy.Begin();
    g_sleepGate.Begin();
    g_forwarder.SetGatewayId(LDSE_GATEWAY_ID);

    mq135_init();
    dht22_init(NODE_DHT_GPIO);
    if (!classifier_start())
    {
        printf("[NODE] classifier_start() failed\n");
    }

    if (!g_radio.Begin(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL))
    {
        printf("[LDSE] Radio init FAILED\n");
        for (;;)
        {
            delay(1000);
        }
    }
    g_radio.SetChannel(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL);
    printf("[LDSE] Node listening on Prc1\n");

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
                g_sleepGate.Wake();
                classifier_set_mic_powered(true);
                g_radio.SetChannel(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL);
            }
            else if (win == WIN_DATA)
            {
                g_energy.Wake();
                g_sleepGate.Wake();
                classifier_set_mic_powered(true);
                g_radio.SetChannel(LDSE_FREQ_PRC1_MHZ, LDSE_SF_NORMAL);
                delay(LDSE_NODE_TX_OFFSET_MS);
                SendData();
            }
            else // WIN_SLEEP
            {
                g_energy.EnterSleep();
                g_sleepGate.Sleep();
                classifier_set_mic_powered(false);
                g_radio.Sleep();
                printf("[NODE] Sleep: energy=%.3f J battery=%.1f mAh txOK=%lu txFail=%lu\n",
                       g_energy.GetEnergyConsumedJ(), g_energy.GetBatteryMouth(),
                       (unsigned long)g_txSuccess, (unsigned long)g_txFail);
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

        vTaskDelay(pdMS_TO_TICKS(1)); // yield to the idle task (WDT)
    }
}
