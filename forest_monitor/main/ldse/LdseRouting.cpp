#include "LdseRouting.h"

namespace ldse
{

LdseRouting::LdseRouting()
    : m_count(0),
      m_alpha(0.5f),
      m_beta(0.5f)
{
    memset(m_entries, 0, sizeof(m_entries));
}

float
LdseRouting::Score(const LdseRouteEntry& e) const
{
    float energy = static_cast<float>(e.energyPct) / 100.0f;
    // Link quality normalized from RSSI: -30 dBm (best) .. -120 dBm (worst).
    float link = ldse_clampf(1.0f - (static_cast<float>(e.rssiDbm) + 120.0f) / 90.0f, 0.0f, 1.0f);
    return m_alpha * energy + m_beta * link;
}

void
LdseRouting::UpdateParent(uint8_t parentId, uint8_t layer, int8_t rssiDbm, uint8_t energyPct)
{
    for (uint8_t i = 0; i < m_count; ++i)
    {
        if (m_entries[i].parentId == parentId)
        {
            m_entries[i].layer = layer;
            m_entries[i].rssiDbm = rssiDbm;
            m_entries[i].energyPct = energyPct;
            m_entries[i].lastSeenMs = millis();
            return;
        }
    }
    if (m_count >= MAX_PARENTS)
    {
        return; // table full: ignore new parents
    }
    m_entries[m_count].parentId = parentId;
    m_entries[m_count].layer = layer;
    m_entries[m_count].rssiDbm = rssiDbm;
    m_entries[m_count].energyPct = energyPct;
    m_entries[m_count].lastSeenMs = millis();
    m_count++;
}

void
LdseRouting::UpdateParentEnergy(uint8_t parentId, uint8_t energyPct)
{
    for (uint8_t i = 0; i < m_count; ++i)
    {
        if (m_entries[i].parentId == parentId)
        {
            m_entries[i].energyPct = energyPct;
            m_entries[i].lastSeenMs = millis();
            return;
        }
    }
}

void
LdseRouting::RemoveParent(uint8_t parentId)
{
    for (uint8_t i = 0; i < m_count; ++i)
    {
        if (m_entries[i].parentId == parentId)
        {
            for (uint8_t j = i; j + 1 < m_count; ++j)
            {
                m_entries[j] = m_entries[j + 1];
            }
            m_count--;
            return;
        }
    }
}

uint8_t
LdseRouting::PruneExpired(uint32_t maxAgeMs)
{
    uint8_t pruned = 0;
    uint8_t i = 0;
    while (i < m_count)
    {
        if (millis() - m_entries[i].lastSeenMs > maxAgeMs)
        {
            RemoveParent(m_entries[i].parentId);
            pruned++;
        }
        else
        {
            i++;
        }
    }
    return pruned;
}

uint8_t
LdseRouting::SelectBestParent() const
{
    if (m_count == 0)
    {
        return LDSE_BROADCAST;
    }
    uint8_t bestIdx = 0;
    for (uint8_t i = 1; i < m_count; ++i)
    {
        bool lowerLayer = m_entries[i].layer < m_entries[bestIdx].layer;
        bool sameLayerBetterScore =
            (m_entries[i].layer == m_entries[bestIdx].layer) &&
            (Score(m_entries[i]) > Score(m_entries[bestIdx]));
        if (lowerLayer || sameLayerBetterScore)
        {
            bestIdx = i;
        }
    }
    return m_entries[bestIdx].parentId;
}

uint8_t
LdseRouting::GetSize() const
{
    return m_count;
}

bool
LdseRouting::HasParent() const
{
    return m_count > 0;
}

void
LdseRouting::SetAlpha(float alpha)
{
    m_alpha = alpha;
}

void
LdseRouting::SetBeta(float beta)
{
    m_beta = beta;
}

} // namespace ldse
