#include "target.h"

// Firmware entry points from Clawdmeter's main.cpp, renamed at compile time
// (setup->clawdmeter_setup, loop->clawdmeter_loop) so multiple targets can
// coexist in one binary. See this target's CMakeLists.txt.
void clawdmeter_setup();
void clawdmeter_loop();

static const BoardDesc kBoard = {
    "Waveshare AMOLED 2.16 (C6)", 480, 480, 2, false, true, true
};

static const SimTarget kTarget = {
    "clawdmeter",
    "Waveshare ESP32-C6 AMOLED 2.16 (Wi-Fi limits)",
    clawdmeter_setup,
    clawdmeter_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
