#include "bsp.h"

#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "bsp";

esp_err_t bsp_display_start(void)
{
    const bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = BSP_LCD_H_RES * 40,
        .double_buffer = true,
        .flags = {
            .buff_dma    = false,   /* RGB panel: pixels live in SPIRAM, no DMA flag */
            .buff_spiram = true,
        },
    };

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return ESP_FAIL;
    }

    /* Touch is auto-registered by the BSP when CHSC6540 is detected. */
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "display up: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

bool bsp_lvgl_lock(int timeout_ms) { return bsp_display_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { bsp_display_unlock(); }
