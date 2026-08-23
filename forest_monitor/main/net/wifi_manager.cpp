/*
 * wifi_manager.cpp - gateway-only Wi-Fi station connectivity.
 *
 * Event-driven esp_wifi STA mode. WIFI_EVENT_STA_START kicks off the first
 * connect attempt; WIFI_EVENT_STA_DISCONNECTED schedules a retry via a
 * one-shot esp_timer with exponential backoff capped at 10 s (never a tight
 * retry loop). IP_EVENT_STA_GOT_IP flags "connected" and resets the backoff.
 *
 * esp_wifi_set_ps(WIFI_PS_NONE) is called immediately after esp_wifi_start()
 * per this project's Wi-Fi skill: modem sleep power-save is the main source
 * of jitter risk against the gateway's LDSE SYNC beacon cadence.
 */

#include "net/wifi_manager.h"

#include <string.h>
#include <time.h>

#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_RETRY_BACKOFF_INITIAL_MS 1000
#define WIFI_RETRY_BACKOFF_MAX_MS 10000

static EventGroupHandle_t s_wifi_event_group = nullptr;
static esp_timer_handle_t s_retry_timer = nullptr;
static uint32_t s_retry_backoff_ms = WIFI_RETRY_BACKOFF_INITIAL_MS;

static void RetryTimerCallback(void* arg)
{
    (void)arg;
    ESP_LOGI(TAG, "wifi_mgr: reconnecting (backoff was %lu ms)", (unsigned long)s_retry_backoff_ms);
    esp_wifi_connect();
    // Exponential backoff, capped - a hotspot briefly out of range shouldn't
    // spin the radio.
    s_retry_backoff_ms *= 2;
    if (s_retry_backoff_ms > WIFI_RETRY_BACKOFF_MAX_MS)
    {
        s_retry_backoff_ms = WIFI_RETRY_BACKOFF_MAX_MS;
    }
}

static void ScheduleRetry()
{
    if (s_retry_timer == nullptr)
    {
        esp_timer_create_args_t args = {};
        args.callback = &RetryTimerCallback;
        args.name = "wifi_retry";
        esp_timer_create(&args, &s_retry_timer);
    }
    else
    {
        esp_timer_stop(s_retry_timer); // no-op if not running
    }
    esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_backoff_ms * 1000ULL);
}

static void WifiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "wifi_mgr: disconnected, retry in %lu ms", (unsigned long)s_retry_backoff_ms);
        ScheduleRetry();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* evt = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "wifi_mgr: got IP, connected - " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_backoff_ms = WIFI_RETRY_BACKOFF_INITIAL_MS; // reset backoff on success
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEventHandler, nullptr));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, CONFIG_GATEWAY_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, CONFIG_GATEWAY_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode =
        (strlen(CONFIG_GATEWAY_WIFI_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Must follow esp_wifi_start() immediately - modem sleep is the main
    // source of jitter risk against the LDSE SYNC beacon cadence.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "wifi_mgr: connecting to SSID:%s", CONFIG_GATEWAY_WIFI_SSID);
    // Non-blocking by design: do not wait on WIFI_CONNECTED_BIT here. The
    // caller (ldse_gateway_main) must enter its LDSE loop immediately after
    // this returns, with or without an IP yet.

    // SNTP so firebase_uploader's "timestamp" field is real Unix epoch
    // seconds, not just millis()-since-boot. Also non-blocking - it syncs
    // opportunistically once Wi-Fi is up (see IP_EVENT_STA_GOT_IP above)
    // and firebase_uploader treats an unsynced clock (time() < 2020-01-01)
    // as "no timestamp available yet" rather than a hard error.
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
}

bool wifi_manager_is_connected(void)
{
    if (s_wifi_event_group == nullptr)
    {
        return false;
    }
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}
