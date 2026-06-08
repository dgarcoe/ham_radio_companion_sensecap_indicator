#include "app_sats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "app_sats";

/* -------- Orbital + Earth constants ------------------------------- */
#define MU_KM3_PER_MIN2  1.434961505500000e9   /* GM in km^3 / min^2  */
#define RE_KM            6378.137              /* Earth equatorial    */
#define J2               1.08262668e-3
#define DEG_TO_RAD       (M_PI / 180.0)
#define RAD_TO_DEG       (180.0 / M_PI)
#define TWOPI            (2.0 * M_PI)
#define JD_UNIX_EPOCH    2440587.5             /* JD of 1970-01-01 0h */

/* -------- Roster ----------------------------------------------------
 * Names below MUST match what CelesTrak puts on the name line of the
 * amateur.txt feed. Frequencies + modes are static reference data;
 * orbital elements get filled in by parse_tle() once HTTP returns. */
typedef struct {
    const char *name;
    int32_t     norad_id;
    const char *mode;
    const char *up_freq;
    const char *down_freq;
} sat_def_t;

static const sat_def_t s_roster[] = {
    { "ISS (ZARYA)",    25544, "APRS / Voice", "145.825 simplex",   "145.825 FM"          },
    { "SO-50",          27607, "FM repeater",  "145.850 (67.0)",    "436.795 FM"          },
    { "AO-91",          43017, "FM repeater",  "435.250 (67.0)",    "145.960 FM"          },
    { "PO-101",         43678, "FM repeater",  "437.500 (141.3)",   "145.825 FM"          },
    { "RS-44",          44909, "SSB linear",   "145.935-145.995",   "435.670-435.610 SSB" },
    { "FO-29",          24278, "SSB linear",   "146.000-145.900",   "435.800-435.900 SSB" },
    { "AO-7",            7530, "SSB Mode B",   "432.125-432.175",   "145.975-145.925 SSB" },
    { "AO-73",          39444, "SSB linear",   "435.150-435.130",   "145.950-145.970 SSB" },
    { "IO-86",          40931, "FM repeater",  "145.880 (88.5)",    "435.880 FM"          },
};
#define ROSTER_COUNT  (sizeof(s_roster) / sizeof(s_roster[0]))

_Static_assert(ROSTER_COUNT <= APP_SATS_MAX,
               "roster exceeds APP_SATS_MAX -- bump in app_sats.h");

/* -------- State --------------------------------------------------- */
EXT_RAM_BSS_ATTR static app_sats_state_t s_state;
EXT_RAM_BSS_ATTR static app_sats_state_t s_snap;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_poke;
static app_sats_cb_t     s_cb;

static float s_qth_lat = NAN;
static float s_qth_lon = NAN;

/* TLE feed: CelesTrak amateur radio satellite group, classic TLE
 * format (3 lines per sat). ~50 KB for ~120 sats. */
#define TLE_URL    "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"
#define TLE_BUFSZ  (96 * 1024)

#define FETCH_INTERVAL_MS  (12 * 3600 * 1000)
#define RETRY_INTERVAL_MS  (60 * 1000)
#define PREDICT_INTERVAL_MS (15 * 60 * 1000)

/* -------- Julian Date helpers ------------------------------------- */

static double unix_ms_to_jd(int64_t unix_ms)
{
    return JD_UNIX_EPOCH + ((double)unix_ms / 86400000.0);
}

static int64_t now_unix_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

/* Gregorian (Y, M, D, hours-UT) -> Julian Date. */
static double gregorian_to_jd(int y, int m, int d, double hour_ut)
{
    int a = (14 - m) / 12;
    int yy = y + 4800 - a;
    int mm = m + 12 * a - 3;
    long jdn = (long)d + (153L * mm + 2) / 5
             + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045L;
    return (double)jdn - 0.5 + hour_ut / 24.0;
}

/* Greenwich Mean Sidereal Time in radians, from a JD. */
static double gmst_rad(double jd)
{
    double d = jd - 2451545.0;
    double hours = 18.697374558 + 24.06570982441908 * d;
    double frac = hours - floor(hours / 24.0) * 24.0;
    if (frac < 0.0) frac += 24.0;
    return frac * (M_PI / 12.0);
}

/* -------- TLE parsing -------------------------------------------- */

/* Pull a fixed-width decimal field from a TLE line. */
static double tle_field(const char *line, int col_from, int col_to)
{
    /* TLE columns are 1-indexed in the spec; we convert here. */
    char tmp[24];
    int n = col_to - col_from + 1;
    if (n <= 0 || n >= (int)sizeof(tmp)) return 0.0;
    memcpy(tmp, line + col_from - 1, n);
    tmp[n] = '\0';
    return atof(tmp);
}

static int tle_field_int(const char *line, int col_from, int col_to)
{
    char tmp[16];
    int n = col_to - col_from + 1;
    if (n <= 0 || n >= (int)sizeof(tmp)) return 0;
    memcpy(tmp, line + col_from - 1, n);
    tmp[n] = '\0';
    return atoi(tmp);
}

/* Eccentricity in TLE is "EEEEEEE" with an implied leading "0." */
static double tle_ecc(const char *line)
{
    char tmp[12] = "0.";
    memcpy(tmp + 2, line + 26, 7);
    tmp[9] = '\0';
    return atof(tmp);
}

/* Parse one TLE pair into the named slot. Returns true on success. */
static bool parse_tle(app_sat_t *sat, const char *l1, const char *l2)
{
    if (strlen(l1) < 68 || strlen(l2) < 68) return false;
    if (l1[0] != '1' || l2[0] != '2') return false;

    /* Line 1 epoch: cols 19-20 = YY, 21-32 = DDD.DDDDDDDD */
    int yy = tle_field_int(l1, 19, 20);
    double doy = tle_field(l1, 21, 32);
    int year = (yy < 57) ? (2000 + yy) : (1900 + yy);
    double jd_jan1 = gregorian_to_jd(year, 1, 1, 0.0);
    sat->tle_epoch_jd = jd_jan1 + (doy - 1.0);

    /* Line 2: i, RAAN, e, omega, M, n */
    double inc_deg  = tle_field(l2,  9, 16);
    double raan_deg = tle_field(l2, 18, 25);
    double ecc      = tle_ecc(l2);
    double argp_deg = tle_field(l2, 35, 42);
    double m_deg    = tle_field(l2, 44, 51);
    double n_rpd    = tle_field(l2, 53, 63);   /* revs/day */

    sat->inclination  = inc_deg  * DEG_TO_RAD;
    sat->raan         = raan_deg * DEG_TO_RAD;
    sat->ecc          = ecc;
    sat->arg_perigee  = argp_deg * DEG_TO_RAD;
    sat->mean_anomaly = m_deg    * DEG_TO_RAD;

    double n0_rad_min = n_rpd * (TWOPI / 1440.0);

    /* a from n via Kepler's third (mu in km^3/min^2). */
    double a = pow(MU_KM3_PER_MIN2 / (n0_rad_min * n0_rad_min), 1.0 / 3.0);
    double p = a * (1.0 - ecc * ecc);
    double re_over_p_sq = (RE_KM / p) * (RE_KM / p);
    double cos_i = cos(sat->inclination);
    double cos2_i = cos_i * cos_i;

    /* J2 secular drift rates (rad/min). */
    sat->drift_raan = -1.5 * n0_rad_min * J2 * re_over_p_sq * cos_i;
    sat->drift_argp =  0.75 * n0_rad_min * J2 * re_over_p_sq *
                      (5.0 * cos2_i - 1.0);

    /* J2-corrected mean motion. The (1 + ...) factor folds the secular
     * shortening of the nodal period into n. Small for typical LEO
     * orbits but matters over many revs. */
    double sqrt_one_minus_e2 = sqrt(1.0 - ecc * ecc);
    sat->mean_motion = n0_rad_min *
        (1.0 + 1.5 * J2 * re_over_p_sq * sqrt_one_minus_e2 *
         (1.5 * cos2_i - 0.5));
    sat->semi_major = a;
    sat->tle_valid  = true;
    return true;
}

/* -------- Kepler solve ------------------------------------------ */

static double solve_kepler(double M, double e)
{
    /* Newton-Raphson; converges in 3-6 iterations for e < 0.05. */
    double E = M;
    for (int i = 0; i < 30; i++) {
        double f  = E - e * sin(E) - M;
        double fp = 1.0 - e * cos(E);
        double d  = f / fp;
        E -= d;
        if (fabs(d) < 1e-10) break;
    }
    return E;
}

/* -------- Position + look angle --------------------------------- */

typedef struct { double x, y, z; } vec3_t;

/* ECI position of the satellite at minutes-since-epoch dt. */
static vec3_t sat_eci(const app_sat_t *s, double dt_min)
{
    double M    = s->mean_anomaly + s->mean_motion * dt_min;
    double raan = s->raan         + s->drift_raan * dt_min;
    double argp = s->arg_perigee  + s->drift_argp * dt_min;

    double E = solve_kepler(M, s->ecc);

    double cos_E = cos(E), sin_E = sin(E);
    double r = s->semi_major * (1.0 - s->ecc * cos_E);
    /* true anomaly via half-angle (numerically nicer than atan2 of cos/sin) */
    double nu = 2.0 * atan2(sqrt(1.0 + s->ecc) * sin(E * 0.5),
                            sqrt(1.0 - s->ecc) * cos(E * 0.5));

    double u  = argp + nu;
    double cu = cos(u),  su = sin(u);
    double co = cos(raan), so = sin(raan);
    double ci = cos(s->inclination), si = sin(s->inclination);

    vec3_t out = {
        .x = r * (co * cu - so * su * ci),
        .y = r * (so * cu + co * su * ci),
        .z = r *  su * si,
    };
    return out;
}

/* Observer ECI position from QTH lat/lon and current GMST. */
static vec3_t observer_eci(double lat_rad, double lon_rad, double gmst)
{
    double theta = gmst + lon_rad;     /* local sidereal time */
    double c_lat = cos(lat_rad);
    vec3_t out = {
        .x = RE_KM * c_lat * cos(theta),
        .y = RE_KM * c_lat * sin(theta),
        .z = RE_KM * sin(lat_rad),
    };
    return out;
}

/* Topocentric look angle (az, el in degrees + range km) from observer
 * ECI to satellite ECI at the given JD. */
static void look_angle(const vec3_t *sat, const vec3_t *obs,
                       double lat_rad, double lon_rad, double gmst,
                       double *az_deg, double *el_deg, double *range_km)
{
    double rx = sat->x - obs->x;
    double ry = sat->y - obs->y;
    double rz = sat->z - obs->z;
    double range = sqrt(rx * rx + ry * ry + rz * rz);

    double theta = gmst + lon_rad;
    double s_lat = sin(lat_rad), c_lat = cos(lat_rad);
    double s_th  = sin(theta),   c_th  = cos(theta);

    /* SEZ topocentric frame: South, East, Up. */
    double south =  s_lat * c_th * rx + s_lat * s_th * ry - c_lat * rz;
    double east  = -s_th * rx + c_th * ry;
    double up    =  c_lat * c_th * rx + c_lat * s_th * ry + s_lat * rz;

    *el_deg    = asin(up / range) * RAD_TO_DEG;
    double az  = atan2(east, -south) * RAD_TO_DEG;     /* 0 = north */
    if (az < 0.0) az += 360.0;
    *az_deg    = az;
    *range_km  = range;
}

/* -------- Pass prediction --------------------------------------- */

/* Sweep the next 24 h in 30 s steps. AOS is the first elevation
 * crossing above 0 deg; LOS is the next descent below 0; peak elev
 * is tracked between them. Returns false if no pass within the
 * window (polar orbits over equatorial QTH may legitimately go
 * passless for a day). */
static bool predict_next_pass(app_sat_t *s, double lat_rad, double lon_rad,
                              int64_t now_ms)
{
    if (!s->tle_valid) return false;
    s->next_aos_ms = 0;
    s->next_los_ms = 0;
    s->next_peak_elev_deg = 0.0f;
    s->next_peak_az_deg   = 0.0f;
    s->currently_above    = false;
    s->current_elev_deg   = 0.0f;
    s->current_az_deg     = 0.0f;

    const int    step_s    = 30;
    const int    n_steps   = 24 * 3600 / step_s;
    const double dt_step_min = step_s / 60.0;

    /* dt at t=0 (now) relative to the TLE epoch. */
    double jd_now = unix_ms_to_jd(now_ms);
    double dt_now_min = (jd_now - s->tle_epoch_jd) * 1440.0;

    double prev_el = -90.0;
    bool   in_pass = false;
    double peak_el = -90.0, peak_az = 0.0;
    int64_t aos_ms = 0, los_ms = 0;

    for (int i = 0; i <= n_steps; i++) {
        double t_min = dt_now_min + i * dt_step_min;
        int64_t t_ms = now_ms + (int64_t)i * step_s * 1000LL;
        double jd_t = unix_ms_to_jd(t_ms);
        double gmst = gmst_rad(jd_t);

        vec3_t r_sat = sat_eci(s, t_min);
        vec3_t r_obs = observer_eci(lat_rad, lon_rad, gmst);
        double az, el, range;
        look_angle(&r_sat, &r_obs, lat_rad, lon_rad, gmst, &az, &el, &range);

        if (i == 0) {
            s->current_elev_deg = (float)el;
            s->current_az_deg   = (float)az;
            s->currently_above  = el > 0.0;
            if (el > 0.0) {
                in_pass = true;
                peak_el = el;
                peak_az = az;
                aos_ms  = now_ms;     /* already mid-pass */
            }
        }

        if (!in_pass && prev_el <= 0.0 && el > 0.0) {
            in_pass = true;
            peak_el = el;
            peak_az = az;
            aos_ms  = t_ms;
        } else if (in_pass) {
            if (el > peak_el) { peak_el = el; peak_az = az; }
            if (el <= 0.0) {
                los_ms = t_ms;
                s->next_aos_ms = aos_ms;
                s->next_los_ms = los_ms;
                s->next_peak_elev_deg = (float)peak_el;
                s->next_peak_az_deg   = (float)peak_az;
                return true;
            }
        }
        prev_el = el;
    }

    /* Started in a pass that hasn't yet ended at the 24 h horizon, or
     * no pass found at all. Report what we have. */
    if (in_pass) {
        s->next_aos_ms = aos_ms;
        s->next_los_ms = now_ms + 24LL * 3600 * 1000;
        s->next_peak_elev_deg = (float)peak_el;
        s->next_peak_az_deg   = (float)peak_az;
        return true;
    }
    return false;
}

/* -------- HTTP TLE fetch + line walker --------------------------- */

typedef struct {
    char  *buf;
    size_t cap;
    size_t total;
} fetch_ctx_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    fetch_ctx_t *ctx = evt->user_data;
    if (!ctx) return ESP_OK;
    size_t want = evt->data_len;
    if (ctx->total + want >= ctx->cap) want = ctx->cap - ctx->total - 1;
    if (want == 0) return ESP_OK;
    memcpy(ctx->buf + ctx->total, evt->data, want);
    ctx->total += want;
    return ESP_OK;
}

static char *next_line(char *p)
{
    while (*p && *p != '\n') p++;
    if (*p == '\n') *p++ = '\0';
    return p;
}

static void trim_trailing_ws(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

/* Walk the TLE feed in 3-line chunks; for each chunk look at the NORAD
 * id in line 1 and update the matching roster slot. */
static int parse_and_store(char *body)
{
    int matched = 0;
    char *p = body;
    while (*p) {
        char *name = p;
        p = next_line(p);
        if (!*p) break;
        char *l1 = p;
        p = next_line(p);
        if (!*p) break;
        char *l2 = p;
        p = next_line(p);
        trim_trailing_ws(name);
        trim_trailing_ws(l1);
        trim_trailing_ws(l2);
        if (strlen(l1) < 68 || strlen(l2) < 68) continue;

        int norad = tle_field_int(l1, 3, 7);

        for (size_t k = 0; k < ROSTER_COUNT; k++) {
            if (s_roster[k].norad_id != norad) continue;
            if (parse_tle(&s_state.sats[k], l1, l2)) matched++;
            break;
        }
    }
    return matched;
}

static bool fetch_once(char *buf)
{
    fetch_ctx_t ctx = { .buf = buf, .cap = TLE_BUFSZ, .total = 0 };
    esp_http_client_config_t cfg = {
        .url                  = TLE_URL,
        .timeout_ms           = 20000,
        .user_agent           = "HamRadioCompanion/1.0 (esp32s3)",
        .event_handler        = http_event,
        .user_data            = &ctx,
        .crt_bundle_attach    = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "TLE fetch err=%s status=%d bytes=%u",
             esp_err_to_name(err), status, (unsigned)ctx.total);

    bool ok = false;
    if (err == ESP_OK && status == 200 && ctx.total > 0) {
        buf[ctx.total] = '\0';
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        int n = parse_and_store(buf);
        s_state.last_tle_fetch_ms = esp_timer_get_time() / 1000;
        s_state.tle_ever_fetched  = (n > 0);
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "matched %d/%u roster TLEs", n, (unsigned)ROSTER_COUNT);
        ok = (n > 0);
    }
    esp_http_client_cleanup(client);
    return ok;
}

/* -------- Predictor pass over all sats --------------------------- */

static void run_predict(void)
{
    if (isnan(s_qth_lat) || isnan(s_qth_lon)) return;
    double lat_rad = s_qth_lat * DEG_TO_RAD;
    double lon_rad = s_qth_lon * DEG_TO_RAD;
    int64_t now_ms = now_unix_ms();
    /* Don't run before NTP has set the clock - everything else
     * would be referenced to 1970 and the answers garbage. */
    if (now_ms < 1700000000LL * 1000LL) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_state.count; i++) {
        app_sat_t *s = &s_state.sats[i];
        if (!s->tle_valid) continue;
        predict_next_pass(s, lat_rad, lon_rad, now_ms);
    }
    s_state.last_predict_ms = esp_timer_get_time() / 1000;
    xSemaphoreGive(s_mutex);
}

/* -------- Background task --------------------------------------- */

static void fetch_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(TLE_BUFSZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "tle buf alloc failed");
        vTaskDelete(NULL);
        return;
    }

    int64_t next_fetch_ms = 0;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        bool need_fetch = !s_state.tle_ever_fetched || now >= next_fetch_ms;
        if (need_fetch) {
            bool ok = fetch_once(buf);
            next_fetch_ms = now + (ok ? FETCH_INTERVAL_MS : RETRY_INTERVAL_MS);
        }
        if (s_state.tle_ever_fetched) {
            run_predict();
            if (s_cb) {
                app_sats_get(&s_snap);
                s_cb(&s_snap);
            }
        }
        uint32_t wait_ms = s_state.tle_ever_fetched
                         ? PREDICT_INTERVAL_MS
                         : RETRY_INTERVAL_MS;
        xSemaphoreTake(s_poke, pdMS_TO_TICKS(wait_ms));
    }
}

/* -------- Public API -------------------------------------------- */

esp_err_t app_sats_init(app_sats_cb_t on_update)
{
    if (s_mutex) return ESP_OK;            /* already inited */
    s_cb    = on_update;
    s_mutex = xSemaphoreCreateMutex();
    s_poke  = xSemaphoreCreateBinary();
    if (!s_mutex || !s_poke) return ESP_ERR_NO_MEM;

    /* Seed the static slots so the UI can show names + freqs even
     * before the first TLE fetch lands. */
    memset(&s_state, 0, sizeof(s_state));
    s_state.count = ROSTER_COUNT;
    for (size_t i = 0; i < ROSTER_COUNT; i++) {
        const sat_def_t *d = &s_roster[i];
        app_sat_t *s = &s_state.sats[i];
        strncpy(s->name,      d->name,      APP_SATS_NAME_MAX - 1);
        strncpy(s->mode,      d->mode,      APP_SATS_NAME_MAX - 1);
        strncpy(s->up_freq,   d->up_freq,   APP_SATS_FREQ_MAX - 1);
        strncpy(s->down_freq, d->down_freq, APP_SATS_FREQ_MAX - 1);
        s->norad_id = d->norad_id;
    }

    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(
        fetch_task, "sats", 7168, NULL, 2, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return (r == pdPASS) ? ESP_OK : ESP_FAIL;
}

void app_sats_set_qth(float lat_deg, float lon_deg)
{
    s_qth_lat = lat_deg;
    s_qth_lon = lon_deg;
    if (s_poke) xSemaphoreGive(s_poke);
}

void app_sats_get(app_sats_state_t *out)
{
    if (!s_mutex) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}

void app_sats_request_refresh(void)
{
    if (s_poke) xSemaphoreGive(s_poke);
}
