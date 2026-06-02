/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Trimmed to FT5x06 only: the SenseCAP Indicator D1L uses the Focal Tech
 * capacitive touch IC. Other vendors (TT, Goodix, XPT) were removed when
 * the BSP was vendored.
 */
#include <string.h>
#include "bsp_i2c.h"
#include "bsp_board.h"
#include "indev_tp.h"
#include "esp_log.h"
#include "esp_err.h"

#include "ft5x06.h"

static const char *TAG = "indev_tp";
static bool s_ft_ok = false;

esp_err_t indev_tp_init(void)
{
    /* Probe the FT5x06 address (0x48 on D1L) before init so we get a
     * clear error if the chip isn't talking. */
    esp_err_t probe = bsp_i2c_probe_addr(0x48);
    if (probe != ESP_OK) {
        ESP_LOGW(TAG, "FT5x06 NOT found at 0x48 (probe=%d) - touch will be dead", probe);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "FT5x06 detected at 0x48");

    esp_err_t ret = ft5x06_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ft5x06_init failed: %d", ret);
        return ret;
    }
    s_ft_ok = true;
    ESP_LOGI(TAG, "touch initialized");
    return ESP_OK;
}

esp_err_t indev_tp_get_dev(char **dev_name, uint8_t *dev_addr)
{
    if (dev_name) *dev_name = "Focal Tech";
    if (dev_addr) *dev_addr = 0x48;
    return ESP_OK;
}

esp_err_t indev_tp_read(uint8_t *tp_num, uint16_t *x, uint16_t *y, uint8_t *btn_val)
{
    if (btn_val) *btn_val = 0;
    if (!s_ft_ok) {
        *tp_num = 0;
        return ESP_OK;
    }
    esp_err_t ret = ft5x06_read_pos(tp_num, x, y);
    if (ret != ESP_OK) return ret;

    /* Log the first press we see so we can confirm the path works. */
    static bool seen_press = false;
    if (*tp_num > 0 && !seen_press) {
        seen_press = true;
        ESP_LOGI(TAG, "first touch at (%u, %u)", *x, *y);
    }

    const board_res_desc_t *brd = bsp_board_get_description();
    if (brd->TOUCH_PANEL_SWAP_XY) {
        uint16_t swap = *x;
        *x = *y;
        *y = swap;
    }
    if (brd->TOUCH_PANEL_INVERSE_X) {
        *x = brd->LCD_WIDTH  - (*x + 1);
    }
    if (brd->TOUCH_PANEL_INVERSE_Y) {
        *y = brd->LCD_HEIGHT - (*y + 1);
    }
    return ESP_OK;
}
