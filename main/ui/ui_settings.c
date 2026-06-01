#include "ui_internal.h"
#include "ui_theme.h"

#include <stdio.h>

/* Stub settings screen: shows current values + a hint to use the web portal.
 * Future steps: on-screen keyboard for callsign/locator/SSID and a "Save" action
 * that writes NVS and re-applies WiFi. */
lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 10, 0);

    lv_obj_t *card = lv_obj_create(scr);
    ui_theme_style_panel(card);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(card, 8, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "STATION");
    lv_obj_set_style_text_color(title, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    char buf[96];
    snprintf(buf, sizeof(buf), "Callsign  %s",
             cfg->callsign[0] ? cfg->callsign : "—");
    lv_obj_t *l1 = lv_label_create(card);
    lv_label_set_text(l1, buf);

    snprintf(buf, sizeof(buf), "Locator   %s",
             cfg->locator[0] ? cfg->locator : "—");
    lv_obj_t *l2 = lv_label_create(card);
    lv_label_set_text(l2, buf);

    snprintf(buf, sizeof(buf), "WiFi      %s",
             cfg->wifi_ssid[0] ? cfg->wifi_ssid : "—");
    lv_obj_t *l3 = lv_label_create(card);
    lv_label_set_text(l3, buf);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Edit via web portal: connect to the device's AP\n"
        "when it appears on boot, then visit 192.168.4.1");
    lv_obj_set_style_text_color(hint, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));

    return scr;
}
