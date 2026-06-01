#include "bsp.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "bsp_board.h"
#include "bsp_lcd.h"
#include "indev_tp.h"

static const char *TAG = "bsp";

static lv_display_t *s_disp;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
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
    /* 1. Bring up the panel + touch via the vendored BSP. */
    if (bsp_board_init() != ESP_OK) {
        ESP_LOGE(TAG, "bsp_board_init failed");
        return ESP_FAIL;
    }

    esp_lcd_panel_handle_t panel = bsp_lcd_get_panel_handle();
    if (!panel) {
        ESP_LOGE(TAG, "no panel handle");
        return ESP_FAIL;
    }

    const board_res_desc_t *brd = bsp_board_get_description();
    const int w = brd->LCD_WIDTH;
    const int h = brd->LCD_HEIGHT;
    ESP_LOGI(TAG, "panel %dx%d", w, h);

    /* 2. esp_lvgl_port owns the LVGL task + mutex. */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    /* 3. Hand the ST7701 RGB panel to esp_lvgl_port. With
     *    avoid_tearing + direct_mode the port:
     *      - claims the panel's two PSRAM framebuffers as LVGL buffers,
     *      - renders the full frame into the inactive one,
     *      - on flush_is_last, swaps via esp_lcd_panel_draw_bitmap and
     *        blocks on the vsync semaphore.
     *    No partial writes into the live FB => no tearing / no flashing. */
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size  = w * h,   /* overridden internally but required for the direct_mode size check */
        .double_buffer = true,
        .hres = w,
        .vres = h,
        .monochrome = false,
        .rotation = {
            .swap_xy = brd->LCD_SWAP_XY,
            .mirror_x = brd->LCD_MIRROR_X,
            .mirror_y = brd->LCD_MIRROR_Y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }

    /* 4. Touch indev (FT5x06 reads through the BSP). */
    if (lvgl_port_lock(0)) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, s_disp);
        lvgl_port_unlock();
    }

    bsp_lcd_set_backlight(true);
    ESP_LOGI(TAG, "display + touch up");
    return ESP_OK;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }
