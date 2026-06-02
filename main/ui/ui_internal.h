#pragma once

#include "lvgl.h"
#include "ui.h"

/* Each screen creator renders into `parent` and owns all children. */
lv_obj_t *ui_watch_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_menu_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_pota_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_sota_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_propagation_create(lv_obj_t *parent, const app_config_t *cfg);

/* Drives the watch's seconds tick. Owned by ui.c, called by ui_watch.c. */
void ui_watch_register_tick(lv_obj_t *screen);
