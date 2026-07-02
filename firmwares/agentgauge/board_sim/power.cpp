#include "hal/power_hal.h"
#include "hal/board_caps.h"
#include "sim_input.h"

// The sim has no AXP2101 PMU, so there is no separate "is a cell physically
// plugged in" signal beyond the board's static has_battery capability -- the
// injected battery_pct/charging state (via `esprite battery`) always applies
// once a board declares has_battery.

bool power_hal_init(void) { return true; }

bool power_hal_battery_present(void) {
    return board_caps().has_battery;
}

uint8_t power_hal_battery_pct(void) {
    if (!board_caps().has_battery) return 0;
    int pct = sim_input().battery_pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

bool power_hal_is_charging(void) { return sim_input().charging; }
