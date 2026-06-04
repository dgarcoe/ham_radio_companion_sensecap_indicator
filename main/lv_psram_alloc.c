/* PSRAM-backed allocator implementation for LVGL.
 *
 * LVGL's default allocator (builtin tlsf or libc malloc) puts every
 * widget tree, label text, draw descriptor and style buffer in internal
 * RAM. On the SenseCAP D1L that's ~150 KB after WiFi/LWIP/FreeRTOS, and
 * a single screen of LVGL panels was enough to OOM.
 *
 * With LV_USE_CUSTOM_MALLOC = y, LVGL calls these wrappers instead.
 * We route to heap_caps_malloc(MALLOC_CAP_SPIRAM) - 8 MB of slower-
 * but-plentiful PSRAM. Internal RAM stays reserved for things that
 * genuinely need it (DMA buffers, FreeRTOS objects, ISR contexts).
 */

#include <string.h>
#include "esp_heap_caps.h"
#include "lvgl.h"

#define LVGL_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, LVGL_CAPS);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, LVGL_CAPS);
}

void lv_mem_init_core(void)   {}
void lv_mem_deinit_core(void) {}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    /* The system heap doesn't expose tlsf-style fragmentation stats,
     * so just return zeroes. LVGL's diagnostics will look empty, which
     * is fine - we're not relying on its internal mem monitor. */
    if (mon) memset(mon, 0, sizeof(*mon));
}
