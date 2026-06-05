#include "app_sota.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "cJSON.h"

static const char *TAG = "sota";
/* `all` filter -> HF + VHF; 50 most recent spots. */
static const char *URL = "https://api2.sota.org.uk/api/spots/50/all";

#define BUF_SIZE             (32 * 1024)
#define REFRESH_INTERVAL_MS  (90 * 1000)
#define RETRY_INTERVAL_MS    (30 * 1000)

/* ~16 KB each - keep in PSRAM .bss, internal DRAM is for WiFi/stacks. */
EXT_RAM_BSS_ATTR static app_sota_state_t  s_state;
EXT_RAM_BSS_ATTR static app_sota_state_t  s_snap;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_poke;
static app_sota_cb_t     s_cb;

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

/* SOTA returns frequency in MHz as a string ("14.062", "144.350"). The
 * UI / band table use kHz, so convert here once. */
static void copy_freq_mhz_to_khz(cJSON *obj, char *out, size_t out_sz)
{
    out[0] = '\0';
    cJSON *f = cJSON_GetObjectItem(obj, "frequency");
    double mhz = 0;
    if (cJSON_IsString(f) && f->valuestring) {
        mhz = strtod(f->valuestring, NULL);
    } else if (cJSON_IsNumber(f)) {
        mhz = f->valuedouble;
    }
    if (mhz > 0) snprintf(out, out_sz, "%.1f", mhz * 1000.0);
}

/* "2024-01-15T14:30:00" -> "1430Z". */
static void extract_hhmm(const char *iso, char *out, size_t out_sz)
{
    out[0] = '\0';
    if (!iso) return;
    const char *t = strchr(iso, 'T');
    if (!t || strlen(t) < 6 || out_sz < 6) return;
    t++;
    out[0] = t[0];
    out[1] = t[1];
    out[2] = t[3];
    out[3] = t[4];
    out[4] = 'Z';
    out[5] = '\0';
}

static void upcase(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
    }
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
    if (n > APP_SOTA_MAX_SPOTS) n = APP_SOTA_MAX_SPOTS;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.count = 0;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item)) continue;

        app_sota_spot_t *sp = &s_state.spots[s_state.count];
        memset(sp, 0, sizeof(*sp));

        copy_str(item, "callsign",     sp->activator,    sizeof(sp->activator));
        copy_freq_mhz_to_khz(item,     sp->freq,         sizeof(sp->freq));
        copy_str(item, "mode",         sp->mode,         sizeof(sp->mode));
        upcase(sp->mode);
        copy_str(item, "summitCode",   sp->summit_ref,   sizeof(sp->summit_ref));
        copy_str(item, "summitName",   sp->summit_name,  sizeof(sp->summit_name));
        copy_str(item, "spotter",      sp->spotter,      sizeof(sp->spotter));
        copy_str(item, "comments",     sp->comments,     sizeof(sp->comments));

        char iso[32];
        copy_str(item, "timeStamp", iso, sizeof(iso));
        extract_hhmm(iso, sp->time_str, sizeof(sp->time_str));

        sp->received_ms = esp_timer_get_time() / 1000;
        s_state.count++;
    }
    s_state.connected      = true;
    s_state.last_update_ms = esp_timer_get_time() / 1000;
    s_snap = s_state;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "stored %d SOTA spots", s_state.count);
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
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_state.connected = false;
        s_snap = s_state;
        xSemaphoreGive(s_mutex);
        if (s_cb) s_cb(&s_snap);
    }
    esp_http_client_cleanup(client);
    return ok;
}

static void sota_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "buf alloc failed");
        vTaskDelete(NULL);
        return;
    }
    /* Stagger start so we don't race POTA's first fetch on a single
     * mbedtls heap. */
    vTaskDelay(pdMS_TO_TICKS(5000));
    for (;;) {
        bool ok = fetch_once(buf);
        uint32_t wait_ms = ok ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS;
        xSemaphoreTake(s_poke, pdMS_TO_TICKS(wait_ms));
    }
}

esp_err_t app_sota_init(app_sota_cb_t cb)
{
    s_cb = cb;
    s_mutex = xSemaphoreCreateMutex();
    s_poke  = xSemaphoreCreateBinary();
    if (xTaskCreate(sota_task, "sota", 8 * 1024, NULL, 2, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_sota_get(app_sota_state_t *out)
{
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

void app_sota_request_refresh(void)
{
    if (s_poke) xSemaphoreGive(s_poke);
}
