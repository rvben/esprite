#include "hal/power_hal.h"
#include "hal/board_caps.h"
#include "sim_input.h"

static bool drain(int kind) {
    auto& q = sim_input().pwr_events;
    for (auto it = q.begin(); it != q.end(); ++it) {
        if (*it == kind) { q.erase(it); return true; }
    }
    return false;
}

void power_hal_init(void) {}
void power_hal_tick(void) {}

int  power_hal_battery_pct(void) {
    return board_caps().has_battery ? sim_input().battery_pct : -1;
}
bool power_hal_is_charging(void) { return sim_input().charging; }
bool power_hal_is_vbus_in(void)  { return sim_input().vbus; }

bool power_hal_pwr_pressed(void)      { return drain(1); }
bool power_hal_pwr_long_pressed(void) { return drain(2); }
bool power_hal_pwr_released(void)     { return drain(3); }
