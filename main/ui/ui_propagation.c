#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"
#include "app_propagation.h"
#include "bsp.h"

#include <stdio.h>
#include <string.h>

/* Cached pointers so the background refresh can update labels in place. */
static lv_obj_t *s_lbl_sfi;
static lv_obj_t *s_lbl_ssn;
static lv_obj_t *s_lbl_a;
static lv_obj_t *s_lbl_k;
static lv_obj_t *s_lbl_xray;
static lv_obj_t *s_lbl_geomag;
static lv_obj_t *s_lbl_updated;
static lv_obj_t *s_band_grid;

static lv_color_t condition_color(const char *cond)
{
    if (strstr(cond, "Good")) return lv_color_hex(0x00d97e);
    if (strstr(cond, "Fair")) return lv_color_hex(0xffb020);
    if (strstr(cond, "Poor")) return lv_color_hex(0xff4d6d);
    return UI_COL_MUTED;
}

static lv_obj_t *make_metric_card(lv_obj_t *parent,
                                  const char *label, lv_obj_t **value_lbl)
{
    lv_obj_t *card = lv_obj_create(parent);
    ui_theme_style_panel(card);
    lv_obj_set_size(card, 100, 80);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_gap(card, 2, 0);

    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "—");
    lv_obj_set_style_text_color(v, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_24, 0);
    *value_lbl = v;
    return card;
}

static void refresh(const app_prop_data_t *d)
{
    if (!d || !d->valid) return;
    char buf[32];

    snprintf(buf, sizeof(buf), "%d", d->solar_flux);
    lv_label_set_text(s_lbl_sfi, buf);
    snprintf(buf, sizeof(buf), "%d", d->sunspots);
    lv_label_set_text(s_lbl_ssn, buf);
    snprintf(buf, sizeof(buf), "%d", d->a_index);
    lv_label_set_text(s_lbl_a, buf);
    snprintf(buf, sizeof(buf), "%d", d->k_index);
    lv_label_set_text(s_lbl_k, buf);

    lv_label_set_text(s_lbl_xray, d->xray[0] ? d->xray : "—");
    lv_label_set_text(s_lbl_geomag, d->geomag[0] ? d->geomag : "—");

    snprintf(buf, sizeof(buf), "Updated %s", d->updated);
    lv_label_set_text(s_lbl_updated, buf);

    /* Rebuild the band grid from scratch each refresh. */
    lv_obj_clean(s_band_grid);
    for (int i = 0; i < d->band_count; i++) {
        const app_prop_band_t *b = &d->bands[i];

        lv_obj_t *row = lv_obj_create(s_band_grid);
        ui_theme_style_panel(row);
        lv_obj_set_size(row, LV_PCT(48), 44);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);

        char left[24];
        snprintf(left, sizeof(left), "%s · %s", b->band, b->time);
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, left);
        lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

        lv_obj_t *c = lv_label_create(row);
        lv_label_set_text(c, b->condition);
        lv_obj_set_style_text_color(c, condition_color(b->condition), 0);
        lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
    }
}

void ui_propagation_on_update(const app_prop_data_t *data)
{
    if (!s_lbl_sfi) return;            /* screen not built yet */
    if (!bsp_lvgl_lock(200)) return;
    refresh(data);
    bsp_lvgl_unlock();
}

lv_obj_t *ui_propagation_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 10, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);

    /* Solar metrics row */
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_metric_card(row, "SFI",  &s_lbl_sfi);
    make_metric_card(row, "SSN",  &s_lbl_ssn);
    make_metric_card(row, "A",    &s_lbl_a);
    make_metric_card(row, "K",    &s_lbl_k);

    /* X-ray + geomag line */
    lv_obj_t *info = lv_obj_create(scr);
    ui_theme_style_panel(info);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(info, 10, 0);
    lv_obj_set_style_pad_ver(info, 6, 0);

    lv_obj_t *l_xray_lbl = lv_label_create(info);
    lv_label_set_text(l_xray_lbl, "X-RAY");
    lv_obj_set_style_text_color(l_xray_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l_xray_lbl, &lv_font_montserrat_14, 0);
    s_lbl_xray = lv_label_create(info);
    lv_label_set_text(s_lbl_xray, "—");
    lv_obj_set_style_text_color(s_lbl_xray, UI_COL_TEXT, 0);

    lv_obj_t *l_geo_lbl = lv_label_create(info);
    lv_label_set_text(l_geo_lbl, "GEOMAG");
    lv_obj_set_style_text_color(l_geo_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l_geo_lbl, &lv_font_montserrat_14, 0);
    s_lbl_geomag = lv_label_create(info);
    lv_label_set_text(s_lbl_geomag, "—");
    lv_obj_set_style_text_color(s_lbl_geomag, UI_COL_TEXT, 0);

    /* Band grid (two columns) */
    s_band_grid = lv_obj_create(scr);
    lv_obj_remove_style_all(s_band_grid);
    lv_obj_set_width(s_band_grid, LV_PCT(100));
    lv_obj_set_flex_grow(s_band_grid, 1);
    lv_obj_set_flex_flow(s_band_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_band_grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(s_band_grid, 8, 0);
    lv_obj_set_scroll_dir(s_band_grid, LV_DIR_VER);

    /* Footer with last-updated stamp */
    s_lbl_updated = lv_label_create(scr);
    lv_label_set_text(s_lbl_updated, "Waiting for data…");
    lv_obj_set_style_text_color(s_lbl_updated, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_updated, &lv_font_montserrat_14, 0);

    /* If data already arrived before the UI was built, paint it now. */
    app_prop_data_t snap;
    app_propagation_get(&snap);
    if (snap.valid) refresh(&snap);

    return scr;
}
