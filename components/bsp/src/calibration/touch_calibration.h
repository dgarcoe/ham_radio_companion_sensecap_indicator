/* Minimal stub: the original Seeed touch_calibration drew an LVGL-8
 * calibration UI. For now we use a hard-coded linear transform that maps
 * the XPT2046's 12-bit raw range to 480x480 pixels. Good enough to drive
 * the watch UI; we can swap in a calibration screen later. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t touch_calibration_transform(int32_t *x, int32_t *y)
{
    /* XPT2046 raw values roughly land between ~200 and ~3900 across the
     * panel. Clamp + linearly scale into 0..479. Adjust per-board if the
     * pointer is offset; the constants are intentionally easy to tweak. */
    const int32_t RAW_MIN = 200;
    const int32_t RAW_MAX = 3900;
    const int32_t SCREEN  = 480;

    int32_t rx = *x;
    int32_t ry = *y;
    if (rx < RAW_MIN) rx = RAW_MIN;
    if (rx > RAW_MAX) rx = RAW_MAX;
    if (ry < RAW_MIN) ry = RAW_MIN;
    if (ry > RAW_MAX) ry = RAW_MAX;

    *x = (rx - RAW_MIN) * (SCREEN - 1) / (RAW_MAX - RAW_MIN);
    *y = (ry - RAW_MIN) * (SCREEN - 1) / (RAW_MAX - RAW_MIN);
    return ESP_OK;
}

static inline esp_err_t touch_calibration_run(
    int (*is_pressed_fn)(void),
    esp_err_t (*get_raw_fn)(uint16_t *x, uint16_t *y),
    bool recalibrate)
{
    (void)is_pressed_fn;
    (void)get_raw_fn;
    (void)recalibrate;
    /* No-op: calibration UI removed when the BSP was vendored. */
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
