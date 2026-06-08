#include "ui_internal.h"
#include "ui_theme.h"
#include "ui.h"
#include "app_sats.h"
#include "app_time.h"
#include "bsp.h"
#include "esp_log.h"
#include "esp_attr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "ui_sats";

/* Satellite tracker screen. One card per satellite in the roster:
 *   - Name + NORAD id + mode (FM repeater / SSB linear / APRS).
 *   - Pass status: NOW VISIBLE + az/el if above the horizon,
 *     otherwise 'next AOS in HH:MM' + peak elev + azimuth, or
 *     '(no pass in 24h)' if the predictor came up empty.
 *   - Frequencies: up + down + (CTCSS) + mode.
 * The cards are coloured by status -- currently visible is green
 * to nudge the user out to the radio. */

typedef struct {
    lv_obj_t *card;
    lv_obj_t *name_lbl;
    lv_obj_t *status_lbl;
    lv_obj_t *freq_lbl;
} row_t;

static row_t s_rows[APP_SATS_MAX];
static lv_obj_t *s_lbl_header;

EXT_RAM_BSS_ATTR static app_sats_state_t s_snap;

static lv_color_t col_now(void)      { return lv_color_hex(0x00d97e); }
static lv_color_t col_soon(void)     { return lv_color_hex(0xffcd3a); }
static lv_color_t col_later(void)    { return UI_COL_ACCENT;          }
static lv_color_t col_nopass(void)   { return UI_COL_MUTED;           }

static lv_color_t status_color(const app_sat_t *s)
{
    if (!s->tle_valid)        return col_nopass();
    if (s->currently_above)   return col_now();
    if (s->next_aos_ms == 0)  return col_nopass();
    int64_t until = s->next_aos_ms - (int64_t)time(NULL) * 1000LL;
    if (until <= 30 * 60 * 1000LL) return col_soon();
    return col_later();
}

/* "in 47m" / "in 2h 14m" / "in 14m 32s" formatting. */
static void format_in(int64_t until_ms, char *buf, size_t sz)
{
    int total_s = (int)(until_ms / 1000);
    if (total_s < 0) total_s = 0;
    int h = total_s / 3600;
    int m = (total_s % 3600) / 60;
    int s = total_s % 60;
    if (h > 0) {
        snprintf(buf, sz, "in %dh %02dm", h, m);
    } else if (m > 0) {
        snprintf(buf, sz, "in %dm %02ds", m, s);
    } else {
        snprintf(buf, sz, "in %ds", s);
    }
}

/* Cardinal direction string from a 0..360 bearing. */
static const char *cardinal(float az)
{
    static const char *names[] = {"N","NE","E","SE","S","SW","W","NW"};
    int idx = (int)floorf((az + 22.5f) / 45.0f) & 7;
    return names[idx];
}

static void refresh(const app_sats_state_t *st)
{
    char hdr[80];
    if (!st->tle_ever_fetched) {
        snprintf(hdr, sizeof(hdr), "Fetching satellite TLEs from CelesTrak...");
    } else if (st->last_predict_ms == 0) {
        snprintf(hdr, sizeof(hdr), "TLEs loaded -- predicting passes...");
    } else {
        snprintf(hdr, sizeof(hdr),
                 "%d satellites tracked  \xE2\x80\xA2  predictions live",
                 st->count);
    }
    lv_label_set_text(s_lbl_header, hdr);

    int64_t now_ms = (int64_t)time(NULL) * 1000LL;
    for (int i = 0; i < APP_SATS_MAX; i++) {
        if (i >= st->count) {
            lv_obj_add_flag(s_rows[i].card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const app_sat_t *s = &st->sats[i];
        lv_obj_remove_flag(s_rows[i].card, LV_OBJ_FLAG_HIDDEN);

        char buf[96];
        snprintf(buf, sizeof(buf), "%s   #%ld   %s",
                 s->name, (long)s->norad_id, s->mode);
        lv_label_set_text(s_rows[i].name_lbl, buf);

        if (!s->tle_valid) {
            snprintf(buf, sizeof(buf), "(no TLE yet)");
        } else if (s->currently_above) {
            snprintf(buf, sizeof(buf),
                     "NOW VISIBLE  \xE2\x80\xA2  EL %.0f\xC2\xB0  "
                     "AZ %.0f\xC2\xB0 %s",
                     (double)s->current_elev_deg,
                     (double)s->current_az_deg,
                     cardinal(s->current_az_deg));
        } else if (s->next_aos_ms > 0) {
            char when[24];
            format_in(s->next_aos_ms - now_ms, when, sizeof(when));
            snprintf(buf, sizeof(buf),
                     "AOS %s  \xE2\x80\xA2  peak EL %.0f\xC2\xB0  "
                     "AZ %.0f\xC2\xB0 %s",
                     when,
                     (double)s->next_peak_elev_deg,
                     (double)s->next_peak_az_deg,
                     cardinal(s->next_peak_az_deg));
        } else {
            snprintf(buf, sizeof(buf), "(no pass in 24 h)");
        }
        lv_label_set_text(s_rows[i].status_lbl, buf);
        lv_obj_set_style_text_color(s_rows[i].status_lbl, status_color(s), 0);
        /* Stripe along the card's left edge mirrors the status colour
         * so the dashboard reads at a glance. */
        lv_obj_set_style_border_color(s_rows[i].card, status_color(s), 0);

        snprintf(buf, sizeof(buf),
                 LV_SYMBOL_UPLOAD "  %s     "
                 LV_SYMBOL_DOWNLOAD "  %s",
                 s->up_freq, s->down_freq);
        lv_label_set_text(s_rows[i].freq_lbl, buf);
    }
}

void ui_sats_on_update(const app_sats_state_t *st)
{
    if (!s_lbl_header) return;
    if (!bsp_lvgl_lock(200)) return;
    refresh(st);
    bsp_lvgl_unlock();
}

/* 1 Hz polling timer pulls a fresh snapshot so the 'NOW' azimuth
 * trickles even between background-task updates. */
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    app_sats_get(&s_snap);
    refresh(&s_snap);
}

static void build_row(lv_obj_t *parent, row_t *r)
{
    r->card = lv_obj_create(parent);
    ui_theme_style_panel(r->card);
    lv_obj_set_size(r->card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(r->card, 12, 0);
    lv_obj_set_style_pad_ver(r->card, 6, 0);
    lv_obj_set_style_pad_gap(r->card, 2, 0);
    lv_obj_set_style_border_color(r->card, UI_COL_MUTED, 0);
    lv_obj_set_style_border_width(r->card, 3, 0);
    lv_obj_set_style_border_side(r->card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_add_flag(r->card, LV_OBJ_FLAG_HIDDEN);

    r->name_lbl = lv_label_create(r->card);
    lv_label_set_text(r->name_lbl, "");
    lv_obj_set_style_text_color(r->name_lbl, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(r->name_lbl, &lv_font_montserrat_18, 0);

    r->status_lbl = lv_label_create(r->card);
    lv_label_set_text(r->status_lbl, "");
    lv_obj_set_style_text_color(r->status_lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(r->status_lbl, &lv_font_montserrat_14, 0);

    r->freq_lbl = lv_label_create(r->card);
    lv_label_set_text(r->freq_lbl, "");
    lv_obj_set_style_text_color(r->freq_lbl, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(r->freq_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_width(r->freq_lbl, LV_PCT(100));
    lv_label_set_long_mode(r->freq_lbl, LV_LABEL_LONG_DOT);
}

lv_obj_t *ui_sats_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;
    ESP_LOGI(TAG, "create");

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 6, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);

    s_lbl_header = lv_label_create(scr);
    lv_label_set_text(s_lbl_header, "Initialising satellite tracker...");
    lv_obj_set_style_text_color(s_lbl_header, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_header, &lv_font_montserrat_14, 0);

    for (int i = 0; i < APP_SATS_MAX; i++) {
        build_row(scr, &s_rows[i]);
    }

    /* Seed from the current snapshot so the screen isn't blank if the
     * user navigates here right at boot, before any background tick. */
    app_sats_get(&s_snap);
    refresh(&s_snap);

    lv_timer_create(tick_cb, 1000, NULL);
    return scr;
}
