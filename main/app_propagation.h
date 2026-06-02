#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define APP_PROP_MAX_BANDS 12
#define APP_PROP_MAX_VHF   12

typedef struct {
    char band[16];        /* "80m-40m" */
    char time[8];         /* "day" or "night" */
    char condition[12];   /* "Good", "Fair", "Poor" */
} app_prop_band_t;

typedef struct {
    char name[24];        /* "E-Skip", "VHF Aurora", "6m EsEU" */
    char location[24];    /* "europe", "north_america", ... */
    char status[40];      /* "Band Open", "Band Closed", "Likely", ... */
} app_prop_vhf_t;

typedef struct {
    int  solar_flux;
    int  sunspots;
    int  a_index;
    int  k_index;
    char xray[12];
    char geomag[20];
    char signal_noise[12];
    char updated[40];
    app_prop_band_t bands[APP_PROP_MAX_BANDS];
    int  band_count;
    app_prop_vhf_t  vhf[APP_PROP_MAX_VHF];
    int  vhf_count;
    bool valid;
} app_prop_data_t;

typedef void (*app_prop_cb_t)(const app_prop_data_t *data);

/* Starts a background task that fetches hamqsl.com every 15 minutes and
 * notifies `on_update` after each successful parse. Safe to call before
 * WiFi is up - the task retries until WiFi is connected. */
esp_err_t app_propagation_init(app_prop_cb_t on_update);

/* Snapshot the latest data. `out->valid` is false until first fetch. */
void app_propagation_get(app_prop_data_t *out);

/* Request an out-of-band refresh (e.g. when WiFi just reconnected). */
void app_propagation_request_refresh(void);
