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

/* Navigation. */
void ui_show_watch(void);
void ui_show_menu(void);
void ui_show_feature(int feature_index);   /* index into ui_screens[] */

/* Settings screen calls this when the user taps "Save". The callback
 * receives the new config; the caller is responsible for persisting it
 * (NVS) and applying it (WiFi, status-bar callsign, etc.). */
typedef void (*ui_settings_saved_cb_t)(const app_config_t *cfg);
void ui_settings_set_saved_cb(ui_settings_saved_cb_t cb);
