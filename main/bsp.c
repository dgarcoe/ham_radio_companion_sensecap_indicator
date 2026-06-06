#include "bsp.h"

#include <string.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "driver/uart.h"
#include "driver/gpio.h"

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
    ESP_LOGI(TAG, "panel %dx%d @ %d MHz, swap_xy=%d mirror=%d/%d bus_width=%d",
             w, h, (int)(brd->LCD_FREQ / 1000000),
             brd->LCD_SWAP_XY, brd->LCD_MIRROR_X, brd->LCD_MIRROR_Y,
             brd->LCD_BUS_WIDTH);
    ESP_LOGI(TAG, "porches: H[pw=%d,bp=%d,fp=%d] V[pw=%d,bp=%d,fp=%d] pclk_neg=%d",
             brd->HSYNC_PULSE_WIDTH, brd->HSYNC_BACK_PORCH, brd->HSYNC_FRONT_PORCH,
             brd->VSYNC_PULSE_WIDTH, brd->VSYNC_BACK_PORCH, brd->VSYNC_FRONT_PORCH,
             brd->PCLK_ACTIVE_NEG);
    ESP_LOGI(TAG, "ic='%s' iface=%d", brd->LCD_DISP_IC_STR, brd->LCD_IFACE);

    /* 2. esp_lvgl_port owns the LVGL task + mutex. The default 4 KB
     *    isn't enough for our heavy refresh paths (label-set ->
     *    mark-dirty -> send-event -> task-wake -> enter-critical chain
     *    is 15 frames deep, before any locals). 16 KB gives plenty of
     *    headroom against future feature additions. */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16 * 1024;
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

    /* 4. Touch indev (FT5x06 reads through the BSP).
     *    Use a real timeout - lvgl_port_lock(0) fails silently when the
     *    port's task is mid-flush, which left the indev never created
     *    and touch silently dropped. */
    if (lvgl_port_lock(500)) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, s_disp);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "touch indev registered with LVGL");
    } else {
        ESP_LOGE(TAG, "could not lock LVGL to register touch indev");
    }

    bsp_lcd_set_backlight(true);
    ESP_LOGI(TAG, "display + touch up");
    return ESP_OK;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

/* --- RP2040 buzzer (MLT-8530 on the auxiliary RP2040) -----------------
 *
 * The buzzer is wired to the RP2040, reachable from the ESP32-S3 over
 * UART2 (TX=GPIO19, RX=GPIO20, 115200 8N1). The Seeed RP2040 stock
 * firmware accepts COBS-encoded packets terminated by 0x00, with the
 * first byte being the command id and the rest being the payload.
 *
 *   PKT_TYPE_CMD_BEEP_ON  = 0xA1, payload = uint32_t LE milliseconds
 *   PKT_TYPE_CMD_BEEP_OFF = 0xA2
 *
 * COBS encoding (Consistent Overhead Byte Stuffing) replaces in-band
 * zeros with offset markers so 0x00 can be the unambiguous frame
 * delimiter. For our small payload (no zeros expected in <250 bytes)
 * the encoding is simply: [len+1][cmd][payload...][0x00].
 */

#define RP2040_UART_NUM   UART_NUM_2
#define RP2040_UART_TX    GPIO_NUM_19
#define RP2040_UART_RX    GPIO_NUM_20
#define RP2040_UART_BAUD  115200

#define PKT_TYPE_CMD_BEEP_ON   0xA1
#define BEEP_MS_DEFAULT        120        /* short, polite chirp */

static bool s_rp2040_uart_ready;

static esp_err_t rp2040_uart_init(void)
{
    if (s_rp2040_uart_ready) return ESP_OK;

    const uart_config_t cfg = {
        .baud_rate = RP2040_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* TX-only ring (no RX consumed here; the RP2040 sends sensor data we
     * ignore - the driver still drains its RX FIFO so nothing stalls). */
    esp_err_t e = uart_driver_install(RP2040_UART_NUM, 512, 0, 0, NULL, 0);
    if (e != ESP_OK) return e;
    e = uart_param_config(RP2040_UART_NUM, &cfg);
    if (e != ESP_OK) return e;
    e = uart_set_pin(RP2040_UART_NUM, RP2040_UART_TX, RP2040_UART_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) return e;

    s_rp2040_uart_ready = true;
    ESP_LOGI(TAG, "RP2040 UART up (UART%d tx=%d rx=%d %d 8N1)",
             RP2040_UART_NUM, RP2040_UART_TX, RP2040_UART_RX, RP2040_UART_BAUD);
    return ESP_OK;
}

/* Encode `in` (len bytes, must NOT contain a frame delimiter 0x00 followed
 * by anything you care about) into `out` using classic COBS. Returns the
 * number of bytes written (always len+1, plus the caller appends 0x00). */
static size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t read = 0, write = 0;
    size_t code_idx = write++;
    uint8_t code = 1;
    while (read < len) {
        if (in[read] == 0) {
            out[code_idx] = code;
            code_idx = write++;
            code = 1;
        } else {
            out[write++] = in[read];
            code++;
            if (code == 0xFF) {
                out[code_idx] = code;
                code_idx = write++;
                code = 1;
            }
        }
        read++;
    }
    out[code_idx] = code;
    return write;
}

void bsp_alert_beep(void)
{
    if (rp2040_uart_init() != ESP_OK) {
        ESP_LOGW(TAG, "beep: UART not ready");
        return;
    }
    uint8_t raw[5];
    uint32_t ms = BEEP_MS_DEFAULT;
    raw[0] = PKT_TYPE_CMD_BEEP_ON;
    raw[1] = (uint8_t)(ms      );
    raw[2] = (uint8_t)(ms >>  8);
    raw[3] = (uint8_t)(ms >> 16);
    raw[4] = (uint8_t)(ms >> 24);

    uint8_t enc[8];
    size_t  n = cobs_encode(raw, sizeof(raw), enc);
    enc[n++] = 0x00;  /* frame delimiter */
    uart_write_bytes(RP2040_UART_NUM, (const char *)enc, n);
}
