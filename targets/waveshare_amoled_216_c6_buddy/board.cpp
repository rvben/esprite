#include "target.h"

// The Waveshare AMOLED 2.16 C6 board running the Clawdmeter BLE Hardware
// Buddy flavor (clawdmeter_buddy): heartbeat snapshots, permission prompts,
// HID keys, and pairing over the sim's virtual BLE link. Same hardware as
// waveshare_amoled_216_c6; only the transport differs.
void clawdmeter_buddy_setup();
void clawdmeter_buddy_loop();

static const SimButton kButtons[] = {
    {"BOOT", ACT_PRIMARY,   0, ' '},   // approve prompt / HID Space
    {"PWR",  ACT_PWR,       0, 'p'},   // screens/brightness; hold-to-pair
    {"KEY",  ACT_SECONDARY, 0, '\t'},  // deny prompt / HID Shift+Tab
};

static const BoardDesc kBoard = {
    "Waveshare AMOLED 2.16 (C6)", 480, 480,
    false, true, true,           // has_rotation, has_battery, has_imu
    kButtons, 3,
};

static const SimTarget kTarget = {
    "waveshare_amoled_216_c6_buddy",
    "Waveshare ESP32-C6 AMOLED 2.16 running Clawdmeter (BLE Hardware Buddy)",
    clawdmeter_buddy_setup,
    clawdmeter_buddy_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
