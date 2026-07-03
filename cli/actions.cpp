#include "actions.h"
#include "runtime.h"
#include "backend.h"
#include "sim_input.h"
#include "sim_ble.h"
#include "lvgl_snapshot.h"
#include "Arduino.h"
#include "WiFi.h"
#include "Print.h"
#include <strings.h>

// True when injection must route through the qemu guest agent instead of the
// in-process input bus (the booted target runs in a child QEMU process).
static bool qemu_agent_active() {
    const SimTarget* t = sim_active_target();
    return t && t->backend == BACKEND_QEMU;
}

// Matches serial_settle()'s native-steps branch in cli.cpp for the "serial
// expect" command; the scenario runner has no qemu backend to consider, so
// there is only the one (native) case here.
static const int SERIAL_SETTLE_STEPS = 60;

const BoardDesc* active_board() {
    const SimTarget* t = sim_active_target();
    return t ? t->board : nullptr;
}

bool board_has_action(SimInputAction act) {
    const BoardDesc* b = active_board();
    if (!b) return false;
    for (int i = 0; i < b->button_count; ++i)
        if (b->buttons[i].action == act) return true;
    return false;
}

// A board button whose label matches `which` case-insensitively, or null.
static const SimButton* find_button_by_label(const std::string& which) {
    const BoardDesc* b = active_board();
    if (!b) return nullptr;
    for (int i = 0; i < b->button_count; ++i)
        if (strcasecmp(b->buttons[i].label, which.c_str()) == 0) return &b->buttons[i];
    return nullptr;
}

ActionError apply_button(const std::string& which) {
    // The PWR control has three injectable edges, matching the power HAL:
    // press (1), long-press (2), and release (3). Long-press + release drive
    // a firmware's hold-release gesture with `steps` in between.
    int pwr_event = (which == "pwr")         ? 1
                  : (which == "pwr-long")    ? 2
                  : (which == "pwr-release") ? 3 : 0;
    bool semantic = pwr_event || which == "primary" || which == "secondary";

    // A board button addressed by its silk-screen label works on every
    // backend: qemu boards declare ACT_GPIO buttons pulsed via the guest
    // agent, native ones map to whatever action the button declares.
    if (!semantic) {
        const SimButton* btn = find_button_by_label(which);
        if (!btn)
            return {"bad_args", "button takes primary|secondary|pwr|pwr-long|pwr-release or a board button label"};
        if (btn->action == ACT_GPIO) {
            int pressed = btn->active_low ? 0 : 1;
            if (qemu_agent_active()) {
                std::string err;
                if (!sim_backend().agent_pulse(btn->gpio, pressed, 120, &err))
                    return {"agent_failed", err};
                return {};
            }
            sim_gpio_set(btn->gpio, pressed);  sim_run_steps(5);
            sim_gpio_set(btn->gpio, !pressed); sim_run_steps(3);
            return {};
        }
        // Native semantic actions addressed by label fall through to the
        // same injection the semantic names use.
        pwr_event = (btn->action == ACT_PWR) ? 1 : 0;
        if (pwr_event) { sim_input().pwr_events.push_back(1); sim_run_steps(5); return {}; }
        int idx = (btn->action == ACT_SECONDARY) ? 1 : 0;
        sim_input().button[idx] = true;  sim_run_steps(5);
        sim_input().button[idx] = false; sim_run_steps(3);
        return {};
    }

    SimInputAction act = pwr_event              ? ACT_PWR
                       : (which == "secondary") ? ACT_SECONDARY
                                                : ACT_PRIMARY;
    if (!board_has_action(act))
        return {"unsupported", "this board has no '" + which + "' button"};
    if (pwr_event) { sim_input().pwr_events.push_back(pwr_event); sim_run_steps(5); }
    else {
        int idx = (which == "secondary") ? 1 : 0;
        sim_input().button[idx] = true;  sim_run_steps(5);
        sim_input().button[idx] = false; sim_run_steps(3);
    }
    return {};
}

ActionError apply_battery(int pct, const bool* charging, const bool* vbus) {
    if (pct < 0 || pct > 100)
        return {"bad_args", "battery needs a percentage 0-100"};
    if (!active_board() || !active_board()->has_battery)
        return {"unsupported", "this board has no battery"};
    sim_input().battery_pct = pct;
    if (charging) sim_input().charging = *charging;
    if (vbus)     sim_input().vbus = *vbus;
    sim_run_steps(5);
    return {};
}

ActionError apply_rotate(int quadrant) {
    if (quadrant < 0 || quadrant > 3)
        return {"bad_args", "rotate needs a quadrant 0-3"};
    if (!active_board() || !active_board()->has_rotation)
        return {"unsupported", "this board does not support rotation"};
    sim_input().quadrant = quadrant;
    sim_run_steps(5);
    return {};
}

ActionError apply_motion() {
    if (!active_board() || !active_board()->has_imu)
        return {"unsupported", "this board has no IMU"};
    sim_input().motion_nudge = true;
    sim_run_steps(5);
    return {};
}

ActionError apply_serial_expect(const char* text, const char* absent) {
    if (text && *text && !sim_serial_regex_valid(text))
        return {"bad_args", std::string("invalid regex '") + text + "'"};
    if (absent && *absent && !sim_serial_regex_valid(absent))
        return {"bad_args", std::string("invalid regex '") + absent + "'"};
    sim_run_steps(SERIAL_SETTLE_STEPS);
    if (text && *text && !sim_serial_contains(text))
        return {"expect_failed", std::string("serial output does not match '") + text + "'"};
    if (absent && *absent && sim_serial_contains(absent))
        return {"expect_failed", std::string("serial output unexpectedly matches '") + absent + "'"};
    return {};
}

ActionError apply_tap(int x, int y) {
    const BoardDesc* b = active_board();
    if (!b || x < 0 || y < 0 || x >= b->width || y >= b->height)
        return {"bad_args", "tap needs x 0-" + std::to_string(b ? b->width - 1 : 0) +
                            " and y 0-" + std::to_string(b ? b->height - 1 : 0)};
    if (qemu_agent_active()) {
        std::string err;
        if (!sim_backend().agent_touch(true, x, y, &err)) return {"agent_failed", err};
        if (!sim_backend().agent_touch(false, x, y, &err)) return {"agent_failed", err};
        return {};
    }
    sim_input().touch_pressed = true;
    sim_input().touch_x = x;
    sim_input().touch_y = y;
    sim_run_steps(4);
    sim_input().touch_pressed = false;
    sim_run_steps(4);
    return {};
}

ActionError apply_swipe(int x1, int y1, int x2, int y2) {
    const BoardDesc* b = active_board();
    auto oob = [&](int x, int y) { return !b || x < 0 || y < 0 || x >= b->width || y >= b->height; };
    if (oob(x1, y1) || oob(x2, y2))
        return {"bad_args", "swipe needs x1 y1 x2 y2 within 0-" +
                            std::to_string(b ? b->width - 1 : 0) + " / 0-" +
                            std::to_string(b ? b->height - 1 : 0)};
    if (qemu_agent_active()) {
        // Same waypoints as the native gesture; the guest firmware's own
        // input poll rate decides how it reads the moving press.
        std::string err;
        for (int i = 0; i <= 5; i++) {
            int x = x1 + (x2 - x1) * i / 5;
            int y = y1 + (y2 - y1) * i / 5;
            if (!sim_backend().agent_touch(true, x, y, &err)) return {"agent_failed", err};
        }
        if (!sim_backend().agent_touch(false, x2, y2, &err)) return {"agent_failed", err};
        return {};
    }
    // A moving press across >=8 steps/point (>33ms indev read at 5ms/step) with
    // large per-read deltas, so LVGL registers a gesture rather than a tap.
    sim_input().touch_pressed = true;
    for (int i = 0; i <= 5; i++) {
        sim_input().touch_x = x1 + (x2 - x1) * i / 5;
        sim_input().touch_y = y1 + (y2 - y1) * i / 5;
        sim_run_steps(8);
    }
    sim_input().touch_pressed = false;
    sim_run_steps(8);
    return {};
}

ActionError apply_expect(const char* text, const char* absent, bool exact) {
    if (text && *text && !lvgl_has_text(text, exact))
        return {"expect_failed", std::string("expected text '") + text + "' not found"};
    if (absent && *absent && lvgl_has_text(absent, exact))
        return {"expect_failed", std::string("unexpected text '") + absent + "' present"};
    return {};
}

ActionError apply_gpio(int pin, int level) {
    if (pin < 0 || pin > 63 || (level != 0 && level != 1))
        return {"bad_args", "gpio needs a pin 0-63 and a level 0|1"};
    if (qemu_agent_active()) {
        std::string err;
        if (!sim_backend().agent_gpio(pin, level, &err)) return {"agent_failed", err};
        return {};
    }
    sim_gpio_set(pin, level);
    sim_run_steps(5);
    return {};
}

ActionError apply_wifi(const std::string& state) {
    if (state != "up" && state != "down")
        return {"bad_args", "wifi needs 'up' or 'down'"};
    sim_wifi_set_connected(state == "up");
    sim_run_steps(5);
    return {};
}

ActionError ble_guarded() {
    if (!sim_ble_available())
        return {"unsupported", "this firmware has no BLE (boot a buddy target)"};
    return {};
}

ActionError apply_ble_connect(unsigned passkey) {
    if (ActionError e = ble_guarded()) return e;
    sim_ble_host_connect(passkey);
    sim_settle_ms();   // let the firmware notice the link and update its UI
    return {};
}

ActionError apply_ble_pair() {
    if (ActionError e = ble_guarded()) return e;
    if (sim_ble_passkey() == 0)
        return {"bad_args", "no pairing in progress (ble connect --passkey N first)"};
    sim_ble_host_confirm_pairing();
    sim_settle_ms();
    return {};
}

ActionError apply_ble_disconnect() {
    if (ActionError e = ble_guarded()) return e;
    sim_ble_host_disconnect();
    sim_settle_ms();
    return {};
}

ActionError apply_ble_send(const std::string& line) {
    if (ActionError e = ble_guarded()) return e;
    if (sim_ble_link_state() != SIM_BLE_CONNECTED)
        return {"post_failed", "no BLE connection (ble connect first)"};
    sim_ble_host_send(line);
    sim_settle_ms();   // let the firmware drain and render the line
    return {};
}
