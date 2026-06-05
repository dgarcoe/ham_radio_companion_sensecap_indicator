#include "app_propagation.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"

static const char *TAG = "prop";
/* hamqsl serves plain HTTP with a 301 redirect to HTTPS, and
 * esp_http_client_perform follows redirects automatically when given a
 * cert bundle, so just go straight to HTTPS. */
static const char *URL = "https://www.hamqsl.com/solarxml.php";

#define BUF_SIZE             (16 * 1024)
#define REFRESH_INTERVAL_MS  (15 * 60 * 1000)
#define RETRY_INTERVAL_MS    (30 * 1000)

static app_prop_data_t s_data;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_poke;
static app_prop_cb_t s_cb;

typedef struct {
    char  *buf;
    size_t cap;
    size_t total;
} fetch_ctx_t;

/* --- Tiny XML scrapers. --- */

static void extract_tag(const char *xml, const char *tag, char *out, size_t out_sz)
{
    out[0] = '\0';
    char open[32], close[32];
    snprintf(open,  sizeof(open),  "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *p = strstr(xml, open);
    if (!p) return;
    p += strlen(open);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    const char *e = strstr(p, close);
    if (!e) return;
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
    size_t n = (size_t)(e - p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static int extract_tag_int(const char *xml, const char *tag)
{
    char buf[16];
    extract_tag(xml, tag, buf, sizeof(buf));
    return buf[0] ? atoi(buf) : 0;
}

static void extract_attr(const char *tag_start, const char *key,
                         char *out, size_t out_sz)
{
    out[0] = '\0';
    const char *tag_end = strchr(tag_start, '>');
    if (!tag_end) return;

    char needle[24];
    snprintf(needle, sizeof(needle), "%s=\"", key);
    const char *p = strstr(tag_start, needle);
    if (!p || p > tag_end) return;
    p += strlen(needle);
    const char *e = strchr(p, '"');
    if (!e || e > tag_end) return;
    size_t n = (size_t)(e - p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static void parse_bands(const char *xml, app_prop_data_t *data)
{
    data->band_count = 0;
    const char *p = xml;
    while ((p = strstr(p, "<band ")) != NULL &&
           data->band_count < APP_PROP_MAX_BANDS) {
        app_prop_band_t *b = &data->bands[data->band_count];
        b->band[0] = b->time[0] = b->condition[0] = '\0';

        extract_attr(p, "name", b->band, sizeof(b->band));
        extract_attr(p, "time", b->time, sizeof(b->time));

        const char *gt = strchr(p, '>');
        if (gt) {
            gt++;
            const char *end = strstr(gt, "</band>");
            if (end) {
                size_t n = (size_t)(end - gt);
                if (n >= sizeof(b->condition)) n = sizeof(b->condition) - 1;
                memcpy(b->condition, gt, n);
                b->condition[n] = '\0';
            }
        }
        data->band_count++;
        p = strstr(p, "</band>");
        if (!p) break;
        p += strlen("</band>");
    }
}

static void parse_vhf(const char *xml, app_prop_data_t *data)
{
    data->vhf_count = 0;
    const char *p = xml;
    while ((p = strstr(p, "<phenomenon")) != NULL &&
           data->vhf_count < APP_PROP_MAX_VHF) {
        app_prop_vhf_t *v = &data->vhf[data->vhf_count];
        v->name[0] = v->location[0] = v->status[0] = '\0';

        extract_attr(p, "name",     v->name,     sizeof(v->name));
        extract_attr(p, "location", v->location, sizeof(v->location));

        const char *gt = strchr(p, '>');
        if (gt) {
            gt++;
            const char *end = strstr(gt, "</phenomenon>");
            if (end) {
                size_t n = (size_t)(end - gt);
                if (n >= sizeof(v->status)) n = sizeof(v->status) - 1;
                memcpy(v->status, gt, n);
                v->status[n] = '\0';
            }
        }
        data->vhf_count++;
        p = strstr(p, "</phenomenon>");
        if (!p) break;
        p += strlen("</phenomenon>");
    }
}

static void parse_and_store(const char *xml)
{
    app_prop_data_t d = {0};
    d.solar_flux = extract_tag_int(xml, "solarflux");
    d.sunspots   = extract_tag_int(xml, "sunspots");
    d.a_index    = extract_tag_int(xml, "aindex");
    d.k_index    = extract_tag_int(xml, "kindex");
    extract_tag(xml, "xray",        d.xray,         sizeof(d.xray));
    extract_tag(xml, "geomagfield", d.geomag,       sizeof(d.geomag));
    extract_tag(xml, "signalnoise", d.signal_noise, sizeof(d.signal_noise));
    extract_tag(xml, "updated",     d.updated,      sizeof(d.updated));
    parse_bands(xml, &d);
    parse_vhf(xml,   &d);
    d.valid = true;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data = d;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "SFI=%d SSN=%d A=%d K=%d bands=%d vhf=%d",
             d.solar_flux, d.sunspots, d.a_index, d.k_index,
             d.band_count, d.vhf_count);

    if (s_cb) s_cb(&d);
}

/* esp_http_client streams the body in chunks; accumulate into our buf. */
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
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "perform err=%s status=%d bytes=%u",
             esp_err_to_name(err), status, (unsigned)ctx.total);

    bool ok = false;
    if (err == ESP_OK && status == 200 && ctx.total > 0) {
        buf[ctx.total] = '\0';
        parse_and_store(buf);
        ok = true;
    }
    esp_http_client_cleanup(client);
    return ok;
}

static void fetch_task(void *arg)
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

esp_err_t app_propagation_init(app_prop_cb_t on_update)
{
    s_cb = on_update;
    s_mutex = xSemaphoreCreateMutex();
    s_poke  = xSemaphoreCreateBinary();
    if (xTaskCreatePinnedToCoreWithCaps(fetch_task, "prop", 6144, NULL, 2,
                                        NULL, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_propagation_get(app_prop_data_t *out)
{
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_mutex);
}

void app_propagation_request_refresh(void)
{
    if (s_poke) xSemaphoreGive(s_poke);
}
