#include "target.h"

// Entry points from sketch.cpp, renamed at compile time so they do not clash
// with other targets in the same binary. See this target's CMakeLists.txt.
void samplegfx_setup();
void samplegfx_loop();

// A bare sketch with no physical controls: the window shows just the screen.
static const BoardDesc kBoard = {
    "Generic GFX 320x240", 320, 240,
    false, false, false,   // has_rotation, has_battery, has_imu
    nullptr, 0,            // no buttons
};

static const SimTarget kTarget = {
    "sample_gfx",
    "Minimal Arduino_GFX sketch (generality proof, zero app-specific sim code)",
    samplegfx_setup,
    samplegfx_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
