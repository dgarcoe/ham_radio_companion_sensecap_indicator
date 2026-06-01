#include "bsp.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"

static const char *TAG = "bsp";

#define BSP_LCD_H_RES 480
#define BSP_LCD_V_RES 480

/* Headless flush: discards pixels but acknowledges so LVGL keeps ticking.
 * Replace once the real ST7701S panel handle is registered via
 * lvgl_port_add_disp(). */
static void noop_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

esp_err_t bsp_display_start(void)
{
    /* TODO(hardware bring-up): drive the D1L's ST7701S RGB display and
     * CHSC6540 touch. Two viable paths:
     *
     *   1. Vendor Seeed's reference drivers
     *      (github.com/Seeed-Studio/sensecap-indicator-esp32 -> the
     *      indicator_display / indicator_touch components) into
     *      ./components and call their init here.
     *
     *   2. Pull the standalone drivers from the ESP component registry
     *      (esp_lcd_st7701, esp_lcd_touch_chsc6x), build the esp_lcd
     *      panel handle, then call lvgl_port_add_disp() / add_touch()
     *      instead of the headless display below.
     *
     * Until that lands, LVGL boots with a headless display so the rest
     * of the app (WiFi, portal, SNTP, UI tree) runs end-to-end. The
     * screen stays black. */

    lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&cfg));

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "could not lock LVGL");
        return ESP_FAIL;
    }

    lv_display_t *disp = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    if (!disp) {
        lvgl_port_unlock();
        return ESP_FAIL;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    const size_t buf_sz = BSP_LCD_H_RES * 40 * 2; /* RGB565 partial buffer */
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "draw-buf alloc failed (%u bytes)", (unsigned)buf_sz);
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(disp, buf, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, noop_flush);

    lvgl_port_unlock();

    ESP_LOGW(TAG, "LVGL up with a HEADLESS display - hardware drivers not "
                  "integrated yet, the panel will stay dark");
    return ESP_OK;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }
