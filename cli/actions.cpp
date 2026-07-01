#include "actions.h"
#include "runtime.h"
#include "sim_input.h"
#include "Arduino.h"

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

ActionError apply_button(const std::string& which) {
    if (which != "primary" && which != "secondary" && which != "pwr")
        return {"bad_args", "button takes primary|secondary|pwr"};
    SimInputAction act = (which == "pwr")       ? ACT_PWR
                       : (which == "secondary") ? ACT_SECONDARY
                                                : ACT_PRIMARY;
    if (!board_has_action(act))
        return {"unsupported", "this board has no '" + which + "' button"};
    if (which == "pwr") { sim_input().pwr_events.push_back(1); sim_run_steps(5); }
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

ActionError apply_tap(int x, int y) {
    const BoardDesc* b = active_board();
    if (!b || x < 0 || y < 0 || x >= b->width || y >= b->height)
        return {"bad_args", "tap needs x 0-" + std::to_string(b ? b->width - 1 : 0) +
                            " and y 0-" + std::to_string(b ? b->height - 1 : 0)};
    sim_input().touch_pressed = true;
    sim_input().touch_x = x;
    sim_input().touch_y = y;
    sim_run_steps(4);
    sim_input().touch_pressed = false;
    sim_run_steps(4);
    return {};
}

ActionError apply_gpio(int pin, int level) {
    if (pin < 0 || pin > 63 || (level != 0 && level != 1))
        return {"bad_args", "gpio needs a pin 0-63 and a level 0|1"};
    sim_gpio_set(pin, level);
    sim_run_steps(5);
    return {};
}
