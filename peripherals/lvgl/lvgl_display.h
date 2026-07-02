#pragma once
#include "lvgl.h"

// Initialize LVGL (once) and create a display whose flush callback writes into
// the sim framebuffer, sized w x h. Reusable by any LVGL-based target that does
// not set up its own display. Firmwares that build their own lv_display (like
// agentgauge's main.cpp, which flushes through display_hal_blit) do not need
// this.
lv_display_t* lvgl_bind_framebuffer(int w, int h);
