#pragma once

#include <stdbool.h>
#include "app_nvs.h"

/* Match-source tag, used only for the toast / log. */
typedef enum {
    APP_ALERT_SRC_DX,
    APP_ALERT_SRC_POTA,
    APP_ALERT_SRC_SOTA,
} app_alert_source_t;

/* The visual layer registers one callback. It runs on whatever task fed
 * the spot (dx, pota, sota), so the handler must marshal to LVGL itself
 * (atomic dirty + 1 Hz timer is fine). `text` is short and lives on the
 * caller's stack - copy if you need it past return. */
typedef void (*app_alert_visual_cb_t)(app_alert_source_t src, const char *text);

void app_alert_init(const app_config_t *cfg, app_alert_visual_cb_t cb);

/* Live-update the watchlist / enable flag (called from settings save). */
void app_alert_apply_config(const app_config_t *cfg);

/* Spot ingest hooks. callsign is the activator/DX; extra is freq or
 * summit/park ref, used only for the toast text. Returns true if it
 * matched (and the alert was fired). */
bool app_alert_check(app_alert_source_t src, const char *callsign, const char *extra);
