#include "ui_internal.h"
#include "ui_theme.h"
#include "app_time.h"
#include "app_propagation.h"
#include "app_dxcluster.h"
#include "app_pota.h"
#include "esp_attr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* HamClock-style at-a-glance summary on the main watch screen.
 *
 * Top:    UTC clock + date / DOY.
 * Row 1:  SFI / SSN / A / K metric cards (from app_propagation).
 * Row 2:  Geomag + X-ray strip.
 * Row 3:  Sunrise / sunset (UTC) for the operator's QTH.
 * Row 4:  Best HF bands right now -- the propagation feed tags each
 *         band as "day" or "night"; we pick which set to show based
 *         on whether the sun is currently above the horizon at QTH.
 * Row 5:  Latest DX spot (from app_dxcluster's spot ring).
 * Row 6:  Latest POTA activation.
 *
 * All cards are polled off the same 250 ms tick that drives the clock.
 * The clock label updates every tick; the data cards only re-pull
 * snapshots once a second (every 4 ticks). Snapshots are copied into
 * PSRAM .bss buffers to keep the LVGL task stack small. */

static lv_obj_t *s_time_lbl;
static lv_obj_t *s_date_lbl;
static lv_obj_t *s_sfi_lbl, *s_ssn_lbl, *s_a_lbl, *s_k_lbl;
static lv_obj_t *s_geomag_lbl, *s_xray_lbl;
static lv_obj_t *s_sun_rise_lbl, *s_sun_set_lbl;
static lv_obj_t *s_band_lbl;
static lv_obj_t *s_dx_lbl;
static lv_obj_t *s_pota_lbl;

static float s_qth_lat = NAN;
static float s_qth_lon = NAN;

EXT_RAM_BSS_ATTR static app_prop_data_t  s_snap_prop;
EXT_RAM_BSS_ATTR static app_dx_state_t   s_snap_dx;
EXT_RAM_BSS_ATTR static app_pota_state_t s_snap_pota;

/* ---- Maidenhead locator -> lat/lon (same parser as ui_grayline) ---- */
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
    int d1 = L[2] - '0', d2 = L[3] - '0';
    if (d1 < 0 || d1 > 9 || d2 < 0 || d2 > 9) return false;
    float lon = A * 20.0f - 180.0f + d1 * 2.0f + 1.0f;
    float lat = B * 10.0f -  90.0f + d2 * 1.0f + 0.5f;
    if (n >= 6) {
        int s1 = (L[4] >= 'a') ? (L[4] - 'a') : (L[4] - 'A');
        int s2 = (L[5] >= 'a') ? (L[5] - 'a') : (L[5] - 'A');
        if (s1 >= 0 && s1 < 24 && s2 >= 0 && s2 < 24) {
            lon += -1.0f + s1 * (2.0f / 24.0f) + (1.0f / 24.0f);
            lat += -0.5f + s2 * (1.0f / 24.0f) + (0.5f / 24.0f);
        }
    }
    *out_lon = lon;
    *out_lat = lat;
    return true;
}

/* ---- solar declination + equation of time (NOAA Fourier series) ---- */
static void sun_params(int yday, float hours_utc,
                       float *decl_rad, float *sublon_deg)
{
    float gamma = 2.0f * (float)M_PI / 365.0f
                * (yday + (hours_utc - 12.0f) / 24.0f);
    float decl = 0.006918f
               - 0.399912f * cosf(gamma)
               + 0.070257f * sinf(gamma)
               - 0.006758f * cosf(2.0f * gamma)
               + 0.000907f * sinf(2.0f * gamma)
               - 0.002697f * cosf(3.0f * gamma)
               + 0.001480f * sinf(3.0f * gamma);
    float eot_min = 229.18f * (0.000075f
                             + 0.001868f * cosf(gamma)
                             - 0.032077f * sinf(gamma)
                             - 0.014615f * cosf(2.0f * gamma)
                             - 0.040849f * sinf(2.0f * gamma));
    float sublon = -15.0f * (hours_utc + eot_min / 60.0f - 12.0f);
    while (sublon >  180.0f) sublon -= 360.0f;
    while (sublon < -180.0f) sublon += 360.0f;
    *decl_rad = decl;
    *sublon_deg = sublon;
}

/* Sunrise + sunset UTC, decimal hours mod 24. False -> polar night/day. */
static bool compute_rise_set(int yday, float lat_deg, float lon_deg,
                             float *rise_utc, float *set_utc)
{
    float decl_rad, sublon_unused;
    sun_params(yday, 12.0f, &decl_rad, &sublon_unused);
    float lat_rad = lat_deg * (float)M_PI / 180.0f;
    float cos_H = -tanf(lat_rad) * tanf(decl_rad);
    if (cos_H >= 1.0f || cos_H <= -1.0f) return false;
    float H_hours = acosf(cos_H) * 12.0f / (float)M_PI;
    /* Equation of time is rolled into the sublon term inside sun_params;
     * for solar noon UTC we recompute the EOT correction directly. */
    float gamma = 2.0f * (float)M_PI / 365.0f * yday;
    float eot_min = 229.18f * (0.000075f
                             + 0.001868f * cosf(gamma)
                             - 0.032077f * sinf(gamma)
                             - 0.014615f * cosf(2.0f * gamma)
                             - 0.040849f * sinf(2.0f * gamma));
    float solar_noon_utc = 12.0f - lon_deg / 15.0f - eot_min / 60.0f;
    float r = solar_noon_utc - H_hours;
    float s = solar_noon_utc + H_hours;
    while (r <  0.0f) r += 24.0f;
    while (r >= 24.0f) r -= 24.0f;
    while (s <  0.0f) s += 24.0f;
    while (s >= 24.0f) s -= 24.0f;
    *rise_utc = r;
    *set_utc  = s;
    return true;
}

static bool qth_is_daylit(int yday, float hours_utc, float lat_deg, float lon_deg)
{
    float decl_rad, sublon_deg;
    sun_params(yday, hours_utc, &decl_rad, &sublon_deg);
    float lat_rad = lat_deg * (float)M_PI / 180.0f;
    float dlon = (lon_deg - sublon_deg) * (float)M_PI / 180.0f;
    float sin_elev = sinf(lat_rad) * sinf(decl_rad)
                   + cosf(lat_rad) * cosf(decl_rad) * cosf(dlon);
    return sin_elev > 0.0f;
}

/* ---- band condition rendering ---- */
static int cond_rank(const char *c)
{
    if (strstr(c, "Good")) return 3;
    if (strstr(c, "Fair")) return 2;
    if (strstr(c, "Poor")) return 1;
    return 0;
}
static const char *cond_short(const char *c)
{
    if (strstr(c, "Good")) return "OPEN";
    if (strstr(c, "Fair")) return "FAIR";
    if (strstr(c, "Poor")) return "CLOSED";
    return "-";
}
static lv_color_t geomag_color(const char *g)
{
    if (strstr(g, "Quiet"))     return lv_color_hex(0x00d97e);
    if (strstr(g, "Storm"))     return lv_color_hex(0xff4d6d);
    if (strstr(g, "Active") ||
        strstr(g, "Unsettled")) return lv_color_hex(0xffb020);
    return UI_COL_MUTED;
}

static void format_best_bands(const app_prop_data_t *p, bool day,
                              char *buf, size_t sz)
{
    const char *match = day ? "day" : "night";
    int best_idx[2] = {-1, -1};
    int best_rank[2] = {0, 0};
    for (int i = 0; i < p->band_count; i++) {
        if (strcmp(p->bands[i].time, match) != 0) continue;
        int r = cond_rank(p->bands[i].condition);
        if (r > best_rank[0]) {
            best_rank[1] = best_rank[0]; best_idx[1] = best_idx[0];
            best_rank[0] = r;            best_idx[0] = i;
        } else if (r > best_rank[1]) {
            best_rank[1] = r;            best_idx[1] = i;
        }
    }
    if (best_idx[0] < 0) {
        snprintf(buf, sz, "(no data)");
    } else if (best_idx[1] < 0) {
        snprintf(buf, sz, "%s %s",
                 p->bands[best_idx[0]].band,
                 cond_short(p->bands[best_idx[0]].condition));
    } else {
        snprintf(buf, sz, "%s %s  \xC2\xB7  %s %s",
                 p->bands[best_idx[0]].band,
                 cond_short(p->bands[best_idx[0]].condition),
                 p->bands[best_idx[1]].band,
                 cond_short(p->bands[best_idx[1]].condition));
    }
}

/* ---- 1 Hz refresh ---- */
static void refresh_slow(const struct tm *utc)
{
    char buf[80];

    /* Sun rise/set at QTH */
    if (!isnan(s_qth_lat) && !isnan(s_qth_lon)) {
        float rise, set;
        if (compute_rise_set(utc->tm_yday, s_qth_lat, s_qth_lon, &rise, &set)) {
            int rh = (int)rise, rm = (int)((rise - rh) * 60.0f) % 60;
            int sh = (int)set,  sm = (int)((set  - sh) * 60.0f) % 60;
            snprintf(buf, sizeof(buf), LV_SYMBOL_UP "  %02d:%02d UTC", rh, rm);
            lv_label_set_text(s_sun_rise_lbl, buf);
            snprintf(buf, sizeof(buf), LV_SYMBOL_DOWN "  %02d:%02d UTC", sh, sm);
            lv_label_set_text(s_sun_set_lbl, buf);
        } else {
            lv_label_set_text(s_sun_rise_lbl, LV_SYMBOL_UP   "  ---");
            lv_label_set_text(s_sun_set_lbl,  LV_SYMBOL_DOWN "  ---");
        }
    } else {
        lv_label_set_text(s_sun_rise_lbl, LV_SYMBOL_UP   "  set locator");
        lv_label_set_text(s_sun_set_lbl,  LV_SYMBOL_DOWN "  --");
    }

    /* Solar metrics + best HF */
    app_propagation_get(&s_snap_prop);
    if (s_snap_prop.valid) {
        snprintf(buf, sizeof(buf), "%d", s_snap_prop.solar_flux);
        lv_label_set_text(s_sfi_lbl, buf);
        snprintf(buf, sizeof(buf), "%d", s_snap_prop.sunspots);
        lv_label_set_text(s_ssn_lbl, buf);
        snprintf(buf, sizeof(buf), "%d", s_snap_prop.a_index);
        lv_label_set_text(s_a_lbl, buf);
        snprintf(buf, sizeof(buf), "%d", s_snap_prop.k_index);
        lv_label_set_text(s_k_lbl, buf);

        lv_label_set_text(s_geomag_lbl,
                          s_snap_prop.geomag[0] ? s_snap_prop.geomag : "-");
        lv_obj_set_style_text_color(s_geomag_lbl,
                                    geomag_color(s_snap_prop.geomag), 0);
        lv_label_set_text(s_xray_lbl,
                          s_snap_prop.xray[0] ? s_snap_prop.xray : "-");

        float hours_now = utc->tm_hour
                        + utc->tm_min  / 60.0f
                        + utc->tm_sec  / 3600.0f;
        bool day_qth;
        if (!isnan(s_qth_lat)) {
            day_qth = qth_is_daylit(utc->tm_yday, hours_now,
                                    s_qth_lat, s_qth_lon);
        } else {
            day_qth = (utc->tm_hour >= 6 && utc->tm_hour < 18);
        }
        char bbuf[64];
        format_best_bands(&s_snap_prop, day_qth, bbuf, sizeof(bbuf));
        snprintf(buf, sizeof(buf), "%s HF:  %s",
                 day_qth ? "Day" : "Night", bbuf);
        lv_label_set_text(s_band_lbl, buf);
    }

    /* Latest DX spot */
    app_dxcluster_get(&s_snap_dx);
    if (s_snap_dx.count > 0) {
        const app_dx_spot_t *sp = &s_snap_dx.spots[s_snap_dx.head];
        snprintf(buf, sizeof(buf), "%s  %s  %s",
                 sp->dx_call, sp->freq, sp->time);
        lv_label_set_text(s_dx_lbl, buf);
    } else {
        lv_label_set_text(s_dx_lbl, "(waiting)");
    }

    /* Latest POTA activation */
    app_pota_get(&s_snap_pota);
    if (s_snap_pota.count > 0) {
        const app_pota_spot_t *sp = &s_snap_pota.spots[0];
        snprintf(buf, sizeof(buf), "%s  %s  %s %s",
                 sp->activator, sp->park_ref, sp->freq, sp->mode);
        lv_label_set_text(s_pota_lbl, buf);
    } else {
        lv_label_set_text(s_pota_lbl, "(waiting)");
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    struct tm utc;
    app_time_now_utc(&utc);

    char buf[24];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             utc.tm_hour, utc.tm_min, utc.tm_sec);
    lv_label_set_text(s_time_lbl, buf);

    static const char *months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                   "JUL","AUG","SEP","OCT","NOV","DEC"};
    char date[40];
    snprintf(date, sizeof(date), "%02d %s %04d  \xC2\xB7  DOY %03d",
             utc.tm_mday, months[utc.tm_mon % 12],
             1900 + utc.tm_year, utc.tm_yday + 1);
    lv_label_set_text(s_date_lbl, date);

    /* Heavier data cards only every 4 ticks (~1 Hz). */
    static int slow_div = 0;
    if (++slow_div >= 4) {
        slow_div = 0;
        refresh_slow(&utc);
    }
}

/* ---- card builders ---- */
static void build_metric(lv_obj_t *parent, const char *name, lv_obj_t **value_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    ui_theme_style_panel(card);
    lv_obj_set_size(card, LV_PCT(23), 58);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_style_pad_gap(card, 2, 0);

    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_color(l, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_color(v, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_24, 0);
    *value_out = v;
}

static lv_obj_t *build_strip(lv_obj_t *parent)
{
    lv_obj_t *s = lv_obj_create(parent);
    ui_theme_style_panel(s);
    lv_obj_set_size(s, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(s, 10, 0);
    lv_obj_set_style_pad_ver(s, 4, 0);
    return s;
}

static void labeled_value(lv_obj_t *parent, const char *header,
                          lv_obj_t **value_out, lv_color_t value_color)
{
    lv_obj_t *h = lv_label_create(parent);
    lv_label_set_text(h, header);
    lv_obj_set_style_text_color(h, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_color(v, value_color, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    *value_out = v;
}

lv_obj_t *ui_watch_create(lv_obj_t *parent, const app_config_t *cfg)
{
    if (cfg && cfg->locator[0]) {
        if (!parse_locator(cfg->locator, &s_qth_lat, &s_qth_lon)) {
            s_qth_lat = NAN; s_qth_lon = NAN;
        }
    }

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 6, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);

    s_time_lbl = lv_label_create(scr);
    lv_label_set_text(s_time_lbl, "--:--:--");
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_lbl, UI_COL_ACCENT, 0);

    s_date_lbl = lv_label_create(scr);
    lv_label_set_text(s_date_lbl, "-- --- ----  \xC2\xB7  DOY ---");
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_date_lbl, UI_COL_TEXT, 0);

    /* Solar metric cards */
    lv_obj_t *mrow = lv_obj_create(scr);
    lv_obj_remove_style_all(mrow);
    lv_obj_set_size(mrow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mrow, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    build_metric(mrow, "SFI", &s_sfi_lbl);
    build_metric(mrow, "SSN", &s_ssn_lbl);
    build_metric(mrow, "A",   &s_a_lbl);
    build_metric(mrow, "K",   &s_k_lbl);

    /* Geomag + X-ray */
    lv_obj_t *geo = build_strip(scr);
    labeled_value(geo, "GEOMAG", &s_geomag_lbl, UI_COL_TEXT);
    labeled_value(geo, "X-RAY",  &s_xray_lbl,   UI_COL_TEXT);

    /* Sun rise/set at QTH */
    lv_obj_t *sun = build_strip(scr);
    s_sun_rise_lbl = lv_label_create(sun);
    lv_label_set_text(s_sun_rise_lbl, LV_SYMBOL_UP "  --");
    lv_obj_set_style_text_color(s_sun_rise_lbl, UI_COL_TEXT, 0);
    s_sun_set_lbl = lv_label_create(sun);
    lv_label_set_text(s_sun_set_lbl, LV_SYMBOL_DOWN "  --");
    lv_obj_set_style_text_color(s_sun_set_lbl, UI_COL_TEXT, 0);

    /* Best HF bands now */
    lv_obj_t *bands = build_strip(scr);
    s_band_lbl = lv_label_create(bands);
    lv_label_set_text(s_band_lbl, "HF: --");
    lv_obj_set_style_text_color(s_band_lbl, UI_COL_TEXT, 0);

    /* Latest DX */
    lv_obj_t *dx = build_strip(scr);
    lv_obj_t *dx_hdr = lv_label_create(dx);
    lv_label_set_text(dx_hdr, "DX");
    lv_obj_set_style_text_color(dx_hdr, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(dx_hdr, &lv_font_montserrat_14, 0);
    s_dx_lbl = lv_label_create(dx);
    lv_label_set_text(s_dx_lbl, "(waiting)");
    lv_obj_set_style_text_color(s_dx_lbl, UI_COL_ACCENT, 0);

    /* Latest POTA */
    lv_obj_t *pota = build_strip(scr);
    lv_obj_t *pota_hdr = lv_label_create(pota);
    lv_label_set_text(pota_hdr, "POTA");
    lv_obj_set_style_text_color(pota_hdr, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(pota_hdr, &lv_font_montserrat_14, 0);
    s_pota_lbl = lv_label_create(pota);
    lv_label_set_text(s_pota_lbl, "(waiting)");
    lv_obj_set_style_text_color(s_pota_lbl, UI_COL_ACCENT, 0);

    return scr;
}

void ui_watch_register_tick(lv_obj_t *screen)
{
    (void)screen;
    lv_timer_create(tick_cb, 250, NULL);
    tick_cb(NULL);
}
