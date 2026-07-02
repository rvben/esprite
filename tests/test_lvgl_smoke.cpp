#include "doctest.h"
#include "cli_test_helpers.h"
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

TEST_CASE("ui never leaks a stale LVGL tree, even at a matching resolution") {
    // Defeat case for a resolution-based staleness heuristic: leave an LVGL
    // screen at exactly cyd's 320x240 with a widget on it, then boot cyd (a
    // non-LVGL target). The ui snapshot must be empty: the screen belongs to
    // a previous owner, not to this boot.
    lv_display_t* disp = lvgl_bind_framebuffer(320, 240);
    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "stale-widget");
    lv_refr_now(disp);

    std::string out;
    CHECK(run_cli_out({"esprite", "ui", "--target", "cyd"}, &out) == 0);
    CHECK(out.find("stale-widget") == std::string::npos);
    CHECK(out.find("\"total\":0") != std::string::npos);
}
