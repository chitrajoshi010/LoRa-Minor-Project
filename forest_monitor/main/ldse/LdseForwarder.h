#pragma once

/*
 * LdseForwarder.h - forwarding engine with congestion control (Phase 8).
 *
 * Mirrors the ns-3 LdseForwarder:
 *   - LdseRelayQueue with a bounded capacity
 *   - congestion flag when buffer utilization >= LDSE_CONGESTION_THRESHOLD
 *   - GetDataSpreadingFactor(): SF9 normal, SF10 congested
 *   - ShouldBypassToGateway(): true when congested
 *   - exponential backoff + CAD listen before transmit (TryTransmit)
 *   - OnParentFailure / IsParentFailed / route-refresh signal
 *
 * Used by the relay (and by the node for its own uplink retransmissions).
 */

#include "LdseCompat.h"

#include "LdseConfig.h"
#include "LdsePacket.h"
#include "LdseRadio.h"

namespace ldse
{

class LdseForwarder
{
  public:
    LdseForwarder();

    void SetRelayCapacity(uint8_t capacity);
    void SetGatewayId(uint8_t gatewayId);

    // ---- relay queue ----
    bool Enqueue(const LdsePacket& pkt);
    bool Dequeue(LdsePacket& pkt);
    uint8_t GetQueueSize() const;
    float GetBufferUtilization() const;

    // ---- congestion control ----
    bool IsCongested() const;
    uint8_t GetDataSpreadingFactor() const;
    bool ShouldBypassToGateway() const;

    // ---- CAD listen / exponential backoff ----
    /**
     * Try to transmit with CAD listen + exponential backoff (slot * 2^attempt).
     *
     * @param radio The radio (on the intended channel already).
     * @param pkt The packet to send.
     * @param parentId The intended receiver (for ACK wait / failure handling).
     * @return true if transmitted successfully.
     */
    bool TryTransmit(LdseRadio& radio, const LdsePacket& pkt, uint8_t parentId);

    // ---- parent failure ----
    void OnParentFailure(uint8_t parentId);
    bool IsParentFailed(uint8_t parentId) const;
    uint8_t GetParentFailureDroppedCount() const;

    /** True if a route refresh (new route discovery) is requested. */
    bool RouteRefreshRequested() const;
    void ClearRouteRefresh();

    uint16_t GetForwardedCount() const;
    uint16_t GetDroppedCount() const;

  private:
    LdsePacket m_queue[LDSE_RELAY_QUEUE_CAPACITY];
    uint8_t m_head;
    uint8_t m_tail;
    uint8_t m_size;
    uint8_t m_capacity;
    uint8_t m_gatewayId;

    bool m_congested;
    uint8_t m_failedParent;
    bool m_routeRefresh;
    uint16_t m_forwarded;
    uint16_t m_dropped;
    uint16_t m_parentFailureDropped;
    uint32_t m_backoffAttempts;
};

} // namespace ldse
