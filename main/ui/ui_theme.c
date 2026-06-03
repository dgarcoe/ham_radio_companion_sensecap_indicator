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
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
    /* No radius, no shadow: both would allocate per-panel CPU-side mask
     * / corner buffers, which on a screen with 30+ panels exhausted the
     * internal-RAM heap and crashed in lv_malloc->lv_memset. Crisp
     * 1 px borders carry the visual rhythm fine. */
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
