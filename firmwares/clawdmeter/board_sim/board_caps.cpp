#include "hal/board_caps.h"
#include "target.h"     // SimTarget, BoardDesc, SimButton
#include "runtime.h"    // sim_active_target

// The board profile is selected at runtime, so one compiled firmware serves
// every board variant (480x480 C6, 368x448 1.8, ...). board_caps() reflects
// whichever target is currently booted, derived from its BoardDesc. The HAL
// button_count is the number of held primary/secondary buttons (PWR is an edge
// event, not a held HAL button), matching each board's caps.cpp on hardware.
const BoardCaps& board_caps(void) {
    static BoardCaps c{};
    const SimTarget* t = sim_active_target();
    const BoardDesc* b = t ? t->board : nullptr;
    if (b) {
        c.name         = b->name;
        c.width        = (int16_t)b->width;
        c.height       = (int16_t)b->height;
        c.has_rotation = b->has_rotation;
        c.has_battery  = b->has_battery;
        c.has_imu      = b->has_imu;
        uint8_t n = 0;
        for (int i = 0; i < b->button_count; ++i)
            if (b->buttons[i].action == ACT_PRIMARY || b->buttons[i].action == ACT_SECONDARY) ++n;
        c.button_count = n;
    }
    return c;
}
