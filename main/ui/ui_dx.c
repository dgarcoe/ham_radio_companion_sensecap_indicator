#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "app_dxcluster.h"
#include "bsp.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define UI_DX_VISIBLE 15

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
static dx_row_t  s_rows[UI_DX_VISIBLE];
static atomic_bool s_dirty = ATOMIC_VAR_INIT(true);

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

    int visible = state->count < UI_DX_VISIBLE ? state->count : UI_DX_VISIBLE;

    /* Empty-state label visibility */
    if (visible == 0) {
        lv_label_set_text(s_empty_lbl,
            state->connected ? "Waiting for spots…"
                             : "Connecting to cluster…");
        lv_obj_remove_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Update / show / hide the pre-built rows in place. No allocations
     * happen here - which is the whole point: rebuilding the subtree
     * on every spot churned LVGL's draw-descriptor heap to death. */
    for (int i = 0; i < UI_DX_VISIBLE; i++) {
        dx_row_t *r = &s_rows[i];
        if (i < visible) {
            int idx = (state->head - i + APP_DX_MAX_SPOTS) % APP_DX_MAX_SPOTS;
            const app_dx_spot_t *spot = &state->spots[idx];

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
        } else {
            lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        }
    }
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
    if (!s_list) return;
    app_dx_state_t snap;
    app_dxcluster_get(&snap);
    refresh(&snap);
}

lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 8, 0);

    /* Header */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hdr_lbl = lv_label_create(header);
    lv_label_set_text(hdr_lbl, "DX SPOTS");
    lv_obj_set_style_text_color(hdr_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_14, 0);

    s_status_lbl = lv_label_create(header);
    lv_label_set_text(s_status_lbl, "CONNECTING…");
    lv_obj_set_style_text_color(s_status_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);

    /* Scrollable list */
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    /* Empty-state placeholder */
    s_empty_lbl = lv_label_create(s_list);
    lv_label_set_text(s_empty_lbl, "Connecting to cluster…");
    lv_obj_set_style_text_color(s_empty_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_empty_lbl, &lv_font_montserrat_14, 0);

    /* Pre-built row widgets - reused across refreshes. */
    for (int i = 0; i < UI_DX_VISIBLE; i++) {
        build_row(s_list, &s_rows[i]);
    }

    lv_timer_create(dirty_timer_cb, 1000, NULL);

    app_dx_state_t snap;
    app_dxcluster_get(&snap);
    refresh(&snap);

    return scr;
}
