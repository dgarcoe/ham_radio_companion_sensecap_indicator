#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"

#include <stdint.h>

static void on_tile_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_show_feature(idx);
}

lv_obj_t *ui_menu_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(scr, 14, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);

    for (size_t i = 0; i < ui_screen_count; i++) {
        lv_obj_t *tile = lv_btn_create(scr);
        ui_theme_style_panel(tile);
        lv_obj_set_size(tile, 130, 130);
        lv_obj_set_style_pad_all(tile, 10, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(tile, 10, 0);
        lv_obj_set_style_bg_color(tile, UI_COL_PANEL, 0);
        lv_obj_set_style_bg_color(tile, UI_COL_PANEL_HI, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(tile, UI_COL_ACCENT, LV_STATE_PRESSED);

        lv_obj_t *icon = lv_label_create(tile);
        lv_label_set_text(icon, ui_screens[i].icon);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(icon, UI_COL_ACCENT, 0);

        lv_obj_t *lbl = lv_label_create(tile);
        lv_label_set_text(lbl, ui_screens[i].title);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, UI_COL_TEXT, 0);

        lv_obj_add_event_cb(tile, on_tile_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    return scr;
}
