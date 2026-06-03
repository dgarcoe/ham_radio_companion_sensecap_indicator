#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_DX_MAX_SPOTS 50

typedef struct {
    char    spotter[16];     /* "W1AW-3" */
    char    dx_call[16];     /* "DL1ABC" */
    char    freq[12];        /* "14074.0" */
    char    comment[40];     /* "FT8 from Germany" */
    char    time[8];         /* "1234Z" */
    int64_t received_ms;     /* uptime ms when stored */
} app_dx_spot_t;

typedef struct {
    app_dx_spot_t spots[APP_DX_MAX_SPOTS];
    int  head;               /* index of newest valid entry */
    int  count;              /* number of valid entries (<= MAX) */
    bool connected;          /* true while telnet session is open */
} app_dx_state_t;

/* Called from the cluster task. `new_spot` is NULL for connection-state
 * transitions; otherwise it points to the spot just appended. `state` is
 * always non-NULL. */
typedef void (*app_dx_cb_t)(const app_dx_spot_t *new_spot,
                            const app_dx_state_t *state);

/* Connects to host:port (telnet), logs in with `login_call`, and starts
 * streaming spots. Reconnects automatically on disconnect. */
esp_err_t app_dxcluster_init(const char *host, int port,
                             const char *login_call,
                             app_dx_cb_t cb);

/* Replace the cluster endpoint and/or login at runtime (called when the
 * user saves new values in the settings screen). The current session is
 * torn down and a fresh connection is made to the new host. */
void app_dxcluster_reconfigure(const char *host, int port,
                               const char *login_call);

/* Snapshot of the spot ring + connection state. Safe before init. */
void app_dxcluster_get(app_dx_state_t *out);
