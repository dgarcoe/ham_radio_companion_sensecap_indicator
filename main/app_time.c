#include "app_time.h"

#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "time";
static bool s_synced = false;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "SNTP synced");
}

esp_err_t app_time_init(void)
{
    /* UTC for the watch face; the user-configurable tz is layered on top later. */
    setenv("TZ", "UTC0", 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    cfg.start = false;
    esp_netif_sntp_init(&cfg);
    esp_netif_sntp_start();
    return ESP_OK;
}

bool app_time_is_synced(void) { return s_synced; }

void app_time_now_utc(struct tm *out)
{
    time_t now = 0;
    time(&now);
    gmtime_r(&now, out);
}
