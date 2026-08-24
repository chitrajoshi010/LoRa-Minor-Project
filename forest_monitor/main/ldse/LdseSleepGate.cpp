#include "LdseSleepGate.h"

#include "LdseConfig.h"

#include "driver/gpio.h"

namespace ldse
{

void
LdseSleepGate::Begin()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << LDSE_PIN_SLEEP_GATE;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    // Power the peripheral rail on at boot so sensors/mic are ready before
    // the first parent sync arrives.
    m_awake = true;
    gpio_set_level((gpio_num_t)LDSE_PIN_SLEEP_GATE, 1);
}

void
LdseSleepGate::Wake()
{
    if (m_awake)
    {
        return;
    }
    m_awake = true;
    gpio_set_level((gpio_num_t)LDSE_PIN_SLEEP_GATE, 1);
}

void
LdseSleepGate::Sleep()
{
    if (!m_awake)
    {
        return;
    }
    m_awake = false;
    gpio_set_level((gpio_num_t)LDSE_PIN_SLEEP_GATE, 0);
}

bool
LdseSleepGate::IsAwake() const
{
    return m_awake;
}

} // namespace ldse
