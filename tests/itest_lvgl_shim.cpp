#include "doctest.h"
#include "lvgl.h"
#include "TFT_eSPI.h"
#include "framebuffer.h"
#include <vector>

// One LVGL display whose flush_cb pushes through the TFT_eSPI shim, exactly as
// an Arduino LVGL app wires it. Proves the driver-boundary approach: pixels
// reach sim_framebuffer with no esprite-specific display code.
static TFT_eSPI g_tft(64, 64);

static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    // Exactly how an Arduino LVGL app wires TFT_eSPI, swap=true and all. The sim
    // ignores the hardware swap and writes native RGB565, so colors stay correct.
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    g_tft.startWrite();
    g_tft.setAddrWindow(area->x1, area->y1, w, h);
    g_tft.pushColors((uint16_t*)px, (uint32_t)(w * h), true);
    g_tft.endWrite();
    lv_display_flush_ready(d);
}

TEST_CASE("an LVGL app flushing through TFT_eSPI renders into the framebuffer") {
    g_tft.init();
    lv_init();
    lv_display_t* disp = lv_display_create(64, 64);
    static std::vector<uint8_t> buf(64 * 64 * 2);
    lv_display_set_buffers(disp, buf.data(), nullptr, (uint32_t)buf.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_refr_now(disp);   // force one full flush now

    // The screen was painted red; LVGL renders native RGB565 red = 0xF800, and
    // the shim writes it native, so the framebuffer holds 0xF800.
    CHECK(sim_framebuffer().w() == 64);
    CHECK(sim_framebuffer().pixel(0, 0) == 0xF800);
    CHECK(sim_framebuffer().pixel(63, 63) == 0xF800);
}
