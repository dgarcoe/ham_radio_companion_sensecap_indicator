#include "app_pota.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "cJSON.h"

static const char *TAG = "pota";
static const char *URL = "https://api.pota.app/spot/activator";

#define BUF_SIZE             (96 * 1024)   /* api.pota.app feed has grown past 32 KB; buffer lives in PSRAM */
#define REFRESH_INTERVAL_MS  (60 * 1000)
#define RETRY_INTERVAL_MS    (30 * 1000)

static app_pota_state_t  s_state;
static app_pota_state_t  s_snap;            /* given to UI callback, .bss only */
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_poke;
static app_pota_cb_t     s_cb;

typedef struct {
    char  *buf;
    size_t cap;
    size_t total;
} fetch_ctx_t;

/* --- JSON helpers --- */

static void copy_str(cJSON *obj, const char *key, char *out, size_t out_sz)
{
    out[0] = '\0';
    cJSON *f = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(f) && f->valuestring) {
        strncpy(out, f->valuestring, out_sz - 1);
        out[out_sz - 1] = '\0';
    } else if (cJSON_IsNumber(f)) {
        snprintf(out, out_sz, "%g", f->valuedouble);
    }
}

/* "2024-01-15T14:30:00" -> "1430Z". */
static void extract_hhmm(const char *iso, char *out, size_t out_sz)
{
    out[0] = '\0';
    if (!iso) return;
    const char *t = strchr(iso, 'T');
    if (!t || strlen(t) < 6 || out_sz < 6) return;
    t++;                 /* skip 'T' */
    out[0] = t[0];
    out[1] = t[1];       /* hour */
    out[2] = t[3];
    out[3] = t[4];       /* minute (skip colon) */
    out[4] = 'Z';
    out[5] = '\0';
}

static void parse_and_store(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "JSON not an array");
        cJSON_Delete(root);
        return;
    }

    int n = cJSON_GetArraySize(root);
    if (n > APP_POTA_MAX_SPOTS) n = APP_POTA_MAX_SPOTS;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.count = 0;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item)) continue;

        app_pota_spot_t *sp = &s_state.spots[s_state.count];
        memset(sp, 0, sizeof(*sp));

        copy_str(item, "activator",    sp->activator,  sizeof(sp->activator));
        copy_str(item, "frequency",    sp->freq,       sizeof(sp->freq));
        copy_str(item, "mode",         sp->mode,       sizeof(sp->mode));
        copy_str(item, "reference",    sp->park_ref,   sizeof(sp->park_ref));
        copy_str(item, "name",         sp->park_name,  sizeof(sp->park_name));
        copy_str(item, "spotter",      sp->spotter,    sizeof(sp->spotter));
        copy_str(item, "comments",     sp->comments,   sizeof(sp->comments));
        copy_str(item, "locationDesc", sp->location,   sizeof(sp->location));

        char iso[32];
        copy_str(item, "spotTime", iso, sizeof(iso));
        extract_hhmm(iso, sp->time_str, sizeof(sp->time_str));

        sp->received_ms = esp_timer_get_time() / 1000;
        s_state.count++;
    }
    s_state.connected      = true;
    s_state.last_update_ms = esp_timer_get_time() / 1000;
    s_snap = s_state;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "stored %d POTA spots", s_state.count);
    if (s_cb) s_cb(&s_snap);

    cJSON_Delete(root);
}

/* --- HTTP fetch --- */

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    fetch_ctx_t *ctx = (fetch_ctx_t *)evt->user_data;
    if (!ctx || !ctx->buf) return ESP_OK;
    size_t room = ctx->cap - 1 - ctx->total;
    size_t n = evt->data_len > 0 ? (size_t)evt->data_len : 0;
    if (n > room) n = room;
    if (n) {
        memcpy(ctx->buf + ctx->total, evt->data, n);
        ctx->total += n;
    }
    return ESP_OK;
}

static bool fetch_once(char *buf)
{
    fetch_ctx_t ctx = { .buf = buf, .cap = BUF_SIZE, .total = 0 };

    ESP_LOGI(TAG, "GET %s", URL);
    esp_http_client_config_t cfg = {
        .url                  = URL,
        .timeout_ms           = 15000,
        .user_agent           = "HamRadioCompanion/1.0 (esp32s3)",
        .event_handler        = http_event,
        .user_data            = &ctx,
        .crt_bundle_attach    = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGW(TAG, "http_client_init failed");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "perform err=%s status=%d bytes=%u",
             esp_err_to_name(err), status, (unsigned)ctx.total);

    bool ok = false;
    if (err == ESP_OK && status == 200 && ctx.total > 0) {
        buf[ctx.total] = '\0';
        parse_and_store(buf);
        ok = true;
    } else {
        /* Mark offline so the UI shows the right message. */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_state.connected = false;
        s_snap = s_state;
        xSemaphoreGive(s_mutex);
        if (s_cb) s_cb(&s_snap);
    }
    esp_http_client_cleanup(client);
    return ok;
}

static void pota_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "buf alloc failed");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        bool ok = fetch_once(buf);
        uint32_t wait_ms = ok ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS;
        xSemaphoreTake(s_poke, pdMS_TO_TICKS(wait_ms));
    }
}

esp_err_t app_pota_init(app_pota_cb_t cb)
{
    s_cb = cb;
    s_mutex = xSemaphoreCreateMutex();
    s_poke  = xSemaphoreCreateBinary();
    if (xTaskCreate(pota_task, "pota", 8 * 1024, NULL, 2, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_pota_get(app_pota_state_t *out)
{
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

void app_pota_request_refresh(void)
{
    if (s_poke) xSemaphoreGive(s_poke);
}
