#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"
#include "app_propagation.h"
#include "bsp.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define HF_ROWS_PER_COL  4
#define VHF_MAX_ROWS     8

static const char *TAG = "ui_prop";

typedef struct {
    lv_obj_t *row;
    lv_obj_t *name;
    lv_obj_t *status;
} prop_row_t;

static lv_obj_t *s_lbl_sfi;
static lv_obj_t *s_lbl_ssn;
static lv_obj_t *s_lbl_a;
static lv_obj_t *s_lbl_k;
static lv_obj_t *s_lbl_xray;
static lv_obj_t *s_lbl_geomag;
static lv_obj_t *s_lbl_updated;
static prop_row_t s_hf_day[HF_ROWS_PER_COL];
static prop_row_t s_hf_night[HF_ROWS_PER_COL];
static prop_row_t s_vhf[VHF_MAX_ROWS];

static app_prop_data_t s_snap_buf;

static lv_color_t condition_color(const char *cond)
{
    if (strstr(cond, "Good")) return lv_color_hex(0x00d97e);
    if (strstr(cond, "Fair")) return lv_color_hex(0xffb020);
    if (strstr(cond, "Poor")) return lv_color_hex(0xff4d6d);
    return UI_COL_MUTED;
}

static bool contains_ci(const char *hay, const char *needle)
{
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

static void refresh(const app_prop_data_t *d)
{
    if (!d || !d->valid) return;
    char buf[64];

    snprintf(buf, sizeof(buf), "%d", d->solar_flux);
    lv_label_set_text(s_lbl_sfi, buf);
    snprintf(buf, sizeof(buf), "%d", d->sunspots);
    lv_label_set_text(s_lbl_ssn, buf);
    snprintf(buf, sizeof(buf), "%d", d->a_index);
    lv_label_set_text(s_lbl_a, buf);
    snprintf(buf, sizeof(buf), "%d", d->k_index);
    lv_label_set_text(s_lbl_k, buf);

    lv_label_set_text(s_lbl_xray,   d->xray[0]   ? d->xray   : "—");
    lv_label_set_text(s_lbl_geomag, d->geomag[0] ? d->geomag : "—");

    snprintf(buf, sizeof(buf), "Updated %s", d->updated);
    lv_label_set_text(s_lbl_updated, buf);

    int day_idx = 0, night_idx = 0;
    for (int i = 0; i < d->band_count; i++) {
        const app_prop_band_t *b = &d->bands[i];
        prop_row_t *r = NULL;
        if (strcmp(b->time, "day") == 0 && day_idx < HF_ROWS_PER_COL) {
            r = &s_hf_day[day_idx++];
        } else if (strcmp(b->time, "night") == 0 && night_idx < HF_ROWS_PER_COL) {
            r = &s_hf_night[night_idx++];
        }
        if (!r) continue;
        lv_label_set_text(r->name, b->band);
        lv_label_set_text(r->status, band_status_text(b->condition));
        lv_obj_set_style_text_color(r->status, condition_color(b->condition), 0);
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = day_idx;   i < HF_ROWS_PER_COL; i++) lv_obj_add_flag(s_hf_day[i].row,   LV_OBJ_FLAG_HIDDEN);
    for (int i = night_idx; i < HF_ROWS_PER_COL; i++) lv_obj_add_flag(s_hf_night[i].row, LV_OBJ_FLAG_HIDDEN);

    int vhf_n = d->vhf_count < VHF_MAX_ROWS ? d->vhf_count : VHF_MAX_ROWS;
    for (int i = 0; i < vhf_n; i++) {
        const app_prop_vhf_t *v = &d->vhf[i];
        prop_row_t *r = &s_vhf[i];
        char left[48];
        if (v->location[0]) {
            snprintf(left, sizeof(left), "%s (%s)", v->name, v->location);
        } else {
            snprintf(left, sizeof(left), "%s", v->name);
        }
        lv_label_set_text(r->name, left);
        lv_label_set_text(r->status, v->status);
        lv_obj_set_style_text_color(r->status, vhf_status_color(v->status), 0);
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = vhf_n; i < VHF_MAX_ROWS; i++) {
        lv_obj_add_flag(s_vhf[i].row, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_propagation_on_update(const app_prop_data_t *data)
{
    if (!s_lbl_sfi) return;
    if (!bsp_lvgl_lock(200)) return;
    refresh(data);
    bsp_lvgl_unlock();
}

/* Helpers used during one-shot create. Keep them in a single small
 * function so we can log progress and bail cleanly. */
static lv_obj_t *card_with_value(lv_obj_t *parent, const char *label,
                                 lv_obj_t **value_lbl)
{
    lv_obj_t *card = lv_obj_create(parent);
    if (!card) return NULL;
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

static void build_row(lv_obj_t *parent, prop_row_t *r, bool wide)
{
    r->row = lv_obj_create(parent);
    if (!r->row) return;
    ui_theme_style_panel(r->row);
    lv_obj_set_size(r->row, LV_PCT(100), wide ? LV_SIZE_CONTENT : 40);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(r->row, 10, 0);
    lv_obj_set_style_pad_ver(r->row, 4, 0);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);

    r->name = lv_label_create(r->row);
    lv_obj_set_style_text_color(r->name, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(r->name, &lv_font_montserrat_14, 0);
    if (wide) {
        lv_label_set_long_mode(r->name, LV_LABEL_LONG_CLIP);
        lv_obj_set_flex_grow(r->name, 1);
    }

    r->status = lv_label_create(r->row);
    lv_obj_set_style_text_font(r->status, &lv_font_montserrat_14, 0);
}

lv_obj_t *ui_propagation_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;
    ESP_LOGI(TAG, "create start");

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 10, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);

    /* Solar metrics row */
    lv_obj_t *mrow = lv_obj_create(scr);
    lv_obj_remove_style_all(mrow);
    lv_obj_set_width(mrow, LV_PCT(100));
    lv_obj_set_height(mrow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mrow, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    card_with_value(mrow, "SFI", &s_lbl_sfi);
    card_with_value(mrow, "SSN", &s_lbl_ssn);
    card_with_value(mrow, "A",   &s_lbl_a);
    card_with_value(mrow, "K",   &s_lbl_k);
    ESP_LOGI(TAG, "metric cards built");

    /* X-ray + geomag */
    lv_obj_t *info = lv_obj_create(scr);
    ui_theme_style_panel(info);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(info, 10, 0);
    lv_obj_set_style_pad_ver(info, 6, 0);

    lv_obj_t *xl = lv_label_create(info);
    lv_label_set_text(xl, "X-RAY");
    lv_obj_set_style_text_color(xl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(xl, &lv_font_montserrat_14, 0);
    s_lbl_xray = lv_label_create(info);
    lv_label_set_text(s_lbl_xray, "—");
    lv_obj_set_style_text_color(s_lbl_xray, UI_COL_TEXT, 0);

    lv_obj_t *gl = lv_label_create(info);
    lv_label_set_text(gl, "GEOMAG");
    lv_obj_set_style_text_color(gl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_14, 0);
    s_lbl_geomag = lv_label_create(info);
    lv_label_set_text(s_lbl_geomag, "—");
    lv_obj_set_style_text_color(s_lbl_geomag, UI_COL_TEXT, 0);
    ESP_LOGI(TAG, "info panel built");

    /* HF section: two columns side-by-side, DAY on left, NIGHT on right.
     * Each column has a coloured header and HF_ROWS_PER_COL pre-built
     * (hidden) row widgets that refresh() populates in place. */
    lv_obj_t *hf_hdr = lv_label_create(scr);
    lv_label_set_text(hf_hdr, "HF BANDS");
    lv_obj_set_style_text_color(hf_hdr, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(hf_hdr, &lv_font_montserrat_14, 0);

    lv_obj_t *hf_grid = lv_obj_create(scr);
    lv_obj_remove_style_all(hf_grid);
    lv_obj_set_width(hf_grid, LV_PCT(100));
    lv_obj_set_height(hf_grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hf_grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hf_grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(hf_grid, 8, 0);

    lv_obj_t *day_col = lv_obj_create(hf_grid);
    lv_obj_remove_style_all(day_col);
    lv_obj_set_flex_grow(day_col, 1);
    lv_obj_set_height(day_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(day_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(day_col, 6, 0);
    lv_obj_t *day_hdr = lv_label_create(day_col);
    lv_label_set_text(day_hdr, "DAY");
    lv_obj_set_style_text_color(day_hdr, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(day_hdr, &lv_font_montserrat_14, 0);
    for (int i = 0; i < HF_ROWS_PER_COL; i++) {
        build_row(day_col, &s_hf_day[i], false);
    }

    lv_obj_t *night_col = lv_obj_create(hf_grid);
    lv_obj_remove_style_all(night_col);
    lv_obj_set_flex_grow(night_col, 1);
    lv_obj_set_height(night_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(night_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(night_col, 6, 0);
    lv_obj_t *night_hdr = lv_label_create(night_col);
    lv_label_set_text(night_hdr, "NIGHT");
    lv_obj_set_style_text_color(night_hdr, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(night_hdr, &lv_font_montserrat_14, 0);
    for (int i = 0; i < HF_ROWS_PER_COL; i++) {
        build_row(night_col, &s_hf_night[i], false);
    }
    ESP_LOGI(TAG, "HF rows built");

    /* VHF section */
    lv_obj_t *vhf_hdr = lv_label_create(scr);
    lv_label_set_text(vhf_hdr, "VHF / AURORA / E-SKIP");
    lv_obj_set_style_text_color(vhf_hdr, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(vhf_hdr, &lv_font_montserrat_14, 0);

    for (int i = 0; i < VHF_MAX_ROWS; i++) {
        build_row(scr, &s_vhf[i], true);
    }
    ESP_LOGI(TAG, "VHF rows built");

    s_lbl_updated = lv_label_create(scr);
    lv_label_set_text(s_lbl_updated, "Waiting for data…");
    lv_obj_set_style_text_color(s_lbl_updated, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_updated, &lv_font_montserrat_14, 0);

    app_propagation_get(&s_snap_buf);
    if (s_snap_buf.valid) refresh(&s_snap_buf);

    ESP_LOGI(TAG, "create done");
    return scr;
}
