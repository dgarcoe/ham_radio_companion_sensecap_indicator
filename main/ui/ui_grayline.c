#include "ui_internal.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui.h"
#include "app_nvs.h"
#include "app_propagation.h"
#include "bsp.h"
#include "esp_log.h"
#include "esp_attr.h"

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Grayline map - full-width equirectangular world view shaded by the
 * solar elevation at the current UTC instant. Continent outlines are
 * rasterized once at boot from a set of hand-traced polygons (compact,
 * recognizable, no asset to bundle). Land and sea get distinct day +
 * night palettes so the map reads like Earth even when most of it's
 * in shadow. Markers: subsolar point, operator's Maidenhead locator. */

#define GL_W 460
#define GL_H 230
#define LANDMASK_BYTES_PER_ROW ((GL_W + 7) / 8)

static const char *TAG = "ui_grayline";

/* Canvas backing buffer (RGB565) + land/sea bitmask, both in PSRAM .bss
 * so we don't touch internal DRAM. The canvas is the big one at
 * 480x240x2 = 225 KB; the mask is one bit per pixel = ~14 KB. */
EXT_RAM_BSS_ATTR static uint16_t s_canvas_buf[GL_W * GL_H];
EXT_RAM_BSS_ATTR static uint8_t  s_landmask[GL_H * LANDMASK_BYTES_PER_ROW];

static lv_obj_t *s_canvas;
static lv_obj_t *s_lbl_utc;
static lv_obj_t *s_lbl_sun;
static lv_obj_t *s_lbl_station;
static lv_obj_t *s_lbl_moon;
static lv_obj_t *s_lbl_inspect;
static lv_timer_t *s_refresh_timer;
static lv_timer_t *s_inspect_hide_timer;

static float s_station_lat = NAN;
static float s_station_lon = NAN;

/* MUF contour thresholds (squared so we can compare against muf^2 and
 * skip the per-pixel sqrtf). Values: 14 MHz (20m), 21 MHz (15m),
 * 28 MHz (10m). Colours picked to read against both day and night
 * fills -- 20m warm red, 15m amber, 10m bright green. */
#define MUF_BAND_COUNT 3
static const float    s_muf_thresh_sq[MUF_BAND_COUNT] = {196.0f, 441.0f, 784.0f};
static const uint16_t s_muf_colors[MUF_BAND_COUNT] = {
    /* rgb565 inlined to keep the array const */
    ((0xFF & 0xF8) << 8) | ((0x60 & 0xFC) << 3) | (0x60 >> 3),  /* 14 */
    ((0xFF & 0xF8) << 8) | ((0xC0 & 0xFC) << 3) | (0x40 >> 3),  /* 21 */
    ((0x60 & 0xF8) << 8) | ((0xFF & 0xFC) << 3) | (0xA0 >> 3),  /* 28 */
};

/* Davies-style overhead MUF approximation:
 *   foF2 (MHz) ~ MUF_K * sqrt(max(SFI, SFI_FLOOR))
 *                       * sqrt(max(cos_zenith, COS_ZEN_FLOOR))
 *   MUF_overhead ~ foF2 * MUF_HOP_FACTOR
 * Squared form (so the per-pixel test stays sqrt-free):
 *   muf^2 = MUF_K^2 * MUF_HOP_FACTOR^2 * SFI_eff * cos_zen_eff
 *         = M2_BASE                  * cos_zen_eff      (with SFI baked in)
 * The cos-zenith floor gives a small residual ionization on the night
 * side so contours degrade smoothly across the terminator instead of
 * snapping to zero. */
#define MUF_K           1.15f
#define MUF_HOP_FACTOR  3.0f
#define MUF_SFI_FLOOR   60.0f
#define MUF_COS_FLOOR   0.10f

/* ---------- continent polygons -------------------------------------
 * Each polygon is a flat (lat, lon) int16_t pair list, traced from the
 * coastline at roughly 5-degree precision -- coarse, but enough to be
 * unambiguously a world map at 480 px wide. Antarctica is drawn as a
 * latitude strip in rasterize_world() since wrapping a polygon around
 * the south pole is awkward. */

#define V(lat, lon) (int16_t)(lat), (int16_t)(lon)

static const int16_t POLY_NORTH_AMERICA[] = {
    /* One clean clockwise trace: NW Alaska arctic east -> Atlantic
     * down to Florida -> Gulf -> east Mexico to Panama isthmus ->
     * Pacific north back through Baja, California, Vancouver, Alaska. */
    V( 70,-160), V( 73,-150), V( 73,-130), V( 73,-115), V( 73, -95),
    V( 78, -80), V( 80, -70), V( 75, -62),
    V( 60, -65), V( 55, -65), V( 50, -57), V( 47, -53),
    V( 45, -62), V( 42, -70), V( 38, -75), V( 35, -76),
    V( 30, -81), V( 25, -80),
    V( 30, -84), V( 30, -88), V( 26, -97),
    V( 22, -97), V( 18, -95), V( 16, -92), V( 13, -85),
    V( 11, -84), V(  8, -78),
    V( 10, -86), V( 13, -91), V( 15, -95), V( 18,-102), V( 22,-107),
    V( 27,-112), V( 33,-118), V( 40,-124), V( 47,-124),
    V( 55,-134), V( 60,-145), V( 60,-158), V( 65,-167), V( 70,-167),
    V( 70,-160)
};

static const int16_t POLY_GREENLAND[] = {
    V( 83, -32), V( 80, -20), V( 75, -20), V( 70, -22), V( 65, -38),
    V( 70, -52), V( 76, -58), V( 80, -55), V( 83, -32)
};

static const int16_t POLY_SOUTH_AMERICA[] = {
    V( 12, -72), V( 12, -62), V( 10, -60), V(  8, -52), V(  5, -52),
    V(  0, -50), V( -5, -36), V(-13, -38), V(-23, -41), V(-34, -53),
    V(-40, -62), V(-50, -68), V(-55, -68), V(-54, -71), V(-50, -74),
    V(-42, -74), V(-30, -71), V(-18, -71), V( -6, -80), V(  2, -79),
    V( 10, -76), V( 12, -72)
};

static const int16_t POLY_AFRICA[] = {
    V( 36,  -6), V( 37,  10), V( 33,  22), V( 32,  33), V( 30,  33),
    V( 22,  37), V( 12,  44), V( 12,  51), V(  4,  47), V(  0,  42),
    V( -5,  40), V(-12,  40), V(-20,  35), V(-28,  33), V(-34,  20),
    V(-30,  18), V(-20,  14), V(-10,  13), V(  0,   9), V(  5,  -2),
    V(  5,  -8), V( 10, -15), V( 16, -17), V( 22, -17), V( 28, -10),
    V( 32,  -8), V( 36,  -6)
};

static const int16_t POLY_EURASIA[] = {
    /* Clean single-loop trace: NW Russia round the arctic east to the
     * Bering, down the East Asian coast, around the Malay peninsula,
     * around India, around Arabia, up through Anatolia, across the
     * Mediterranean / Iberia, up the Atlantic, back across the North
     * Sea to NW Russia. Britain + Ireland + Iceland + Japan are
     * separate polygons. */
    /* Arctic, NW to NE */
    V( 70,  30), V( 76,  60), V( 78,  90), V( 76, 110), V( 73, 140),
    V( 68, 165), V( 60, 168),
    /* East Asia south to Indochina */
    V( 55, 162), V( 50, 145), V( 42, 134), V( 40, 130), V( 38, 122),
    V( 32, 121), V( 30, 122), V( 22, 113), V( 18, 109), V( 11, 109),
    V(  9, 104), V(  2, 102),
    /* Malay W coast up to Bay of Bengal */
    V(  8,  99), V( 16,  98), V( 21,  92), V( 22,  90),
    /* India E coast down to tip */
    V( 19,  85), V( 13,  80), V(  8,  77),
    /* India W coast up to Karachi */
    V( 15,  73), V( 22,  69), V( 24,  65),
    /* Arabian Sea / Gulf of Oman south to Yemen */
    V( 25,  60), V( 22,  59), V( 17,  56), V( 12,  53),
    /* Across S Arabia + up the Red Sea side */
    V( 12,  44), V( 16,  43), V( 22,  39), V( 28,  34),
    /* Levant + Anatolia */
    V( 33,  35), V( 36,  36), V( 41,  29), V( 41,  26),
    /* Balkans + Italy + S France */
    V( 38,  23), V( 40,  19), V( 44,  13), V( 41,  10),
    V( 43,   8), V( 43,   4),
    /* Iberia + Bay of Biscay + Channel */
    V( 36,  -5), V( 38, -10), V( 43,  -9), V( 48,  -5),
    V( 48,   0), V( 51,   2), V( 53,   8),
    /* Denmark + Scandinavia back to Kola */
    V( 56,  12), V( 60,  10), V( 65,  18), V( 70,  25), V( 70,  30)
};

static const int16_t POLY_BRITAIN[] = {
    V( 58,  -5), V( 56,  -1), V( 53,   2), V( 51,   1), V( 50,  -4),
    V( 51,  -5), V( 53,  -4), V( 55,  -6), V( 58,  -5)
};

static const int16_t POLY_IRELAND[] = {
    V( 55,  -7), V( 54,  -6), V( 52,  -6), V( 51,  -9), V( 53, -10),
    V( 55,  -7)
};

static const int16_t POLY_ICELAND[] = {
    V( 66, -22), V( 66, -14), V( 64, -14), V( 63, -22), V( 66, -22)
};

static const int16_t POLY_AUSTRALIA[] = {
    V(-12, 130), V(-12, 142), V(-17, 142), V(-22, 150), V(-32, 153),
    V(-38, 145), V(-38, 140), V(-35, 137), V(-32, 117), V(-22, 114),
    V(-12, 130)
};

static const int16_t POLY_NEW_ZEALAND_N[] = {
    V(-34, 173), V(-37, 178), V(-41, 175), V(-40, 174), V(-37, 173),
    V(-34, 173)
};

static const int16_t POLY_NEW_ZEALAND_S[] = {
    V(-41, 174), V(-43, 173), V(-47, 168), V(-45, 167), V(-42, 171),
    V(-41, 174)
};

static const int16_t POLY_JAPAN[] = {
    V( 45, 142), V( 43, 146), V( 38, 142), V( 35, 140), V( 33, 132),
    V( 36, 137), V( 40, 140), V( 45, 142)
};

static const int16_t POLY_MADAGASCAR[] = {
    V(-12,  49), V(-18,  49), V(-25,  47), V(-25,  44), V(-18,  44),
    V(-12,  49)
};

static const int16_t POLY_BORNEO[] = {
    V(  7, 117), V(  4, 119), V(  0, 119), V( -4, 114), V( -2, 110),
    V(  2, 109), V(  7, 117)
};

static const int16_t POLY_SUMATRA[] = {
    V(  6,  95), V(  3, 100), V( -3, 104), V( -6, 105), V( -5,  99),
    V(  1,  96), V(  6,  95)
};

static const int16_t POLY_NEW_GUINEA[] = {
    V( -1, 131), V( -1, 141), V( -3, 144), V( -8, 148), V(-10, 150),
    V(-10, 142), V( -8, 138), V( -5, 132), V( -1, 131)
};

static const int16_t POLY_PHILIPPINES[] = {
    V( 18, 122), V( 14, 124), V(  9, 126), V(  5, 125), V(  8, 122),
    V( 13, 121), V( 18, 122)
};

static const int16_t POLY_SRI_LANKA[] = {
    V(  9,  80), V(  7,  82), V(  6,  81), V(  7,  80), V(  9,  80)
};

#undef V

typedef struct {
    const int16_t *pts;
    size_t n_verts;
} polygon_t;

#define POLY(name) { name, sizeof(name) / sizeof(name[0]) / 2 }

static const polygon_t s_polygons[] = {
    POLY(POLY_NORTH_AMERICA),
    POLY(POLY_GREENLAND),
    POLY(POLY_SOUTH_AMERICA),
    POLY(POLY_AFRICA),
    POLY(POLY_EURASIA),
    POLY(POLY_BRITAIN),
    POLY(POLY_IRELAND),
    POLY(POLY_ICELAND),
    POLY(POLY_AUSTRALIA),
    POLY(POLY_NEW_ZEALAND_N),
    POLY(POLY_NEW_ZEALAND_S),
    POLY(POLY_JAPAN),
    POLY(POLY_MADAGASCAR),
    POLY(POLY_BORNEO),
    POLY(POLY_SUMATRA),
    POLY(POLY_NEW_GUINEA),
    POLY(POLY_PHILIPPINES),
    POLY(POLY_SRI_LANKA),
};
static const size_t s_polygon_count = sizeof(s_polygons) / sizeof(s_polygons[0]);

#undef POLY

/* ---------- land-mask rasterisation -------------------------------- */

static inline void mask_set(int x, int y)
{
    if (x < 0 || x >= GL_W || y < 0 || y >= GL_H) return;
    s_landmask[y * LANDMASK_BYTES_PER_ROW + (x >> 3)] |= (1u << (x & 7));
}

static inline bool mask_get(int x, int y)
{
    return (s_landmask[y * LANDMASK_BYTES_PER_ROW + (x >> 3)] >> (x & 7)) & 1u;
}

/* Standard even-odd scan-line fill. For each row we find every x where
 * an edge of the polygon crosses the horizontal y line, sort them, and
 * fill between consecutive pairs. */
static void rasterize_polygon(const int16_t *poly, size_t n_verts)
{
    int x_cross[64];
    for (int py = 0; py < GL_H; py++) {
        float lat_deg = 90.0f - (py + 0.5f) * (180.0f / GL_H);
        int n_cross = 0;
        for (size_t i = 0; i < n_verts; i++) {
            size_t j = (i + 1) % n_verts;
            float lat_i = poly[i * 2 + 0];
            float lon_i = poly[i * 2 + 1];
            float lat_j = poly[j * 2 + 0];
            float lon_j = poly[j * 2 + 1];
            if ((lat_i > lat_deg) == (lat_j > lat_deg)) continue;
            float t = (lat_deg - lat_i) / (lat_j - lat_i);
            float lon_x = lon_i + t * (lon_j - lon_i);
            int x = (int)((lon_x + 180.0f) * (GL_W / 360.0f));
            if (n_cross < (int)(sizeof(x_cross) / sizeof(x_cross[0]))) {
                x_cross[n_cross++] = x;
            }
        }
        /* insertion-sort the crossings */
        for (int i = 1; i < n_cross; i++) {
            int v = x_cross[i], k = i;
            while (k > 0 && x_cross[k - 1] > v) {
                x_cross[k] = x_cross[k - 1];
                k--;
            }
            x_cross[k] = v;
        }
        for (int i = 0; i + 1 < n_cross; i += 2) {
            int x0 = x_cross[i], x1 = x_cross[i + 1];
            if (x0 < 0) x0 = 0;
            if (x1 > GL_W) x1 = GL_W;
            for (int x = x0; x < x1; x++) mask_set(x, py);
        }
    }
}

static void rasterize_world(void)
{
    memset(s_landmask, 0, sizeof(s_landmask));
    for (size_t i = 0; i < s_polygon_count; i++) {
        rasterize_polygon(s_polygons[i].pts, s_polygons[i].n_verts);
    }
    /* Antarctica: simple latitude band south of -63 deg. */
    int y0 = (int)((90.0f - -63.0f) * (GL_H / 180.0f));
    for (int y = y0; y < GL_H; y++) {
        for (int x = 0; x < GL_W; x++) mask_set(x, y);
    }
}

/* ---------- locator parse + solar position ------------------------- */

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

static void compute_sun(time_t now, float *decl_deg, float *sublon_deg)
{
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    int N = tm_utc.tm_yday;
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

/* ---------- colour palette ----------------------------------------- */

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Day, sea: cool blue. Day, land: warm tan. Night, sea: near-black
 * navy. Night, land: dim slate so continents stay visible after dark.
 * The grayline band fades between day and night using the same pair
 * of colours, so the silhouette of every continent stays continuous
 * across the terminator. */
static inline uint16_t shade(bool land, float sin_elev)
{
    uint8_t day_r, day_g, day_b;
    uint8_t nig_r, nig_g, nig_b;
    if (land) {
        day_r = 0xC8; day_g = 0xA8; day_b = 0x60;
        nig_r = 0x28; nig_g = 0x26; nig_b = 0x22;
    } else {
        day_r = 0x48; day_g = 0x80; day_b = 0xC0;
        nig_r = 0x04; nig_g = 0x08; nig_b = 0x20;
    }
    if (sin_elev >  0.10f) return rgb565(day_r, day_g, day_b);
    if (sin_elev < -0.10f) return rgb565(nig_r, nig_g, nig_b);
    float t = (sin_elev + 0.10f) / 0.20f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* Push a hint of warm grayline tint into the blend - feels right
     * at sunrise/sunset on a real map. */
    float tint = 1.0f - fabsf(2.0f * t - 1.0f);   /* peak in middle */
    uint8_t r = (uint8_t)(nig_r + t * (day_r - nig_r) + tint * 0x18);
    uint8_t g = (uint8_t)(nig_g + t * (day_g - nig_g) + tint * 0x06);
    uint8_t b = (uint8_t)(nig_b + t * (day_b - nig_b));
    return rgb565(r, g, b);
}

static void plot_marker_r(int cx, int cy, uint16_t color, int r)
{
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r2) continue;
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= GL_W || py < 0 || py >= GL_H) continue;
            s_canvas_buf[py * GL_W + px] = color;
        }
    }
}

static void plot_marker(int cx, int cy, uint16_t color)
{
    plot_marker_r(cx, cy, color, 3);
}

/* ---------- moon subpoint (low-precision lunar ephemeris) ---------- */

/* Approximate location of the moon's subpoint at `now`. Based on the
 * standard short series from Meeus chap. 47 (mean longitude + first
 * correction in M; mean argument-of-latitude for ecliptic lat). Good
 * to a few degrees -- plenty for plotting a dot on a 460-px map.
 * Uses double precision because the days-since-J2000 term grows past
 * what a 24-bit float mantissa preserves to the day. */
static void compute_moon(time_t now, float *sublat_deg, float *sublon_deg)
{
    /* J2000.0 = 2000-01-01 12:00:00 UTC = unix 946728000. */
    double d = ((double)now - 946728000.0) / 86400.0;

    double L = fmod(218.316 + 13.176396 * d, 360.0);  /* mean longitude */
    double M = fmod(134.963 + 13.064993 * d, 360.0);  /* mean anomaly  */
    double F = fmod( 93.272 + 13.229350 * d, 360.0);  /* arg. of lat.  */

    const double DEG = M_PI / 180.0;
    double lambda = (L + 6.289 * sin(M * DEG)) * DEG;    /* ecliptic lon */
    double beta   = (5.128 * sin(F * DEG))     * DEG;    /* ecliptic lat */
    double eps    = 23.4397 * DEG;                       /* obliquity   */

    double sin_dec = sin(beta) * cos(eps)
                   + cos(beta) * sin(eps) * sin(lambda);
    double dec_rad = asin(sin_dec);
    double ra_rad  = atan2(sin(lambda) * cos(eps) - tan(beta) * sin(eps),
                           cos(lambda));
    double ra_hours = ra_rad * (12.0 / M_PI);
    if (ra_hours < 0.0) ra_hours += 24.0;

    /* GMST in hours, simplified from days since J2000. */
    double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0);
    if (gmst < 0.0) gmst += 24.0;

    double ha = gmst - ra_hours;                /* hour angle, hours */
    double sub_lon = -15.0 * ha;
    while (sub_lon >  180.0) sub_lon -= 360.0;
    while (sub_lon < -180.0) sub_lon += 360.0;

    *sublat_deg = (float)(dec_rad * (180.0 / M_PI));
    *sublon_deg = (float)sub_lon;
}

/* ---------- Maidenhead + great-circle (for touch-to-inspect) ------- */

static void latlon_to_maidenhead(float lat, float lon, char out[7])
{
    float a = lon + 180.0f;
    float b = lat +  90.0f;
    if (a < 0.0f)         a = 0.0f;
    if (a >= 360.0f)      a = 359.999f;
    if (b < 0.0f)         b = 0.0f;
    if (b >= 180.0f)      b = 179.999f;

    int A = (int)(a / 20.0f);     a -= A * 20.0f;
    int B = (int)(b / 10.0f);     b -= B * 10.0f;
    int d1 = (int)(a / 2.0f);     a -= d1 * 2.0f;
    int d2 = (int)b;              b -= d2;
    int s1 = (int)(a * 12.0f);    /* 24 subsquares per 2-deg square */
    int s2 = (int)(b * 24.0f);
    if (s1 > 23) s1 = 23;
    if (s2 > 23) s2 = 23;

    out[0] = 'A' + A;
    out[1] = 'A' + B;
    out[2] = '0' + d1;
    out[3] = '0' + d2;
    out[4] = 'a' + s1;
    out[5] = 'a' + s2;
    out[6] = '\0';
}

/* Haversine distance (km) + initial bearing (degrees, 0 = north,
 * clockwise) from point 1 to point 2. */
static void great_circle(float lat1, float lon1, float lat2, float lon2,
                         float *km, float *bearing_deg)
{
    const float DEG = (float)M_PI / 180.0f;
    float phi1 = lat1 * DEG;
    float phi2 = lat2 * DEG;
    float dphi = (lat2 - lat1) * DEG;
    float dlam = (lon2 - lon1) * DEG;

    float sdp = sinf(dphi * 0.5f);
    float sdl = sinf(dlam * 0.5f);
    float a = sdp * sdp + cosf(phi1) * cosf(phi2) * sdl * sdl;
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    *km = 6371.0f * c;

    float y = sinf(dlam) * cosf(phi2);
    float x = cosf(phi1) * sinf(phi2)
            - sinf(phi1) * cosf(phi2) * cosf(dlam);
    float brg = atan2f(y, x) * (180.0f / (float)M_PI);
    if (brg < 0.0f) brg += 360.0f;
    *bearing_deg = brg;
}

/* ---------- touch-to-inspect --------------------------------------- */

static void inspect_hide_cb(lv_timer_t *t)
{
    (void)t;
    if (s_lbl_inspect) lv_obj_add_flag(s_lbl_inspect, LV_OBJ_FLAG_HIDDEN);
    s_inspect_hide_timer = NULL;
}

static void on_canvas_clicked(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    lv_area_t area;
    lv_obj_get_coords(s_canvas, &area);
    int px = pt.x - area.x1;
    int py = pt.y - area.y1;
    if (px < 0 || px >= GL_W || py < 0 || py >= GL_H) return;

    float lon = -180.0f + (px + 0.5f) * (360.0f / GL_W);
    float lat =   90.0f - (py + 0.5f) * (180.0f / GL_H);

    char grid[7];
    latlon_to_maidenhead(lat, lon, grid);

    /* Sun elevation at the tapped point => day/night flag. */
    time_t now = time(NULL);
    float decl_deg, sublon_deg;
    compute_sun(now, &decl_deg, &sublon_deg);
    float decl_rad = decl_deg * (float)M_PI / 180.0f;
    float lat_rad  = lat       * (float)M_PI / 180.0f;
    float dlon_rad = (lon - sublon_deg) * (float)M_PI / 180.0f;
    float sin_elev = sinf(lat_rad) * sinf(decl_rad)
                   + cosf(lat_rad) * cosf(decl_rad) * cosf(dlon_rad);
    const char *daynight = (sin_elev >  0.10f) ? "DAY"
                         : (sin_elev < -0.10f) ? "NIGHT"
                                               : "GRAYLINE";

    /* Local mean time at this longitude, no DST / tz corrections. */
    struct tm utc;
    gmtime_r(&now, &utc);
    float utc_hours = utc.tm_hour + utc.tm_min / 60.0f;
    float local_hours = utc_hours + lon / 15.0f;
    while (local_hours <  0.0f) local_hours += 24.0f;
    while (local_hours >= 24.0f) local_hours -= 24.0f;
    int lh = (int)local_hours;
    int lm = (int)((local_hours - lh) * 60.0f);
    if (lm < 0) lm = 0;
    if (lm > 59) lm = 59;

    char dist_str[48];
    if (!isnan(s_station_lat) && !isnan(s_station_lon)) {
        float km, brg;
        great_circle(s_station_lat, s_station_lon, lat, lon, &km, &brg);
        snprintf(dist_str, sizeof(dist_str),
                 "%.0f km   bearing %.0f\xC2\xB0",
                 (double)km, (double)brg);
    } else {
        snprintf(dist_str, sizeof(dist_str), "(set locator for distance)");
    }

    char buf[160];
    snprintf(buf, sizeof(buf),
             "%+5.1f\xC2\xB0 %+6.1f\xC2\xB0   %s   %s   %02d:%02d local\n%s",
             (double)lat, (double)lon, grid, daynight, lh, lm, dist_str);
    lv_label_set_text(s_lbl_inspect, buf);
    lv_obj_remove_flag(s_lbl_inspect, LV_OBJ_FLAG_HIDDEN);

    /* Re-arm a 6 s one-shot to dismiss. */
    if (s_inspect_hide_timer) {
        lv_timer_reset(s_inspect_hide_timer);
    } else {
        s_inspect_hide_timer = lv_timer_create(inspect_hide_cb, 6000, NULL);
        lv_timer_set_repeat_count(s_inspect_hide_timer, 1);
    }
}

/* ---------- main draw ---------------------------------------------- */

static void draw_grayline(time_t now)
{
    float decl_deg, sublon_deg;
    compute_sun(now, &decl_deg, &sublon_deg);
    float decl_rad = decl_deg * (float)M_PI / 180.0f;
    float sin_decl = sinf(decl_rad);
    float cos_decl = cosf(decl_rad);

    /* cos(lon - sublon) depends only on x; precompute it once per
     * column to drop ~115000 cosf calls per repaint to ~480. */
    static float cos_dlon[GL_W];
    for (int x = 0; x < GL_W; x++) {
        float lon_deg = -180.0f + (x + 0.5f) * (360.0f / GL_W);
        float dlon = (lon_deg - sublon_deg) * (float)M_PI / 180.0f;
        cos_dlon[x] = cosf(dlon);
    }

    /* Pull SFI from the propagation snapshot. If it isn't valid yet
     * (first fetch hasn't landed), skip the MUF contours entirely --
     * a fake SFI of 0 would just paint a sea-level contour everywhere. */
    app_prop_data_t prop;
    app_propagation_get(&prop);
    bool muf_on = prop.valid && prop.solar_flux > 0;
    float sfi_eff = (float)prop.solar_flux;
    if (sfi_eff < MUF_SFI_FLOOR) sfi_eff = MUF_SFI_FLOOR;
    /* M2 collapses MUF_K^2 * HOP^2 * SFI into one constant per refresh:
     * we then just need muf^2 = M2 * cos_zen_clamped per pixel. */
    float m2 = (MUF_K * MUF_K) * (MUF_HOP_FACTOR * MUF_HOP_FACTOR) * sfi_eff;

    /* Scratch row for vertical contour-crossing detection: the previous
     * row's muf^2 at column x. We sentinel-fill with NaN so the first
     * row never spuriously triggers a vertical crossing. */
    static float prev_row_muf2[GL_W];
    for (int x = 0; x < GL_W; x++) prev_row_muf2[x] = NAN;

    for (int y = 0; y < GL_H; y++) {
        float lat_deg = 90.0f - (y + 0.5f) * (180.0f / GL_H);
        float lat_rad = lat_deg * (float)M_PI / 180.0f;
        float sin_lat = sinf(lat_rad);
        float cos_lat = cosf(lat_rad);
        float a = sin_lat * sin_decl;
        float b = cos_lat * cos_decl;
        uint16_t *row  = &s_canvas_buf[y * GL_W];
        const uint8_t *mrow = &s_landmask[y * LANDMASK_BYTES_PER_ROW];

        float prev_col_muf2 = NAN;
        for (int x = 0; x < GL_W; x++) {
            bool land = (mrow[x >> 3] >> (x & 7)) & 1u;
            float sin_elev = a + b * cos_dlon[x];
            uint16_t color = shade(land, sin_elev);

            if (muf_on) {
                float cz = sin_elev;
                if (cz < MUF_COS_FLOOR) cz = MUF_COS_FLOOR;
                float muf2 = m2 * cz;

                /* A contour pixel is one whose muf^2 sits on the
                 * opposite side of a threshold from either its left
                 * or upper neighbour. Test each threshold; pick the
                 * highest band whose contour passes through here so
                 * 10 m draws over 15 m draws over 20 m. */
                for (int k = MUF_BAND_COUNT - 1; k >= 0; k--) {
                    float t2 = s_muf_thresh_sq[k];
                    bool cross_h = !isnan(prev_col_muf2)
                                && ((prev_col_muf2 < t2) != (muf2 < t2));
                    bool cross_v = !isnan(prev_row_muf2[x])
                                && ((prev_row_muf2[x] < t2) != (muf2 < t2));
                    if (cross_h || cross_v) {
                        color = s_muf_colors[k];
                        break;
                    }
                }
                prev_col_muf2 = muf2;
                prev_row_muf2[x] = muf2;
            }

            row[x] = color;
        }
    }

    /* Subsolar point (warm white) */
    int sx = (int)((sublon_deg + 180.0f) * (GL_W / 360.0f));
    int sy = (int)((90.0f      - decl_deg) * (GL_H / 180.0f));
    plot_marker(sx, sy, rgb565(0xFF, 0xF0, 0xA0));

    /* Moon subpoint (cool white, smaller than the sun). */
    float moon_lat, moon_lon;
    compute_moon(now, &moon_lat, &moon_lon);
    int mx = (int)((moon_lon + 180.0f) * (GL_W / 360.0f));
    int my = (int)((90.0f - moon_lat) * (GL_H / 180.0f));
    plot_marker_r(mx, my, rgb565(0xD8, 0xD8, 0xE8), 2);

    /* Operator QTH (cyan) */
    if (!isnan(s_station_lat) && !isnan(s_station_lon)) {
        int hx = (int)((s_station_lon + 180.0f) * (GL_W / 360.0f));
        int hy = (int)((90.0f - s_station_lat)  * (GL_H / 180.0f));
        plot_marker(hx, hy, rgb565(0x10, 0xF0, 0xD8));
    }

    /* Labels under the map */
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char buf[80];
    snprintf(buf, sizeof(buf), "UTC %02d:%02d:%02d",
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    lv_label_set_text(s_lbl_utc, buf);

    if (muf_on) {
        snprintf(buf, sizeof(buf),
                 "Sun %+5.1f\xC2\xB0 %+6.1f\xC2\xB0  \xE2\x80\xA2  "
                 "MUF lines: 14 / 21 / 28 MHz  SFI %d",
                 (double)decl_deg, (double)sublon_deg, prop.solar_flux);
    } else {
        snprintf(buf, sizeof(buf), "Sun  %+5.1f\xC2\xB0  %+6.1f\xC2\xB0",
                 (double)decl_deg, (double)sublon_deg);
    }
    lv_label_set_text(s_lbl_sun, buf);

    snprintf(buf, sizeof(buf), "Moon %+5.1f\xC2\xB0 %+6.1f\xC2\xB0",
             (double)moon_lat, (double)moon_lon);
    lv_label_set_text(s_lbl_moon, buf);

    if (!isnan(s_station_lat)) {
        snprintf(buf, sizeof(buf), "QTH  %+5.1f\xC2\xB0  %+6.1f\xC2\xB0",
                 (double)s_station_lat, (double)s_station_lon);
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

    /* One-shot land/sea mask. Cheap (~20 ms) but only useful before the
     * first paint, so do it here rather than at app boot. */
    rasterize_world();

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 2, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);

    s_lbl_utc = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_utc, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_utc, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_lbl_utc, "UTC --:--:--");

    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf,
                         GL_W, GL_H, LV_COLOR_FORMAT_RGB565);
    /* Touch-to-inspect: any tap on the map shows lat/lon/grid/distance
     * for the touched point, with a 6 s auto-dismiss. Need the
     * CLICKABLE flag explicitly because lv_canvas defaults to
     * non-interactive. */
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, on_canvas_clicked, LV_EVENT_CLICKED, NULL);

    s_lbl_sun = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_sun, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_sun, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_lbl_sun, "Sun  --");

    s_lbl_moon = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_moon, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_moon, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_lbl_moon, "Moon --");

    s_lbl_station = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_station, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_station, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_lbl_station, "QTH  --");

    /* Touch popup. Hidden until the user taps the map; auto-dismisses
     * 6 s after the most recent tap. Two lines of montserrat_14 so the
     * full coords + distance string fits without truncation. */
    s_lbl_inspect = lv_label_create(scr);
    lv_obj_set_width(s_lbl_inspect, LV_PCT(96));
    lv_obj_set_style_text_color(s_lbl_inspect, UI_COL_ACCENT_2, 0);
    lv_obj_set_style_text_font(s_lbl_inspect, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_lbl_inspect, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_lbl_inspect, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_lbl_inspect, "");
    lv_obj_add_flag(s_lbl_inspect, LV_OBJ_FLAG_HIDDEN);

    draw_grayline(time(NULL));
    s_refresh_timer = lv_timer_create(refresh_cb, 30 * 1000, NULL);

    return scr;
}
