#include "ui_screens.h"
#include "ui_internal.h"
#include "ui_theme.h"

const ui_screen_def_t ui_screens[] = {
    { "DX Cluster",  LV_SYMBOL_LIST,     ui_dx_create },
    { "POTA",        LV_SYMBOL_GPS,      ui_pota_create },
    { "SOTA",        LV_SYMBOL_UP,       ui_sota_create },
    { "Propagation", LV_SYMBOL_CHARGE,   ui_propagation_create },
    { "Settings",    LV_SYMBOL_SETTINGS, ui_settings_create },
};
const size_t ui_screen_count = sizeof(ui_screens) / sizeof(ui_screens[0]);

lv_obj_t *ui_stub_create(lv_obj_t *parent, const char *title, const char *body)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(scr, 12, 0);
    lv_obj_set_style_pad_all(scr, 24, 0);

    lv_obj_t *chip = lv_label_create(scr);
    lv_label_set_text(chip, "COMING SOON");
    ui_theme_style_accent_chip(chip);
    lv_obj_set_style_text_font(chip, &lv_font_montserrat_14, 0);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t, UI_COL_TEXT, 0);

    lv_obj_t *b = lv_label_create(scr);
    lv_label_set_text(b, body);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(b, UI_COL_MUTED, 0);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, LV_PCT(80));
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);

    return scr;
}
