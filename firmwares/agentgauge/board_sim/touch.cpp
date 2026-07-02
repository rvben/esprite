#include "hal/touch_hal.h"
#include "sim_input.h"

bool touch_hal_init(void) { return true; }

bool touch_hal_read(int16_t* x, int16_t* y) {
    SimInput& s = sim_input();
    if (!s.touch_pressed) return false;
    *x = (int16_t)s.touch_x;
    *y = (int16_t)s.touch_y;
    return true;
}
