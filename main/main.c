#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

#include "app_nvs.h"
#include "app_wifi.h"
#include "app_portal.h"
#include "app_time.h"
#include "app_propagation.h"
#include "app_dxcluster.h"
#include "bsp.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

static const char *TAG = "main";

/* --- LVGL custom allocator -------------------------------------------
 *
 * CONFIG_LV_USE_CUSTOM_MALLOC=y removes LVGL's built-in lv_mem_init /
 * lv_malloc_core / etc. from its own build, expecting these symbols
 * to come from application code. Living here (in main.c, always
 * compiled) is the simplest way to guarantee they're linked - putting
 * them in a separate .c file requires the file to be in
 * main/CMakeLists.txt SRCS and we kept missing that.
 *
 * Effect: every LVGL allocation goes to PSRAM via heap_caps_malloc,
 * keeping internal RAM available for WiFi/LWIP/FreeRTOS.
 */
#define LVGL_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)        { ESP_LOGI(TAG, "LVGL allocator: PSRAM via heap_caps_malloc"); }
void lv_mem_deinit(void)      { }
void *lv_malloc_core(size_t size)               { return heap_caps_malloc(size,        LVGL_CAPS); }
void *lv_realloc_core(void *p, size_t new_size) { return heap_caps_realloc(p, new_size, LVGL_CAPS); }
void  lv_free_core(void *p)                     { heap_caps_free(p); }
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool)            { (void)pool; }
void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)      { if (mon_p) memset(mon_p, 0, sizeof(*mon_p)); }
lv_result_t lv_mem_test_core(void)                     { return LV_RESULT_OK; }

/* ----------------------------------------------------------------------- */

static app_config_t s_cfg;
static esp_timer_handle_t s_reconfig_timer;
static bool s_prop_started;
static bool s_dx_started;

/* Runs off the esp_timer task, AFTER the HTTP handler has returned and the
 * portal's worker thread is idle. Safe to stop httpd and switch WiFi mode
 * from here. */
static void reconfig_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "tearing down portal, switching to STA");
    app_portal_stop();
    app_wifi_apply(&s_cfg);
}

static void on_portal_saved(const app_config_t *new_cfg)
{
    s_cfg = *new_cfg;
    app_nvs_save(&s_cfg);
    ui_set_callsign(s_cfg.callsign);

    if (!s_reconfig_timer) {
        const esp_timer_create_args_t args = {
            .callback = reconfig_timer_cb,
            .name = "wifi_reconfig",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_reconfig_timer));
    }
    /* 500 ms is enough for the HTTP response to flush and the worker to
     * unwind. */
    esp_timer_start_once(s_reconfig_timer, 500 * 1000);
}

/* Settings screen "Save" -> persist + apply. */
static void on_settings_saved(const app_config_t *new_cfg)
{
    s_cfg = *new_cfg;
    app_nvs_save(&s_cfg);
    ui_set_callsign(s_cfg.callsign);
    ESP_LOGI(TAG, "settings saved: callsign='%s' locator='%s' tz='%s' dx=%s:%d",
             s_cfg.callsign, s_cfg.locator, s_cfg.tz,
             s_cfg.dx_host, s_cfg.dx_port);

    /* If the DX cluster is already running, push the new endpoint /
     * callsign at it. If it hasn't started yet (no WiFi or no callsign
     * earlier), the on_wifi_state path will pick up the new values when
     * WiFi connects. */
    if (s_dx_started) {
        app_dxcluster_reconfigure(s_cfg.dx_host, s_cfg.dx_port, s_cfg.callsign);
    }
}

static void on_wifi_state(app_wifi_state_t st, const char *ip)
{
    ESP_LOGI(TAG, "wifi -> %d (%s)", st, ip ? ip : "");
    ui_set_wifi(st, ip);

    if (st == APP_WIFI_AP_PORTAL) {
        app_portal_start(&s_cfg, on_portal_saved);
    } else if (st == APP_WIFI_CONNECTED) {
        if (!s_prop_started) {
            ESP_ERROR_CHECK(app_propagation_init(ui_propagation_on_update));
            s_prop_started = true;
        } else {
            app_propagation_request_refresh();
        }
        /* DX cluster needs a callsign to log in. Skip cleanly if missing. */
        if (!s_dx_started && s_cfg.callsign[0] && s_cfg.dx_host[0]) {
            ESP_ERROR_CHECK(app_dxcluster_init(s_cfg.dx_host, s_cfg.dx_port,
                                               s_cfg.callsign,
                                               ui_dx_on_update));
            s_dx_started = true;
        }
    }
}

static void time_watch_task(void *arg)
{
    (void)arg;
    bool last = false;
    for (;;) {
        bool now = app_time_is_synced();
        if (now != last) {
            ui_set_time_synced(now);
            last = now;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void ui_init_task(void *arg)
{
    SemaphoreHandle_t done = (SemaphoreHandle_t)arg;
    if (bsp_lvgl_lock(-1)) {
        ui_init(&s_cfg);
        ui_settings_set_saved_cb(on_settings_saved);
        bsp_lvgl_unlock();
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Kill the task watchdog at runtime - sdkconfig.defaults options
     * don't help if the user's sdkconfig is stale, and the
     * 'esp_task_wdt_reset(707): task not found' flood was making Core 1
     * spin in UART output forever (holding the FreeRTOS scheduler
     * spinlock) while Core 0 timed out in vPortEnterCritical. */
    esp_task_wdt_deinit();

    ESP_ERROR_CHECK(app_nvs_init());
    ESP_ERROR_CHECK(app_nvs_load(&s_cfg));

    ESP_ERROR_CHECK(bsp_display_start());

    /* Build the whole UI tree on a dedicated 24 KB stack: this is a
     * one-shot, deep recursive cascade (lv_obj_create + style cascade
     * + flex layout, per screen, x many screens) and was overflowing
     * the main task stack even when bumped via Kconfig - and any
     * out-of-date sdkconfig would silently keep the old smaller value. */
    SemaphoreHandle_t ui_done = xSemaphoreCreateBinary();
    xTaskCreate(ui_init_task, "ui_init", 24 * 1024, ui_done, 5, NULL);
    xSemaphoreTake(ui_done, portMAX_DELAY);
    vSemaphoreDelete(ui_done);

    ESP_ERROR_CHECK(app_wifi_init(on_wifi_state));
    ESP_ERROR_CHECK(app_wifi_start(&s_cfg));

    ESP_ERROR_CHECK(app_time_init());

    xTaskCreate(time_watch_task, "time_watch", 3072, NULL, 3, NULL);
}
