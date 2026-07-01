#include "target.h"

// Entry points from sketch.cpp, renamed at compile time so they do not clash
// with other targets in the same binary. See this target's CMakeLists.txt.
void samplegfx_setup();
void samplegfx_loop();

static const BoardDesc kBoard = {
    "Generic GFX 320x240", 320, 240, 0, false, false, false
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
