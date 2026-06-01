/* Stub: we don't build the codec for this companion, but bsp_board.h
 * still references this typedef in a function-pointer signature. Keep
 * the type so the header compiles. */
#pragma once

typedef enum {
    AUDIO_HAL_08K_SAMPLES,
    AUDIO_HAL_16K_SAMPLES,
    AUDIO_HAL_32K_SAMPLES,
    AUDIO_HAL_44K_SAMPLES,
    AUDIO_HAL_48K_SAMPLES,
} audio_hal_iface_samples_t;
