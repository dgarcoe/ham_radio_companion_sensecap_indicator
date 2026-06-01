#pragma once

#include "lvgl.h"
#include "app_nvs.h"
#include "app_wifi.h"

/* Builds the screen graph and shows the watch. Must be called with LVGL locked. */
void ui_init(const app_config_t *cfg);

/* Status-bar updates (safe to call from any task; takes LVGL lock internally). */
void ui_set_wifi(app_wifi_state_t st, const char *ip_or_ap);
void ui_set_time_synced(bool synced);
void ui_set_callsign(const char *callsign);

/* Screen routing. */
void ui_show_watch(void);
void ui_show_settings(void);
