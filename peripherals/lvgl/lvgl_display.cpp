#include "lvgl_display.h"
#include "framebuffer.h"
#include "lvgl.h"
#include <cstdint>

static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    int w = a->x2 - a->x1 + 1;
    int h = a->y2 - a->y1 + 1;
    sim_framebuffer().blit(a->x1, a->y1, w, h, reinterpret_cast<const uint16_t*>(px));
    lv_display_flush_ready(d);
}

lv_display_t* lvgl_bind_framebuffer(int w, int h) {
    sim_framebuffer().init(w, h);

    static bool lv_inited = false;
    if (!lv_inited) { lv_init(); lv_inited = true; }

    // The glue owns one virtual panel: re-binding resizes it and replaces the
    // draw buffers instead of leaking a display and two buffers per call.
    static lv_display_t* disp = nullptr;
    static uint16_t* b1 = nullptr;
    static uint16_t* b2 = nullptr;

    if (!disp) {
        disp = lv_display_create(w, h);
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
        lv_display_set_flush_cb(disp, flush_cb);
    } else {
        lv_display_set_resolution(disp, w, h);
    }

    const size_t buf_px = (size_t)w * 40;
    delete[] b1;
    delete[] b2;
    b1 = new uint16_t[buf_px];
    b2 = new uint16_t[buf_px];
    lv_display_set_buffers(disp, b1, b2, buf_px * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    return disp;
}
