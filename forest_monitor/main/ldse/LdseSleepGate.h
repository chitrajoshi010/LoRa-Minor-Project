#pragma once

/*
 * LdseSleepGate.h - MOSFET load-switch gate for the SLEEP window (relay/node).
 *
 * The relay and node each have a MOSFET wired between the battery and their
 * peripheral rail (MQ-135, DHT22, INMP441 mic, etc.); its gate/base is driven
 * by CONFIG_LDSE_PIN_SLEEP_GATE (digital pin 10 by default). Driving that pin
 * LOW turns the MOSFET off and cuts power to the peripherals for the SLEEP
 * window; driving it HIGH re-powers them for SYNC/DATA. The window boundary
 * is the epoch schedule the child received from its parent (gateway/relay)
 * via FTSP sync (see LdseEpoch::GetWindow with the synced phaseOffsetMs), so
 * the gate always toggles on the parent-driven schedule rather than a local
 * clock.
 */

#include "LdseCompat.h"

namespace ldse
{

class LdseSleepGate
{
  public:
    /** Configure the gate GPIO as output and leave the rail powered (HIGH). */
    void Begin();

    /** Drive the gate HIGH (awake / peripherals powered). */
    void Wake();

    /** Drive the gate LOW (asleep / peripherals cut off), per the parent's schedule. */
    void Sleep();

    bool IsAwake() const;

  private:
    bool m_awake = true;
};

} // namespace ldse
