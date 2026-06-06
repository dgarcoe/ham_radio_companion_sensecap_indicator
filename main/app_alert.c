#include "app_alert.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bsp.h"

static const char *TAG = "alert";

/* Dedup window: don't re-alert on the same callsign within 10 minutes. */
#define DEDUP_WINDOW_MS  (10 * 60 * 1000)
#define DEDUP_MAX        16

typedef struct {
    char    callsign[16];
    int64_t when_ms;
} dedup_entry_t;

/* Watchlist + dedup live in PSRAM .bss; tiny, but keeps the pattern. */
EXT_RAM_BSS_ATTR static dedup_entry_t s_dedup[DEDUP_MAX];
static int s_dedup_n;

static char s_list[APP_ALERT_LIST_MAX];
static bool s_enabled;
static SemaphoreHandle_t s_mutex;
static app_alert_visual_cb_t s_visual;

static void to_upper_copy(const char *in, char *out, size_t out_sz)
{
    size_t i = 0;
    for (; i + 1 < out_sz && in[i]; i++) {
        out[i] = (char)toupper((unsigned char)in[i]);
    }
    out[i] = '\0';
}

/* Returns true if `call` contains any comma-separated token from `list`
 * as a substring (after uppercasing both). Empty/blank tokens skipped. */
static bool list_matches(const char *list, const char *call)
{
    if (!list[0] || !call[0]) return false;

    char up_call[24];
    to_upper_copy(call, up_call, sizeof(up_call));

    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char tok[16];
        size_t n = 0;
        while (*p && *p != ',' && n + 1 < sizeof(tok)) {
            if (*p != ' ') tok[n++] = (char)toupper((unsigned char)*p);
            p++;
        }
        tok[n] = '\0';
        while (*p && *p != ',') p++;
        if (n > 0 && strstr(up_call, tok)) return true;
    }
    return false;
}

static bool dedup_should_fire(const char *call, int64_t now_ms)
{
    /* Drop entries older than the window. */
    int w = 0;
    for (int i = 0; i < s_dedup_n; i++) {
        if (now_ms - s_dedup[i].when_ms < DEDUP_WINDOW_MS) {
            if (w != i) s_dedup[w] = s_dedup[i];
            w++;
        }
    }
    s_dedup_n = w;

    for (int i = 0; i < s_dedup_n; i++) {
        if (strcasecmp(s_dedup[i].callsign, call) == 0) return false;
    }
    if (s_dedup_n == DEDUP_MAX) {
        /* Drop oldest (index 0). */
        memmove(&s_dedup[0], &s_dedup[1], sizeof(s_dedup[0]) * (DEDUP_MAX - 1));
        s_dedup_n--;
    }
    strncpy(s_dedup[s_dedup_n].callsign, call,
            sizeof(s_dedup[s_dedup_n].callsign) - 1);
    s_dedup[s_dedup_n].callsign[sizeof(s_dedup[s_dedup_n].callsign) - 1] = '\0';
    s_dedup[s_dedup_n].when_ms = now_ms;
    s_dedup_n++;
    return true;
}

static const char *src_tag(app_alert_source_t s)
{
    switch (s) {
        case APP_ALERT_SRC_DX:   return "DX";
        case APP_ALERT_SRC_POTA: return "POTA";
        case APP_ALERT_SRC_SOTA: return "SOTA";
    }
    return "?";
}

void app_alert_init(const app_config_t *cfg, app_alert_visual_cb_t cb)
{
    s_mutex = xSemaphoreCreateMutex();
    s_visual = cb;
    app_alert_apply_config(cfg);
}

void app_alert_apply_config(const app_config_t *cfg)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_list, cfg->alert_list, sizeof(s_list) - 1);
    s_list[sizeof(s_list) - 1] = '\0';
    s_enabled = cfg->alert_enabled;
    /* Reset dedup so a re-enable / list change re-arms cleanly. */
    s_dedup_n = 0;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "watchlist='%s' enabled=%d", s_list, s_enabled);
}

bool app_alert_check(app_alert_source_t src, const char *callsign, const char *extra)
{
    if (!s_mutex || !s_enabled || !callsign || !callsign[0]) return false;

    bool fire = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_list[0] && list_matches(s_list, callsign)) {
        int64_t now = esp_timer_get_time() / 1000;
        fire = dedup_should_fire(callsign, now);
    }
    xSemaphoreGive(s_mutex);

    if (!fire) return false;

    char toast[64];
    if (extra && extra[0]) {
        snprintf(toast, sizeof(toast), "%s %s  %s",
                 src_tag(src), callsign, extra);
    } else {
        snprintf(toast, sizeof(toast), "%s %s", src_tag(src), callsign);
    }
    ESP_LOGI(TAG, "ALERT: %s", toast);

    bsp_alert_beep();
    if (s_visual) s_visual(src, toast);
    return true;
}
