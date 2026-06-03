#include "app_dxcluster.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "dx";

static char     s_host[64];
static int      s_port;
static char     s_login[16];
static app_dx_cb_t       s_cb;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_kick;     /* poked to break reconnect-sleep */
static volatile int      s_sock = -1; /* current socket, -1 when idle */
static app_dx_state_t    s_state;

/* --- Spot line parser ---
 *
 * AR-Cluster / DX-Spider format:
 *   "DX de <spotter>:<spaces><freq><spaces><dxcall><spaces><comment><spaces><time>Z"
 *
 * We carve fields out by whitespace from the left and take the trailing
 * HHMMZ as time. */
static bool parse_spot(const char *line, app_dx_spot_t *out)
{
    if (strncmp(line, "DX de ", 6) != 0) return false;
    const char *p = line + 6;
    const char *colon = strchr(p, ':');
    if (!colon) return false;

    size_t n = (size_t)(colon - p);
    if (n >= sizeof(out->spotter)) n = sizeof(out->spotter) - 1;
    memcpy(out->spotter, p, n);
    out->spotter[n] = '\0';

    p = colon + 1;
    while (*p == ' ' || *p == '\t') p++;

    /* freq */
    const char *e = p;
    while (*e && *e != ' ' && *e != '\t') e++;
    n = (size_t)(e - p);
    if (n >= sizeof(out->freq)) n = sizeof(out->freq) - 1;
    memcpy(out->freq, p, n);
    out->freq[n] = '\0';

    p = e;
    while (*p == ' ' || *p == '\t') p++;

    /* dx call */
    e = p;
    while (*e && *e != ' ' && *e != '\t') e++;
    n = (size_t)(e - p);
    if (n >= sizeof(out->dx_call)) n = sizeof(out->dx_call) - 1;
    memcpy(out->dx_call, p, n);
    out->dx_call[n] = '\0';

    p = e;
    while (*p == ' ' || *p == '\t') p++;

    size_t rest = strlen(p);
    while (rest > 0 && (p[rest - 1] == ' ' || p[rest - 1] == '\r' ||
                        p[rest - 1] == '\n' || p[rest - 1] == '\t')) {
        rest--;
    }

    out->time[0] = '\0';
    if (rest >= 5 && p[rest - 1] == 'Z') {
        memcpy(out->time, p + rest - 5, 5);
        out->time[5] = '\0';
        size_t clen = rest - 5;
        while (clen > 0 && (p[clen - 1] == ' ' || p[clen - 1] == '\t')) clen--;
        if (clen >= sizeof(out->comment)) clen = sizeof(out->comment) - 1;
        memcpy(out->comment, p, clen);
        out->comment[clen] = '\0';
    } else {
        size_t clen = rest;
        if (clen >= sizeof(out->comment)) clen = sizeof(out->comment) - 1;
        memcpy(out->comment, p, clen);
        out->comment[clen] = '\0';
    }

    out->received_ms = esp_timer_get_time() / 1000;
    return true;
}

static void store_spot(const app_dx_spot_t *spot)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state.count == 0) {
        s_state.head = 0;
    } else {
        s_state.head = (s_state.head + 1) % APP_DX_MAX_SPOTS;
    }
    s_state.spots[s_state.head] = *spot;
    if (s_state.count < APP_DX_MAX_SPOTS) s_state.count++;
    app_dx_state_t snap = s_state;
    xSemaphoreGive(s_mutex);
    if (s_cb) s_cb(spot, &snap);
}

static void set_connected(bool on)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.connected = on;
    app_dx_state_t snap = s_state;
    xSemaphoreGive(s_mutex);
    if (s_cb) s_cb(NULL, &snap);
}

static int connect_cluster(void)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", s_port);

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *result = NULL;
    int err = getaddrinfo(s_host, port_str, &hints, &result);
    if (err != 0 || result == NULL) {
        ESP_LOGW(TAG, "DNS %s:%d failed (gai=%d)", s_host, s_port, err);
        if (result) freeaddrinfo(result);
        return -1;
    }

    int sock = socket(result->ai_family, result->ai_socktype, 0);
    if (sock < 0) {
        ESP_LOGW(TAG, "socket() failed: %d", errno);
        freeaddrinfo(result);
        return -1;
    }
    if (connect(sock, result->ai_addr, result->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "connect() failed: %d", errno);
        close(sock);
        freeaddrinfo(result);
        return -1;
    }
    freeaddrinfo(result);

    /* Long recv timeout - clusters can be quiet for a while. We'll
     * reconnect on real disconnect (recv returns 0). */
    struct timeval tv = { .tv_sec = 5 * 60 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return sock;
}

static void session(int sock)
{
    set_connected(true);
    ESP_LOGI(TAG, "session start: login=%s", s_login);

    /* Wait briefly for the login prompt, then send callsign. Some
     * clusters prompt with "login:", others with their banner; either
     * way, sending callsign\r\n after a short read is the safest pattern. */
    char buf[1024];
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n > 0) buf[n] = '\0';

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s\r\n", s_login);
    send(sock, cmd, strlen(cmd), 0);

    /* Ask for recent backlog so the UI populates immediately. */
    vTaskDelay(pdMS_TO_TICKS(1500));
    const char *sh = "SH/DX 30\r\n";
    send(sock, sh, strlen(sh), 0);

    char line[512];
    size_t lpos = 0;

    for (;;) {
        n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n < 0 && errno == EAGAIN) {
            /* recv timeout - just keep waiting. */
            continue;
        }
        if (n <= 0) {
            ESP_LOGW(TAG, "recv ended: %d errno=%d", n, errno);
            break;
        }

        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line[lpos] = '\0';
                if (lpos > 0 && line[lpos - 1] == '\r') {
                    line[lpos - 1] = '\0';
                }
                if (strncmp(line, "DX de ", 6) == 0) {
                    app_dx_spot_t spot = {0};
                    if (parse_spot(line, &spot)) {
                        ESP_LOGI(TAG, "spot %-10s %-9s by %s",
                                 spot.dx_call, spot.freq, spot.spotter);
                        store_spot(&spot);
                    }
                }
                lpos = 0;
            } else if (lpos < sizeof(line) - 1) {
                line[lpos++] = c;
            } else {
                lpos = 0; /* overflow, drop line */
            }
        }
    }

    set_connected(false);
}

static void dx_task(void *arg)
{
    (void)arg;
    for (;;) {
        int sock = connect_cluster();
        if (sock >= 0) {
            s_sock = sock;
            ESP_LOGI(TAG, "connected to %s:%d", s_host, s_port);
            session(sock);
            s_sock = -1;
            close(sock);
        }
        ESP_LOGI(TAG, "reconnect in 30s (or on settings change)");
        /* Sleep up to 30s, but wake immediately if reconfigure poked us. */
        xSemaphoreTake(s_kick, pdMS_TO_TICKS(30 * 1000));
    }
}

esp_err_t app_dxcluster_init(const char *host, int port,
                             const char *login_call,
                             app_dx_cb_t cb)
{
    if (!host || !login_call) return ESP_ERR_INVALID_ARG;
    strncpy(s_host, host, sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = '\0';
    s_port = port;
    strncpy(s_login, login_call, sizeof(s_login) - 1);
    s_login[sizeof(s_login) - 1] = '\0';
    s_cb = cb;
    s_mutex = xSemaphoreCreateMutex();
    s_kick  = xSemaphoreCreateBinary();
    if (xTaskCreate(dx_task, "dx", 5120, NULL, 2, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_dxcluster_reconfigure(const char *host, int port,
                               const char *login_call)
{
    bool changed = false;
    if (host && *host && strncmp(s_host, host, sizeof(s_host)) != 0) {
        strncpy(s_host, host, sizeof(s_host) - 1);
        s_host[sizeof(s_host) - 1] = '\0';
        changed = true;
    }
    if (port > 0 && port != s_port) {
        s_port = port;
        changed = true;
    }
    if (login_call && *login_call &&
        strncmp(s_login, login_call, sizeof(s_login)) != 0) {
        strncpy(s_login, login_call, sizeof(s_login) - 1);
        s_login[sizeof(s_login) - 1] = '\0';
        changed = true;
    }
    if (!changed) return;

    ESP_LOGI(TAG, "reconfigure -> %s:%d login=%s", s_host, s_port, s_login);

    /* Break the active recv() by shutting down the socket; the task will
     * close it and loop back to connect_cluster(). Don't close() here -
     * the task owns the descriptor. */
    int sock = s_sock;
    if (sock >= 0) shutdown(sock, SHUT_RDWR);
    if (s_kick) xSemaphoreGive(s_kick);
}

void app_dxcluster_get(app_dx_state_t *out)
{
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}
