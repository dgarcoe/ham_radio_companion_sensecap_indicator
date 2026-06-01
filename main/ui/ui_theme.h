#pragma once

#include "lvgl.h"

/* Dark + neon-accent palette. The watch + settings draw from these only,
 * so a future theme swap is just changing this file. */
#define UI_COL_BG        lv_color_hex(0x05070D)
#define UI_COL_PANEL     lv_color_hex(0x0B1220)
#define UI_COL_PANEL_HI  lv_color_hex(0x111A2E)
#define UI_COL_BORDER    lv_color_hex(0x1A2540)
#define UI_COL_ACCENT    lv_color_hex(0x00F0C8)   /* mint neon  */
#define UI_COL_ACCENT_2  lv_color_hex(0x3A8DFF)   /* electric   */
#define UI_COL_TEXT      lv_color_hex(0xE6F5FF)
#define UI_COL_MUTED     lv_color_hex(0x6E7EA0)
#define UI_COL_DANGER    lv_color_hex(0xFF4D6D)

void ui_theme_apply(lv_obj_t *root);
void ui_theme_style_panel(lv_obj_t *obj);
void ui_theme_style_accent_chip(lv_obj_t *obj);
