#include "LdseForwarder.h"

namespace ldse
{

LdseForwarder::LdseForwarder()
    : m_head(0),
      m_tail(0),
      m_size(0),
      m_capacity(LDSE_RELAY_QUEUE_CAPACITY),
      m_gatewayId(LDSE_GATEWAY_ID),
      m_congested(false),
      m_failedParent(LDSE_BROADCAST),
      m_routeRefresh(false),
      m_forwarded(0),
      m_dropped(0),
      m_parentFailureDropped(0),
      m_backoffAttempts(0)
{
}

void
LdseForwarder::SetRelayCapacity(uint8_t capacity)
{
    m_capacity = capacity;
}

void
LdseForwarder::SetGatewayId(uint8_t gatewayId)
{
    m_gatewayId = gatewayId;
}

bool
LdseForwarder::Enqueue(const LdsePacket& pkt)
{
    if (m_size >= m_capacity)
    {
        m_dropped++;
        return false; // queue full
    }
    m_queue[m_tail] = pkt;
    m_tail = (m_tail + 1) % m_capacity;
    m_size++;
    m_congested = GetBufferUtilization() >= LDSE_CONGESTION_THRESHOLD;
    return true;
}

bool
LdseForwarder::Dequeue(LdsePacket& pkt)
{
    if (m_size == 0)
    {
        return false;
    }
    pkt = m_queue[m_head];
    m_head = (m_head + 1) % m_capacity;
    m_size--;
    m_congested = GetBufferUtilization() >= LDSE_CONGESTION_THRESHOLD;
    return true;
}

uint8_t
LdseForwarder::GetQueueSize() const
{
    return m_size;
}

float
LdseForwarder::GetBufferUtilization() const
{
    if (m_capacity == 0)
    {
        return 0.0f;
    }
    return static_cast<float>(m_size) / static_cast<float>(m_capacity);
}

bool
LdseForwarder::IsCongested() const
{
    return m_congested;
}

uint8_t
LdseForwarder::GetDataSpreadingFactor() const
{
    return m_congested ? LDSE_SF_CONGESTED : LDSE_SF_NORMAL;
}

bool
LdseForwarder::ShouldBypassToGateway() const
{
    return m_congested;
}

bool
LdseForwarder::TryTransmit(LdseRadio& radio, const LdsePacket& pkt, uint8_t parentId)
{
    // CAD listen: wait for the channel to clear, with exponential backoff.
    for (uint8_t attempt = 0; attempt < LDSE_MAX_ACK_RETRIES; ++attempt)
    {
        m_backoffAttempts = attempt;
        uint32_t delayMs = LDSE_BACKOFF_SLOT_MS * (1u << attempt);
        delay(delayMs);

        if (!radio.ChannelFree(500))
        {
            continue; // channel busy: retry with a larger backoff
        }
        if (radio.Send(pkt))
        {
            m_forwarded++;
            return true;
        }
    }
    // All attempts failed: treat as a parent failure.
    OnParentFailure(parentId);
    return false;
}

void
LdseForwarder::OnParentFailure(uint8_t parentId)
{
    m_failedParent = parentId;
    m_parentFailureDropped++;
    m_routeRefresh = true;
}

bool
LdseForwarder::IsParentFailed(uint8_t parentId) const
{
    return m_failedParent == parentId;
}

uint8_t
LdseForwarder::GetParentFailureDroppedCount() const
{
    return m_parentFailureDropped;
}

bool
LdseForwarder::RouteRefreshRequested() const
{
    return m_routeRefresh;
}

void
LdseForwarder::ClearRouteRefresh()
{
    m_routeRefresh = false;
}

uint16_t
LdseForwarder::GetForwardedCount() const
{
    return m_forwarded;
}

uint16_t
LdseForwarder::GetDroppedCount() const
{
    return m_dropped;
}

} // namespace ldse
