#include "ui_internal.h"
#include "ui_screens.h"

lv_obj_t *ui_sota_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;
    return ui_stub_create(parent, "Summits On The Air",
        "SOTA Watch alerts and live spots, with summit info and a "
        "chaser-friendly band filter.");
}
