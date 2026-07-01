#pragma once

typedef struct lv_display_t lv_display_t;

// Initialize LVGL (once) and create a display whose flush callback writes into
// the sim framebuffer, sized w x h. Reusable by any LVGL-based target that does
// not set up its own display. Targets that build their own lv_display (like
// Clawdmeter's main.cpp) do not need this.
lv_display_t* lvgl_bind_framebuffer(int w, int h);
