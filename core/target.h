#pragma once
#include <string>

// A simulated board's runtime description, read by the framebuffer sizing, the
// CLI, and capability checks. Target-agnostic.
struct BoardDesc {
    const char* name;
    int  width;
    int  height;
    int  button_count;
    bool has_rotation;
    bool has_battery;
    bool has_imu;
};

// A registered target: an onboarded app the sim can boot. setup()/loop() are the
// app's Arduino entry points (or a thin wrapper). The registry is populated at
// link time by each target's board.cpp via a static initializer.
struct SimTarget {
    const char*     key;          // e.g. "clawdmeter"
    const char*     description;
    void          (*setup)();
    void          (*loop)();
    const BoardDesc* board;
};

void             sim_register_target(const SimTarget* t);
const SimTarget* sim_target(const std::string& key);
int              sim_target_count();
const SimTarget* sim_target_at(int i);
