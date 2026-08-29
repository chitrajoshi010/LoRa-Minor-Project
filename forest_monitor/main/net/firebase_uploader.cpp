/*
 * firebase_uploader.cpp - gateway-only Firebase RTDB upload.
 *
 * Auth: Firebase Identity Toolkit email/password sign-in (same account and
 * flow as serial_to_firebase.py / the legacy ESP32 Arduino sketch), cached
 * idToken refreshed a little before its ~1h expiry. Transport: esp_http_client
 * over TLS (ESP-IDF's built-in certificate bundle - no pinned/custom cert
 * needed for *.googleapis.com / *.firebaseio.com).
 *
 * JSON is hand-built with snprintf for requests (the record shape is small
 * and fixed) and hand-parsed with strstr for the two auth-response fields we
 * need (idToken, expiresIn) - avoids pulling a full JSON parser into a
 * network path that only ever talks to two known Google endpoints.
 *
 * Everything here runs on its own FreeRTOS task (firebase_uploader_init())
 * fed by a small bounded queue. gateway.cpp's LDSE receive loop only ever
 * calls firebase_uploader_enqueue(), which is non-blocking and drops the
 * item if the queue is full - the LDSE SYNC/DATA timing must never wait on
 * an HTTP round trip.
 */

#include "net/firebase_uploader.h"

#include "sdkconfig.h"

#if CONFIG_FIREBASE_UPLOAD_ENABLE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "ldse/LdseConfig.h"
#include "net/wifi_manager.h"

using namespace ldse;

static const char* TAG = "fb_upload";

// ---------------------------------------------------------------------
// Queue item - a flat copy of everything LogPacket() already decodes, so
// the uploader never touches the LdsePacket/NodePayload memory after
// enqueue() returns.
// ---------------------------------------------------------------------
struct FirebaseUploadItem
{
    char nodeKey[24];
    bool isFire;
    uint8_t classIdx;
    float confidence[LDSE_ACOUSTIC_CLASSES];
    float temperature;
    float humidity;
    float gas;
    float fireScore;
    int16_t rssiDbm;
    uint8_t hopCount;
    uint8_t layer;
    uint16_t seq;
    uint32_t seqCounter;
    uint32_t timestampSec;
};

static const char* kClassNames[LDSE_ACOUSTIC_CLASSES] = {"axe", "chainsaw", "gunshot", "handsaw", "background"};

static QueueHandle_t s_queue = nullptr;
static TaskHandle_t s_task = nullptr;

// ---------------------------------------------------------------------
// Auth token cache (single consumer: the uploader task, so no locking).
// ---------------------------------------------------------------------
static char s_idToken[1024] = {0};
static int64_t s_tokenExpiryUs = 0;

// Response accumulator for esp_http_client's event callback.
static char s_respBuf[2048];
static int s_respLen = 0;

static esp_err_t HttpEventHandler(esp_http_client_event_t* evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA)
    {
        int room = (int)sizeof(s_respBuf) - s_respLen - 1;
        if (room > 0)
        {
            int n = evt->data_len < room ? evt->data_len : room;
            memcpy(s_respBuf + s_respLen, evt->data, n);
            s_respLen += n;
            s_respBuf[s_respLen] = '\0';
        }
    }
    return ESP_OK;
}

// Extracts the string value of `"key":"..."` (Identity Toolkit always
// quotes idToken; expiresIn may or may not be quoted, both handled).
static bool ExtractJsonField(const char* json, const char* key, char* out, size_t outSize)
{
    char needle[40];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p)
    {
        return false;
    }
    p = strchr(p + strlen(needle), ':');
    if (!p)
    {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '"')
    {
        p++;
    }
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && i + 1 < outSize)
    {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

// Blocking HTTP helper shared by auth + push (called only from the
// uploader task, never from the LDSE loop).
static esp_err_t DoHttpRequest(const char* url, esp_http_client_method_t method, const char* body, int* outStatus)
{
    s_respLen = 0;
    s_respBuf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = method;
    cfg.event_handler = HttpEventHandler;
    cfg.timeout_ms = 8000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    // The Firebase idToken (~950 chars) rides in the URL's ?auth= query
    // param, so the request line alone can exceed esp_http_client's default
    // ~512-byte TX buffer ("Out of buffer" error). Size both buffers to
    // comfortably fit the longest URL we build (see url[] in callers).
    cfg.buffer_size = sizeof(s_idToken) + 256;
    cfg.buffer_size_tx = sizeof(s_idToken) + 256;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (body)
    {
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && outStatus)
    {
        *outStatus = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return err;
}

// Refresh (or reuse) the Firebase idToken. Returns nullptr on failure.
static const char* GetIdToken(bool forceRefresh)
{
    int64_t nowUs = esp_timer_get_time();
    if (!forceRefresh && s_idToken[0] != '\0' && nowUs < s_tokenExpiryUs)
    {
        return s_idToken;
    }

    char url[192];
    snprintf(url, sizeof(url),
             "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=%s",
             CONFIG_FIREBASE_API_KEY);

    char body[256];
    snprintf(body, sizeof(body), "{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}",
             CONFIG_FIREBASE_AUTH_EMAIL, CONFIG_FIREBASE_AUTH_PASSWORD);

    int status = 0;
    esp_err_t err = DoHttpRequest(url, HTTP_METHOD_POST, body, &status);
    if (err != ESP_OK || status != 200)
    {
        ESP_LOGW(TAG, "auth failed (err=%d status=%d): %.120s", err, status, s_respBuf);
        s_idToken[0] = '\0';
        return nullptr;
    }

    char expiresStr[16] = "3600";
    ExtractJsonField(s_respBuf, "idToken", s_idToken, sizeof(s_idToken));
    ExtractJsonField(s_respBuf, "expiresIn", expiresStr, sizeof(expiresStr));
    if (s_idToken[0] == '\0')
    {
        ESP_LOGW(TAG, "auth OK but no idToken in response");
        return nullptr;
    }

    long expiresInSec = atol(expiresStr);
    if (expiresInSec <= 0)
    {
        expiresInSec = 3600;
    }
    // Refresh 60 s early so an in-flight upload never races an expiring token.
    s_tokenExpiryUs = nowUs + ((int64_t)expiresInSec - 60) * 1000000LL;
    ESP_LOGI(TAG, "authenticated (token valid ~%ld s)", expiresInSec);
    return s_idToken;
}

// One-shot per node key: writes /nodes/{key}/meta so the dashboard has a
// label/role/layer even before the first history entry lands.
static bool s_metaWritten[2] = {false, false}; // [0]=node key, [1]=relay key

static void PushMetaOnce(const char* nodeKey, const char* role, int layer, int slot, const char* token)
{
    if (s_metaWritten[slot])
    {
        return;
    }
    // sizeof(s_idToken) covers the idToken itself (Identity Toolkit tokens
    // run ~900-1000 chars); the rest is headroom for the DB URL + path.
    char url[sizeof(s_idToken) + 192];
    int urlLen = snprintf(url, sizeof(url), "%s/nodes/%s/meta.json?auth=%s", CONFIG_FIREBASE_DB_URL, nodeKey, token);
    if (urlLen < 0 || urlLen >= (int)sizeof(url))
    {
        ESP_LOGW(TAG, "meta URL truncated, skipping upload");
        return;
    }
    char body[128];
    snprintf(body, sizeof(body), "{\"label\":\"%s\",\"role\":\"%s\",\"layer\":%d}", nodeKey, role, layer);
    int status = 0;
    if (DoHttpRequest(url, HTTP_METHOD_PUT, body, &status) == ESP_OK && (status == 200 || status == 204))
    {
        s_metaWritten[slot] = true;
    }
}

static void PushItem(const FirebaseUploadItem& item)
{
    const char* token = GetIdToken(false);
    if (!token)
    {
        return;
    }

    bool isNode = (strcmp(item.nodeKey, CONFIG_FIREBASE_NODE_KEY_NODE) == 0);
    PushMetaOnce(item.nodeKey, isNode ? "node" : "relay", isNode ? 2 : 1, isNode ? 0 : 1, token);

    // Record shape matches CLAUDE.md's mesh schema exactly.
    char body[512];
    int n = snprintf(body, sizeof(body),
                      "{\"axe\":%.4f,\"chainsaw\":%.4f,\"gunshot\":%.4f,\"handsaw\":%.4f,\"background\":%.4f,"
                      "\"prediction\":\"%s\",\"confidence\":%.4f,"
                      "\"temperature\":%.2f,\"humidity\":%.2f,\"gas\":%.1f,\"fireScore\":%.3f,"
                      "\"rssi\":%d,\"hops\":%u,\"layer\":%u,\"src\":\"%s\","
                      "\"seq\":%u,\"timestamp\":%lu,\"node\":\"%s\"}",
                      item.confidence[0], item.confidence[1], item.confidence[2], item.confidence[3],
                      item.confidence[4],
                      (item.classIdx < LDSE_ACOUSTIC_CLASSES) ? kClassNames[item.classIdx] : "background",
                      item.confidence[(item.classIdx < LDSE_ACOUSTIC_CLASSES) ? item.classIdx : 4], item.temperature,
                      item.humidity, item.gas, item.fireScore, item.rssiDbm, item.hopCount, item.layer, item.nodeKey,
                      item.seq, (unsigned long)item.timestampSec, item.nodeKey);
    if (n <= 0 || n >= (int)sizeof(body))
    {
        ESP_LOGW(TAG, "record JSON truncated, skipping upload");
        return;
    }

    // sizeof(s_idToken) covers the idToken itself (Identity Toolkit tokens
    // run ~900-1000 chars); the rest is headroom for the DB URL + path. A
    // smaller fixed buffer here silently truncated the auth token, which
    // Firebase then rejected as "Permission denied" even with correct rules.
    char url[sizeof(s_idToken) + 192];
    int status = 0;

    // /nodes/{key}/latest - overwritten every cycle.
    int urlLen = snprintf(url, sizeof(url), "%s/nodes/%s/latest.json?auth=%s", CONFIG_FIREBASE_DB_URL, item.nodeKey,
                           token);
    if (urlLen < 0 || urlLen >= (int)sizeof(url))
    {
        ESP_LOGW(TAG, "latest URL truncated, skipping upload");
        return;
    }
    esp_err_t err = DoHttpRequest(url, HTTP_METHOD_PUT, body, &status);
    if (err == ESP_OK && status == 401)
    {
        // Token expired/revoked server-side ahead of our cached expiry - force
        // one refresh and retry this single request, then give up.
        token = GetIdToken(true);
        if (token)
        {
            urlLen = snprintf(url, sizeof(url), "%s/nodes/%s/latest.json?auth=%s", CONFIG_FIREBASE_DB_URL,
                               item.nodeKey, token);
            if (urlLen < 0 || urlLen >= (int)sizeof(url))
            {
                ESP_LOGW(TAG, "latest URL truncated on retry, skipping upload");
                return;
            }
            err = DoHttpRequest(url, HTTP_METHOD_PUT, body, &status);
        }
    }
    if (err != ESP_OK || (status != 200 && status != 204))
    {
        ESP_LOGW(TAG, "latest push failed (err=%d status=%d): %.120s", err, status, s_respBuf);
    }

    // /nodes/{key}/history - append-only.
    urlLen = snprintf(url, sizeof(url), "%s/nodes/%s/history.json?auth=%s", CONFIG_FIREBASE_DB_URL, item.nodeKey,
                       token);
    if (urlLen < 0 || urlLen >= (int)sizeof(url))
    {
        ESP_LOGW(TAG, "history URL truncated, skipping upload");
        return;
    }
    err = DoHttpRequest(url, HTTP_METHOD_POST, body, &status);
    if (err != ESP_OK || status != 200)
    {
        ESP_LOGW(TAG, "history push failed (err=%d status=%d): %.120s", err, status, s_respBuf);
    }
}

static void FirebaseUploadTask(void* arg)
{
    (void)arg;
    FirebaseUploadItem item;
    for (;;)
    {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        if (!wifi_manager_is_connected())
        {
            ESP_LOGD(TAG, "Wi-Fi down, dropping queued upload (seq=%u)", item.seq);
            continue;
        }
        PushItem(item);
    }
}

void firebase_uploader_init(void)
{
    if (s_queue)
    {
        return; // already initialized
    }
    s_queue = xQueueCreate(4, sizeof(FirebaseUploadItem));
    // Priority below the LDSE/radio path; stack sized for TLS handshake
    // buffers (esp_http_client + mbedtls typically need ~6-8 KB here).
    xTaskCreate(FirebaseUploadTask, "fb_upload", 8192, nullptr, tskIDLE_PRIORITY + 2, &s_task);
    ESP_LOGI(TAG, "Firebase uploader ready (db=%s)", CONFIG_FIREBASE_DB_URL);
}

void firebase_uploader_enqueue(const ldse::LdsePacket& pkt, uint32_t nowEpochSec, uint32_t seqCounter)
{
    if (!s_queue)
    {
        return;
    }
    if (pkt.payloadLen != sizeof(NodePayload))
    {
        return; // not a decodable NodePayload (e.g. malformed / wrong size)
    }

    const char* nodeKey = nullptr;
    if (pkt.originId == LDSE_NODE_ID)
    {
        nodeKey = CONFIG_FIREBASE_NODE_KEY_NODE;
    }
    else if (pkt.originId == LDSE_RELAY_ID)
    {
        nodeKey = CONFIG_FIREBASE_NODE_KEY_RELAY;
    }
    else
    {
        return; // gateway never originates DATA/FIRE
    }

    NodePayload np;
    memcpy(&np, pkt.payload, sizeof(NodePayload));

    FirebaseUploadItem item = {};
    strncpy(item.nodeKey, nodeKey, sizeof(item.nodeKey) - 1);
    item.isFire = (pkt.type == MSG_FIRE_ALERT);
    item.classIdx = np.classIdx;
    memcpy(item.confidence, np.confidence, sizeof(item.confidence));
    item.temperature = np.temperature;
    item.humidity = np.humidity;
    item.gas = np.gas;
    item.fireScore = np.fireScore;
    item.rssiDbm = pkt.rssiDbm;
    item.hopCount = pkt.hopCount;
    item.layer = pkt.layer;
    item.seq = pkt.seq;
    item.seqCounter = seqCounter;
    item.timestampSec = nowEpochSec;

    // Non-blocking: if the queue is full (Wi-Fi has been down a while),
    // drop the oldest item to make room rather than ever blocking the
    // LDSE receive loop that calls us.
    if (xQueueSend(s_queue, &item, 0) != pdTRUE)
    {
        FirebaseUploadItem discard;
        xQueueReceive(s_queue, &discard, 0);
        xQueueSend(s_queue, &item, 0);
    }
}

#else // !CONFIG_FIREBASE_UPLOAD_ENABLE

void firebase_uploader_init(void) {}
void firebase_uploader_enqueue(const ldse::LdsePacket&, uint32_t, uint32_t) {}

#endif
