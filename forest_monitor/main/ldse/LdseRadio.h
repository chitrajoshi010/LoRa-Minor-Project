#pragma once

/*
 * LdseRadio.h - thin RadioLib wrapper around the SX1278 LoRa radio.
 *
 * Maps to the ns-3 SimpleEndDeviceLoraPhy / LoraChannelManager usage:
 *   - channel switching (Puc / Prc) via SetChannel
 *   - CAD (clear channel assessment) equivalent of the ns-3 "channel busy"
 *     listen used by the forwarder backoff (Phase 8)
 *   - Sleep / Standby used by the epoch scheduler (Phase 7)
 */

#include <RadioLib.h>

#include "LdseConfig.h"
#include "LdsePacket.h"

namespace ldse
{

class LdseRadio
{
  public:
    LdseRadio();
    ~LdseRadio();

    /** Open the radio on the Puc channel at the given spreading factor. */
    bool Begin(float freqMhz, uint8_t sf);

    /** Switch channel (frequency + SF) and re-arm reception. */
    void SetChannel(float freqMhz, uint8_t sf);

    /** Transmit a frame. @return true on success. */
    bool Send(const LdsePacket& pkt);

    /** Block until a valid frame arrives or timeout. @return true on RX. */
    bool Receive(LdsePacket& pkt, uint32_t timeoutMs);

    /**
     * Clear channel assessment. Returns true if the channel is free
     * (equivalent to the forwarder "channel idle" check).
     */
    bool ChannelFree(uint32_t cadTimeoutMs);

    /** Low-power sleep (SLEEP window). */
    void Sleep();

    /** Standby (RX idle). */
    void Standby();

    float GetLastRssi() const;

  private:
    SX1278* m_radio;
    Module* m_mod;
    EspHal* m_hal;
    float m_lastRssi;
};

} // namespace ldse
