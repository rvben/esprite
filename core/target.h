#pragma once
#include <string>

// What a physical control injects into the sim input bus when pressed.
enum SimInputAction {
    ACT_PRIMARY = 0,   // held  -> sim_input().button[0]
    ACT_SECONDARY,     // held  -> sim_input().button[1]
    ACT_PWR,           // edge  -> sim_input().pwr_events (one pulse per press)
    ACT_GPIO,          // held  -> digitalRead(gpio) reads high while pressed
};

// Which physical edge of the device a control sits on, for bezel layout.
enum SimEdge { EDGE_RIGHT = 0, EDGE_LEFT, EDGE_TOP, EDGE_BOTTOM };

// A physical control on the device (a side button, BOOT/PWR/KEY, A/B, ...).
// Targets declare their own set so the simulator window is device-accurate.
struct SimButton {
    const char*    label;   // silk-screen name shown on the bezel, e.g. "BOOT"
    SimInputAction action;
    int            gpio;    // pin for ACT_GPIO; ignored for other actions
    char           key;     // optional keyboard shortcut (0 = none)
    // Defaulted so every existing aggregate initializer in targets/*/board.cpp
    // (which only lists the four fields above) keeps compiling unchanged.
    SimEdge        edge = EDGE_RIGHT;  // which device edge holds the control
    float          pos  = -1.0f;       // 0..1 along that edge; -1 = auto-stack
};

// A simulated board's runtime description, read by framebuffer sizing, the CLI,
// and the live window. Target-agnostic; each target supplies one.
struct BoardDesc {
    const char*      name;
    int              width;
    int              height;
    bool             has_rotation;
    bool             has_battery;
    bool             has_imu;
    const SimButton* buttons;       // physical controls, or nullptr if none
    int              button_count;  // length of buttons[]
};

// Which backend boots and drives a target's firmware. Native runs the firmware
// in-process (sim_boot/sim_run_steps); qemu boots it in a child QEMU process
// (Task 6). Kept here, next to SimTarget, since a target picks its backend.
enum SimBackendKind { BACKEND_NATIVE = 0, BACKEND_QEMU };

// QEMU machine parameters for a BACKEND_QEMU target: the -machine name and the
// architecture qemu-system-<arch> to launch. Set iff backend == BACKEND_QEMU.
struct QemuMachineSpec {
    const char* machine;   // e.g. "esp32c3"
    const char* arch;      // e.g. "riscv32"
};

// A registered target: an onboarded app the sim can boot. setup()/loop() are the
// app's Arduino entry points (or a thin wrapper). The registry is populated at
// link time by each target's board.cpp via a static initializer.
struct SimTarget {
    const char*      key;          // e.g. "agentgauge"
    const char*      description;
    void           (*setup)();
    void           (*loop)();
    const BoardDesc* board;
    // Defaulted so every existing aggregate initializer in targets/*/board.cpp
    // (which only lists the five fields above) keeps compiling unchanged.
    SimBackendKind        backend = BACKEND_NATIVE;
    const QemuMachineSpec* qemu   = nullptr;
};

void             sim_register_target(const SimTarget* t);
const SimTarget* sim_target(const std::string& key);
int              sim_target_count();
const SimTarget* sim_target_at(int i);
