#include "ui_theme.h"

void ui_theme_apply(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root, UI_COL_TEXT, 0);
}

void ui_theme_style_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
    /* Shadows look nice on a single hero card but they need a
     * CPU-accessible corner buffer (~hundreds of bytes each) that
     * doesn't live in PSRAM. With 30+ panels on the propagation/DX
     * screens we exhausted internal RAM and crashed in lv_memset on a
     * NULL allocation. The neon-accent border carries the look. */
}

void ui_theme_style_accent_chip(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, UI_COL_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, UI_COL_ACCENT, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 999, 0);
    lv_obj_set_style_text_color(obj, UI_COL_ACCENT, 0);
    lv_obj_set_style_pad_hor(obj, 12, 0);
    lv_obj_set_style_pad_ver(obj, 6, 0);
}
