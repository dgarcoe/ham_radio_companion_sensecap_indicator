#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"
#include "app_nvs.h"
#include "bsp.h"
#include "esp_log.h"
#include "esp_attr.h"

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Grayline map - equirectangular world view (lon -180..+180, lat
 * +90..-90), shaded according to the solar elevation at the current
 * UTC moment, with a gradient band on the terminator (the "gray
 * line") because that band is what propagates on the low HF bands.
 * Markers: subsolar point and the operator's Maidenhead locator.
 * Re-renders every 30 s; the terminator moves ~0.25 deg/min. */

#define GL_W 240
#define GL_H 120

static const char *TAG = "ui_grayline";

/* RGB565 canvas backing buffer; .bss in PSRAM so we don't burn 56 KB
 * of internal DRAM (matches the EXT_RAM_BSS_ATTR pattern used by
 * pota/sota/dx for their snapshot buffers). */
EXT_RAM_BSS_ATTR static uint16_t s_canvas_buf[GL_W * GL_H];

static lv_obj_t *s_canvas;
static lv_obj_t *s_lbl_utc;
static lv_obj_t *s_lbl_sun;
static lv_obj_t *s_lbl_station;
static lv_timer_t *s_refresh_timer;

static float s_station_lat = NAN;
static float s_station_lon = NAN;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Maidenhead grid square -> centre lat/lon in degrees. Accepts 4 or 6
 * characters; bails on malformed input. */
static bool parse_locator(const char *loc, float *out_lat, float *out_lon)
{
    if (!loc) return false;
    size_t n = strlen(loc);
    if (n < 4) return false;
    if (n > 6) n = 6;
    char L[6] = {0};
    for (size_t i = 0; i < n; i++) L[i] = loc[i];

    int A = (L[0] >= 'a') ? (L[0] - 'a') : (L[0] - 'A');
    int B = (L[1] >= 'a') ? (L[1] - 'a') : (L[1] - 'A');
    if (A < 0 || A > 17 || B < 0 || B > 17) return false;
    int d1 = L[2] - '0';
    int d2 = L[3] - '0';
    if (d1 < 0 || d1 > 9 || d2 < 0 || d2 > 9) return false;

    /* Field (20 deg lon x 10 deg lat) + square (2 deg x 1 deg). Centre
     * of the 4-char square sits at +1 deg lon / +0.5 deg lat from its
     * SW corner. */
    float lon = A * 20.0f - 180.0f + d1 * 2.0f + 1.0f;
    float lat = B * 10.0f -  90.0f + d2 * 1.0f + 0.5f;

    if (n >= 6) {
        int s1 = (L[4] >= 'a') ? (L[4] - 'a') : (L[4] - 'A');
        int s2 = (L[5] >= 'a') ? (L[5] - 'a') : (L[5] - 'A');
        if (s1 >= 0 && s1 < 24 && s2 >= 0 && s2 < 24) {
            /* Subsquare (5' x 2.5'). Replace the 4-char centring with
             * the 6-char centring. */
            lon += -1.0f + s1 * (2.0f / 24.0f) + (1.0f / 24.0f);
            lat += -0.5f + s2 * (1.0f / 24.0f) + (0.5f / 24.0f);
        }
    }
    *out_lon = lon;
    *out_lat = lat;
    return true;
}

/* NOAA solar-position approximation. decl is the solar declination
 * (degrees, +north); sublon is the longitude of the subsolar point
 * (degrees, +east). */
static void compute_sun(time_t now, float *decl_deg, float *sublon_deg)
{
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    int N = tm_utc.tm_yday;     /* 0..365 */
    float hours = tm_utc.tm_hour
                + tm_utc.tm_min  / 60.0f
                + tm_utc.tm_sec  / 3600.0f;

    float gamma = 2.0f * (float)M_PI / 365.0f
                * (N + (hours - 12.0f) / 24.0f);

    float decl = 0.006918f
               - 0.399912f * cosf(gamma)
               + 0.070257f * sinf(gamma)
               - 0.006758f * cosf(2.0f * gamma)
               + 0.000907f * sinf(2.0f * gamma)
               - 0.002697f * cosf(3.0f * gamma)
               + 0.001480f * sinf(3.0f * gamma);

    float eot_min = 229.18f * ( 0.000075f
                              + 0.001868f * cosf(gamma)
                              - 0.032077f * sinf(gamma)
                              - 0.014615f * cosf(2.0f * gamma)
                              - 0.040849f * sinf(2.0f * gamma));

    float sublon = -15.0f * (hours + eot_min / 60.0f - 12.0f);
    while (sublon >  180.0f) sublon -= 360.0f;
    while (sublon < -180.0f) sublon += 360.0f;

    *decl_deg   = decl * 180.0f / (float)M_PI;
    *sublon_deg = sublon;
}

/* sin(elevation) -> RGB565. Day side stays warm and saturated; the
 * grayline band fades through orange to deep red over the +/-0.10
 * range (about +/-5.7 deg solar elevation, comfortably wider than
 * civil twilight so the band is visible at 240x120). */
static inline uint16_t elevation_color(float sin_elev)
{
    if (sin_elev > 0.10f) {
        return rgb565(0xE8, 0xC8, 0x60);    /* day */
    }
    if (sin_elev < -0.10f) {
        return rgb565(0x05, 0x0D, 0x22);    /* night */
    }
    /* Linear blend night -> day across the grayline band. */
    float t = (sin_elev + 0.10f) / 0.20f;   /* 0..1 */
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint8_t r = (uint8_t)(0x05 + t * (0xE8 - 0x05));
    uint8_t g = (uint8_t)(0x0D + t * (0xC8 - 0x0D));
    uint8_t b = (uint8_t)(0x22 + t * (0x60 - 0x22));
    return rgb565(r, g, b);
}

static void plot_marker(int cx, int cy, uint16_t color)
{
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx * dx + dy * dy > 4) continue;
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= GL_W || py < 0 || py >= GL_H) continue;
            s_canvas_buf[py * GL_W + px] = color;
        }
    }
}

static void draw_grayline(time_t now)
{
    float decl_deg, sublon_deg;
    compute_sun(now, &decl_deg, &sublon_deg);
    float decl_rad = decl_deg * (float)M_PI / 180.0f;
    float sin_decl = sinf(decl_rad);
    float cos_decl = cosf(decl_rad);

    /* cos(lon - sublon) depends only on the column, so precompute one
     * value per x and the per-pixel inner loop drops to two mults +
     * one add. Saves about 28000 cosf calls per repaint. */
    static float cos_dlon[GL_W];
    for (int x = 0; x < GL_W; x++) {
        float lon_deg = -180.0f + (x + 0.5f) * (360.0f / GL_W);
        float dlon = (lon_deg - sublon_deg) * (float)M_PI / 180.0f;
        cos_dlon[x] = cosf(dlon);
    }

    for (int y = 0; y < GL_H; y++) {
        float lat_deg = 90.0f - (y + 0.5f) * (180.0f / GL_H);
        float lat_rad = lat_deg * (float)M_PI / 180.0f;
        float sin_lat = sinf(lat_rad);
        float cos_lat = cosf(lat_rad);
        float a = sin_lat * sin_decl;
        float b = cos_lat * cos_decl;
        uint16_t *row = &s_canvas_buf[y * GL_W];
        for (int x = 0; x < GL_W; x++) {
            row[x] = elevation_color(a + b * cos_dlon[x]);
        }
    }

    /* Subsolar point (warm white) */
    int sx = (int)((sublon_deg + 180.0f) * (GL_W / 360.0f));
    int sy = (int)((90.0f - decl_deg)    * (GL_H / 180.0f));
    plot_marker(sx, sy, rgb565(0xFF, 0xF4, 0xB0));

    /* Operator QTH (cyan) */
    if (!isnan(s_station_lat) && !isnan(s_station_lon)) {
        int hx = (int)((s_station_lon + 180.0f) * (GL_W / 360.0f));
        int hy = (int)((90.0f - s_station_lat)  * (GL_H / 180.0f));
        plot_marker(hx, hy, rgb565(0x10, 0xE8, 0xD0));
    }

    /* Labels */
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char buf[64];
    snprintf(buf, sizeof(buf), "UTC %02d:%02d:%02d",
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    lv_label_set_text(s_lbl_utc, buf);

    snprintf(buf, sizeof(buf), "Sun  %+5.1f\xC2\xB0  %+6.1f\xC2\xB0",
             decl_deg, sublon_deg);
    lv_label_set_text(s_lbl_sun, buf);

    if (!isnan(s_station_lat)) {
        snprintf(buf, sizeof(buf), "QTH  %+5.1f\xC2\xB0  %+6.1f\xC2\xB0",
                 s_station_lat, s_station_lon);
    } else {
        snprintf(buf, sizeof(buf), "QTH  (set locator in Settings)");
    }
    lv_label_set_text(s_lbl_station, buf);

    lv_obj_invalidate(s_canvas);
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    draw_grayline(time(NULL));
}

lv_obj_t *ui_grayline_create(lv_obj_t *parent, const app_config_t *cfg)
{
    ESP_LOGI(TAG, "create");
    if (cfg && cfg->locator[0]) {
        if (!parse_locator(cfg->locator, &s_station_lat, &s_station_lon)) {
            s_station_lat = NAN;
            s_station_lon = NAN;
        }
    }

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 8, 0);

    s_lbl_utc = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_utc, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_utc, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_lbl_utc, "UTC --:--:--");

    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf,
                         GL_W, GL_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_style_border_width(s_canvas, 1, 0);
    lv_obj_set_style_border_color(s_canvas, UI_COL_MUTED, 0);

    s_lbl_sun = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_sun, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_sun, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_lbl_sun, "Sun  --");

    s_lbl_station = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_station, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_station, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_lbl_station, "QTH  --");

    /* First paint right away so the user doesn't briefly see whatever
     * pattern .bss-in-PSRAM started life with. */
    draw_grayline(time(NULL));

    /* Re-render every 30 s. The terminator moves at 0.25 deg per minute
     * of UTC, so 30 s is well below one map-pixel of drift. */
    s_refresh_timer = lv_timer_create(refresh_cb, 30 * 1000, NULL);

    return scr;
}
