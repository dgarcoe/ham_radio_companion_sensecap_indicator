#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"
#include "app_nvs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Editable settings screen.
 *
 * Each card holds labeled lv_textareas; a single lv_keyboard floats over
 * the bottom of the tab area (sibling of the scrollable form, not a
 * child of it, so scrolling the form doesn't move the kb). Focusing a
 * TA shows the kb in TEXT_LOWER mode - or NUMBER mode for the DX port
 * field. Tapping the kb's check / X hides it. "Save" reads every TA
 * back into an app_config_t and fires the registered callback, which
 * main.c persists to NVS + applies live. */

static ui_settings_saved_cb_t s_saved_cb;
static app_config_t s_local_cfg;

static lv_obj_t *s_kb;
static lv_obj_t *s_ta_call, *s_ta_loc, *s_ta_tz;
static lv_obj_t *s_ta_ssid, *s_ta_psk;
static lv_obj_t *s_ta_dxhost, *s_ta_dxport;
static lv_obj_t *s_ta_alert;
static lv_obj_t *s_sw_alert_en;
static lv_obj_t *s_form;   /* the scrollable column - we pad it when kb shows */

#define KB_HEIGHT_PCT  45

static void show_kb_for(lv_obj_t *ta)
{
    bool numeric = (ta == s_ta_dxport);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_keyboard_set_mode(s_kb, numeric ? LV_KEYBOARD_MODE_NUMBER
                                       : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kb);
    /* Reserve scroll room below the focused field so the kb doesn't
     * cover the TA the user just tapped. */
    lv_obj_set_style_pad_bottom(s_form, LV_PCT(KB_HEIGHT_PCT) + 16, 0);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}

static void hide_kb(void)
{
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, NULL);
    lv_obj_set_style_pad_bottom(s_form, 8, 0);
}

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) {
        show_kb_for(lv_event_get_target_obj(e));
    }
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) {
        hide_kb();
    }
}

static void copy_ta(lv_obj_t *ta, char *dst, size_t sz)
{
    const char *t = lv_textarea_get_text(ta);
    if (!t) t = "";
    strncpy(dst, t, sz - 1);
    dst[sz - 1] = '\0';
}

static void save_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!s_saved_cb) return;

    app_config_t nc = s_local_cfg;
    copy_ta(s_ta_call,   nc.callsign,   sizeof(nc.callsign));
    copy_ta(s_ta_loc,    nc.locator,    sizeof(nc.locator));
    copy_ta(s_ta_tz,     nc.tz,         sizeof(nc.tz));
    copy_ta(s_ta_ssid,   nc.wifi_ssid,  sizeof(nc.wifi_ssid));

    /* PSK left blank means "keep current" so users don't have to retype
     * it just to change unrelated fields. */
    const char *psk = lv_textarea_get_text(s_ta_psk);
    if (psk && psk[0]) {
        strncpy(nc.wifi_psk, psk, sizeof(nc.wifi_psk) - 1);
        nc.wifi_psk[sizeof(nc.wifi_psk) - 1] = '\0';
    }

    copy_ta(s_ta_dxhost, nc.dx_host,    sizeof(nc.dx_host));
    int port = atoi(lv_textarea_get_text(s_ta_dxport));
    if (port > 0 && port < 65536) nc.dx_port = port;

    copy_ta(s_ta_alert,  nc.alert_list, sizeof(nc.alert_list));
    nc.alert_enabled = lv_obj_has_state(s_sw_alert_en, LV_STATE_CHECKED);
    nc.configured = (nc.callsign[0] != '\0' && nc.wifi_ssid[0] != '\0');

    hide_kb();
    s_saved_cb(&nc);
    s_local_cfg = nc;
}

static lv_obj_t *card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *c = lv_obj_create(parent);
    ui_theme_style_panel(c);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(c, 6, 0);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    return c;
}

static lv_obj_t *add_field(lv_obj_t *parent, const char *label, const char *value,
                           uint32_t maxlen, bool password)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, maxlen);
    if (password) lv_textarea_set_password_mode(ta, true);
    if (value && value[0]) lv_textarea_set_text(ta, value);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_CLICKED, NULL);
    return ta;
}

lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg)
{
    s_local_cfg = *cfg;

    /* Scrollable form (returned to the tab). */
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 10, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    s_form = scr;

    /* STATION */
    lv_obj_t *st = card(scr, "STATION");
    s_ta_call = add_field(st, "Callsign", cfg->callsign, APP_CALLSIGN_MAX - 1, false);
    s_ta_loc  = add_field(st, "Locator",  cfg->locator,  APP_LOCATOR_MAX  - 1, false);
    s_ta_tz   = add_field(st, "Timezone (POSIX)", cfg->tz, APP_TZ_MAX - 1, false);

    /* WIFI */
    lv_obj_t *wif = card(scr, "WIFI");
    s_ta_ssid = add_field(wif, "SSID", cfg->wifi_ssid, APP_SSID_MAX - 1, false);
    s_ta_psk  = add_field(wif, "Password (blank = keep)", "", APP_PSK_MAX - 1, true);

    /* DX CLUSTER */
    lv_obj_t *cl = card(scr, "DX CLUSTER");
    s_ta_dxhost = add_field(cl, "Host", cfg->dx_host, APP_DX_HOST_MAX - 1, false);
    char portbuf[8];
    snprintf(portbuf, sizeof(portbuf), "%d", cfg->dx_port);
    s_ta_dxport = add_field(cl, "Port", portbuf, 5, false);

    /* ALERTS */
    lv_obj_t *al = card(scr, "ALERTS");
    lv_obj_t *row = lv_obj_create(al);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *enbl = lv_label_create(row);
    lv_label_set_text(enbl, "Enabled");
    s_sw_alert_en = lv_switch_create(row);
    if (cfg->alert_enabled) lv_obj_add_state(s_sw_alert_en, LV_STATE_CHECKED);
    s_ta_alert = add_field(al, "Watchlist (comma-separated)",
                           cfg->alert_list, APP_ALERT_LIST_MAX - 1, false);

    /* Save */
    lv_obj_t *save = lv_button_create(scr);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_add_event_cb(save, save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(save);
    lv_label_set_text(sl, "Save");
    lv_obj_center(sl);

    /* Keyboard - sibling of the form so scrolling the form doesn't move
     * it. Hidden until a textarea is focused. */
    s_kb = lv_keyboard_create(parent);
    lv_obj_set_size(s_kb, LV_PCT(100), LV_PCT(KB_HEIGHT_PCT));
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_CANCEL, NULL);

    return scr;
}

void ui_settings_set_saved_cb(ui_settings_saved_cb_t cb)
{
    s_saved_cb = cb;
}
