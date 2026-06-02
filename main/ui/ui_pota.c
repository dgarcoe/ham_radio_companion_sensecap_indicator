#include "ui_internal.h"
#include "ui_screens.h"

lv_obj_t *ui_pota_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;
    return ui_stub_create(parent, "Parks On The Air",
        "Active POTA spots, nearby parks, and a hunter / activator view "
        "pulled live from pota.app.");
}
