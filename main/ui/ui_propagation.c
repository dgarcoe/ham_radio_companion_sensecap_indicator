#include "ui_internal.h"
#include "ui_screens.h"

lv_obj_t *ui_propagation_create(lv_obj_t *parent, const app_config_t *cfg)
{
    (void)cfg;
    return ui_stub_create(parent, "Propagation",
        "Solar flux, SSN, A/K indices and HF band conditions, refreshed "
        "from hamqsl.com every few minutes.");
}
