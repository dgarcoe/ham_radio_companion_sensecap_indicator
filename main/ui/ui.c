#include "ui.h"
#include "ui_internal.h"
#include "ui_theme.h"
#include "bsp.h"

#include <string.h>
#include <stdio.h>

static app_config_t s_cfg;

static lv_obj_t *s_root;
static lv_obj_t *s_status_bar;
static lv_obj_t *s_lbl_callsign;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_sync;
static lv_obj_t *s_content;
static lv_obj_t *s_screen_watch;
static lv_obj_t *s_screen_settings;

static void build_status_bar(lv_obj_t *parent)
{
    s_status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_status_bar);
    lv_obj_set_size(s_status_bar, LV_PCT(100), 40);
    lv_obj_set_style_pad_hor(s_status_bar, 16, 0);
    lv_obj_set_flex_flow(s_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_lbl_callsign = lv_label_create(s_status_bar);
    lv_label_set_text(s_lbl_callsign, s_cfg.callsign[0] ? s_cfg.callsign : "NO CALL");
    lv_obj_set_style_text_color(s_lbl_callsign, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_callsign, &lv_font_montserrat_18, 0);

    lv_obj_t *right = lv_obj_create(s_status_bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(right, 8, 0);

    s_lbl_sync = lv_label_create(right);
    lv_label_set_text(s_lbl_sync, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(s_lbl_sync, UI_COL_MUTED, 0);

    s_lbl_wifi = lv_label_create(right);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, UI_COL_MUTED, 0);
}

void ui_init(const app_config_t *cfg)
{
    s_cfg = *cfg;

    s_root = lv_screen_active();
    ui_theme_apply(s_root);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_STRETCH, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_root, 0, 0);

    build_status_bar(s_root);

    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_style_pad_all(s_content, 12, 0);

    s_screen_watch    = ui_watch_create(s_content, &s_cfg);
    s_screen_settings = ui_settings_create(s_content, &s_cfg);
    lv_obj_add_flag(s_screen_settings, LV_OBJ_FLAG_HIDDEN);

    ui_watch_register_tick(s_screen_watch);
}

void ui_set_wifi(app_wifi_state_t st, const char *ip_or_ap)
{
    if (!bsp_lvgl_lock(100)) return;
    lv_color_t c = UI_COL_MUTED;
    const char *sym = LV_SYMBOL_WIFI;
    switch (st) {
        case APP_WIFI_CONNECTED:  c = UI_COL_ACCENT;  break;
        case APP_WIFI_CONNECTING: c = UI_COL_ACCENT_2; break;
        case APP_WIFI_AP_PORTAL:  c = UI_COL_DANGER; sym = LV_SYMBOL_SETTINGS; break;
        default: break;
    }
    lv_label_set_text(s_lbl_wifi, sym);
    lv_obj_set_style_text_color(s_lbl_wifi, c, 0);
    (void)ip_or_ap;
    bsp_lvgl_unlock();
}

void ui_set_time_synced(bool synced)
{
    if (!bsp_lvgl_lock(100)) return;
    lv_obj_set_style_text_color(s_lbl_sync, synced ? UI_COL_ACCENT : UI_COL_MUTED, 0);
    bsp_lvgl_unlock();
}

void ui_set_callsign(const char *callsign)
{
    if (!bsp_lvgl_lock(100)) return;
    lv_label_set_text(s_lbl_callsign, (callsign && callsign[0]) ? callsign : "NO CALL");
    bsp_lvgl_unlock();
}

void ui_show_watch(void)
{
    lv_obj_add_flag(s_screen_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screen_watch, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_settings(void)
{
    lv_obj_add_flag(s_screen_watch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screen_settings, LV_OBJ_FLAG_HIDDEN);
}
