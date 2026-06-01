#include "bsp.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"

#include "bsp_board.h"
#include "bsp_lcd.h"
#include "indev_tp.h"

static const char *TAG = "bsp";

static lv_display_t *s_disp;
static lv_indev_t *s_touch;

/* Called from the BSP's RGB LCD trans-done IRQ. Signal LVGL the flush is
 * done, ask for a yield since we may have woken a higher-prio task. */
static bool IRAM_ATTR on_lcd_trans_done(void *arg)
{
    (void)arg;
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    bsp_lcd_flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t tp_num = 0, btn = 0;
    uint16_t x = 0, y = 0;
    if (indev_tp_read(&tp_num, &x, &y, &btn) == ESP_OK && tp_num > 0) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t bsp_display_start(void)
{
    /* 1. Bring up display + touch on real hardware. */
    if (bsp_board_init() != ESP_OK) {
        ESP_LOGE(TAG, "bsp_board_init failed");
        return ESP_FAIL;
    }
    bsp_lcd_set_cb(on_lcd_trans_done, NULL);

    const board_res_desc_t *brd = bsp_board_get_description();
    const int w = brd->LCD_WIDTH;
    const int h = brd->LCD_HEIGHT;
    ESP_LOGI(TAG, "panel %dx%d", w, h);

    /* 2. esp_lvgl_port owns the LVGL task + mutex. */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "could not lock LVGL");
        return ESP_FAIL;
    }

    /* 3. Register an LVGL 9 display that flushes through bsp_lcd_flush.
     *    Partial RGB565 buffer in PSRAM keeps internal RAM free. */
    s_disp = lv_display_create(w, h);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    const size_t buf_sz = w * 40 * 2; /* RGB565 partial buffer */
    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "draw-buf alloc failed");
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);

    /* 4. Wire the touch IC the BSP detected. */
    s_touch = lv_indev_create();
    lv_indev_set_type(s_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch, lvgl_touch_read_cb);
    lv_indev_set_display(s_touch, s_disp);

    lvgl_port_unlock();

    bsp_lcd_set_backlight(true);
    ESP_LOGI(TAG, "display + touch up");
    return ESP_OK;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }
