#pragma once
#include <deque>

// Virtual input bus. The CLI writes injected state here; each target's HAL
// adapter reads from it. Target-agnostic so injection commands (battery, button,
// tap, rotate, gpio) work the same across targets.
struct SimInput {
    int  battery_pct = 75;
    bool charging    = false;
    bool vbus        = true;

    bool button[4]   = {false, false, false, false};   // logical buttons

    bool touch_pressed = false;
    int  touch_x = 0, touch_y = 0;

    int  quadrant = 0;                 // 0..3 rotation

    bool motion_nudge = false;         // one-shot accelerometer wake nudge (agentgauge)

    std::deque<int> pwr_events;        // 1=press, 2=long, 3=release
};

SimInput& sim_input();
void      sim_input_reset();
