/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "bsp_board.h"
#include "bsp_i2c.h"
#include "tca9535.h"
#include "ht8574.h"
#include "lcd_panel_config.h"
#include "bsp_lcd.h"
#include "../indev/indev_tp.h"

/* Minimal board: button / LoRa / sensor init removed. We only need LCD +
 * I2C bus + IO expander + touch for this companion build. */

static const char *TAG = "board sensecap";

#define EXPANDER_IO_TP_RESET    7
#define EXPANDER_IO_LCD_CS      4
#define EXPANDER_IO_LCD_RESET   5

#define EXPANDER_IO_RP2040_RESET   8

#define EXPANDER_IO_BMP_PWR       10

#define SENSOR_BMP_ADDR    (0X77)

#define Delay(t) vTaskDelay(pdMS_TO_TICKS(t))
#define udelay(_t) ets_delay_us(_t)


static const board_button_t g_btns[] = {
    {BOARD_BTN_ID_USER, 0,      GPIO_NUM_38,    0},
};

static const io_expander_ops_t g_board_lcd_evb_io_expander_ops = {
    .init = tca9535_init,
    .set_direction = tca9535_set_direction,
    .set_level = tca9535_set_level,
    .read_output_pins = NULL,
    .read_input_pins = NULL, /* TCA9535 returns uint16_t pin mask vs the typedef's uint8_t; not used here */
    .multi_write_start = tca9535_multi_write_start,
    .multi_write_new_level = tca9535_multi_write_new_level,
    .multi_write_end = tca9535_multi_write_end,
};

static const board_res_desc_t g_board_lcd_evb_res = {

    .FUNC_LCD_EN =     (1),
    .LCD_BUS_WIDTH =   (16),

#if CONFIG_LCD_EVB_SCREEN_ROTATION_0
    .LCD_SWAP_XY =              (0),
    .LCD_MIRROR_X =             (0),
    .LCD_MIRROR_Y =             (0),
    .TOUCH_PANEL_SWAP_XY =      (0),
    .TOUCH_PANEL_INVERSE_X =    (0),
    .TOUCH_PANEL_INVERSE_Y =    (0),
#elif CONFIG_LCD_EVB_SCREEN_ROTATION_90
    .LCD_SWAP_XY =              (1),
    .LCD_MIRROR_X =             (0),
    .LCD_MIRROR_Y =             (1),
    .TOUCH_PANEL_SWAP_XY =      (1),
    .TOUCH_PANEL_INVERSE_X =    (1),
    .TOUCH_PANEL_INVERSE_Y =    (0),
#elif CONFIG_LCD_EVB_SCREEN_ROTATION_180
    .LCD_SWAP_XY =              (0),
    .LCD_MIRROR_X =             (1),
    .LCD_MIRROR_Y =             (1),
    .TOUCH_PANEL_SWAP_XY =      (0),
    .TOUCH_PANEL_INVERSE_X =    (1),
    .TOUCH_PANEL_INVERSE_Y =    (1),
#elif CONFIG_LCD_EVB_SCREEN_ROTATION_270
    .LCD_SWAP_XY =              (1),
    .LCD_MIRROR_X =             (1),
    .LCD_MIRROR_Y =             (0),
    .TOUCH_PANEL_SWAP_XY =      (1),
    .TOUCH_PANEL_INVERSE_X =    (0),
    .TOUCH_PANEL_INVERSE_Y =    (1),
#endif
    .LCD_COLOR_INV =            (false),
    .BSP_INDEV_IS_TP =          (1),

    .LCD_IFACE =                (LCD_IFACE_RGB),
    .LCD_DISP_IC_STR =          "st7701",
    /* Hard-coded so we can't be tripped up by missing Kconfig defaults.
     * Original used CONFIG_LCD_EVB_SCREEN_{WIDTH,HEIGHT,FREQ} which all
     * silently fall to 0 when SCREEN_GX/DX aren't selected, producing a
     * scan-stride mismatch (two horizontal copies of the framebuffer). */
    .LCD_FREQ =                 (16 * 1000 * 1000),
    .LCD_WIDTH =                480,
    .LCD_HEIGHT =               480,
    .LCD_COLOR_SPACE = ESP_LCD_COLOR_SPACE_RGB,

    // RGB interface GPIOs for LCD panel
    .GPIO_LCD_DISP_EN = GPIO_NUM_NC,  //future controlled via expanded io
    .GPIO_LCD_VSYNC   = GPIO_NUM_17,
    .GPIO_LCD_HSYNC   = GPIO_NUM_16,
    .GPIO_LCD_DE      = GPIO_NUM_18,
    .GPIO_LCD_PCLK    = GPIO_NUM_21,
    .GPIO_LCD_BL      = GPIO_NUM_45,
    .GPIO_LCD_BL_ON   = 1,

    .GPIO_LCD_DATA0   = GPIO_NUM_15, // B0
    .GPIO_LCD_DATA1   = GPIO_NUM_14, // B1
    .GPIO_LCD_DATA2   = GPIO_NUM_13, // B2
    .GPIO_LCD_DATA3   = GPIO_NUM_12, // B3
    .GPIO_LCD_DATA4   = GPIO_NUM_11, // B4
    .GPIO_LCD_DATA5   = GPIO_NUM_10, // G0
    .GPIO_LCD_DATA6   = GPIO_NUM_9, // G1
    .GPIO_LCD_DATA7   = GPIO_NUM_8, // G2
    .GPIO_LCD_DATA8   = GPIO_NUM_7, // G3
    .GPIO_LCD_DATA9   = GPIO_NUM_6, // G4
    .GPIO_LCD_DATA10  = GPIO_NUM_5, // G5
    .GPIO_LCD_DATA11  = GPIO_NUM_4,  // R0
    .GPIO_LCD_DATA12  = GPIO_NUM_3,  // R1
    .GPIO_LCD_DATA13  = GPIO_NUM_2, // R2
    .GPIO_LCD_DATA14  = GPIO_NUM_1, // R3
    .GPIO_LCD_DATA15  = GPIO_NUM_0, // R4

    /* Match the ST7701S chip's internal VBP/VFP from 0xC1 register
     * (0x0D, 0x02 in lcd_panel_st7701s_init) so the controller's frame
     * total equals the chip's expected frame total. Mismatched totals
     * make every frame start a few lines off from the previous one
     * which presents as the content scrolling and wrapping. */
    .HSYNC_BACK_PORCH = 50,
    .HSYNC_FRONT_PORCH = 10,
    .HSYNC_PULSE_WIDTH = 8,
    .VSYNC_BACK_PORCH = 13,
    .VSYNC_FRONT_PORCH = 2,
    .VSYNC_PULSE_WIDTH = 2,
    .PCLK_ACTIVE_NEG = 1,

    .TOUCH_PANEL_I2C_ADDR = 0,
    .TOUCH_WITH_HOME_BUTTON = 0,

    .BSP_BUTTON_EN =   (1),
    .BUTTON_TAB =  g_btns,
    .BUTTON_TAB_LEN = sizeof(g_btns) / sizeof(g_btns[0]),

    .FUNC_I2C_EN =     (1),
    .GPIO_I2C_SCL =    (GPIO_NUM_40),
    .GPIO_I2C_SDA =    (GPIO_NUM_39),

    .FUNC_SDMMC_EN =   (0),
    .SDMMC_BUS_WIDTH = (1),
    .GPIO_SDMMC_CLK =  (GPIO_NUM_NC),
    .GPIO_SDMMC_CMD =  (GPIO_NUM_NC),
    .GPIO_SDMMC_D0 =   (GPIO_NUM_NC),
    .GPIO_SDMMC_D1 =   (GPIO_NUM_NC),
    .GPIO_SDMMC_D2 =   (GPIO_NUM_NC),
    .GPIO_SDMMC_D3 =   (GPIO_NUM_NC),
    .GPIO_SDMMC_DET =  (GPIO_NUM_NC),

    .FUNC_SDSPI_EN =       (0),
    .SDSPI_HOST =          (SPI2_HOST),
    .GPIO_SDSPI_CS =       (GPIO_NUM_NC),
    .GPIO_SDSPI_SCLK =     (GPIO_NUM_NC),
    .GPIO_SDSPI_MISO =     (GPIO_NUM_NC),
    .GPIO_SDSPI_MOSI =     (GPIO_NUM_NC),

    .FUNC_SPI_EN =         (0),
    .GPIO_SPI_CS =         (GPIO_NUM_NC),
    .GPIO_SPI_MISO =       (GPIO_NUM_47),
    .GPIO_SPI_MOSI =       (GPIO_NUM_48),
    .GPIO_SPI_SCLK =       (GPIO_NUM_41),

    .FUNC_RMT_EN =         (0),
    .GPIO_RMT_IR =         (GPIO_NUM_NC),
    .GPIO_RMT_LED =        (GPIO_NUM_4),
    .RMT_LED_NUM  =        1,

    .FUNC_I2S_EN =         (0),
    .GPIO_I2S_LRCK =       (GPIO_NUM_7),
    .GPIO_I2S_MCLK =       (GPIO_NUM_5),
    .GPIO_I2S_SCLK =       (GPIO_NUM_16),
    .GPIO_I2S_SDIN =       (GPIO_NUM_15),
    .GPIO_I2S_DOUT =       (GPIO_NUM_6),
    .CODEC_I2C_ADDR = 0,
    .AUDIO_ADC_I2C_ADDR = 0,

    .IMU_I2C_ADDR = 0,

    .FUNC_PWR_CTRL =       (0),
    .GPIO_PWR_CTRL =       (GPIO_NUM_NC),
    .GPIO_PWR_ON_LEVEL =   (1),

    .GPIO_MUTE_NUM =   GPIO_NUM_NC,
    .GPIO_MUTE_LEVEL = 1,

    .PMOD1 = NULL,
    .PMOD2 = NULL,

    .FUNC_IO_EXPANDER_EN = true,
    .IO_EXPANDER_I2C_ADDR = 0x20,

    .codec_ops = NULL,
    .io_expander_ops = &g_board_lcd_evb_io_expander_ops,
};

static uint8_t g_io_expander_addr = 0;
esp_err_t bsp_board_sensecap_indicator_detect(void)
{
    //uint8_t a[10];
    //bsp_i2c_scan_device(a, 10);
    //ESP_LOGD(TAG, "i2c scan result: ");
    //ESP_LOG_BUFFER_HEXDUMP(TAG, a, 10, ESP_LOG_DEBUG);

    esp_err_t ret = bsp_i2c_probe_addr(g_board_lcd_evb_res.IO_EXPANDER_I2C_ADDR); // probe io expander
    if (ESP_OK == ret) {
        g_io_expander_addr = g_board_lcd_evb_res.IO_EXPANDER_I2C_ADDR;
    } else {
        /**
         * @brief probe io expander other latent address
         */
        g_io_expander_addr = 0x39;
        ret = bsp_i2c_probe_addr(g_io_expander_addr);
    }
    ESP_RETURN_ON_FALSE(ESP_OK == ret, ESP_FAIL, TAG, "Can't detect io expander @0x%x", g_io_expander_addr);

    return ESP_OK;
}

esp_err_t bsp_board_sensecap_indicator_init(void)
{
    if (g_board_lcd_evb_res.FUNC_IO_EXPANDER_EN) {
        g_board_lcd_evb_io_expander_ops.init(g_io_expander_addr);
        g_board_lcd_evb_io_expander_ops.set_level(EXPANDER_IO_TP_RESET, 0);
        g_board_lcd_evb_io_expander_ops.set_direction(EXPANDER_IO_TP_RESET, 1);
        Delay(5);
        g_board_lcd_evb_io_expander_ops.set_level(EXPANDER_IO_TP_RESET, 1);
    }
    g_board_lcd_evb_io_expander_ops.set_direction(EXPANDER_IO_RP2040_RESET, 1);
    g_board_lcd_evb_io_expander_ops.set_level(EXPANDER_IO_RP2040_RESET, 1);

    g_board_lcd_evb_io_expander_ops.set_direction(EXPANDER_IO_BMP_PWR, 1);
    g_board_lcd_evb_io_expander_ops.set_level(EXPANDER_IO_BMP_PWR, 1);
    
    indev_tp_init();

    ESP_ERROR_CHECK(bsp_lcd_init());
    //bsp_led_init();
    //bsp_led_set_rgb(0, 255, 0, 0);
    //vTaskDelay(pdMS_TO_TICKS(100));
    //bsp_led_set_rgb(0, 0, 255, 0);
    //vTaskDelay(pdMS_TO_TICKS(100));
    //ESP_ERROR_CHECK(bsp_i2s_init(I2S_NUM_0, 16000));
    //ESP_ERROR_CHECK(bsp_codec_init(AUDIO_HAL_16K_SAMPLES));
    //bsp_led_set_rgb(0, 0, 0, 255);
    lcd_panel_st7701s_init();
    //bsp_led_set_rgb(0, 0, 0, 0);

    return ESP_OK;
}

esp_err_t bsp_board_sensecap_indicator_power_ctrl(power_module_t module, bool on)
{

    return ESP_OK;
}

const board_res_desc_t *bsp_board_sensecap_indicator_get_res_desc(void)
{
    return &g_board_lcd_evb_res;
}

