#pragma once

/*
 * EspHal.h - portable ESP-IDF RadioLibHal implementation for the SX1278.
 *
 * Vendored here because "EspHal" (referenced throughout ARCHITECTURE.md /
 * AGENTS.md as "RadioLib's built-in ESP-IDF EspHal") does not actually ship
 * inside the jgromes/radiolib managed component. It only exists as example
 * glue code under
 * managed_components/jgromes__radiolib/examples/NonArduino/ESP-IDF/, and
 * that example pokes ESP32 SPI/DPORT registers directly and has a hard
 * `#error` on any non-ESP32 target - it would not compile for the ESP32-S3
 * relay/node roles this project needs.
 *
 * This is a from-scratch, portable implementation using the standard
 * ESP-IDF `driver/spi_master.h` API on SPI2_HOST (as documented), which
 * works identically on ESP32 and ESP32-S3.
 *
 * LdseRadio never wires DIO0/DIO1 to a real GPIO interrupt - it polls the
 * SX127x IRQ flags register directly (see LdseRadio::Receive) - but
 * attachInterrupt/detachInterrupt are still implemented properly (not
 * stubbed) in case a future change needs them.
 *
 * RadioLib's Module class toggles the NSS/CS pin itself via digitalWrite()
 * around spiBeginTransaction/spiTransfer/spiEndTransaction (see
 * Module.cpp), so the SPI device here is configured with `spics_io_num =
 * -1` (CS not managed by the driver).
 */

#include <RadioLib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

class EspHal : public RadioLibHal
{
  public:
    EspHal(int8_t sck, int8_t miso, int8_t mosi);

    void init() override;
    void term() override;

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

  private:
    int8_t m_sck;
    int8_t m_miso;
    int8_t m_mosi;
    spi_device_handle_t m_spiHandle;
};
