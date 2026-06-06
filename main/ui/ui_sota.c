#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "app_sota.h"
#include "app_alert.h"
#include "bsp.h"
#include "esp_attr.h"

#include <stdatomic.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_SOTA_ROWS_PER_PAGE 5

typedef struct {
    lv_obj_t *row;
    lv_obj_t *activator;
    lv_obj_t *freq;
    lv_obj_t *time;
    lv_obj_t *summit;
} sota_row_t;

/* Same band table as POTA / DX. */
static const char *const BAND_NAMES[] = {
    "ALL", "160m", "80m", "60m", "40m", "30m", "20m",
    "17m", "15m", "12m", "10m", "6m", "2m",
};
#define BAND_COUNT (sizeof(BAND_NAMES) / sizeof(BAND_NAMES[0]))

static const struct { float lo; float hi; } BAND_EDGES[] = {
    {1.8f,   2.0f},
    {3.5f,   4.0f},
    {5.0f,   5.5f},
    {7.0f,   7.3f},
    {10.1f, 10.15f},
    {14.0f, 14.35f},
    {18.068f, 18.168f},
    {21.0f, 21.45f},
    {24.89f, 24.99f},
    {28.0f, 29.7f},
    {50.0f, 54.0f},
    {144.0f, 148.0f},
};

static const char *const MODE_NAMES[] = { "ALL", "CW", "DIGI", "PHO" };
#define MODE_COUNT (sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0]))

/* --- State --- */

static lv_obj_t *s_status_lbl;
static lv_obj_t *s_list;
static lv_obj_t *s_empty_lbl;
static lv_obj_t *s_page_lbl;
static lv_obj_t *s_btn_prev_lbl;
static lv_obj_t *s_btn_next_lbl;
static lv_obj_t *s_chip_band_lbl;
static lv_obj_t *s_chip_mode_lbl;
static sota_row_t s_rows[UI_SOTA_ROWS_PER_PAGE];

static int s_page;
static int s_filter_band;
static int s_filter_mode;

EXT_RAM_BSS_ATTR static app_sota_state_t s_snap_buf;
static atomic_bool s_dirty = ATOMIC_VAR_INIT(true);

/* --- Helpers --- */

static int band_index_for_freq_khz(const char *freq_str)
{
    if (!freq_str || !*freq_str) return 0;
    float khz = strtof(freq_str, NULL);
    float mhz = khz / 1000.0f;
    for (size_t i = 0; i < sizeof(BAND_EDGES) / sizeof(BAND_EDGES[0]); i++) {
        if (mhz >= BAND_EDGES[i].lo && mhz <= BAND_EDGES[i].hi) {
            return (int)(i + 1);
        }
    }
    return 0;
}

static int mode_index_for_str(const char *m)
{
    if (!m || !*m) return 0;
    char up[16] = {0};
    size_t i;
    for (i = 0; i < sizeof(up) - 1 && m[i]; i++) {
        up[i] = (char)toupper((unsigned char)m[i]);
    }
    up[i] = '\0';

    if (strstr(up, "FT8") || strstr(up, "FT4") || strstr(up, "RTTY") ||
        strstr(up, "PSK") || strstr(up, "JT")  || strstr(up, "DIGI") ||
        strstr(up, "DATA")) {
        return 2;
    }
    if (strstr(up, "CW")) return 1;
    if (strstr(up, "SSB") || strstr(up, "USB") || strstr(up, "LSB") ||
        strstr(up, "FM")  || strstr(up, " AM") || strstr(up, "PHONE")) {
        return 3;
    }
    return 0;
}

static bool spot_passes_filters(const app_sota_spot_t *sp)
{
    if (s_filter_band != 0) {
        if (band_index_for_freq_khz(sp->freq) != s_filter_band) return false;
    }
    if (s_filter_mode != 0) {
        if (mode_index_for_str(sp->mode) != s_filter_mode) return false;
    }
    return true;
}

/* --- Row construction --- */

static void build_row(lv_obj_t *parent, sota_row_t *r)
{
    r->row = lv_obj_create(parent);
    ui_theme_style_panel(r->row);
    lv_obj_set_size(r->row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(r->row, 8, 0);
    lv_obj_set_style_pad_ver(r->row, 4, 0);
    lv_obj_set_style_pad_gap(r->row, 1, 0);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(r->row);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    r->activator = lv_label_create(top);
    lv_obj_set_style_text_color(r->activator, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(r->activator, &lv_font_montserrat_18, 0);

    r->freq = lv_label_create(top);
    lv_obj_set_style_text_color(r->freq, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(r->freq, &lv_font_montserrat_14, 0);

    r->time = lv_label_create(top);
    lv_obj_set_style_text_color(r->time, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(r->time, &lv_font_montserrat_14, 0);

    r->summit = lv_label_create(r->row);
    lv_obj_set_style_text_color(r->summit, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(r->summit, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(r->summit, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(r->summit, LV_PCT(100));
}

/* --- Refresh --- */

static void refresh(const app_sota_state_t *state)
{
    if (!state) return;

    if (state->connected) {
        lv_label_set_text(s_status_lbl, "ONLINE");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x00d97e), 0);
    } else {
        lv_label_set_text(s_status_lbl, "OFFLINE");
        lv_obj_set_style_text_color(s_status_lbl, UI_COL_DANGER, 0);
    }

    lv_label_set_text(s_chip_band_lbl, BAND_NAMES[s_filter_band]);
    lv_label_set_text(s_chip_mode_lbl, MODE_NAMES[s_filter_mode]);

    const app_sota_spot_t *filtered[APP_SOTA_MAX_SPOTS];
    int filtered_n = 0;
    for (int i = 0; i < state->count; i++) {
        const app_sota_spot_t *sp = &state->spots[i];
        if (spot_passes_filters(sp)) {
            filtered[filtered_n++] = sp;
        }
    }

    int total = filtered_n;
    int max_page = (total + UI_SOTA_ROWS_PER_PAGE - 1) / UI_SOTA_ROWS_PER_PAGE - 1;
    if (max_page < 0) max_page = 0;
    if (s_page > max_page) s_page = max_page;
    if (s_page < 0) s_page = 0;

    char page_buf[64];
    if (total == 0) {
        snprintf(page_buf, sizeof(page_buf), "-");
    } else {
        snprintf(page_buf, sizeof(page_buf), "Page %d/%d  |  %d spot%s",
                 s_page + 1, max_page + 1, total, total == 1 ? "" : "s");
    }
    lv_label_set_text(s_page_lbl, page_buf);

    lv_obj_set_style_text_color(s_btn_prev_lbl,
                                s_page > 0 ? UI_COL_ACCENT : UI_COL_MUTED, 0);
    lv_obj_set_style_text_color(s_btn_next_lbl,
                                s_page < max_page ? UI_COL_ACCENT : UI_COL_MUTED, 0);

    if (total == 0) {
        const char *msg = state->connected
            ? (state->count == 0 ? "No active SOTA spots"
                                 : "No spots match the current filter")
            : "Fetching from sota.org.uk...";
        lv_label_set_text(s_empty_lbl, msg);
        lv_obj_remove_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < UI_SOTA_ROWS_PER_PAGE; i++) {
            lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);

    int start = s_page * UI_SOTA_ROWS_PER_PAGE;

    for (int i = 0; i < UI_SOTA_ROWS_PER_PAGE; i++) {
        int offset = start + i;
        if (offset >= total) {
            lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const app_sota_spot_t *sp = filtered[offset];
        sota_row_t *r = &s_rows[i];

        lv_label_set_text(r->activator, sp->activator);
        lv_label_set_text(r->freq, sp->freq);
        lv_label_set_text(r->time, sp->time_str[0] ? sp->time_str : "");

        char summit[96];
        const char *mode = sp->mode[0] ? sp->mode : "?";
        if (sp->summit_name[0]) {
            snprintf(summit, sizeof(summit), "%s | %s | %s",
                     sp->summit_ref, sp->summit_name, mode);
        } else if (sp->summit_ref[0]) {
            snprintf(summit, sizeof(summit), "%s | %s", sp->summit_ref, mode);
        } else {
            snprintf(summit, sizeof(summit), "%s", mode);
        }
        lv_label_set_text(r->summit, summit);
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_now(void)
{
    if (!s_list) return;
    app_sota_get(&s_snap_buf);
    refresh(&s_snap_buf);
}

/* --- Event handlers --- */

static void on_prev_clicked(lv_event_t *e) { (void)e; if (s_page > 0) { s_page--; refresh_now(); } }
static void on_next_clicked(lv_event_t *e) { (void)e; s_page++; refresh_now(); }

static void cycle_filter(int *value, int count, int delta)
{
    *value = (*value + delta + count) % count;
    s_page = 0;
    refresh_now();
}
static void on_band_prev (lv_event_t *e) { (void)e; cycle_filter(&s_filter_band, BAND_COUNT, -1); }
static void on_band_next (lv_event_t *e) { (void)e; cycle_filter(&s_filter_band, BAND_COUNT, +1); }
static void on_mode_prev (lv_event_t *e) { (void)e; cycle_filter(&s_filter_mode, MODE_COUNT, -1); }
static void on_mode_next (lv_event_t *e) { (void)e; cycle_filter(&s_filter_mode, MODE_COUNT, +1); }

void ui_sota_on_update(const app_sota_state_t *state)
{
    if (state) {
        for (int i = 0; i < state->count; i++) {
            const app_sota_spot_t *sp = &state->spots[i];
            char loc[24];
            if (sp->summit_ref[0]) snprintf(loc, sizeof(loc), "%s", sp->summit_ref);
            else                   loc[0] = '\0';
            app_alert_check(APP_ALERT_SRC_SOTA, sp->activator, loc);
        }
    }
    atomic_store(&s_dirty, true);
}

static void dirty_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!atomic_exchange(&s_dirty, false)) return;
    refresh_now();
}

/* --- Widget helpers --- */

static lv_obj_t *make_chevron(lv_obj_t *parent, const char *symbol,
                              lv_event_cb_t cb, lv_obj_t **out_lbl)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 44, 36);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
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

static void make_filter_chip(lv_obj_t *parent, const char *header,
                             lv_event_cb_t on_prev, lv_event_cb_t on_next,
                             lv_obj_t **out_value_lbl)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 220, 30);
    lv_obj_set_style_bg_color(chip, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_hor(chip, 6, 0);
    lv_obj_set_style_pad_ver(chip, 0, 0);
    lv_obj_set_style_pad_gap(chip, 4, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *prev = lv_obj_create(chip);
    lv_obj_remove_style_all(prev);
    lv_obj_set_size(prev, 24, 28);
    lv_obj_add_flag(prev, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(prev, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(prev, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prev, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *prev_lbl = lv_label_create(prev);
    lv_label_set_text(prev_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(prev_lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(prev_lbl, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(prev, on_prev, LV_EVENT_CLICKED, NULL);

    lv_obj_t *mid = lv_obj_create(chip);
    lv_obj_remove_style_all(mid);
    lv_obj_set_size(mid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(mid, 6, 0);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(mid);
    lv_label_set_text(h, header);
    lv_obj_set_style_text_color(h, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(mid);
    lv_label_set_text(v, "ALL");
    lv_obj_set_style_text_color(v, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);

    lv_obj_t *next = lv_obj_create(chip);
    lv_obj_remove_style_all(next);
    lv_obj_set_size(next, 24, 28);
    lv_obj_add_flag(next, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(next, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(next, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(next, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *next_lbl = lv_label_create(next);
    lv_label_set_text(next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(next_lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(next_lbl, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(next, on_next, LV_EVENT_CLICKED, NULL);

    if (out_value_lbl) *out_value_lbl = v;
}

/* --- Public create --- */

lv_obj_t *ui_sota_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_row = lv_obj_create(scr);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, "SOTA SPOTS");
    lv_obj_set_style_text_color(title, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    s_status_lbl = lv_label_create(title_row);
    lv_label_set_text(s_status_lbl, "CONNECTING...");
    lv_obj_set_style_text_color(s_status_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *filters = lv_obj_create(scr);
    lv_obj_remove_style_all(filters);
    lv_obj_set_size(filters, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(filters, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filters, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(filters, 6, 0);
    lv_obj_clear_flag(filters, LV_OBJ_FLAG_SCROLLABLE);

    make_filter_chip(filters, "Band", on_band_prev, on_band_next, &s_chip_band_lbl);
    make_filter_chip(filters, "Mode", on_mode_prev, on_mode_next, &s_chip_mode_lbl);

    lv_obj_t *pager = lv_obj_create(scr);
    lv_obj_remove_style_all(pager);
    lv_obj_set_size(pager, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pager, LV_OBJ_FLAG_SCROLLABLE);

    make_chevron(pager, LV_SYMBOL_LEFT,  on_prev_clicked, &s_btn_prev_lbl);

    s_page_lbl = lv_label_create(pager);
    lv_label_set_text(s_page_lbl, "-");
    lv_obj_set_style_text_color(s_page_lbl, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_page_lbl, &lv_font_montserrat_14, 0);

    make_chevron(pager, LV_SYMBOL_RIGHT, on_next_clicked, &s_btn_next_lbl);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list, 6, 0);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    s_empty_lbl = lv_label_create(s_list);
    lv_label_set_text(s_empty_lbl, "Fetching from sota.org.uk...");
    lv_obj_set_style_text_color(s_empty_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_empty_lbl, &lv_font_montserrat_14, 0);

    for (int i = 0; i < UI_SOTA_ROWS_PER_PAGE; i++) {
        build_row(s_list, &s_rows[i]);
    }

    lv_timer_create(dirty_timer_cb, 1000, NULL);
    refresh_now();

    return scr;
}
