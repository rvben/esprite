#include "doctest.h"
#include "lvgl.h"
#include "lvgl_display.h"
#include "framebuffer.h"

TEST_CASE("LVGL renders a solid screen into the framebuffer") {
    lv_display_t* disp = lvgl_bind_framebuffer(480, 480);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_refr_now(disp);
    CHECK(sim_framebuffer().pixel(240, 240) == 0x001F);   // blue in RGB565
}
