#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_SOTA_MAX_SPOTS 100

typedef struct {
    char activator[16];      /* "M0ABC" */
    char freq[12];           /* "14062.0" (kHz, converted from MHz returned by API) */
    char mode[8];            /* "CW", "SSB", "FM", "DATA" */
    char summit_ref[16];     /* "G/LD-001" */
    char summit_name[40];    /* "Scafell Pike" */
    char spotter[16];        /* who reported the activation */
    char time_str[8];        /* "1430Z" extracted from timeStamp */
    char comments[40];       /* free-form note */
    int64_t received_ms;
} app_sota_spot_t;

typedef struct {
    app_sota_spot_t spots[APP_SOTA_MAX_SPOTS];
    int   count;
    bool  connected;
    int64_t last_update_ms;
} app_sota_state_t;

typedef void (*app_sota_cb_t)(const app_sota_state_t *state);

/* Starts a background task that GETs the SOTA spot list every 90 s. */
esp_err_t app_sota_init(app_sota_cb_t cb);

void app_sota_get(app_sota_state_t *out);

void app_sota_request_refresh(void);
