/* PSRAM-backed allocator for LVGL.
 *
 * With CONFIG_LV_USE_CUSTOM_MALLOC=y, LVGL pulls these symbols from
 * the application instead of providing them itself. We route every
 * allocation to heap_caps_malloc(MALLOC_CAP_SPIRAM) so widget trees,
 * draw descriptors, label text and style buffers live in the 8 MB of
 * PSRAM rather than the ~150 KB of internal RAM that WiFi/LWIP/
 * FreeRTOS share. Signatures match LVGL 9.1.0
 * src/stdlib/clib/lv_mem_core_clib.c (the reference implementation). */

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "lv_psram";

#define LVGL_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
    /* Log so we can confirm this allocator is actually linked + called
     * (it was silently missing once due to a stale build/ folder). */
    ESP_LOGI(TAG, "LVGL allocator: PSRAM via heap_caps_malloc");
}

void lv_mem_deinit(void)
{
    /* Nothing to deinit. */
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    (void)mem;
    (void)bytes;
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    (void)pool;
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, LVGL_CAPS);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, LVGL_CAPS);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    /* heap_caps_get_info exposes far more than lv_mem_monitor_t wants
     * and the fields don't map cleanly, so just zero it - LVGL only
     * uses these stats for its own debug print. */
    if (mon_p) memset(mon_p, 0, sizeof(*mon_p));
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}
