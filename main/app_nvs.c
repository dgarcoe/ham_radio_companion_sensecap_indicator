#include "app_nvs.h"

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "nvs";
static const char *NS = "hamcomp";

esp_err_t app_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t read_str(nvs_handle_t h, const char *key, char *out, size_t out_sz)
{
    size_t sz = out_sz;
    esp_err_t err = nvs_get_str(h, key, out, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    return err;
}

esp_err_t app_nvs_load(app_config_t *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->tz, "UTC0");
    /* Sensible default cluster; user can change via settings later. */
    strcpy(out->dx_host, "dxc.nc7j.com");
    out->dx_port = 7373;
    out->alert_enabled = true;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    read_str(h, "callsign", out->callsign, sizeof(out->callsign));
    read_str(h, "locator",  out->locator,  sizeof(out->locator));
    read_str(h, "ssid",     out->wifi_ssid,sizeof(out->wifi_ssid));
    read_str(h, "psk",      out->wifi_psk, sizeof(out->wifi_psk));
    read_str(h, "tz",       out->tz,       sizeof(out->tz));
    read_str(h, "dx_host",  out->dx_host,  sizeof(out->dx_host));
    int32_t port = 0;
    if (nvs_get_i32(h, "dx_port", &port) == ESP_OK && port > 0) {
        out->dx_port = port;
    }
    read_str(h, "alert_list", out->alert_list, sizeof(out->alert_list));
    uint8_t aen = 1;
    nvs_get_u8(h, "alert_en", &aen);
    out->alert_enabled = aen != 0;

    uint8_t cfg = 0;
    nvs_get_u8(h, "cfg", &cfg);
    out->configured = cfg != 0;

    nvs_close(h);
    ESP_LOGI(TAG, "loaded: callsign='%s' ssid='%s' dx=%s:%d configured=%d",
             out->callsign, out->wifi_ssid, out->dx_host, out->dx_port,
             out->configured);
    return ESP_OK;
}

esp_err_t app_nvs_save(const app_config_t *cfg)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NS, NVS_READWRITE, &h));
    nvs_set_str(h, "callsign", cfg->callsign);
    nvs_set_str(h, "locator",  cfg->locator);
    nvs_set_str(h, "ssid",     cfg->wifi_ssid);
    nvs_set_str(h, "psk",      cfg->wifi_psk);
    nvs_set_str(h, "tz",       cfg->tz);
    nvs_set_str(h, "dx_host",  cfg->dx_host);
    nvs_set_i32(h, "dx_port",  cfg->dx_port);
    nvs_set_str(h, "alert_list", cfg->alert_list);
    nvs_set_u8(h, "alert_en", cfg->alert_enabled ? 1 : 0);
    nvs_set_u8(h, "cfg", cfg->configured ? 1 : 0);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}
