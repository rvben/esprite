#include "hal/display_hal.h"
#include "hal/board_caps.h"
#include "framebuffer.h"

// Clawdmeter display HAL bound to the sim framebuffer. The C6 profile is a plain
// blit (no rotation), matching boards/waveshare_amoled_216_c6/display.cpp.

void display_hal_init(void) {
    sim_framebuffer().init(board_caps().width, board_caps().height);
}
void display_hal_begin(void) {}
void display_hal_set_brightness(uint8_t) {}
void display_hal_fill_screen(uint16_t color) { sim_framebuffer().fill(color); }

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    sim_framebuffer().blit(x, y, w, h, pixels);
}

void display_hal_tick(void) {}

// CO5300 requires even-aligned flush regions.
void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    *x1 = *x1 & ~1;
    *y1 = *y1 & ~1;
    *x2 = *x2 | 1;
    *y2 = *y2 | 1;
}
