#include "ui_internal.h"
#include "ui_theme.h"
#include "app_time.h"

#include <stdio.h>
#include <time.h>

static lv_obj_t *s_time_lbl;
static lv_obj_t *s_seconds_lbl;
static lv_obj_t *s_date_lbl;
static lv_obj_t *s_jul_lbl;

static int day_of_year(const struct tm *t)
{
    return t->tm_yday + 1;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    struct tm utc;
    app_time_now_utc(&utc);

    char hm[8];
    snprintf(hm, sizeof(hm), "%02d:%02d", utc.tm_hour, utc.tm_min);
    lv_label_set_text(s_time_lbl, hm);

    char ss[4];
    snprintf(ss, sizeof(ss), ":%02d", utc.tm_sec);
    lv_label_set_text(s_seconds_lbl, ss);

    static const char *months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                   "JUL","AUG","SEP","OCT","NOV","DEC"};
    char date[24];
    snprintf(date, sizeof(date), "%02d %s %04d",
             utc.tm_mday, months[utc.tm_mon % 12], 1900 + utc.tm_year);
    lv_label_set_text(s_date_lbl, date);

    char jul[16];
    snprintf(jul, sizeof(jul), "DOY %03d", day_of_year(&utc));
    lv_label_set_text(s_jul_lbl, jul);
}

lv_obj_t *ui_watch_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(scr, 12, 0);

    /* "UTC" chip */
    lv_obj_t *chip = lv_label_create(scr);
    lv_label_set_text(chip, "UTC");
    ui_theme_style_accent_chip(chip);
    lv_obj_set_style_text_font(chip, &lv_font_montserrat_14, 0);

    /* Big HH:MM with neon glow + small :SS riding next to it */
    lv_obj_t *time_row = lv_obj_create(scr);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_gap(time_row, 4, 0);

    s_time_lbl = lv_label_create(time_row);
    lv_label_set_text(s_time_lbl, "--:--");
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_lbl, UI_COL_TEXT, 0);

    s_seconds_lbl = lv_label_create(time_row);
    lv_label_set_text(s_seconds_lbl, ":--");
    lv_obj_set_style_text_font(s_seconds_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_seconds_lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_pad_bottom(s_seconds_lbl, 6, 0);

    s_date_lbl = lv_label_create(scr);
    lv_label_set_text(s_date_lbl, "-- --- ----");
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_date_lbl, UI_COL_TEXT, 0);

    s_jul_lbl = lv_label_create(scr);
    lv_label_set_text(s_jul_lbl, "DOY ---");
    lv_obj_set_style_text_font(s_jul_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_jul_lbl, UI_COL_MUTED, 0);

    return scr;
}

void ui_watch_register_tick(lv_obj_t *screen)
{
    (void)screen;
    lv_timer_create(tick_cb, 250, NULL);
    tick_cb(NULL);
}
