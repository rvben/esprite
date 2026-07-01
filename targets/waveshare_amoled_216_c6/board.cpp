#include "target.h"

// The Waveshare AMOLED 2.16 C6 board. It boots the shared Clawdmeter firmware
// (clawdmeter_fw); this file only describes the hardware. The firmware's entry
// points are renamed at compile time (setup->clawdmeter_setup) so it coexists
// with other firmwares (e.g. sample_gfx) in one binary.
void clawdmeter_setup();
void clawdmeter_loop();

// The three physical side buttons of the Waveshare C6 AMOLED 2.16.
static const SimButton kButtons[] = {
    {"BOOT", ACT_PRIMARY,   0, ' '},   // approve / voice-mode toggle
    {"PWR",  ACT_PWR,       0, 'p'},   // cycle screens / animations
    {"KEY",  ACT_SECONDARY, 0, '\t'},  // deny / mode toggle
};

static const BoardDesc kBoard = {
    "Waveshare AMOLED 2.16 (C6)", 480, 480,
    false, true, true,           // has_rotation, has_battery, has_imu
    kButtons, 3,
};

static const SimTarget kTarget = {
    "waveshare_amoled_216_c6",
    "Waveshare ESP32-C6 AMOLED 2.16 running Clawdmeter",
    clawdmeter_setup,
    clawdmeter_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
