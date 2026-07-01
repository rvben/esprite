#include "hal/board_caps.h"

// Values copied verbatim from
// firmware/src/boards/waveshare_amoled_216_c6/{caps.cpp,board.h}.
static const BoardCaps caps = {
    .name         = "Waveshare AMOLED 2.16 (C6)",
    .width        = 480,
    .height       = 480,
    .button_count = 2,        // BOOT primary + KEY secondary (PWR is separate)
    .has_rotation = false,    // C6 has no PSRAM headroom for rotation
    .has_battery  = true,
    .has_imu      = true,
};

const BoardCaps& board_caps(void) { return caps; }
