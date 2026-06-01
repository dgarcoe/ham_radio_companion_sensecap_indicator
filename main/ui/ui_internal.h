#pragma once

#include "lvgl.h"
#include "ui.h"

/* Per-screen builders. They render into `parent` and own all children of it. */
lv_obj_t *ui_watch_create(lv_obj_t *parent, const app_config_t *cfg);
lv_obj_t *ui_settings_create(lv_obj_t *parent, const app_config_t *cfg);

/* Drives the watch's seconds tick. Owned by ui.c, called by ui_watch.c. */
void ui_watch_register_tick(lv_obj_t *screen);
