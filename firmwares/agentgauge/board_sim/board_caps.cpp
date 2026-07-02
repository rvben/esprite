#include "hal/board_caps.h"
#include "target.h"     // SimTarget, BoardDesc, SimButton
#include "runtime.h"    // sim_active_target

// agentgauge has a single board, but board_caps() still derives from the
// active SimTarget's BoardDesc rather than hard-coding values, so the sim
// shim mirrors the real firmware's contract (a small struct + accessor, no
// bare #defines) and stays honest if a second board variant is ever added.
// button_count is the number of held PRIMARY/SECONDARY buttons (PWR is an
// edge event, not a held HAL button), matching hal/board_caps.cpp's "2" on
// real hardware.
BoardCaps board_caps(void) {
    BoardCaps c{};
    const SimTarget* t = sim_active_target();
    const BoardDesc* b = t ? t->board : nullptr;
    if (b) {
        c.width       = (uint16_t)b->width;
        c.height      = (uint16_t)b->height;
        c.has_battery = b->has_battery;
        uint8_t n = 0;
        for (int i = 0; i < b->button_count; ++i)
            if (b->buttons[i].action == ACT_PRIMARY || b->buttons[i].action == ACT_SECONDARY) ++n;
        c.button_count = n;
    }
    return c;
}
