#include "hal/sound_hal.h"

// C6 has no speaker; the sim sound HAL is a no-op (matches the real board).
void sound_hal_init(void) {}
void sound_hal_tick(void) {}
void sound_hal_play_reset(void) {}
