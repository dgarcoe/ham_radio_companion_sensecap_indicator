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
    /* Scan the whole 7-bit I2C address space and log every device that
     * responds. The FT5x06 is usually at 0x38 or 0x48; the TCA9554
     * IO-expander at 0x20 or 0x39. If we don't see 0x38 / 0x48 here, the
     * touch chip isn't on the bus and there's no point even trying. */
    ESP_LOGI(TAG, "I2C scan:");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (bsp_i2c_probe_addr(addr) == ESP_OK) {
            ESP_LOGI(TAG, "  device at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan done, %d device(s) responded", found);

    /* Try the two known FT5x06 addresses, in order. */
    uint8_t addr = 0;
    if (bsp_i2c_probe_addr(0x48) == ESP_OK)      addr = 0x48;
    else if (bsp_i2c_probe_addr(0x38) == ESP_OK) addr = 0x38;

    if (addr == 0) {
        ESP_LOGW(TAG, "FT5x06 not found at 0x48 or 0x38 - touch will be dead");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "FT5x06 detected at 0x%02X", addr);

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
