#pragma once

#include <stddef.h>
#include "lvgl.h"
#include "app_nvs.h"

/* Each menu-accessible feature is one entry. Adding a new feature is just:
 *   1. write ui_<name>.c with `lv_obj_t *ui_<name>_create(parent, cfg);`
 *   2. add an entry below.
 *   3. (the registry order is the order tiles appear on the menu) */
typedef struct {
    const char *title;
    const char *icon;        /* LV_SYMBOL_* */
    lv_obj_t *(*create)(lv_obj_t *parent, const app_config_t *cfg);
} ui_screen_def_t;

extern const ui_screen_def_t ui_screens[];
extern const size_t ui_screen_count;

/* Shared helper for not-yet-built features. Renders a centered title and
 * a short body in the dark + neon theme. */
lv_obj_t *ui_stub_create(lv_obj_t *parent, const char *title, const char *body);
