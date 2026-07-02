#include "board.h"

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "hal/touch_hal.h"
#include "sim_input.h"

// The sim has no I2C bus, AXP2101 PMU, or QMI8658 IMU to bring up. board_init()
// just runs the shim inits in the same order the real board.cpp uses: power
// before display (the real board's panel rail lives on the PMU), then touch
// and buttons.
bool board_init(void) {
    if (!power_hal_init()) return false;
    if (!display_hal_init()) return false;
    touch_hal_init();     // best-effort on hardware; non-fatal here too
    input_hal_init();
    return true;
}

// Mirrors the real board_motion_detected()'s "since last call" contract: a
// nudge injected via the CLI/scenario "motion" step is consumed by the next
// poll, so it fires exactly once per injection.
bool board_motion_detected(void) {
    SimInput& s = sim_input();
    if (!s.motion_nudge) return false;
    s.motion_nudge = false;
    return true;
}
