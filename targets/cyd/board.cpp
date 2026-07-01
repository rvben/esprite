#include "target.h"

// Entry points from sketch.cpp, renamed at compile time so they do not clash
// with other targets in the same binary. See this target's CMakeLists.txt.
void cyd_setup();
void cyd_loop();

// The CYD's only user button is BOOT (GPIO0); touch is the primary input and is
// available on every target via the sim touch bus (no BoardDesc flag needed).
static const SimButton kButtons[] = {
    {"BOOT", ACT_PRIMARY, 0, ' '},
};

static const BoardDesc kBoard = {
    "ESP32-2432S028R (CYD)", 320, 240,
    false, false, false,   // has_rotation, has_battery, has_imu
    kButtons, 1,
};

static const SimTarget kTarget = {
    "cyd",
    "Cheap Yellow Display (ESP32-2432S028R): 320x240 ILI9341 + touch paint demo",
    cyd_setup,
    cyd_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
