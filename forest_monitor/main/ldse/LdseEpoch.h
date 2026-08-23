#pragma once

/*
 * LdseEpoch.h - epoch scheduling (paper Section 2.2.4, Phase 7).
 *
 * Mirrors the ns-3 LdseEpochScheduler windowChanged trace.  The timeline is
 * split into SYNC, DATA and SLEEP windows; each role reacts to the current
 * window to decide radio mode, routing and sleeping.
 */

#include "LdseCompat.h"

#include "LdseConfig.h"

namespace ldse
{

enum EpochWindow : uint8_t
{
    WIN_SYNC = 0,
    WIN_DATA = 1,
    WIN_SLEEP = 2
};

class LdseEpoch
{
  public:
    /**
     * Window covering the given absolute time (ms).
     *
     * phaseOffsetMs shifts the epoch boundary away from the local boot time,
     * letting a child align its schedule to a parent's reference clock after
     * receiving an FTSP sync beacon (reference phase = local phase + offset).
     */
    static EpochWindow GetWindow(uint32_t nowMs, int32_t phaseOffsetMs = 0)
    {
        uint32_t phase = (uint32_t)((int64_t)nowMs + phaseOffsetMs) % LDSE_EPOCH_MS;
        if (phase < LDSE_SYNC_MS)
        {
            return WIN_SYNC;
        }
        if (phase < LDSE_SYNC_MS + LDSE_DATA_MS)
        {
            return WIN_DATA;
        }
        return WIN_SLEEP;
    }

    /** Time remaining in the current window (ms). */
    static uint32_t RemainingInWindow(uint32_t nowMs, int32_t phaseOffsetMs = 0)
    {
        uint32_t phase = (uint32_t)((int64_t)nowMs + phaseOffsetMs) % LDSE_EPOCH_MS;
        uint32_t winStart = 0;
        uint32_t winLen = 0;
        switch (GetWindow(nowMs, phaseOffsetMs))
        {
        case WIN_SYNC:
            winStart = 0;
            winLen = LDSE_SYNC_MS;
            break;
        case WIN_DATA:
            winStart = LDSE_SYNC_MS;
            winLen = LDSE_DATA_MS;
            break;
        default:
            winStart = LDSE_SYNC_MS + LDSE_DATA_MS;
            winLen = LDSE_SLEEP_MS;
            break;
        }
        return (winStart + winLen) - phase;
    }
};

} // namespace ldse
