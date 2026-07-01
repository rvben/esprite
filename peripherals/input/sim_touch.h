#pragma once
#include "sim_input.h"

// Touch read for non-LVGL sketches (Arduino_GFX / raw framebuffer apps). LVGL
// targets read touch through their HAL; a plain display sketch uses this to poll
// the sim's injected touch (esprite `tap x y`). Coordinates are screen-space,
// since the sim injects taps directly in screen coordinates - no resistive-panel
// ADC calibration to model.
//
// Returns true while a tap is held and fills x,y with the touch point.
inline bool sim_touch(int* x, int* y) {
    if (!sim_input().touch_pressed) return false;
    if (x) *x = sim_input().touch_x;
    if (y) *y = sim_input().touch_y;
    return true;
}
