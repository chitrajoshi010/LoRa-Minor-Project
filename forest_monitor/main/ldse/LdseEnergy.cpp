#include "LdseEnergy.h"

namespace ldse
{

LdseEnergy::LdseEnergy()
    : m_activePowerW(LDSE_ACTIVE_POWER_W),
      m_sleepPowerW(LDSE_SLEEP_POWER_W),
      m_sleeping(false),
      m_lastSwitchMs(0),
      m_energyJ(0.0f)
{
}

void
LdseEnergy::Begin(float activePowerW, float sleepPowerW)
{
    m_activePowerW = activePowerW;
    m_sleepPowerW = sleepPowerW;
    m_sleeping = false;
    m_lastSwitchMs = millis();
    m_energyJ = 0.0f;
}

void
LdseEnergy::Accrue() const
{
    uint32_t nowMs = millis();
    uint32_t dtMs = nowMs - m_lastSwitchMs;
    if (dtMs == 0)
    {
        return;
    }
    float powerW = m_sleeping ? m_sleepPowerW : m_activePowerW;
    m_energyJ += powerW * (static_cast<float>(dtMs) / 1000.0f);
    m_lastSwitchMs = nowMs;
}

void
LdseEnergy::EnterSleep()
{
    if (m_sleeping)
    {
        return;
    }
    Accrue();
    m_sleeping = true;
}

void
LdseEnergy::Wake()
{
    if (!m_sleeping)
    {
        return;
    }
    Accrue();
    m_sleeping = false;
}

bool
LdseEnergy::IsSleeping() const
{
    return m_sleeping;
}

float
LdseEnergy::GetEnergyConsumedJ() const
{
    Accrue();
    return m_energyJ;
}

float
LdseEnergy::GetBatteryMouth() const
{
    // Full battery energy = mAh * V.
    float fullJ = LDSE_BATTERY_CAPACITY_MAH * LDSE_BATTERY_VOLTAGE_V * 3.6f;
    float consumed = GetEnergyConsumedJ();
    float remainingJ = (fullJ - consumed) > 0.0f ? (fullJ - consumed) : 0.0f;
    return remainingJ / (LDSE_BATTERY_VOLTAGE_V * 3.6f); // mAh
}

float
LdseEnergy::GetActivePowerW() const
{
    return m_activePowerW;
}

float
LdseEnergy::GetSleepPowerW() const
{
    return m_sleepPowerW;
}

} // namespace ldse
