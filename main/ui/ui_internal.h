#pragma once

#include "lvgl.h"
#include "ui.h"
#include "app_propagation.h"
#include "app_dxcluster.h"
#include "app_pota.h"
#include "app_sota.h"
#include "app_sats.h"
#include "app_mufmap.h"

/* Each screen creator renders into `parent` and owns all children. */
lv_obj_t *ui_watch_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_menu_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg);
/* Hide the settings keyboard. The kb is a sibling of the settings form
 * (so it doesn't scroll with the cards), which means LV_OBJ_FLAG_HIDDEN
 * on the form doesn't hide it - ui.c calls this when navigating away. */
void ui_settings_on_hidden(void);
lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_pota_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_sota_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_propagation_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_grayline_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_sats_create(lv_obj_t *parent, const app_config_t *cfg);

/* Drives the watch's seconds tick. Owned by ui.c, called by ui_watch.c. */
void ui_watch_register_tick(lv_obj_t *screen);

/* Propagation pushes data updates into its screen; thread-safe (takes
 * the LVGL lock internally). */
void ui_propagation_on_update(const app_prop_data_t *data);

/* DX cluster pushes new spot + state updates into its screen; thread-safe. */
void ui_dx_on_update(const app_dx_spot_t *new_spot, const app_dx_state_t *state);

/* POTA pushes new state into its screen; thread-safe (flips a dirty bit
 * that the LVGL-side timer picks up at 1 Hz). */
void ui_pota_on_update(const app_pota_state_t *state);

/* SOTA: same pattern as POTA. */
void ui_sota_on_update(const app_sota_state_t *state);

/* Satellites: thread-safe push from the predictor task. */
void ui_sats_on_update(const app_sats_state_t *state);

/* MUF map (prop.kc2g.com): bytes-arrived push. Triggers an LVGL image
 * re-decode inside the propagation tab. */
void ui_mufmap_on_update(const app_mufmap_snap_t *snap);
