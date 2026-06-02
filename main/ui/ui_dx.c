#include "ui_internal.h"
#include "ui_screens.h"

lv_obj_t *ui_dx_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;
    return ui_stub_create(parent, "DX Cluster",
        "Live DX spots over WiFi (telnet to a cluster of your choice, "
        "with band / mode filters and a tap-to-log workflow).");
}
