#pragma once

#include "esp_err.h"
#include "lvgl.h"

/* Initialize display + touch via the Seeed SenseCAP Indicator BSP and bring up LVGL.
 * Returns once LVGL is ticking on its own task. */
esp_err_t bsp_display_start(void);

/* Acquire/release the LVGL mutex around UI calls from app tasks. */
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);
