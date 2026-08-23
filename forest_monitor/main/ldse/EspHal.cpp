/*
 * EspHal.cpp - portable ESP-IDF RadioLibHal implementation (see EspHal.h).
 */

#include "EspHal.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "EspHal";

EspHal::EspHal(int8_t sck, int8_t miso, int8_t mosi)
    : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
      m_sck(sck),
      m_miso(miso),
      m_mosi(mosi),
      m_spiHandle(nullptr)
{
}

void EspHal::init()
{
    spiBegin();
}

void EspHal::term()
{
    spiEnd();
}

void EspHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (pin == RADIOLIB_NC)
    {
        return;
    }
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = (gpio_mode_t)mode;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void EspHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (pin == RADIOLIB_NC)
    {
        return;
    }
    gpio_set_level((gpio_num_t)pin, value);
}

uint32_t EspHal::digitalRead(uint32_t pin)
{
    if (pin == RADIOLIB_NC)
    {
        return 0;
    }
    return gpio_get_level((gpio_num_t)pin);
}

void EspHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode)
{
    if (interruptNum == RADIOLIB_NC)
    {
        return;
    }
    static bool isrServiceInstalled = false;
    if (!isrServiceInstalled)
    {
        gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
        isrServiceInstalled = true;
    }
    gpio_set_intr_type((gpio_num_t)interruptNum, (gpio_int_type_t)mode);
    // interruptCb is a plain void(void) ISR (RadioLib convention); the ESP-IDF
    // GPIO ISR API expects void(*)(void*), which is binary-compatible here
    // since the callback takes no meaningful argument.
    gpio_isr_handler_add((gpio_num_t)interruptNum, (void (*)(void*))interruptCb, nullptr);
}

void EspHal::detachInterrupt(uint32_t interruptNum)
{
    if (interruptNum == RADIOLIB_NC)
    {
        return;
    }
    gpio_isr_handler_remove((gpio_num_t)interruptNum);
    gpio_set_intr_type((gpio_num_t)interruptNum, GPIO_INTR_DISABLE);
}

void EspHal::delay(RadioLibTime_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void EspHal::delayMicroseconds(RadioLibTime_t us)
{
    esp_rom_delay_us((uint32_t)us);
}

RadioLibTime_t EspHal::millis()
{
    return (RadioLibTime_t)(esp_timer_get_time() / 1000ULL);
}

RadioLibTime_t EspHal::micros()
{
    return (RadioLibTime_t)esp_timer_get_time();
}

long EspHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout)
{
    if (pin == RADIOLIB_NC)
    {
        return 0;
    }
    pinMode(pin, GPIO_MODE_INPUT);
    RadioLibTime_t start = micros();
    RadioLibTime_t curTick = micros();
    while (digitalRead(pin) == state)
    {
        if ((micros() - curTick) > timeout)
        {
            return 0;
        }
    }
    return (long)(micros() - start);
}

void EspHal::spiBegin()
{
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = m_sck;
    buscfg.mosi_io_num = m_mosi;
    buscfg.miso_io_num = m_miso;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 0; // default

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) // already-initialized is fine
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", err);
        return;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 2 * 1000 * 1000; // 2 MHz, matches SX127x SPI limits
    devcfg.mode = 0;                          // SPI mode 0 (CPOL=0, CPHA=0)
    devcfg.spics_io_num = -1;                 // RadioLib toggles NSS itself
    devcfg.queue_size = 1;

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &m_spiHandle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", err);
    }
}

void EspHal::spiBeginTransaction()
{
    // Nothing to do: clock/mode/bit-order are fixed at spi_bus_add_device()
    // time and NSS is toggled by RadioLib's Module around this call.
}

void EspHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in)
{
    if (m_spiHandle == nullptr || len == 0)
    {
        return;
    }
    spi_transaction_t t = {};
    t.length = len * 8; // bits
    t.tx_buffer = out;
    t.rx_buffer = in;
    spi_device_polling_transmit(m_spiHandle, &t);
}

void EspHal::spiEndTransaction()
{
    // Nothing to do - see spiBeginTransaction().
}

void EspHal::spiEnd()
{
    if (m_spiHandle != nullptr)
    {
        spi_bus_remove_device(m_spiHandle);
        m_spiHandle = nullptr;
    }
    spi_bus_free(SPI2_HOST);
}
