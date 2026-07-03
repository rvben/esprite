#include "target.h"

// Waveshare ESP32-S3-Touch-AMOLED-1.8: a 480x480 AMOLED panel (QSPI CO5300)
// with two GPIO side buttons (PRIMARY screen-cycle, SECONDARY brightness-cycle)
// plus the AXP2101 PWR button. Boots the shared agentgauge firmware
// (agentgauge_fw); this file only describes the hardware. The firmware's entry
// points are renamed at compile time (setup->agentgauge_setup) so it coexists
// with other firmwares (e.g. sample_gfx) in one binary.
void agentgauge_setup();
void agentgauge_loop();

static const SimButton kButtons[] = {
    {"PRIMARY",   ACT_PRIMARY,   0, ' ',  EDGE_RIGHT, 0.30f},  // GPIO9  - screen cycle
    {"PWR",       ACT_PWR,       0, 'p',  EDGE_RIGHT, 0.50f},  // AXP2101 power button (not read by firmware yet)
    {"SECONDARY", ACT_SECONDARY, 0, '\t', EDGE_RIGHT, 0.70f},  // GPIO10 - brightness cycle
};

static const BoardDesc kBoard = {
    "Waveshare ESP32-S3-Touch-AMOLED-1.8", 480, 480,
    false, true, true,           // has_rotation, has_battery, has_imu
    kButtons, 3,
};

static const SimTarget kTarget = {
    "waveshare_amoled_18",
    "Waveshare ESP32-S3-Touch-AMOLED-1.8 (480x480) running the agentgauge firmware",
    agentgauge_setup,
    agentgauge_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
