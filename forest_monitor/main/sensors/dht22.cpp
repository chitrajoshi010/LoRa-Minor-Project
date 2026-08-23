/*
 * dht22.cpp - DHT22 wrapper over the chmorgan/esp-dht component.
 *
 * The component exposes a small global-state C API: setDHTgpio(), readDHT()
 * (returns DHT_OK==0 on success), getTemperature(), getHumidity().
 */

#include "dht22.h"

#include "esp_log.h"
#include "dht.h"

static const char* TAG = "dht22";

void dht22_init(int gpio)
{
    setDHTgpio(gpio);
    ESP_LOGI(TAG, "DHT22 initialized on GPIO%d", gpio);
}

bool dht22_read(float* out_temp_c, float* out_humidity)
{
    int ret = readDHT();
    if (ret != DHT_OK)
    {
        return false;
    }
    if (out_temp_c)
    {
        *out_temp_c = getTemperature();
    }
    if (out_humidity)
    {
        *out_humidity = getHumidity();
    }
    return true;
}
