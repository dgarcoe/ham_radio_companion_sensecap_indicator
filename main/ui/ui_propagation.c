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
static lv_obj_t *s_vhf_grid;

static lv_color_t condition_color(const char *cond)
{
    if (strstr(cond, "Good")) return lv_color_hex(0x00d97e);
    if (strstr(cond, "Fair")) return lv_color_hex(0xffb020);
    if (strstr(cond, "Poor")) return lv_color_hex(0xff4d6d);
    return UI_COL_MUTED;
}

static bool contains_ci(const char *hay, const char *needle)
{
    /* strcasestr is non-portable; do a simple case-insensitive substring search. */
    size_t nl = strlen(needle);
    if (nl == 0) return true;
    for (const char *p = hay; *p; p++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a == 0 || a != b) break;
        }
        if (i == nl) return true;
    }
    return false;
}

static lv_color_t vhf_status_color(const char *status)
{
    if (contains_ci(status, "open"))   return lv_color_hex(0x00d97e);
    if (contains_ci(status, "likely")) return lv_color_hex(0xffb020);
    if (contains_ci(status, "closed")) return lv_color_hex(0xff4d6d);
    return UI_COL_MUTED;
}

static const char *band_status_text(const char *cond)
{
    if (strstr(cond, "Good")) return "OPEN";
    if (strstr(cond, "Fair")) return "FAIR";
    if (strstr(cond, "Poor")) return "CLOSED";
    return cond[0] ? cond : "—";
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

static lv_obj_t *make_band_row(lv_obj_t *parent, const char *band, const char *condition)
{
    lv_obj_t *row = lv_obj_create(parent);
    ui_theme_style_panel(row);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, band);
    lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *c = lv_label_create(row);
    lv_label_set_text(c, band_status_text(condition));
    lv_obj_set_style_text_color(c, condition_color(condition), 0);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
    return row;
}

static lv_obj_t *make_band_column(lv_obj_t *parent, const char *title)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(col, 6, 0);

    lv_obj_t *hdr = lv_label_create(col);
    lv_label_set_text(hdr, title);
    lv_obj_set_style_text_color(hdr, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    return col;
}

static void refresh(const app_prop_data_t *d)
{
    if (!d || !d->valid) return;
    char buf[64];   /* "Updated %s" where %s is up to 40 chars */

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

    /* Rebuild HF section as two columns: DAY on left, NIGHT on right. */
    lv_obj_clean(s_band_grid);
    lv_obj_t *day_col   = make_band_column(s_band_grid, "DAY");
    lv_obj_t *night_col = make_band_column(s_band_grid, "NIGHT");

    for (int i = 0; i < d->band_count; i++) {
        const app_prop_band_t *b = &d->bands[i];
        if (strcmp(b->time, "day") == 0) {
            make_band_row(day_col, b->band, b->condition);
        } else if (strcmp(b->time, "night") == 0) {
            make_band_row(night_col, b->band, b->condition);
        }
    }

    /* VHF / E-skip / Aurora block. Single-column full-width rows. */
    lv_obj_clean(s_vhf_grid);
    for (int i = 0; i < d->vhf_count; i++) {
        const app_prop_vhf_t *v = &d->vhf[i];

        lv_obj_t *row = lv_obj_create(s_vhf_grid);
        ui_theme_style_panel(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);

        char left[48];
        if (v->location[0]) {
            snprintf(left, sizeof(left), "%s (%s)", v->name, v->location);
        } else {
            snprintf(left, sizeof(left), "%s", v->name);
        }
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, left);
        lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_flex_grow(l, 1);

        lv_obj_t *c = lv_label_create(row);
        lv_label_set_text(c, v->status);
        lv_obj_set_style_text_color(c, vhf_status_color(v->status), 0);
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

    /* HF band grid header */
    lv_obj_t *hf_hdr = lv_label_create(scr);
    lv_label_set_text(hf_hdr, "HF BANDS");
    lv_obj_set_style_text_color(hf_hdr, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(hf_hdr, &lv_font_montserrat_14, 0);

    /* HF section: two real columns (DAY, NIGHT) side by side, populated
     * during refresh. */
    s_band_grid = lv_obj_create(scr);
    lv_obj_remove_style_all(s_band_grid);
    lv_obj_set_width(s_band_grid, LV_PCT(100));
    lv_obj_set_height(s_band_grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_band_grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_band_grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(s_band_grid, 8, 0);

    /* VHF / E-skip / Aurora header + list */
    lv_obj_t *vhf_hdr = lv_label_create(scr);
    lv_label_set_text(vhf_hdr, "VHF / AURORA / E-SKIP");
    lv_obj_set_style_text_color(vhf_hdr, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(vhf_hdr, &lv_font_montserrat_14, 0);

    s_vhf_grid = lv_obj_create(scr);
    lv_obj_remove_style_all(s_vhf_grid);
    lv_obj_set_width(s_vhf_grid, LV_PCT(100));
    lv_obj_set_height(s_vhf_grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_vhf_grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_vhf_grid, 6, 0);

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
