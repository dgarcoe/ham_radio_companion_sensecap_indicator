#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define APP_CALLSIGN_MAX 16
#define APP_LOCATOR_MAX  10
#define APP_SSID_MAX     33
#define APP_PSK_MAX      65
#define APP_TZ_MAX       32

typedef struct {
    char callsign[APP_CALLSIGN_MAX];
    char locator[APP_LOCATOR_MAX];
    char wifi_ssid[APP_SSID_MAX];
    char wifi_psk[APP_PSK_MAX];
    char tz[APP_TZ_MAX];
    bool configured;
} app_config_t;

esp_err_t app_nvs_init(void);
esp_err_t app_nvs_load(app_config_t *out);
esp_err_t app_nvs_save(const app_config_t *cfg);
