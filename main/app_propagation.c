#include "app_propagation.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "prop";
static const char *URL = "http://www.hamqsl.com/solarxml.php";

#define BUF_SIZE             (16 * 1024)
#define REFRESH_INTERVAL_MS  (15 * 60 * 1000)
#define RETRY_INTERVAL_MS    (30 * 1000)

static app_prop_data_t s_data;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_poke;
static app_prop_cb_t s_cb;

/* --- Tiny XML scrapers. The hamqsl XML is well-formed and predictable
 * so we don't need a real parser, just substring search. */

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

/* Extract attribute "key" from the tag at `tag_start` (pointing at '<'). */
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
    d.valid = true;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data = d;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "SFI=%d SSN=%d A=%d K=%d bands=%d",
             d.solar_flux, d.sunspots, d.a_index, d.k_index, d.band_count);

    if (s_cb) s_cb(&d);
}

static bool fetch_once(char *buf)
{
    esp_http_client_config_t cfg = {
        .url = URL,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int total = 0, n;
        while ((n = esp_http_client_read(client, buf + total,
                                         BUF_SIZE - 1 - total)) > 0) {
            total += n;
            if (total >= BUF_SIZE - 1) break;
        }
        buf[total] = '\0';
        if (total > 0) {
            parse_and_store(buf);
            ok = true;
        } else {
            ESP_LOGW(TAG, "empty response");
        }
    } else {
        ESP_LOGW(TAG, "http open failed (wifi up?)");
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
        /* Sleep until either the interval passes or someone pokes us. */
        xSemaphoreTake(s_poke, pdMS_TO_TICKS(wait_ms));
    }
}

esp_err_t app_propagation_init(app_prop_cb_t on_update)
{
    s_cb = on_update;
    s_mutex = xSemaphoreCreateMutex();
    s_poke  = xSemaphoreCreateBinary();
    if (xTaskCreate(fetch_task, "prop", 6144, NULL, 2, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_propagation_get(app_prop_data_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_mutex);
}

void app_propagation_request_refresh(void)
{
    if (s_poke) xSemaphoreGive(s_poke);
}
