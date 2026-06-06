#include "app_portal.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "portal";

static httpd_handle_t s_server = NULL;
static app_config_t s_cfg;
static app_portal_saved_cb_t s_saved_cb = NULL;

/* Static HTML; the form is populated via /config (JSON) after load.
 * Keeping it free of printf-style %s means CSS `width:100%` etc. are safe. */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Ham Radio Companion setup</title>"
"<style>"
"body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
"background:#05070d;color:#e6f5ff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px}"
".card{width:100%;max-width:420px;background:#0b1220;border:1px solid #1a2540;border-radius:18px;"
"padding:28px;box-shadow:0 0 40px rgba(0,255,200,.08)}"
"h1{margin:0 0 4px;font-size:22px;letter-spacing:.5px}"
"h1 span{color:#00f0c8}"
".sub{color:#6e7ea0;font-size:13px;margin-bottom:20px}"
"label{display:block;font-size:12px;color:#8aa0c8;margin:14px 0 6px;letter-spacing:.4px;text-transform:uppercase}"
"input{width:100%;box-sizing:border-box;background:#070b16;border:1px solid #1a2540;border-radius:10px;"
"padding:12px 14px;color:#e6f5ff;font-size:15px;outline:none}"
"input:focus{border-color:#00f0c8;box-shadow:0 0 0 3px rgba(0,240,200,.15)}"
"button{margin-top:22px;width:100%;background:linear-gradient(135deg,#00f0c8,#3a8dff);color:#001018;"
"border:0;border-radius:12px;padding:14px;font-size:16px;font-weight:600;letter-spacing:.5px;cursor:pointer}"
".ok{margin-top:18px;color:#00f0c8;font-size:13px;text-align:center;display:none}"
"</style></head><body><div class=card>"
"<h1>Ham Radio <span>Companion</span></h1>"
"<div class=sub>Initial setup &middot; SenseCAP Indicator D1L</div>"
"<form id=f>"
"<label>Callsign</label><input name=callsign id=callsign maxlength=15 autocapitalize=characters required>"
"<label>Grid locator</label><input name=locator id=locator maxlength=8 autocapitalize=characters>"
"<label>WiFi SSID</label><input name=ssid id=ssid maxlength=32 required>"
"<label>WiFi password</label><input name=psk id=psk type=password maxlength=64>"
"<label>Spot alert watchlist</label>"
"<input name=alert_list id=alert_list maxlength=95 placeholder='ZL,VK,EA1RFI,W7XYZ'>"
"<label style=display:flex;align-items:center;gap:8px;text-transform:none;color:#e6f5ff;font-size:14px;margin-top:14px>"
"<input type=checkbox name=alert_enabled id=alert_enabled style=width:auto;margin:0> Enable spot alerts</label>"
"<button type=submit>Save &amp; connect</button>"
"<div class=ok id=ok>Saved. Device is reconnecting&hellip;</div>"
"</form>"
"<script>"
"fetch('/config').then(r=>r.json()).then(c=>{"
"document.getElementById('callsign').value=c.callsign||'';"
"document.getElementById('locator').value=c.locator||'';"
"document.getElementById('ssid').value=c.ssid||'';"
"document.getElementById('alert_list').value=c.alert_list||'';"
"document.getElementById('alert_enabled').checked=c.alert_enabled!==false;});"
"document.getElementById('f').addEventListener('submit',async e=>{e.preventDefault();"
"const fd=new FormData(e.target);const b={};fd.forEach((v,k)=>b[k]=v);"
"b.alert_enabled=document.getElementById('alert_enabled').checked?'1':'0';"
"const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});"
"if(r.ok){document.getElementById('ok').style.display='block';}});"
"</script></div></body></html>";

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

/* Returns just the non-secret fields so the form can prefill. PSK is never
 * sent back to the browser. */
static esp_err_t config_get(httpd_req_t *req)
{
    char body[384];
    int n = snprintf(body, sizeof(body),
                     "{\"callsign\":\"%s\",\"locator\":\"%s\",\"ssid\":\"%s\","
                     "\"alert_list\":\"%s\",\"alert_enabled\":%s}",
                     s_cfg.callsign, s_cfg.locator, s_cfg.wifi_ssid,
                     s_cfg.alert_list,
                     s_cfg.alert_enabled ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, n);
}

/* Minimal JSON string extractor: finds "key":"value" and copies value out.
 * Stops at the closing quote; does not interpret escapes. */
static void extract(const char *json, const char *key, char *out, size_t out_sz)
{
    out[0] = '\0';
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return;
    p = strchr(p + strlen(pat), ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz) out[i++] = *p++;
    out[i] = '\0';
}

static esp_err_t save_post(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) return ESP_ERR_NO_MEM;
    int r = httpd_req_recv(req, body, total);
    if (r <= 0) { free(body); return ESP_FAIL; }
    body[r] = '\0';

    app_config_t nc = s_cfg;
    extract(body, "callsign",   nc.callsign,   sizeof(nc.callsign));
    extract(body, "locator",    nc.locator,    sizeof(nc.locator));
    extract(body, "ssid",       nc.wifi_ssid,  sizeof(nc.wifi_ssid));
    extract(body, "alert_list", nc.alert_list, sizeof(nc.alert_list));

    char psk[APP_PSK_MAX];
    extract(body, "psk", psk, sizeof(psk));
    if (psk[0] != '\0') {
        strncpy(nc.wifi_psk, psk, sizeof(nc.wifi_psk));
    }
    char en[4];
    extract(body, "alert_enabled", en, sizeof(en));
    nc.alert_enabled = (en[0] == '1');
    nc.configured = (nc.callsign[0] != '\0' && nc.wifi_ssid[0] != '\0');

    free(body);

    ESP_LOGI(TAG, "saved: callsign='%s' ssid='%s'", nc.callsign, nc.wifi_ssid);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    if (s_saved_cb) s_saved_cb(&nc);
    s_cfg = nc;
    return ESP_OK;
}

esp_err_t app_portal_start(const app_config_t *current, app_portal_saved_cb_t cb)
{
    s_cfg = *current;
    s_saved_cb = cb;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) != ESP_OK) return ESP_FAIL;

    httpd_uri_t ix = { .uri = "/",       .method = HTTP_GET,  .handler = index_get };
    httpd_uri_t cg = { .uri = "/config", .method = HTTP_GET,  .handler = config_get };
    httpd_uri_t sv = { .uri = "/save",   .method = HTTP_POST, .handler = save_post };
    httpd_register_uri_handler(s_server, &ix);
    httpd_register_uri_handler(s_server, &cg);
    httpd_register_uri_handler(s_server, &sv);
    return ESP_OK;
}

void app_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
