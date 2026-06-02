#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"

#include <string.h>

static lv_obj_t *s_ta_callsign;
static lv_obj_t *s_ta_locator;
static lv_obj_t *s_ta_tz;
static lv_obj_t *s_kb;
static lv_obj_t *s_lbl_status;

static ui_settings_saved_cb_t s_saved_cb;
static app_config_t s_local;

void ui_settings_set_saved_cb(ui_settings_saved_cb_t cb)
{
    s_saved_cb = cb;
}

static void on_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
}

static void on_kb_done(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, NULL);
}

static void on_save_clicked(lv_event_t *e)
{
    (void)e;
    strncpy(s_local.callsign, lv_textarea_get_text(s_ta_callsign), sizeof(s_local.callsign) - 1);
    s_local.callsign[sizeof(s_local.callsign) - 1] = '\0';
    strncpy(s_local.locator,  lv_textarea_get_text(s_ta_locator),  sizeof(s_local.locator) - 1);
    s_local.locator[sizeof(s_local.locator) - 1] = '\0';
    strncpy(s_local.tz,       lv_textarea_get_text(s_ta_tz),       sizeof(s_local.tz) - 1);
    s_local.tz[sizeof(s_local.tz) - 1] = '\0';
    s_local.configured = (s_local.callsign[0] != '\0' && s_local.wifi_ssid[0] != '\0');

    if (s_saved_cb) s_saved_cb(&s_local);
    lv_label_set_text(s_lbl_status, "Saved.");
    lv_obj_set_style_text_color(s_lbl_status, UI_COL_ACCENT, 0);
}

static lv_obj_t *make_field(lv_obj_t *parent, const char *label,
                            const char *initial, int max_len,
                            lv_obj_t **out_ta)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(box, 4, 0);

    lv_obj_t *l = lv_label_create(box);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *ta = lv_textarea_create(box);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_text(ta, initial ? initial : "");
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_style_bg_color(ta, UI_COL_PANEL, 0);
    lv_obj_set_style_text_color(ta, UI_COL_TEXT, 0);
    lv_obj_set_style_border_color(ta, UI_COL_BORDER, 0);
    lv_obj_set_style_border_color(ta, UI_COL_ACCENT, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta, on_focus, LV_EVENT_FOCUSED, NULL);
    *out_ta = ta;
    return box;
}

lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg)
{
    s_local = *cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 12, 0);
    lv_obj_set_style_pad_gap(scr, 10, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);

    /* Fields */
    make_field(scr, "Callsign", s_local.callsign, APP_CALLSIGN_MAX - 1, &s_ta_callsign);
    make_field(scr, "Grid locator", s_local.locator, APP_LOCATOR_MAX - 1, &s_ta_locator);
    make_field(scr, "Timezone (POSIX TZ)", s_local.tz, APP_TZ_MAX - 1, &s_ta_tz);

    /* Save button */
    lv_obj_t *save = lv_button_create(scr);
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_set_height(save, 48);
    lv_obj_set_style_bg_color(save, UI_COL_ACCENT, 0);
    lv_obj_set_style_radius(save, 12, 0);
    lv_obj_add_event_cb(save, on_save_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, "SAVE");
    lv_obj_set_style_text_color(save_lbl, lv_color_hex(0x001018), 0);
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(save_lbl);

    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, "WiFi credentials: use the AP portal at first boot.");
    lv_obj_set_style_text_color(s_lbl_status, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_status, LV_PCT(100));

    /* Keyboard - sibling of the field list so it can overlay the bottom
     * of the screen without being constrained by the column flex. */
    s_kb = lv_keyboard_create(scr);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_kb, LV_PCT(100), 200);
    lv_obj_add_event_cb(s_kb, on_kb_done, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb, on_kb_done, LV_EVENT_CANCEL, NULL);

    return scr;
}
