#include "target.h"

// Entry points from sketch.cpp, renamed at compile time so they do not clash
// with other targets in the same binary. See this target's CMakeLists.txt.
void cydtft_setup();
void cydtft_loop();

// Same CYD hardware as the `cyd` target, but this app is written against the
// real TFT_eSPI library (the CYD community standard) instead of Arduino_GFX.
static const SimButton kButtons[] = {
    {"BOOT", ACT_PRIMARY, 0, ' '},
};

static const BoardDesc kBoard = {
    "ESP32-2432S028R (CYD, TFT_eSPI)", 320, 240,
    false, false, false,   // has_rotation, has_battery, has_imu
    kButtons, 1,
};

static const SimTarget kTarget = {
    "cyd_tft",
    "Cheap Yellow Display running a real TFT_eSPI touch-button UI",
    cydtft_setup,
    cydtft_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
