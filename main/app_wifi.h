#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "app_nvs.h"

typedef enum {
    APP_WIFI_DISCONNECTED,
    APP_WIFI_CONNECTING,
    APP_WIFI_CONNECTED,
    APP_WIFI_AP_PORTAL,
} app_wifi_state_t;

typedef void (*app_wifi_state_cb_t)(app_wifi_state_t state, const char *ip);

esp_err_t app_wifi_init(app_wifi_state_cb_t cb);

/* Try STA with stored creds; if none/fails, start AP portal "HamCompanion-XXXX". */
esp_err_t app_wifi_start(const app_config_t *cfg);

/* Re-connect STA with new credentials (called from portal after save). */
esp_err_t app_wifi_apply(const app_config_t *cfg);

app_wifi_state_t app_wifi_get_state(void);
const char *app_wifi_get_ip(void);
const char *app_wifi_get_ap_ssid(void);
