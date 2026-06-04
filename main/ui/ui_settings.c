#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* Editable settings (textareas + on-screen keyboard) was the single
 * heaviest screen in the app - the LVGL keyboard alone is a 60+ button
 * matrix and the textareas pull in heavy text-input state. Stacking
 * that with DX list, propagation panels, menu tiles and the watch was
 * pushing internal RAM over the edge during build. For now this screen
 * is read-only; configuration is done through the WiFi-portal page that
 * already runs on first boot. Editable-on-device can come back once
 * we route LVGL allocations to PSRAM. */
lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 10, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);

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
    lv_label_set_text(lv_label_create(card), buf);

    snprintf(buf, sizeof(buf), "Locator   %s",
             cfg->locator[0] ? cfg->locator : "—");
    lv_label_set_text(lv_label_create(card), buf);

    snprintf(buf, sizeof(buf), "WiFi      %s",
             cfg->wifi_ssid[0] ? cfg->wifi_ssid : "—");
    lv_label_set_text(lv_label_create(card), buf);

    snprintf(buf, sizeof(buf), "Timezone  %s",
             cfg->tz[0] ? cfg->tz : "—");
    lv_label_set_text(lv_label_create(card), buf);

    lv_obj_t *cluster = lv_obj_create(scr);
    ui_theme_style_panel(cluster);
    lv_obj_set_size(cluster, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cluster, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(cluster, 8, 0);

    lv_obj_t *ct = lv_label_create(cluster);
    lv_label_set_text(ct, "DX CLUSTER");
    lv_obj_set_style_text_color(ct, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(ct, &lv_font_montserrat_14, 0);

    snprintf(buf, sizeof(buf), "Host  %s",
             cfg->dx_host[0] ? cfg->dx_host : "—");
    lv_label_set_text(lv_label_create(cluster), buf);

    snprintf(buf, sizeof(buf), "Port  %d", cfg->dx_port);
    lv_label_set_text(lv_label_create(cluster), buf);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Editable on-device settings coming back once LVGL is on PSRAM.\n"
        "Until then, edit via the WiFi AP portal at first boot\n"
        "(http://192.168.4.1/).");
    lv_obj_set_style_text_color(hint, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));

    return scr;
}

/* No-op so main.c keeps building even though there is nothing to save. */
void ui_settings_set_saved_cb(ui_settings_saved_cb_t cb)
{
    (void)cb;
}
