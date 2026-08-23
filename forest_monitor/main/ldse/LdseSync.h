#pragma once

/*
 * LdseSync.h - FTSP-style clock synchronization (paper Section 2.2.4).
 *
 * Mirrors the ns-3 LdseSyncEngine:
 *   - drift accumulation in ppm (default 45 ppm from the paper)
 *   - OnReceiveSync computes the offset from the reference timestamp
 *   - ApplyCorrection resets the local clock error toward the reference
 *   - local time is available to build forward timestamps for the relay
 */

#include "LdseCompat.h"

#include "LdseConfig.h"

namespace ldse
{

class LdseSync
{
  public:
    LdseSync();

    /** Reset the clock model; start drift accumulation from now. */
    void Begin(float driftPpm = LDSE_DRIFT_PPM);

    /**
     * Receive a sync beacon from the reference (gateway or parent relay).
     *
     * @param txTimestampUs The timestamp embedded by the sender (us).
     * @param rxLocalUs The local micros() at reception.
     */
    void OnReceiveSync(uint32_t txTimestampUs, uint32_t rxLocalUs);

    /** Correct the local clock toward the reference after a sync. */
    void ApplyCorrection();

    /** Current corrected local time in microseconds. */
    uint32_t LocalTimeUs() const;

    /** Latest measured offset in us (positive = local ahead of reference). */
    int32_t GetOffsetUs() const;

    /** Current accumulated clock error in seconds (before correction). */
    double GetClockErrorSeconds() const;

    uint8_t GetHopCount() const;
    void SetHopCount(uint8_t hops);

  private:
    void AdvanceClock() const;

    float m_driftPpm;       //!< local crystal drift [ppm]
    mutable uint32_t m_lastAdvanceUs; //!< last advance time [us]
    mutable int64_t m_errorUs;        //!< accumulated drift error [us]
    int64_t m_offsetUs;               //!< latest FTSP offset [us]
    int64_t m_accumulatedErrorUs;     //!< historical correction magnitude [us]
    bool m_hasSynced;                 //!< true after first beacon
    uint8_t m_hopCount;               //!< hops from the reference
};

} // namespace ldse
