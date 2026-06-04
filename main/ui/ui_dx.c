#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "app_dxcluster.h"
#include "bsp.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* 8 spots per page is a comfortable density on a 480x480 panel - the
 * full ring (APP_DX_MAX_SPOTS = 200) is reached via prev/next paging
 * instead of a long scrollable list, which keeps the LVGL object count
 * fixed and tiny regardless of how many spots arrive. */
#define UI_DX_ROWS_PER_PAGE 8

typedef struct {
    lv_obj_t *row;
    lv_obj_t *call;
    lv_obj_t *freq;
    lv_obj_t *time;
    lv_obj_t *info;
} dx_row_t;

static lv_obj_t *s_status_lbl;
static lv_obj_t *s_list;
static lv_obj_t *s_empty_lbl;
static lv_obj_t *s_page_lbl;
static lv_obj_t *s_btn_prev_lbl;
static lv_obj_t *s_btn_next_lbl;
static dx_row_t  s_rows[UI_DX_ROWS_PER_PAGE];
static int       s_page;                 /* 0 = newest */
static atomic_bool s_dirty = ATOMIC_VAR_INIT(true);

/* Snapshot buffer for refresh_now() - lives in .bss because
 * app_dx_state_t is ~5 KB and the LVGL task only has 8 KB of stack;
 * a stack-resident snap plus the 15-deep label-set / mark-dirty /
 * event-send / spinlock chain was overflowing into the adjacent
 * FreeRTOS event-group memory, scribbling its spinlock owner field
 * and causing the CAS to spin forever (IWDT panic). */
static app_dx_state_t s_snap_buf;

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

    int total = state->count;
    int max_page = (total + UI_DX_ROWS_PER_PAGE - 1) / UI_DX_ROWS_PER_PAGE - 1;
    if (max_page < 0) max_page = 0;
    if (s_page > max_page) s_page = max_page;
    if (s_page < 0) s_page = 0;

    char page_buf[40];
    if (total == 0) {
        snprintf(page_buf, sizeof(page_buf), "—");
    } else {
        snprintf(page_buf, sizeof(page_buf), "Page %d/%d  ·  %d spot%s",
                 s_page + 1, max_page + 1, total, total == 1 ? "" : "s");
    }
    lv_label_set_text(s_page_lbl, page_buf);

    /* Dim the prev/next chevrons when at the edges. */
    lv_obj_set_style_text_color(s_btn_prev_lbl,
                                s_page > 0 ? UI_COL_ACCENT : UI_COL_MUTED, 0);
    lv_obj_set_style_text_color(s_btn_next_lbl,
                                s_page < max_page ? UI_COL_ACCENT : UI_COL_MUTED, 0);

    /* Empty state */
    if (total == 0) {
        lv_label_set_text(s_empty_lbl,
            state->connected ? "Waiting for spots…"
                             : "Connecting to cluster…");
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

        int idx = (state->head - offset + APP_DX_MAX_SPOTS) % APP_DX_MAX_SPOTS;
        const app_dx_spot_t *spot = &state->spots[idx];

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
    /* All callers (timer + chevron click handlers) run on the LVGL
     * task, so there's only ever one in flight at a time - s_snap_buf
     * has a single user and doesn't need synchronisation. */
    app_dxcluster_get(&s_snap_buf);
    refresh(&s_snap_buf);
}

static void on_prev_clicked(lv_event_t *e)
{
    (void)e;
    if (s_page > 0) {
        s_page--;
        refresh_now();
    }
}

static void on_next_clicked(lv_event_t *e)
{
    (void)e;
    s_page++;   /* refresh() clamps */
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

/* Builds a chevron button with a 44 px hit area - matches our touch
 * target size from the status-bar hamburger. */
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

lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);

    /* Title row: DX SPOTS  /  ONLINE indicator */
    lv_obj_t *title_row = lv_obj_create(scr);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, "DX SPOTS");
    lv_obj_set_style_text_color(title, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    s_status_lbl = lv_label_create(title_row);
    lv_label_set_text(s_status_lbl, "CONNECTING…");
    lv_obj_set_style_text_color(s_status_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);

    /* Pager row: [<]   Page X/Y · Z spots   [>] */
    lv_obj_t *pager = lv_obj_create(scr);
    lv_obj_remove_style_all(pager);
    lv_obj_set_size(pager, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_chevron(pager, LV_SYMBOL_LEFT,  on_prev_clicked, &s_btn_prev_lbl);

    s_page_lbl = lv_label_create(pager);
    lv_label_set_text(s_page_lbl, "—");
    lv_obj_set_style_text_color(s_page_lbl, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_page_lbl, &lv_font_montserrat_14, 0);

    make_chevron(pager, LV_SYMBOL_RIGHT, on_next_clicked, &s_btn_next_lbl);

    /* List of rows for the current page */
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list, 6, 0);

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
