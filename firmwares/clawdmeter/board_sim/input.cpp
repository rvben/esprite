#include "hal/input_hal.h"
#include "sim_input.h"

void input_hal_init(void) {}

bool input_hal_is_held(InputButton btn) {
    if (btn == INPUT_BTN_PRIMARY)   return sim_input().button[0];
    if (btn == INPUT_BTN_SECONDARY) return sim_input().button[1];
    return false;
}
