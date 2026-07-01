#include "hal/touch_hal.h"
#include "sim_input.h"

void touch_hal_init(void) {}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    SimInput& s = sim_input();
    *pressed = s.touch_pressed;
    *x = (uint16_t)s.touch_x;
    *y = (uint16_t)s.touch_y;
}
