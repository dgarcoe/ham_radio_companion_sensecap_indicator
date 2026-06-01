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

esp_err_t indev_tp_init(void)
{
    esp_err_t ret = ft5x06_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ft5x06_init failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "FT5x06 touch initialized");
    }
    return ret;
}

esp_err_t indev_tp_get_dev(char **dev_name, uint8_t *dev_addr)
{
    if (dev_name) *dev_name = "Focal Tech";
    if (dev_addr) *dev_addr = 0x38;
    return ESP_OK;
}

esp_err_t indev_tp_read(uint8_t *tp_num, uint16_t *x, uint16_t *y, uint8_t *btn_val)
{
    if (btn_val) *btn_val = 0;
    esp_err_t ret = ft5x06_read_pos(tp_num, x, y);
    if (ret != ESP_OK) return ret;

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
