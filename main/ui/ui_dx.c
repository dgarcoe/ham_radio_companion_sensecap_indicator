#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "app_dxcluster.h"
#include "bsp.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define UI_DX_VISIBLE 15  /* a busier cluster will push 100+ rows/sec
                            through; render at most this many. */

static lv_obj_t *s_status_lbl;
static lv_obj_t *s_list;
static atomic_bool s_dirty = ATOMIC_VAR_INIT(true);

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

    lv_obj_clean(s_list);

    if (state->count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty,
            state->connected
                ? "Waiting for spots…"
                : "Connecting to cluster…");
        lv_obj_set_style_text_color(empty, UI_COL_MUTED, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        return;
    }

    for (int i = 0; i < state->count && i < UI_DX_VISIBLE; i++) {
        int idx = (state->head - i + APP_DX_MAX_SPOTS) % APP_DX_MAX_SPOTS;
        const app_dx_spot_t *spot = &state->spots[idx];

        lv_obj_t *row = lv_obj_create(s_list);
        ui_theme_style_panel(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_gap(row, 2, 0);

        /* Top line: DX call (accent, big) | freq | time */
        lv_obj_t *top = lv_obj_create(row);
        lv_obj_remove_style_all(top);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *call = lv_label_create(top);
        lv_label_set_text(call, spot->dx_call);
        lv_obj_set_style_text_color(call, UI_COL_ACCENT, 0);
        lv_obj_set_style_text_font(call, &lv_font_montserrat_20, 0);

        lv_obj_t *freq = lv_label_create(top);
        lv_label_set_text(freq, spot->freq);
        lv_obj_set_style_text_color(freq, UI_COL_TEXT, 0);
        lv_obj_set_style_text_font(freq, &lv_font_montserrat_18, 0);

        lv_obj_t *t = lv_label_create(top);
        lv_label_set_text(t, spot->time[0] ? spot->time : "");
        lv_obj_set_style_text_color(t, UI_COL_MUTED, 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);

        char info[96];
        if (spot->comment[0]) {
            snprintf(info, sizeof(info), "by %s  %s",
                     spot->spotter, spot->comment);
        } else {
            snprintf(info, sizeof(info), "by %s", spot->spotter);
        }
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, info);
        lv_obj_set_style_text_color(l, UI_COL_MUTED, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(l, LV_PCT(100));
    }
}

/* Called from the cluster task on every new spot / connection event.
 * We just flip a flag and let the LVGL-side timer pick up the latest
 * state at most once per second - this used to take the LVGL lock and
 * rebuild the whole list per-spot, which was hammering the renderer
 * hard enough to trip the task watchdog on busy clusters. */
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

    /* Scrolling list */
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    /* Pull current state and rebuild at most once per second. */
    lv_timer_create(dirty_timer_cb, 1000, NULL);

    app_dx_state_t snap;
    app_dxcluster_get(&snap);
    refresh(&snap);

    return scr;
}
