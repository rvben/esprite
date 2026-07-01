#include "target.h"

// The Waveshare AMOLED 1.8 board. It boots the same shared Clawdmeter firmware
// (clawdmeter_fw) as the 2.16 C6; only the board profile below differs. The
// firmware adapts to the 368x448 panel at runtime via board_caps().
void clawdmeter_setup();
void clawdmeter_loop();

// The Waveshare AMOLED 1.8 has two side buttons (no third).
static const SimButton kButtons[] = {
    {"BOOT", ACT_PRIMARY, 0, ' '},   // approve / voice-mode toggle
    {"PWR",  ACT_PWR,     0, 'p'},   // cycle screens / animations (IO-expander line)
};

static const BoardDesc kBoard = {
    "Waveshare AMOLED 1.8", 368, 448,
    false, true, true,           // has_rotation, has_battery, has_imu
    kButtons, 2,
};

static const SimTarget kTarget = {
    "waveshare_amoled_18",
    "Waveshare ESP32-S3 AMOLED 1.8 running Clawdmeter (368x448 portrait)",
    clawdmeter_setup,
    clawdmeter_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
