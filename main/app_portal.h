#pragma once

#include "esp_err.h"
#include "app_nvs.h"

typedef void (*app_portal_saved_cb_t)(const app_config_t *new_cfg);

esp_err_t app_portal_start(const app_config_t *current, app_portal_saved_cb_t cb);
void app_portal_stop(void);
