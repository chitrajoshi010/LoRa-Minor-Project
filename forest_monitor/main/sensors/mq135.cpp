/*
 * mq135.cpp - MQ-135 analog read via esp_adc oneshot (ADC1, GPIO2).
 *
 * GPIO2 maps to ADC1_CHANNEL_1 on the ESP32-S3 (only compiled for the
 * relay/node S3 roles). Raw counts are converted to millivolts with the ADC
 * calibration API when available.
 */

#include "mq135.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char* TAG = "mq135";

#define MQ135_ADC_UNIT    ADC_UNIT_1
#define MQ135_ADC_CHANNEL ADC_CHANNEL_1 // GPIO2 on ESP32-S3
#define MQ135_ADC_ATTEN   ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t s_adc_handle = nullptr;
static adc_cali_handle_t s_cali_handle = nullptr;
static bool s_calibrated = false;

esp_err_t mq135_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = MQ135_ADC_UNIT;
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = MQ135_ADC_ATTEN;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    ret = adc_oneshot_config_channel(s_adc_handle, MQ135_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Curve-fitting calibration (ESP32-S3). If unavailable, fall back to a
    // linear approximation in mq135_read_mv().
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = MQ135_ADC_UNIT;
    cali_cfg.chan = MQ135_ADC_CHANNEL;
    cali_cfg.atten = MQ135_ADC_ATTEN;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK)
    {
        s_calibrated = true;
    }
    else
    {
        ESP_LOGW(TAG, "ADC calibration unavailable; using raw->mV approximation");
    }

    ESP_LOGI(TAG, "MQ-135 initialized on ADC1 CH1 (GPIO2)");
    return ESP_OK;
}

float mq135_read_mv(void)
{
    if (s_adc_handle == nullptr)
    {
        return 0.0f;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, MQ135_ADC_CHANNEL, &raw) != ESP_OK)
    {
        return 0.0f;
    }
    if (s_calibrated)
    {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK)
        {
            return (float)mv;
        }
    }
    // 12-bit range, ~3.3 V full scale at 12 dB attenuation.
    return (float)raw * (3300.0f / 4095.0f);
}
