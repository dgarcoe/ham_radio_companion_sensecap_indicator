#include "ui.h"
#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "bsp.h"

#include <string.h>
#include <stdio.h>

static app_config_t s_cfg;

static lv_obj_t *s_root;
static lv_obj_t *s_status_bar;
static lv_obj_t *s_lbl_callsign;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_sync;
static lv_obj_t *s_lbl_nav_icon;

static lv_obj_t *s_content;

/* Screen slots: [0] = watch, [1] = menu, [2..] = features in ui_screens[] order. */
#define SCR_WATCH 0
#define SCR_MENU  1
#define SCR_FEATURE_FIRST 2
static lv_obj_t **s_screens;       /* allocated to 2 + ui_screen_count */
static size_t   s_screen_total;
static size_t   s_current;

static void ui_show_index(size_t idx);

static void on_nav_clicked(lv_event_t *e)
{
    (void)e;
    if (s_current == SCR_WATCH) {
        ui_show_menu();
    } else if (s_current == SCR_MENU) {
        ui_show_watch();
    } else {
        /* Feature → back to menu. */
        ui_show_menu();
    }
}

static void build_status_bar(lv_obj_t *parent)
{
    s_status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_status_bar);
    lv_obj_set_size(s_status_bar, LV_PCT(100), 52);
    lv_obj_set_style_pad_hor(s_status_bar, 16, 0);
    lv_obj_set_flex_flow(s_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Left cluster: callsign + (current screen title when not on watch). */
    lv_obj_t *left = lv_obj_create(s_status_bar);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(left, 10, 0);

    s_lbl_callsign = lv_label_create(left);
    lv_label_set_text(s_lbl_callsign, s_cfg.callsign[0] ? s_cfg.callsign : "NO CALL");
    lv_obj_set_style_text_color(s_lbl_callsign, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_callsign, &lv_font_montserrat_18, 0);

    s_lbl_title = lv_label_create(left);
    lv_label_set_text(s_lbl_title, "");
    lv_obj_set_style_text_color(s_lbl_title, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_14, 0);

    /* Right cluster: sync icon, wifi icon, nav button. */
    lv_obj_t *right = lv_obj_create(s_status_bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(right, 14, 0);

    s_lbl_sync = lv_label_create(right);
    lv_label_set_text(s_lbl_sync, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(s_lbl_sync, UI_COL_MUTED, 0);

    s_lbl_wifi = lv_label_create(right);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, UI_COL_MUTED, 0);

    lv_obj_t *nav = lv_obj_create(right);
    lv_obj_remove_style_all(nav);
    lv_obj_set_size(nav, 44, 44);
    lv_obj_add_flag(nav, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_lbl_nav_icon = lv_label_create(nav);
    lv_label_set_text(s_lbl_nav_icon, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(s_lbl_nav_icon, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_nav_icon, &lv_font_montserrat_24, 0);

    lv_obj_add_event_cb(nav, on_nav_clicked, LV_EVENT_CLICKED, NULL);
}

void ui_init(const app_config_t *cfg)
{
    s_cfg = *cfg;

    s_root = lv_screen_active();
    ui_theme_apply(s_root);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_root, 0, 0);

    build_status_bar(s_root);

    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_style_pad_all(s_content, 8, 0);

    /* Build every screen up front; toggle visibility on navigate. */
    s_screen_total = SCR_FEATURE_FIRST + ui_screen_count;
    s_screens = lv_malloc(sizeof(lv_obj_t *) * s_screen_total);

    s_screens[SCR_WATCH] = ui_watch_create(s_content, &s_cfg);
    s_screens[SCR_MENU]  = ui_menu_create(s_content, &s_cfg);
    for (size_t i = 0; i < ui_screen_count; i++) {
        s_screens[SCR_FEATURE_FIRST + i] = ui_screens[i].create(s_content, &s_cfg);
    }
    for (size_t i = 0; i < s_screen_total; i++) {
        if (i != SCR_WATCH) lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_current = SCR_WATCH;

    ui_watch_register_tick(s_screens[SCR_WATCH]);
}

static void ui_show_index(size_t idx)
{
    if (idx >= s_screen_total) return;
    if (s_current == idx) return;
    lv_obj_add_flag(s_screens[s_current], LV_OBJ_FLAG_HIDDEN);
    s_current = idx;
    lv_obj_clear_flag(s_screens[s_current], LV_OBJ_FLAG_HIDDEN);

    /* Status-bar update. */
    if (idx == SCR_WATCH) {
        lv_label_set_text(s_lbl_nav_icon, LV_SYMBOL_LIST);
        lv_label_set_text(s_lbl_title, "");
    } else {
        lv_label_set_text(s_lbl_nav_icon, LV_SYMBOL_LEFT);
        const char *t = (idx == SCR_MENU)
            ? "Menu"
            : ui_screens[idx - SCR_FEATURE_FIRST].title;
        lv_label_set_text(s_lbl_title, t);
    }
}

void ui_show_watch(void) { ui_show_index(SCR_WATCH); }
void ui_show_menu(void)  { ui_show_index(SCR_MENU); }
void ui_show_feature(int feature_index)
{
    ui_show_index(SCR_FEATURE_FIRST + (size_t)feature_index);
}

/* Status-bar setters (call sites take the LVGL lock for us). */
void ui_set_wifi(app_wifi_state_t st, const char *ip_or_ap)
{
    if (!bsp_lvgl_lock(100)) return;
    lv_color_t c = UI_COL_MUTED;
    const char *sym = LV_SYMBOL_WIFI;
    switch (st) {
        case APP_WIFI_CONNECTED:  c = UI_COL_ACCENT;   break;
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
