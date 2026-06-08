#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Amateur-radio satellite tracker.
 *
 * Maintains a small fixed roster of popular ham satellites (FM
 * repeaters, SSB linear transponders, APRS), keeps each one's
 * two-line elements (TLEs) refreshed from CelesTrak, and forecasts
 * the next pass over the operator's QTH for the next 24 hours.
 *
 * The orbital model is a Keplerian propagator with the J2 secular
 * drift corrections to mean motion + RAAN + arg-of-perigee. Good
 * enough for "next pass in 47 minutes, peak elev 35 deg" accuracy
 * over a few days; not enough for fine antenna pointing. */

#define APP_SATS_NAME_MAX  20
#define APP_SATS_FREQ_MAX  28
#define APP_SATS_MAX       10

typedef struct {
    char name[APP_SATS_NAME_MAX];   /* "ISS", "SO-50", ... */
    int32_t norad_id;
    char mode[APP_SATS_NAME_MAX];   /* "FM repeater", "SSB linear", "APRS" */
    char up_freq[APP_SATS_FREQ_MAX];   /* "145.850 (67.0)" */
    char down_freq[APP_SATS_FREQ_MAX]; /* "436.795 FM" */

    /* Parsed TLE elements (radians + revs/day where noted). */
    bool   tle_valid;
    double tle_epoch_jd;        /* Julian Date at TLE epoch (UTC)   */
    double inclination;         /* rad                              */
    double raan;                /* rad, at epoch                    */
    double ecc;                 /* unitless                         */
    double arg_perigee;         /* rad, at epoch                    */
    double mean_anomaly;        /* rad, at epoch                    */
    double mean_motion;         /* rad/min, corrected for J2        */
    double semi_major;          /* km                               */
    double drift_raan;          /* rad/min                          */
    double drift_argp;          /* rad/min                          */

    /* Most recent pass forecast. next_aos_ms == 0 means no pass
     * found within the 24 h search window. */
    int64_t next_aos_ms;        /* unix ms (UTC) of next AOS        */
    int64_t next_los_ms;
    float   next_peak_elev_deg;
    float   next_peak_az_deg;

    /* Live position (refreshed each prediction run). */
    bool    currently_above;
    float   current_elev_deg;
    float   current_az_deg;
} app_sat_t;

typedef struct {
    app_sat_t sats[APP_SATS_MAX];
    int       count;
    int64_t   last_predict_ms;  /* uptime ms of last successful run */
    int64_t   last_tle_fetch_ms;
    bool      tle_ever_fetched; /* false until first successful TLE fetch */
} app_sats_state_t;

typedef void (*app_sats_cb_t)(const app_sats_state_t *state);

/* Spawn the background fetcher + predictor. Safe to call before WiFi
 * is up - the task retries every 60 s until CelesTrak responds. */
esp_err_t app_sats_init(app_sats_cb_t on_update);

/* Tell the predictor where the operator is. Triggers an immediate
 * re-prediction. Lat in degrees north (-90 +90), lon east (-180 +180).
 * NaN means "no QTH" and the predictor will skip the pass search,
 * leaving only ephemeris. Safe to call any time after _init. */
void app_sats_set_qth(float lat_deg, float lon_deg);

/* Thread-safe snapshot copy of the satellite state. */
void app_sats_get(app_sats_state_t *out);

/* Force a re-fetch + re-predict cycle (e.g. on WiFi reconnect). */
void app_sats_request_refresh(void);
