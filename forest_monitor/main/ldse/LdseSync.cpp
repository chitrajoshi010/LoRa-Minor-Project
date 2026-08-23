#include "LdseSync.h"

namespace ldse
{

LdseSync::LdseSync()
    : m_driftPpm(LDSE_DRIFT_PPM),
      m_lastAdvanceUs(0),
      m_errorUs(0),
      m_offsetUs(0),
      m_accumulatedErrorUs(0),
      m_hasSynced(false),
      m_hopCount(0)
{
}

void
LdseSync::Begin(float driftPpm)
{
    m_driftPpm = driftPpm;
    m_lastAdvanceUs = micros();
    m_errorUs = 0;
    m_offsetUs = 0;
    m_accumulatedErrorUs = 0;
    m_hasSynced = false;
}

void
LdseSync::AdvanceClock() const
{
    uint32_t nowUs = micros();
    uint32_t dtUs = nowUs - m_lastAdvanceUs;
    if (dtUs > 0)
    {
        m_errorUs += static_cast<int64_t>(dtUs * m_driftPpm * 1e-6);
        m_lastAdvanceUs = nowUs;
    }
}

void
LdseSync::OnReceiveSync(uint32_t txTimestampUs, uint32_t rxLocalUs)
{
    m_hasSynced = true;
    // offset = local reception time - (reference tx time + propagation delay)
    // Propagation delay is negligible at these distances (~us).
    m_offsetUs = static_cast<int64_t>(rxLocalUs) - static_cast<int64_t>(txTimestampUs);
}

void
LdseSync::ApplyCorrection()
{
    AdvanceClock();
    m_accumulatedErrorUs += m_errorUs < 0 ? -m_errorUs : m_errorUs;
    m_errorUs = 0; // realign the local clock to the reference
}

uint32_t
LdseSync::LocalTimeUs() const
{
    AdvanceClock();
    return static_cast<uint32_t>(static_cast<int64_t>(micros()) + m_offsetUs + m_errorUs);
}

int32_t
LdseSync::GetOffsetUs() const
{
    return static_cast<int32_t>(m_offsetUs);
}

double
LdseSync::GetClockErrorSeconds() const
{
    AdvanceClock();
    return static_cast<double>(m_errorUs) / 1e6;
}

uint8_t
LdseSync::GetHopCount() const
{
    return m_hopCount;
}

void
LdseSync::SetHopCount(uint8_t hops)
{
    m_hopCount = hops;
}

} // namespace ldse
