#include "board.h"

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "hal/touch_hal.h"

// The sim has no I2C bus, AXP2101 PMU, or QMI8658 IMU to bring up (and no
// rotation/IMU use in the firmware at all). board_init() just runs the shim
// inits in the same order the real board.cpp uses: power before display (the
// real board's panel rail lives on the PMU), then touch and buttons.
bool board_init(void) {
    if (!power_hal_init()) return false;
    if (!display_hal_init()) return false;
    touch_hal_init();     // best-effort on hardware; non-fatal here too
    input_hal_init();
    return true;
}
