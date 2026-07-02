#include "target.h"

// The Waveshare AMOLED 2.16 (ESP32-S3) board. Same 480x480 panel as the C6
// variant but with rotation support (SC7A20 IMU quadrant) and a FWD secondary
// button; it boots the same shared Clawdmeter firmware.
void clawdmeter_setup();
void clawdmeter_loop();

static const SimButton kButtons[] = {
    {"BOOT", ACT_PRIMARY,   0, ' '},   // approve / voice-mode toggle
    {"FWD",  ACT_SECONDARY, 0, '\t'},  // deny / mode toggle
    {"PWR",  ACT_PWR,       0, 'p'},   // cycle screens / animations; hold-to-pair
};

static const BoardDesc kBoard = {
    "Waveshare AMOLED 2.16", 480, 480,
    true, true, true,            // has_rotation, has_battery, has_imu
    kButtons, 3,
};

static const SimTarget kTarget = {
    "waveshare_amoled_216",
    "Waveshare ESP32-S3 AMOLED 2.16 running Clawdmeter",
    clawdmeter_setup,
    clawdmeter_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
