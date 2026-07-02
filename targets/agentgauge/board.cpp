#include "target.h"

// The agentgauge board: a 480x480 AMOLED panel (QSPI CO5300) with two GPIO
// side buttons (PRIMARY screen-cycle, SECONDARY brightness-cycle) plus the
// AXP2101 PWR button. Boots the shared agentgauge firmware (agentgauge_fw);
// this file only describes the hardware. The firmware's entry points are
// renamed at compile time (setup->agentgauge_setup) so it coexists with other
// firmwares (e.g. sample_gfx) in one binary.
void agentgauge_setup();
void agentgauge_loop();

static const SimButton kButtons[] = {
    {"PRIMARY",   ACT_PRIMARY,   0, ' '},   // GPIO9  - screen cycle
    {"PWR",       ACT_PWR,       0, 'p'},   // AXP2101 power button (not read by firmware yet)
    {"SECONDARY", ACT_SECONDARY, 0, '\t'},  // GPIO10 - brightness cycle
};

static const BoardDesc kBoard = {
    "agentgauge", 480, 480,
    false, true, false,          // has_rotation, has_battery, has_imu
    kButtons, 3,
};

static const SimTarget kTarget = {
    "agentgauge",
    "agentgauge Wi-Fi Claude usage gauge (480x480 AMOLED)",
    agentgauge_setup,
    agentgauge_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
