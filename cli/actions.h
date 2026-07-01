#pragma once
#include <string>
#include "target.h"

// Input-injection actions shared by the three CLI dialects (one-shot commands,
// the run session, the scenario runner), so capability gating, validation, and
// pump behavior can never drift between them. Each returns {nullptr-kind} on
// success, or the error kind plus a human message.

struct ActionError {
    const char* kind = nullptr;   // nullptr = success
    std::string msg;
    explicit operator bool() const { return kind != nullptr; }
};

ActionError apply_button(const std::string& which);                      // primary|secondary|pwr
ActionError apply_battery(int pct, const bool* charging, const bool* vbus);
ActionError apply_rotate(int quadrant);
ActionError apply_tap(int x, int y);
ActionError apply_gpio(int pin, int level);

// The active board's hardware description (set once a target is booted), and
// whether it has a physical control with the given action.
const BoardDesc* active_board();
bool board_has_action(SimInputAction act);
