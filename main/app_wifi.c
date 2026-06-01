#include "app_wifi.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"

static const char *TAG = "wifi";

static app_wifi_state_t s_state = APP_WIFI_DISCONNECTED;
static app_wifi_state_cb_t s_cb = NULL;
static char s_ip[16] = {0};
static char s_ap_ssid[33] = {0};
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static int s_retry = 0;

#define MAX_STA_RETRY 4

static void set_state(app_wifi_state_t st, const char *ip)
{
    s_state = st;
    if (ip) {
        strncpy(s_ip, ip, sizeof(s_ip) - 1);
        s_ip[sizeof(s_ip) - 1] = '\0';
    } else {
        s_ip[0] = '\0';
    }
    if (s_cb) s_cb(st, s_ip);
}

static void start_ap_portal(void)
{
    wifi_config_t apc = {0};
    strncpy((char *)apc.ap.ssid, s_ap_ssid, sizeof(apc.ap.ssid));
    apc.ap.ssid_len = strlen(s_ap_ssid);
    apc.ap.channel = 1;
    apc.ap.max_connection = 4;
    apc.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP portal up: SSID=%s  http://192.168.4.1/", s_ap_ssid);
    set_state(APP_WIFI_AP_PORTAL, "192.168.4.1");
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state != APP_WIFI_AP_PORTAL && s_retry < MAX_STA_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "STA disconnect, retry %d/%d", s_retry, MAX_STA_RETRY);
            esp_wifi_connect();
            set_state(APP_WIFI_CONNECTING, NULL);
        } else if (s_state != APP_WIFI_AP_PORTAL) {
            ESP_LOGW(TAG, "STA failed, falling back to AP portal");
            esp_wifi_stop();
            start_ap_portal();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "got IP %s", ip);
        s_retry = 0;
        set_state(APP_WIFI_CONNECTED, ip);
    }
}

esp_err_t app_wifi_init(app_wifi_state_cb_t cb)
{
    s_cb = cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "HamCompanion-%02X%02X", mac[4], mac[5]);

    return ESP_OK;
}

static esp_err_t start_sta(const app_config_t *cfg)
{
    wifi_config_t sc = {0};
    strncpy((char *)sc.sta.ssid, cfg->wifi_ssid, sizeof(sc.sta.ssid));
    strncpy((char *)sc.sta.password, cfg->wifi_psk, sizeof(sc.sta.password));
    sc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sc));
    s_retry = 0;
    set_state(APP_WIFI_CONNECTING, NULL);
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t app_wifi_start(const app_config_t *cfg)
{
    if (cfg->configured && cfg->wifi_ssid[0] != '\0') {
        return start_sta(cfg);
    }
    start_ap_portal();
    return ESP_OK;
}

esp_err_t app_wifi_apply(const app_config_t *cfg)
{
    esp_wifi_stop();
    return start_sta(cfg);
}

app_wifi_state_t app_wifi_get_state(void) { return s_state; }
const char *app_wifi_get_ip(void)         { return s_ip; }
const char *app_wifi_get_ap_ssid(void)    { return s_ap_ssid; }
