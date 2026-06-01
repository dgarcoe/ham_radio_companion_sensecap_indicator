#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_nvs.h"
#include "app_wifi.h"
#include "app_portal.h"
#include "app_time.h"
#include "bsp.h"
#include "ui/ui.h"

static const char *TAG = "main";

static app_config_t s_cfg;

static void on_portal_saved(const app_config_t *new_cfg)
{
    s_cfg = *new_cfg;
    app_nvs_save(&s_cfg);
    ui_set_callsign(s_cfg.callsign);
    app_portal_stop();
    app_wifi_apply(&s_cfg);
}

static void on_wifi_state(app_wifi_state_t st, const char *ip)
{
    ESP_LOGI(TAG, "wifi -> %d (%s)", st, ip ? ip : "");
    ui_set_wifi(st, ip);

    if (st == APP_WIFI_AP_PORTAL) {
        app_portal_start(&s_cfg, on_portal_saved);
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

void app_main(void)
{
    ESP_ERROR_CHECK(app_nvs_init());
    ESP_ERROR_CHECK(app_nvs_load(&s_cfg));

    ESP_ERROR_CHECK(bsp_display_start());

    if (bsp_lvgl_lock(-1)) {
        ui_init(&s_cfg);
        bsp_lvgl_unlock();
    }

    ESP_ERROR_CHECK(app_wifi_init(on_wifi_state));
    ESP_ERROR_CHECK(app_wifi_start(&s_cfg));

    ESP_ERROR_CHECK(app_time_init());

    xTaskCreate(time_watch_task, "time_watch", 3072, NULL, 3, NULL);
}
