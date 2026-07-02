#include "hal/display_hal.h"
#include "hal/board_caps.h"
#include "framebuffer.h"

// agentgauge display HAL bound to the sim framebuffer. main.cpp creates its
// own lv_display and its flush_cb calls display_hal_blit directly (rather than
// going through the shared lvgl_glue framebuffer binding), so this shim just
// needs to size the framebuffer once and copy pixels on every blit.

bool display_hal_init(void) {
    BoardCaps c = board_caps();
    sim_framebuffer().init(c.width, c.height);
    return true;
}

void display_hal_blit(int16_t x1, int16_t y1, int16_t w, int16_t h, uint16_t* pixels) {
    sim_framebuffer().blit(x1, y1, w, h, pixels);
}

void display_hal_set_brightness(uint8_t) {}
