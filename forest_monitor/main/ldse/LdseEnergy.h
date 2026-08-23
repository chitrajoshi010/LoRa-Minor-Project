#pragma once

/*
 * LdseEnergy.h - energy accounting and sleep-relay control (Phase 7).
 *
 * Mirrors the ns-3 LdseEnergyController power model:
 *   Ptotal = nActive*Pactive + nSleeping*Psleep + nSwitches*Pswitch
 *
 * On the ESP32 this is a software model: the relay/node track their awake and
 * sleeping time and compute the cumulative energy drawn, so the energy benefit
 * of sleeping in the SLEEP window can be read over the serial monitor.
 */

#include "LdseCompat.h"

#include "LdseConfig.h"

namespace ldse
{

class LdseEnergy
{
  public:
    LdseEnergy();

    void Begin(float activePowerW = LDSE_ACTIVE_POWER_W,
               float sleepPowerW = LDSE_SLEEP_POWER_W);

    /** Mark the device as entering a low-power (sleep) window. */
    void EnterSleep();

    /** Mark the device as active. */
    void Wake();

    bool IsSleeping() const;

    /** Energy drawn so far, in joules. */
    float GetEnergyConsumedJ() const;

    /** Remaining battery capacity in mAh, based on the paper battery. */
    float GetBatteryMouth() const;

    float GetActivePowerW() const;
    float GetSleepPowerW() const;

  private:
    void Accrue() const; //!< add the elapsed-time energy to the total

    float m_activePowerW;
    float m_sleepPowerW;
    bool m_sleeping;
    mutable uint32_t m_lastSwitchMs;
    mutable float m_energyJ;
};

} // namespace ldse
