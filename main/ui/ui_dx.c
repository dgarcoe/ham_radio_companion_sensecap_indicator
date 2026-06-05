#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "app_dxcluster.h"
#include "bsp.h"

#include <stdatomic.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_DX_ROWS_PER_PAGE 8

typedef struct {
    lv_obj_t *row;
    lv_obj_t *call;
    lv_obj_t *freq;
    lv_obj_t *time;
    lv_obj_t *info;
} dx_row_t;

/* --- Filter definitions --- */

static const char *const BAND_NAMES[] = {
    "ALL", "160m", "80m", "60m", "40m", "30m", "20m",
    "17m", "15m", "12m", "10m", "6m", "2m",
};
#define BAND_COUNT (sizeof(BAND_NAMES) / sizeof(BAND_NAMES[0]))

/* Frequency edges in MHz (inclusive low, inclusive high). Index aligned
 * with BAND_NAMES[1..]: BAND_NAMES[0] = "ALL" so this skips it. */
static const struct { float lo; float hi; } BAND_EDGES[] = {
    /* 160m */ {1.8f,   2.0f},
    /* 80m  */ {3.5f,   4.0f},
    /* 60m  */ {5.0f,   5.5f},
    /* 40m  */ {7.0f,   7.3f},
    /* 30m  */ {10.1f, 10.15f},
    /* 20m  */ {14.0f, 14.35f},
    /* 17m  */ {18.068f, 18.168f},
    /* 15m  */ {21.0f, 21.45f},
    /* 12m  */ {24.89f, 24.99f},
    /* 10m  */ {28.0f, 29.7f},
    /* 6m   */ {50.0f, 54.0f},
    /* 2m   */ {144.0f, 148.0f},
};

static const char *const MODE_NAMES[] = {
    "ALL", "CW", "FT8", "FT4", "SSB", "RTTY", "PSK", "JT9", "DIGI",
};
#define MODE_COUNT (sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0]))

static const char *const AREA_NAMES[] = { "ALL", "MINE" };
#define AREA_COUNT (sizeof(AREA_NAMES) / sizeof(AREA_NAMES[0]))

/* --- State --- */

static lv_obj_t *s_status_lbl;
static lv_obj_t *s_list;
static lv_obj_t *s_empty_lbl;
static lv_obj_t *s_page_lbl;
static lv_obj_t *s_btn_prev_lbl;
static lv_obj_t *s_btn_next_lbl;
static lv_obj_t *s_chip_band_lbl;
static lv_obj_t *s_chip_mode_lbl;
static lv_obj_t *s_chip_area_lbl;
static dx_row_t  s_rows[UI_DX_ROWS_PER_PAGE];

static int s_page;
static int s_filter_band;   /* 0 = ALL, else BAND_NAMES[s_filter_band] */
static int s_filter_mode;
static int s_filter_area;
static char s_my_prefix[8]; /* derived from cfg->callsign */

static app_dx_state_t s_snap_buf;
static atomic_bool s_dirty = ATOMIC_VAR_INIT(true);

/* --- Filter helpers --- */

static int band_index_for_freq_khz(const char *freq_str)
{
    if (!freq_str || !*freq_str) return 0;
    float khz = strtof(freq_str, NULL);
    float mhz = khz / 1000.0f;
    for (size_t i = 0; i < sizeof(BAND_EDGES) / sizeof(BAND_EDGES[0]); i++) {
        if (mhz >= BAND_EDGES[i].lo && mhz <= BAND_EDGES[i].hi) {
            return (int)(i + 1); /* +1 because BAND_NAMES[0] = ALL */
        }
    }
    return 0;
}

static int mode_index_for_comment(const char *comment)
{
    if (!comment || !*comment) return 0;
    /* Case-insensitive substring match against each known mode token. */
    char up[64];
    size_t i;
    for (i = 0; i < sizeof(up) - 1 && comment[i]; i++) {
        up[i] = (char)toupper((unsigned char)comment[i]);
    }
    up[i] = '\0';

    /* Try the more specific tokens first (FT8 before SSB etc). Order
     * matches the MODE_NAMES array minus the leading "ALL". */
    for (size_t m = 1; m < MODE_COUNT; m++) {
        if (strstr(up, MODE_NAMES[m])) return (int)m;
    }
    return 0;
}

static void extract_prefix(const char *call, char *out, size_t out_sz)
{
    out[0] = '\0';
    size_t i = 0;
    for (const char *p = call; *p && i + 1 < out_sz; p++) {
        if (*p >= '0' && *p <= '9') break;
        if (isalpha((unsigned char)*p)) {
            out[i++] = (char)toupper((unsigned char)*p);
        }
    }
    out[i] = '\0';
}

static bool spot_passes_filters(const app_dx_spot_t *spot)
{
    if (s_filter_band != 0) {
        if (band_index_for_freq_khz(spot->freq) != s_filter_band) return false;
    }
    if (s_filter_mode != 0) {
        if (mode_index_for_comment(spot->comment) != s_filter_mode) return false;
    }
    if (s_filter_area == 1 && s_my_prefix[0]) {
        char sp_pref[8];
        extract_prefix(spot->spotter, sp_pref, sizeof(sp_pref));
        if (strcmp(sp_pref, s_my_prefix) != 0) return false;
    }
    return true;
}

/* --- Row construction --- */

static void build_row(lv_obj_t *parent, dx_row_t *r)
{
    r->row = lv_obj_create(parent);
    ui_theme_style_panel(r->row);
    lv_obj_set_size(r->row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(r->row, 8, 0);
    lv_obj_set_style_pad_gap(r->row, 2, 0);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *top = lv_obj_create(r->row);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    r->call = lv_label_create(top);
    lv_obj_set_style_text_color(r->call, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(r->call, &lv_font_montserrat_20, 0);

    r->freq = lv_label_create(top);
    lv_obj_set_style_text_color(r->freq, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(r->freq, &lv_font_montserrat_18, 0);

    r->time = lv_label_create(top);
    lv_obj_set_style_text_color(r->time, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(r->time, &lv_font_montserrat_14, 0);

    r->info = lv_label_create(r->row);
    lv_obj_set_style_text_color(r->info, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(r->info, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(r->info, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(r->info, LV_PCT(100));
}

/* --- Refresh --- */

static void refresh(const app_dx_state_t *state)
{
    if (!state) return;

    if (state->connected) {
        lv_label_set_text(s_status_lbl, "ONLINE");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x00d97e), 0);
    } else {
        lv_label_set_text(s_status_lbl, "OFFLINE");
        lv_obj_set_style_text_color(s_status_lbl, UI_COL_DANGER, 0);
    }

    /* Reflect current filter state on the chips. */
    lv_label_set_text(s_chip_band_lbl, BAND_NAMES[s_filter_band]);
    lv_label_set_text(s_chip_mode_lbl, MODE_NAMES[s_filter_mode]);
    lv_label_set_text(s_chip_area_lbl, AREA_NAMES[s_filter_area]);

    /* Build a filtered view by walking the ring newest-first and keeping
     * pointers to spots that pass. Cap at one page worth of pointers
     * plus enough lookahead to compute pagination - APP_DX_MAX_SPOTS is
     * 200, easily walkable. */
    const app_dx_spot_t *filtered[APP_DX_MAX_SPOTS];
    int filtered_n = 0;
    for (int i = 0; i < state->count; i++) {
        int idx = (state->head - i + APP_DX_MAX_SPOTS) % APP_DX_MAX_SPOTS;
        const app_dx_spot_t *sp = &state->spots[idx];
        if (spot_passes_filters(sp)) {
            filtered[filtered_n++] = sp;
        }
    }

    int total = filtered_n;
    int max_page = (total + UI_DX_ROWS_PER_PAGE - 1) / UI_DX_ROWS_PER_PAGE - 1;
    if (max_page < 0) max_page = 0;
    if (s_page > max_page) s_page = max_page;
    if (s_page < 0) s_page = 0;

    char page_buf[48];
    if (total == 0) {
        snprintf(page_buf, sizeof(page_buf), "—");
    } else {
        snprintf(page_buf, sizeof(page_buf), "Page %d/%d  ·  %d spot%s",
                 s_page + 1, max_page + 1, total, total == 1 ? "" : "s");
    }
    lv_label_set_text(s_page_lbl, page_buf);

    lv_obj_set_style_text_color(s_btn_prev_lbl,
                                s_page > 0 ? UI_COL_ACCENT : UI_COL_MUTED, 0);
    lv_obj_set_style_text_color(s_btn_next_lbl,
                                s_page < max_page ? UI_COL_ACCENT : UI_COL_MUTED, 0);

    if (total == 0) {
        const char *msg = state->connected
            ? (state->count == 0 ? "Waiting for spots…"
                                 : "No spots match the current filter")
            : "Connecting to cluster…";
        lv_label_set_text(s_empty_lbl, msg);
        lv_obj_remove_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < UI_DX_ROWS_PER_PAGE; i++) {
            lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);

    int start = s_page * UI_DX_ROWS_PER_PAGE;

    for (int i = 0; i < UI_DX_ROWS_PER_PAGE; i++) {
        int offset = start + i;
        if (offset >= total) {
            lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const app_dx_spot_t *spot = filtered[offset];
        dx_row_t *r = &s_rows[i];
        lv_label_set_text(r->call, spot->dx_call);
        lv_label_set_text(r->freq, spot->freq);
        lv_label_set_text(r->time, spot->time[0] ? spot->time : "");

        char info[96];
        if (spot->comment[0]) {
            snprintf(info, sizeof(info), "by %s  %s",
                     spot->spotter, spot->comment);
        } else {
            snprintf(info, sizeof(info), "by %s", spot->spotter);
        }
        lv_label_set_text(r->info, info);
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_now(void)
{
    if (!s_list) return;
    app_dxcluster_get(&s_snap_buf);
    refresh(&s_snap_buf);
}

/* --- Event handlers --- */

static void on_prev_clicked(lv_event_t *e)
{
    (void)e;
    if (s_page > 0) { s_page--; refresh_now(); }
}

static void on_next_clicked(lv_event_t *e)
{
    (void)e;
    s_page++;
    refresh_now();
}

static void on_band_clicked(lv_event_t *e)
{
    (void)e;
    s_filter_band = (s_filter_band + 1) % BAND_COUNT;
    s_page = 0;
    refresh_now();
}

static void on_mode_clicked(lv_event_t *e)
{
    (void)e;
    s_filter_mode = (s_filter_mode + 1) % MODE_COUNT;
    s_page = 0;
    refresh_now();
}

static void on_area_clicked(lv_event_t *e)
{
    (void)e;
    s_filter_area = (s_filter_area + 1) % AREA_COUNT;
    s_page = 0;
    refresh_now();
}

void ui_dx_on_update(const app_dx_spot_t *new_spot,
                     const app_dx_state_t *state)
{
    (void)new_spot;
    (void)state;
    atomic_store(&s_dirty, true);
}

static void dirty_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!atomic_exchange(&s_dirty, false)) return;
    refresh_now();
}

/* --- Widget construction helpers --- */

static lv_obj_t *make_chevron(lv_obj_t *parent, const char *symbol,
                              lv_event_cb_t cb, lv_obj_t **out_lbl)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 44, 36);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    if (out_lbl) *out_lbl = lbl;
    return btn;
}

static void make_filter_chip(lv_obj_t *parent, const char *label,
                             lv_event_cb_t cb, lv_obj_t **out_value_lbl)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip, UI_COL_BORDER, 0);
    lv_obj_set_style_border_color(chip, UI_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    lv_obj_set_style_pad_gap(chip, 4, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *l = lv_label_create(chip);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(chip);
    lv_label_set_text(v, "ALL");
    lv_obj_set_style_text_color(v, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);

    lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, NULL);
    if (out_value_lbl) *out_value_lbl = v;
}

/* --- Public create --- */

lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg)
{
    /* Remember the user's callsign prefix for the MINE area filter. */
    s_my_prefix[0] = '\0';
    if (cfg && cfg->callsign[0]) {
        extract_prefix(cfg->callsign, s_my_prefix, sizeof(s_my_prefix));
    }

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);
    /* The scr container should not scroll on its own - rows fit per page. */
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Title row */
    lv_obj_t *title_row = lv_obj_create(scr);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, "DX SPOTS");
    lv_obj_set_style_text_color(title, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    s_status_lbl = lv_label_create(title_row);
    lv_label_set_text(s_status_lbl, "CONNECTING…");
    lv_obj_set_style_text_color(s_status_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);

    /* Filter chips row: Band, Mode, Area */
    lv_obj_t *filters = lv_obj_create(scr);
    lv_obj_remove_style_all(filters);
    lv_obj_set_size(filters, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(filters, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filters, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(filters, 6, 0);
    lv_obj_clear_flag(filters, LV_OBJ_FLAG_SCROLLABLE);

    make_filter_chip(filters, "Band", on_band_clicked, &s_chip_band_lbl);
    make_filter_chip(filters, "Mode", on_mode_clicked, &s_chip_mode_lbl);
    make_filter_chip(filters, "Area", on_area_clicked, &s_chip_area_lbl);

    /* Pager row */
    lv_obj_t *pager = lv_obj_create(scr);
    lv_obj_remove_style_all(pager);
    lv_obj_set_size(pager, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pager, LV_OBJ_FLAG_SCROLLABLE);

    make_chevron(pager, LV_SYMBOL_LEFT,  on_prev_clicked, &s_btn_prev_lbl);

    s_page_lbl = lv_label_create(pager);
    lv_label_set_text(s_page_lbl, "—");
    lv_obj_set_style_text_color(s_page_lbl, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_page_lbl, &lv_font_montserrat_14, 0);

    make_chevron(pager, LV_SYMBOL_RIGHT, on_next_clicked, &s_btn_next_lbl);

    /* List of rows - no scroll, no border, no leftover panel. The rows
     * themselves draw the visual panels; the container is invisible. */
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list, 6, 0);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    s_empty_lbl = lv_label_create(s_list);
    lv_label_set_text(s_empty_lbl, "Connecting to cluster…");
    lv_obj_set_style_text_color(s_empty_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_empty_lbl, &lv_font_montserrat_14, 0);

    for (int i = 0; i < UI_DX_ROWS_PER_PAGE; i++) {
        build_row(s_list, &s_rows[i]);
    }

    lv_timer_create(dirty_timer_cb, 1000, NULL);
    refresh_now();

    return scr;
}
