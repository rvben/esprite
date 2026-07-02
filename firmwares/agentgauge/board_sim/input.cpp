#include "hal/input_hal.h"
#include "sim_input.h"

// PRIMARY/SECONDARY map to esprite's ACT_PRIMARY/ACT_SECONDARY held buttons
// (sim_input().button[0]/[1]); PWR is a separate edge-event bus handled by
// the CLI's `button pwr` action and not read here.

void input_hal_init(void) {}

bool input_hal_is_held(InputButton button) {
    if (button == INPUT_BTN_PRIMARY)   return sim_input().button[0];
    if (button == INPUT_BTN_SECONDARY) return sim_input().button[1];
    return false;
}
