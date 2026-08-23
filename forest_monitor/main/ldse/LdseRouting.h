#pragma once

/*
 * LdseRouting.h - IRE-style routing table (paper Section 2.2.3, Phase 4).
 *
 * Mirrors the ns-3 LdseRoutingTable:
 *   - UpdateParent / UpdateParentEnergy
 *   - SelectBestParent (score = alpha*energy + beta*link quality)
 *   - PruneExpired for stale entries
 *
 * A node discovers parents by listening on the Puc during route discovery:
 * the gateway / relay announce themselves, the node records them as parents
 * and selects the best one (lowest layer wins, ties broken by score).
 */

#include "LdseCompat.h"

#include "LdseConfig.h"
#include "LdsePacket.h"

namespace ldse
{

struct LdseRouteEntry
{
    uint8_t parentId;
    uint8_t layer;
    int8_t rssiDbm;
    uint8_t energyPct;
    uint32_t lastSeenMs;
};

class LdseRouting
{
  public:
    static constexpr uint8_t MAX_PARENTS = 4;

    LdseRouting();

    /** Insert or refresh a parent entry. */
    void UpdateParent(uint8_t parentId, uint8_t layer, int8_t rssiDbm, uint8_t energyPct);

    void UpdateParentEnergy(uint8_t parentId, uint8_t energyPct);

    void RemoveParent(uint8_t parentId);

    /** Remove parents not seen for maxAgeMs. @return number pruned. */
    uint8_t PruneExpired(uint32_t maxAgeMs);

    /**
     * Select the best parent: lowest layer, ties broken by
     * score = alpha * energy + beta * link quality.
     *
     * @return parent id, or LDSE_BROADCAST (0xFF) if the table is empty.
     */
    uint8_t SelectBestParent() const;

    uint8_t GetSize() const;
    bool HasParent() const;

    void SetAlpha(float alpha);
    void SetBeta(float beta);

  private:
    LdseRouteEntry m_entries[MAX_PARENTS];
    uint8_t m_count;
    float m_alpha; //!< energy weight
    float m_beta;  //!< link-quality weight

    float Score(const LdseRouteEntry& e) const;
};

} // namespace ldse
