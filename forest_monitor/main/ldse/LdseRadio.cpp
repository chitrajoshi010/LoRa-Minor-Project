#include "LdseRadio.h"

#include "esp_log.h"

namespace ldse
{

static const char* TAG = "LdseRadio";

LdseRadio::LdseRadio()
    : m_radio(nullptr),
      m_mod(nullptr),
      m_hal(nullptr),
      m_lastRssi(-127.0)
{
}

LdseRadio::~LdseRadio()
{
    delete m_radio;
    delete m_mod;
    delete m_hal;
}

bool
LdseRadio::Begin(float freqMhz, uint8_t sf)
{
    // RadioLib's ESP-IDF HAL owns the SPI bus (spi_bus_initialize on
    // SPI2_HOST) and the GPIO setup. Create it with the wired SCK/MISO/MOSI
    // pins so the radio keeps them on every board.
    m_hal = new EspHal(LDSE_PIN_SCK, LDSE_PIN_MISO, LDSE_PIN_MOSI);

    // RadioLib Module(hal, cs, irq, rst, gpio). gpio (DIO1) is used for CAD.
    // The Module must outlive the SX1278 (it keeps the pointer), so it is
    // heap-allocated instead of a stack temporary.
    m_mod = new Module(m_hal, LDSE_PIN_NSS, LDSE_PIN_DIO0, LDSE_PIN_RST, LDSE_PIN_DIO1);
    m_radio = new SX1278(m_mod);

    int state = m_radio->begin(freqMhz,
                               LDSE_BW_KHZ,
                               sf,
                               LDSE_CODING_RATE,
                               LDSE_SYNC_WORD,
                               LDSE_TX_POWER_DBM,
                               LDSE_PREAMBLE_SYMBOLS,
                               0); // gain: 0 = auto (recommended)
    if (state != RADIOLIB_ERR_NONE)
    {
        ESP_LOGE(TAG, "LdseRadio begin failed: %d", state);
        return false;
    }
    m_radio->setCRC(true);
    m_radio->startReceive();
    return true;
}

void
LdseRadio::SetChannel(float freqMhz, uint8_t sf)
{
    m_radio->setFrequency(freqMhz);
    m_radio->setSpreadingFactor(sf);
    m_radio->startReceive();
}

bool
LdseRadio::Send(const LdsePacket& pkt)
{
    uint8_t buf[LDSE_FRAME_MAX];
    uint8_t len = pkt.Encode(buf);
    int state = m_radio->transmit(buf, len);
    if (state != RADIOLIB_ERR_NONE)
    {
        return false;
    }
    m_radio->startReceive();
    return true;
}

bool
LdseRadio::Receive(LdsePacket& pkt, uint32_t timeoutMs)
{
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline)
    {
        // PhysicalLayer::available() only reports data after a DIO0 interrupt
        // fills its internal buffer, and we never configure one. Poll the
        // SX127x IRQ flags register directly instead (the same register
        // readData() checks for the CRC flag), so RX works without any DIO
        // wiring.
        int16_t irq = m_mod->SPIgetRegValue(RADIOLIB_SX127X_REG_IRQ_FLAGS);
        if (irq >= 0 && (irq & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE))
        {
            uint8_t buf[LDSE_FRAME_MAX];
            int state = m_radio->readData(buf, LDSE_FRAME_MAX);
            if (state == RADIOLIB_ERR_NONE)
            {
                m_lastRssi = m_radio->getRSSI();
                pkt.rssiDbm = static_cast<int8_t>(m_lastRssi);
                // readData succeeds; actual length comes from getPacketLength().
                uint8_t len = static_cast<uint8_t>(m_radio->getPacketLength());
                if (pkt.Decode(buf, len))
                {
                    return true;
                }
            }
            m_radio->startReceive();
        }
        else
        {
            // Pure SPI-register busy-poll with no FreeRTOS yield starves the
            // idle task (which resets the task watchdog) when callers loop
            // this for hundreds of ms to seconds (e.g. the relay's WIN_DATA
            // listen phase). One tick is well under any LoRa symbol/airtime
            // margin in this protocol, so it doesn't affect RX latency in
            // practice.
            vTaskDelay(1);
        }
    }
    return false;
}

bool
LdseRadio::ChannelFree(uint32_t cadTimeoutMs)
{
    (void)cadTimeoutMs; // scanChannel() is blocking with its own CAD timeout
    int state = m_radio->scanChannel();
    if (state == RADIOLIB_CHANNEL_FREE)
    {
        return true;
    }
    return false;
}

void
LdseRadio::Sleep()
{
    m_radio->sleep();
}

void
LdseRadio::Standby()
{
    m_radio->standby();
}

float
LdseRadio::GetLastRssi() const
{
    return m_lastRssi;
}

} // namespace ldse
