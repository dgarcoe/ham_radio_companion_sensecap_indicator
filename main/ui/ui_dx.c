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

static const struct { float lo; float hi; } BAND_EDGES[] = {
    /* index aligned with BAND_NAMES[1..] */
    {1.8f,   2.0f},     /* 160m */
    {3.5f,   4.0f},     /* 80m  */
    {5.0f,   5.5f},     /* 60m  */
    {7.0f,   7.3f},     /* 40m  */
    {10.1f, 10.15f},    /* 30m  */
    {14.0f, 14.35f},    /* 20m  */
    {18.068f, 18.168f}, /* 17m  */
    {21.0f, 21.45f},    /* 15m  */
    {24.89f, 24.99f},   /* 12m  */
    {28.0f, 29.7f},     /* 10m  */
    {50.0f, 54.0f},     /* 6m   */
    {144.0f, 148.0f},   /* 2m   */
};

static const char *const MODE_NAMES[] = { "ALL", "CW", "DIGI", "PHO" };
#define MODE_COUNT (sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0]))

static const char *const AREA_NAMES[] = {
    "ALL", "AF", "AN", "AS", "EU", "NA", "OC", "SA",
};
#define AREA_COUNT (sizeof(AREA_NAMES) / sizeof(AREA_NAMES[0]))

/* --- Callsign-prefix to continent table ------------------------------
 *
 * Covers the top ~100 DXCC entities that produce nearly all daily DX
 * traffic. Longer prefixes are checked first; the first match wins.
 * Two-character prefixes are stored upper-case. The table is small
 * enough for a linear scan to stay well under a millisecond per spot.
 */
typedef struct { const char *p; const char *cont; } pfx_t;
static const pfx_t PFX_LONG[] = {
    /* Two-character prefixes (most discriminating). */
    {"EA","EU"},{"EB","EU"},{"EC","EU"},{"ED","EU"},{"EE","EU"},
    {"EF","EU"},{"EG","EU"},{"EH","EU"},{"AM","EU"},{"AN","EU"},
    {"AO","EU"},
    {"DA","EU"},{"DB","EU"},{"DC","EU"},{"DD","EU"},{"DE","EU"},
    {"DF","EU"},{"DG","EU"},{"DH","EU"},{"DJ","EU"},{"DK","EU"},
    {"DL","EU"},{"DM","EU"},{"DN","EU"},{"DO","EU"},{"DP","EU"},
    {"DQ","EU"},{"DR","EU"},
    {"OE","EU"},{"OH","EU"},{"OK","EU"},{"OL","EU"},{"OM","EU"},
    {"ON","EU"},{"OO","EU"},{"OP","EU"},{"OR","EU"},{"OS","EU"},
    {"OT","EU"},{"OZ","EU"},
    {"PA","EU"},{"PB","EU"},{"PC","EU"},{"PD","EU"},{"PE","EU"},
    {"PF","EU"},{"PG","EU"},{"PH","EU"},{"PI","EU"},
    {"HA","EU"},{"HB","EU"},{"HG","EU"},
    {"YL","EU"},{"YO","EU"},{"YU","EU"},{"YT","EU"},{"YR","EU"},
    {"LA","EU"},{"LB","EU"},{"LX","EU"},{"LY","EU"},{"LZ","EU"},
    {"SM","EU"},{"SK","EU"},{"SP","EU"},{"SV","EU"},
    {"S5","EU"},{"S7","AF"},{"S2","AS"},{"S9","AF"},
    {"OY","EU"},{"OX","NA"},  /* Faroe / Greenland */
    {"TF","EU"},{"TK","EU"},{"TG","NA"},{"TI","NA"},{"TJ","AF"},
    {"TR","AF"},{"TT","AF"},{"TZ","AF"},{"TY","AF"},{"TU","AF"},
    {"TN","AF"},{"TL","AF"},
    {"UR","EU"},{"US","EU"},{"UT","EU"},{"UU","EU"},{"UV","EU"},
    {"UW","EU"},{"UX","EU"},{"UY","EU"},{"UZ","EU"},
    {"EM","EU"},{"EN","EU"},{"EO","EU"},{"ER","EU"},
    {"E7","EU"},{"4O","EU"},{"9A","EU"},{"9H","EU"},{"Z3","EU"},
    {"Z6","EU"},{"Z8","AF"},
    /* North America */
    {"AA","NA"},{"AB","NA"},{"AC","NA"},{"AD","NA"},{"AE","NA"},
    {"AF","NA"},{"AG","NA"},{"AH","OC"},{"AI","NA"},{"AJ","NA"},
    {"AK","NA"},{"AL","NA"},
    {"KA","NA"},{"KB","NA"},{"KC","NA"},{"KD","NA"},{"KE","NA"},
    {"KF","NA"},{"KG","NA"},{"KH","OC"},{"KI","NA"},{"KJ","NA"},
    {"KK","NA"},{"KL","NA"},{"KM","NA"},{"KN","NA"},{"KO","NA"},
    {"KP","NA"},{"KQ","NA"},{"KR","NA"},{"KS","NA"},{"KT","NA"},
    {"KU","NA"},{"KV","NA"},{"KW","NA"},{"KX","NA"},{"KY","NA"},
    {"KZ","NA"},
    {"WA","NA"},{"WB","NA"},{"WC","NA"},{"WD","NA"},{"WE","NA"},
    {"WF","NA"},{"WG","NA"},{"WH","OC"},{"WI","NA"},{"WJ","NA"},
    {"WK","NA"},{"WL","NA"},{"WM","NA"},{"WN","NA"},{"WO","NA"},
    {"WP","NA"},{"WQ","NA"},{"WR","NA"},{"WS","NA"},{"WT","NA"},
    {"WU","NA"},{"WV","NA"},{"WW","NA"},{"WX","NA"},{"WY","NA"},
    {"WZ","NA"},
    {"NA","NA"},{"NB","NA"},{"NC","NA"},{"ND","NA"},{"NE","NA"},
    {"NF","NA"},{"NG","NA"},{"NH","OC"},{"NI","NA"},{"NJ","NA"},
    {"NK","NA"},{"NL","NA"},{"NM","NA"},{"NN","NA"},{"NO","NA"},
    {"NP","NA"},{"NQ","NA"},{"NR","NA"},{"NS","NA"},{"NT","NA"},
    {"NU","NA"},{"NV","NA"},{"NW","NA"},{"NX","NA"},{"NY","NA"},
    {"NZ","NA"},
    {"VE","NA"},{"VA","NA"},{"VO","NA"},{"VY","NA"},
    {"XE","NA"},{"XF","NA"},
    {"HH","NA"},{"HI","NA"},{"HP","NA"},{"HR","NA"},
    /* Caribbean / NA misc */
    {"CO","NA"},{"CL","NA"},{"CM","NA"},
    /* South America */
    {"PY","SA"},{"PP","SA"},{"PQ","SA"},{"PR","SA"},{"PS","SA"},
    {"PT","SA"},{"PU","SA"},{"PV","SA"},{"PW","SA"},{"PX","SA"},
    {"ZV","SA"},{"ZW","SA"},{"ZX","SA"},{"ZY","SA"},{"ZZ","SA"},
    {"LU","SA"},{"AY","SA"},{"AZ","SA"},{"L2","SA"},{"L3","SA"},
    {"L4","SA"},{"L5","SA"},{"L6","SA"},{"L7","SA"},{"L8","SA"},
    {"L9","SA"},
    {"CE","SA"},{"CA","SA"},{"CB","SA"},{"CC","SA"},{"CD","SA"},
    {"CX","SA"},{"OA","SA"},{"OB","SA"},{"OC","SA"},
    {"HC","SA"},{"HD","SA"},
    {"YV","SA"},{"YW","SA"},{"YX","SA"},{"YY","SA"},
    {"HK","SA"},{"HJ","SA"},{"5K","SA"},
    {"ZP","SA"},{"CP","SA"},
    {"FY","SA"},{"PJ","SA"},  /* approximate */
    /* Asia */
    {"JA","AS"},{"JE","AS"},{"JF","AS"},{"JG","AS"},{"JH","AS"},
    {"JI","AS"},{"JJ","AS"},{"JK","AS"},{"JL","AS"},{"JM","AS"},
    {"JN","AS"},{"JO","AS"},{"JP","AS"},{"JQ","AS"},{"JR","AS"},
    {"JS","AS"},
    {"BA","AS"},{"BD","AS"},{"BG","AS"},{"BH","AS"},{"BI","AS"},
    {"BJ","AS"},{"BY","AS"},{"BV","AS"},
    {"HL","AS"},{"DS","AS"},
    {"VR","AS"},{"VU","AS"},{"AP","AS"},
    {"DU","AS"},{"9V","AS"},{"9M","AS"},{"4S","AS"},
    {"HS","AS"},{"E2","AS"},{"XU","AS"},{"3W","AS"},{"XV","AS"},
    {"YK","AS"},{"YI","AS"},{"EP","AS"},{"YA","AS"},
    {"9K","AS"},{"A4","AS"},{"A6","AS"},{"A7","AS"},{"A9","AS"},
    {"4X","AS"},{"4Z","AS"},
    /* Africa */
    {"ZS","AF"},{"ZR","AF"},{"ZU","AF"},
    {"5N","AF"},{"5Z","AF"},{"5R","AF"},{"5U","AF"},{"5V","AF"},
    {"5X","AF"},{"5H","AF"},
    {"7Q","AF"},{"7X","AF"},{"7P","AF"},
    {"CN","AF"},
    {"3V","AF"},{"3X","AF"},{"3B","AF"},{"3C","AF"},{"3D","AF"},
    {"5A","AF"},{"SU","AF"},{"6W","AF"},{"6V","AF"},
    {"9G","AF"},{"9J","AF"},{"9L","AF"},{"9Q","AF"},{"9X","AF"},
    {"9U","AF"},
    {"FR","AF"},{"V5","AF"},{"D2","AF"},{"D4","AF"},{"D6","AF"},
    {"C5","AF"},{"C8","AF"},{"C9","AF"},
    /* Oceania */
    {"VK","OC"},{"VI","OC"},
    {"ZL","OC"},{"ZK","OC"},{"ZM","OC"},
    {"FK","OC"},{"FO","OC"},{"FW","OC"},
    {"P2","OC"},{"H4","OC"},{"YJ","OC"},{"A3","OC"},
    {"E5","OC"},{"E6","OC"},{"5W","OC"},
};

/* Single-character fallback for prefixes we didn't list explicitly. */
static const pfx_t PFX_SHORT[] = {
    {"W","NA"},{"K","NA"},{"N","NA"},
    {"F","EU"},{"G","EU"},{"M","EU"},{"I","EU"},{"R","EU"},
};

static const char *continent_for_call(const char *call)
{
    if (!call || !call[0]) return "??";
    char p2[3] = {0};
    p2[0] = (char)toupper((unsigned char)call[0]);
    if (call[1]) p2[1] = (char)toupper((unsigned char)call[1]);

    for (size_t i = 0; i < sizeof(PFX_LONG) / sizeof(PFX_LONG[0]); i++) {
        if (p2[0] == PFX_LONG[i].p[0] && p2[1] == PFX_LONG[i].p[1]) {
            return PFX_LONG[i].cont;
        }
    }
    for (size_t i = 0; i < sizeof(PFX_SHORT) / sizeof(PFX_SHORT[0]); i++) {
        if (p2[0] == PFX_SHORT[i].p[0]) {
            return PFX_SHORT[i].cont;
        }
    }
    return "??";
}

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
static int s_filter_band;
static int s_filter_mode;
static int s_filter_area;

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
            return (int)(i + 1);
        }
    }
    return 0;
}

/* Returns 1=CW, 2=DIGI, 3=PHO, 0=unknown. */
static int mode_index_for_spot(const char *comment, const char *freq_str)
{
    char up[64] = {0};
    if (comment) {
        size_t i;
        for (i = 0; i < sizeof(up) - 1 && comment[i]; i++) {
            up[i] = (char)toupper((unsigned char)comment[i]);
        }
        up[i] = '\0';
    }

    /* Explicit mode tokens in the comment - check most-specific first. */
    if (strstr(up, "FT8") || strstr(up, "FT4") || strstr(up, "RTTY") ||
        strstr(up, "PSK") || strstr(up, "JT9") || strstr(up, "JT65") ||
        strstr(up, "DIGI") || strstr(up, "DATA") || strstr(up, "OLIVIA") ||
        strstr(up, "MFSK") || strstr(up, "FSK") || strstr(up, "Q65")) {
        return 2; /* DIGI */
    }
    if (strstr(up, "CW")) return 1;
    if (strstr(up, "SSB") || strstr(up, "USB") || strstr(up, "LSB") ||
        strstr(up, "FM")  || strstr(up, " AM") || strstr(up, "PHONE")) {
        return 3; /* PHO */
    }

    /* No explicit token - fall back to frequency segment heuristic.
     * On HF the bottom slice is CW, the FT8 sub-band is digital, and
     * everything above the digi window is phone. Rough but reasonable. */
    if (!freq_str || !*freq_str) return 0;
    float khz = strtof(freq_str, NULL);
    float mhz = khz / 1000.0f;
    struct { float cw_lo, cw_hi, digi_lo, digi_hi; } seg[] = {
        {1.8f,   1.84f,  1.84f,  1.85f},  /* 160m */
        {3.5f,   3.6f,   3.573f, 3.580f}, /* 80m  */
        {7.0f,   7.04f,  7.074f, 7.080f}, /* 40m  */
        {10.1f, 10.13f, 10.136f,10.14f},  /* 30m all digi-ish */
        {14.0f, 14.07f, 14.074f,14.080f}, /* 20m  */
        {18.068f,18.095f,18.100f,18.110f},/* 17m  */
        {21.0f, 21.07f, 21.074f,21.080f}, /* 15m  */
        {24.89f,24.915f,24.915f,24.93f},  /* 12m  */
        {28.0f, 28.07f, 28.074f,28.080f}, /* 10m  */
    };
    for (size_t i = 0; i < sizeof(seg)/sizeof(seg[0]); i++) {
        if (mhz >= seg[i].digi_lo && mhz <= seg[i].digi_hi) return 2; /* DIGI */
        if (mhz >= seg[i].cw_lo   && mhz <= seg[i].cw_hi)   return 1; /* CW   */
    }
    /* Above the digi window on each band it's phone territory. */
    return 3; /* PHO */
}

static bool spot_passes_filters(const app_dx_spot_t *spot)
{
    if (s_filter_band != 0) {
        if (band_index_for_freq_khz(spot->freq) != s_filter_band) return false;
    }
    if (s_filter_mode != 0) {
        if (mode_index_for_spot(spot->comment, spot->freq) != s_filter_mode) return false;
    }
    if (s_filter_area != 0) {
        if (strcmp(continent_for_call(spot->spotter), AREA_NAMES[s_filter_area]) != 0) {
            return false;
        }
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
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(r->row);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

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

    lv_label_set_text(s_chip_band_lbl, BAND_NAMES[s_filter_band]);
    lv_label_set_text(s_chip_mode_lbl, MODE_NAMES[s_filter_mode]);
    lv_label_set_text(s_chip_area_lbl, AREA_NAMES[s_filter_area]);

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

    char page_buf[64];
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

static void on_prev_clicked(lv_event_t *e) { (void)e; if (s_page > 0) { s_page--; refresh_now(); } }
static void on_next_clicked(lv_event_t *e) { (void)e; s_page++; refresh_now(); }

static void cycle_filter(int *value, int count, int delta)
{
    *value = (*value + delta + count) % count;
    s_page = 0;
    refresh_now();
}
static void on_band_up   (lv_event_t *e) { (void)e; cycle_filter(&s_filter_band, BAND_COUNT, -1); }
static void on_band_down (lv_event_t *e) { (void)e; cycle_filter(&s_filter_band, BAND_COUNT, +1); }
static void on_mode_up   (lv_event_t *e) { (void)e; cycle_filter(&s_filter_mode, MODE_COUNT, -1); }
static void on_mode_down (lv_event_t *e) { (void)e; cycle_filter(&s_filter_mode, MODE_COUNT, +1); }
static void on_area_up   (lv_event_t *e) { (void)e; cycle_filter(&s_filter_area, AREA_COUNT, -1); }
static void on_area_down (lv_event_t *e) { (void)e; cycle_filter(&s_filter_area, AREA_COUNT, +1); }

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

/* Filter chip: vertical stack of header / up button / value / down button.
 * Tapping ▲ goes to the previous filter value, ▼ to the next. */
static void make_filter_chip(lv_obj_t *parent, const char *header,
                             lv_event_cb_t on_up, lv_event_cb_t on_down,
                             lv_obj_t **out_value_lbl)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 110, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_all(chip, 4, 0);
    lv_obj_set_style_pad_gap(chip, 2, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(chip);
    lv_label_set_text(h, header);
    lv_obj_set_style_text_color(h, UI_COL_MUTED, 0);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);

    /* Up button */
    lv_obj_t *up = lv_obj_create(chip);
    lv_obj_remove_style_all(up);
    lv_obj_set_size(up, LV_PCT(100), 24);
    lv_obj_add_flag(up, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(up, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(up, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(up, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *up_lbl = lv_label_create(up);
    lv_label_set_text(up_lbl, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(up_lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(up, on_up, LV_EVENT_CLICKED, NULL);

    /* Value */
    lv_obj_t *v = lv_label_create(chip);
    lv_label_set_text(v, "ALL");
    lv_obj_set_style_text_color(v, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_18, 0);

    /* Down button */
    lv_obj_t *dn = lv_obj_create(chip);
    lv_obj_remove_style_all(dn);
    lv_obj_set_size(dn, LV_PCT(100), 24);
    lv_obj_add_flag(dn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *dn_lbl = lv_label_create(dn);
    lv_label_set_text(dn_lbl, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(dn_lbl, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_font(dn_lbl, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(dn, on_down, LV_EVENT_CLICKED, NULL);

    if (out_value_lbl) *out_value_lbl = v;
}

/* --- Public create --- */

lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);
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

    /* Filter chips row */
    lv_obj_t *filters = lv_obj_create(scr);
    lv_obj_remove_style_all(filters);
    lv_obj_set_size(filters, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(filters, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filters, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(filters, 6, 0);
    lv_obj_clear_flag(filters, LV_OBJ_FLAG_SCROLLABLE);

    make_filter_chip(filters, "Band", on_band_up, on_band_down, &s_chip_band_lbl);
    make_filter_chip(filters, "Mode", on_mode_up, on_mode_down, &s_chip_mode_lbl);
    make_filter_chip(filters, "Area", on_area_up, on_area_down, &s_chip_area_lbl);

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

    /* List */
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
