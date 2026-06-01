#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t app_time_init(void);
bool app_time_is_synced(void);
/* Fills tm with current UTC. */
void app_time_now_utc(struct tm *out);
