#include "app_mufmap.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "app_mufmap";

/* prop.kc2g.com serves the live MUF render only as SVG, and LVGL's
 * on-device SVG decoders are too inconsistent across minor versions
 * to rely on. Solution: route through images.weserv.nl, a free
 * libvips-backed proxy that fetches a remote URL, rasterises it
 * server-side, and hands us back a PNG at whatever pixel width we
 * ask for. The kc2g SVG has a 2:1 aspect, so 460 px wide => 230 px
 * tall, which lines up exactly with the propagation tab's content
 * area. The fetched PNG is small (~40-80 KB typical) and lodepng
 * handles it without complaint.
 *
 * If weserv.nl is ever unreachable the tab gracefully degrades to
 * 'Waiting...' -- no crash, just no picture until the next retry. */
#define MUFMAP_URL \
    "https://images.weserv.nl/" \
    "?url=prop.kc2g.com/renders/current/mufd-normal-now.svg" \
    "&w=460&output=png"
#define MUFMAP_BUF_BYTES    (256 * 1024)

#define FETCH_INTERVAL_MS   (15 * 60 * 1000)
#define RETRY_INTERVAL_MS   (30 * 1000)

static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_poke;
static app_mufmap_cb_t   s_cb;

/* Two buffers: one being filled by an in-flight fetch, one being
 * displayed. We swap pointers under the mutex so the UI never sees
 * a half-written PNG. */
EXT_RAM_BSS_ATTR static uint8_t s_buf_a[MUFMAP_BUF_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_buf_b[MUFMAP_BUF_BYTES];

static uint8_t *s_active_buf;       /* the one the UI is reading from */
static size_t   s_active_size;
static int64_t  s_active_fetch_ms;
static bool     s_ever_fetched;

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   total;
} fetch_ctx_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    fetch_ctx_t *ctx = evt->user_data;
    if (!ctx) return ESP_OK;
    size_t want = evt->data_len;
    if (ctx->total + want > ctx->cap) want = ctx->cap - ctx->total;
    if (want == 0) return ESP_OK;
    memcpy(ctx->buf + ctx->total, evt->data, want);
    ctx->total += want;
    return ESP_OK;
}

/* Fetch one image into the *inactive* buffer (i.e. whichever of
 * s_buf_a/s_buf_b is not currently being read by the UI). On success
 * we swap the active pointer under the mutex. */
static bool fetch_once(void)
{
    uint8_t *target = (s_active_buf == s_buf_a) ? s_buf_b : s_buf_a;
    fetch_ctx_t ctx = { .buf = target, .cap = MUFMAP_BUF_BYTES, .total = 0 };

    esp_http_client_config_t cfg = {
        .url                  = MUFMAP_URL,
        .timeout_ms           = 30000,
        .user_agent           = "HamRadioCompanion/1.0 (esp32s3)",
        .event_handler        = http_event,
        .user_data            = &ctx,
        .crt_bundle_attach    = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GET %s -> err=%s status=%d bytes=%u",
             MUFMAP_URL, esp_err_to_name(err), status, (unsigned)ctx.total);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || ctx.total < 128) return false;

    /* Cheap signature sniff: weserv.nl should hand us a PNG when we
     * ask for output=png. If it doesn't (proxy failure, a 200 with an
     * HTML error page) we'd just hand garbage to LVGL. The 8-byte PNG
     * signature is fixed: 89 50 4E 47 0D 0A 1A 0A. */
    static const uint8_t PNG_SIG[8] = { 0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a };
    if (memcmp(target, PNG_SIG, 8) != 0) {
        ESP_LOGW(TAG, "response is not a PNG (first bytes %02x %02x %02x %02x)",
                 target[0], target[1], target[2], target[3]);
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active_buf      = target;
    s_active_size     = ctx.total;
    s_active_fetch_ms = esp_timer_get_time() / 1000;
    s_ever_fetched    = true;
    xSemaphoreGive(s_mutex);
    return true;
}

static void fetch_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool ok = fetch_once();
        if (ok && s_cb) {
            app_mufmap_snap_t snap;
            app_mufmap_get(&snap);
            s_cb(&snap);
        }
        uint32_t wait_ms = ok ? FETCH_INTERVAL_MS : RETRY_INTERVAL_MS;
        xSemaphoreTake(s_poke, pdMS_TO_TICKS(wait_ms));
    }
}

esp_err_t app_mufmap_init(app_mufmap_cb_t on_update)
{
    if (s_mutex) return ESP_OK;
    s_cb    = on_update;
    s_mutex = xSemaphoreCreateMutex();
    s_poke  = xSemaphoreCreateBinary();
    if (!s_mutex || !s_poke) return ESP_ERR_NO_MEM;
    s_active_buf = NULL;
    /* Stack 6 KB: TLS handshake + esp_http_client + memcmp + a small
     * amount of locals. PSRAM-allocated task control block keeps the
     * heap pressure off internal DRAM. */
    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(
        fetch_task, "mufmap", 6144, NULL, 2, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return (r == pdPASS) ? ESP_OK : ESP_FAIL;
}

void app_mufmap_get(app_mufmap_snap_t *out)
{
    if (!s_mutex) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    out->data          = s_active_buf;
    out->size          = s_active_size;
    out->last_fetch_ms = s_active_fetch_ms;
    out->ever_fetched  = s_ever_fetched;
    xSemaphoreGive(s_mutex);
}

void app_mufmap_request_refresh(void)
{
    if (s_poke) xSemaphoreGive(s_poke);
}
