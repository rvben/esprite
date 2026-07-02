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

ActionError apply_button(const std::string& which);   // primary|secondary|pwr|pwr-long|pwr-release
ActionError apply_battery(int pct, const bool* charging, const bool* vbus);
ActionError apply_rotate(int quadrant);
ActionError apply_tap(int x, int y);
ActionError apply_swipe(int x1, int y1, int x2, int y2);   // moving press -> LVGL gesture
ActionError apply_gpio(int pin, int level);

// BLE link actions (the CLI stands in for the Claude desktop app). All reject
// with 'unsupported' when the booted firmware never attached a BLE stack.
ActionError apply_ble_connect(unsigned passkey);   // 0 = bonded fast path
ActionError apply_ble_pair();                      // confirm a pending passkey
ActionError apply_ble_disconnect();
ActionError apply_ble_send(const std::string& line);
ActionError ble_guarded();                         // ok iff BLE is available

// The active board's hardware description (set once a target is booted), and
// whether it has a physical control with the given action.
const BoardDesc* active_board();
bool board_has_action(SimInputAction act);
