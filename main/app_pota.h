#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_POTA_MAX_SPOTS 100

typedef struct {
    char activator[16];      /* "K1ABC" */
    char freq[12];           /* "14274.0" (kHz) */
    char mode[8];            /* "CW", "SSB", "FT8", ... */
    char park_ref[12];       /* "US-1234" */
    char park_name[40];      /* "Acadia National Park" */
    char spotter[16];        /* who reported the activation */
    char time_str[8];        /* "1430Z" extracted from spotTime */
    char comments[40];       /* free-form note */
    char location[12];       /* e.g. "ME" or "QLD" */
    int64_t received_ms;     /* uptime ms when stored */
} app_pota_spot_t;

typedef struct {
    app_pota_spot_t spots[APP_POTA_MAX_SPOTS];
    int   count;
    bool  connected;         /* last fetch returned a parseable body */
    int64_t last_update_ms;
} app_pota_state_t;

/* Fired after each fetch (success or failure). `state` is always non-NULL. */
typedef void (*app_pota_cb_t)(const app_pota_state_t *state);

/* Starts a background task that GETs the POTA spot list every 60 s.
 * Safe to call before WiFi is up - the task retries until it gets a
 * response. */
esp_err_t app_pota_init(app_pota_cb_t cb);

/* Snapshot the latest state. */
void app_pota_get(app_pota_state_t *out);

/* Wake the task and re-fetch immediately (e.g. on WiFi reconnect). */
void app_pota_request_refresh(void);
