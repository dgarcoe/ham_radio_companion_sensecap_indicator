#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* MUF map fetcher: pulls the current global F2-layer MUF render from
 * https://prop.kc2g.com/ every 15 minutes and stashes the raw PNG bytes
 * in PSRAM so the UI can hand them straight to LVGL's image decoder.
 *
 * The data is just bytes -- this module deliberately doesn't decode
 * anything, so LVGL's lodepng integration owns the heavy lifting. */

typedef struct {
    const uint8_t *data;        /* PNG bytes; NULL until first success */
    size_t         size;
    int64_t        last_fetch_ms;   /* uptime ms of last successful pull */
    bool           ever_fetched;
} app_mufmap_snap_t;

typedef void (*app_mufmap_cb_t)(const app_mufmap_snap_t *snap);

/* Spawn the background fetcher. Safe to call before WiFi is up -- the
 * task retries every 30 s until kc2g.com responds. */
esp_err_t app_mufmap_init(app_mufmap_cb_t on_update);

/* Snapshot the currently-cached PNG. The returned pointer is valid
 * until the next successful fetch; copy if you need to outlive that. */
void app_mufmap_get(app_mufmap_snap_t *out);

/* Ask for an out-of-band refresh (e.g. after WiFi reconnect). */
void app_mufmap_request_refresh(void);
