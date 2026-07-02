#include "doctest.h"
#include "runtime.h"
#include "screenshot.h"
#include "framebuffer.h"
#include "sim_input.h"
#include "Print.h"

// Generality proofs: standard Arduino_GFX sketches boot and render with no
// app-specific sim code, on boards unlike the Waveshare/agentgauge targets.
// Shares the sim_itests binary (main is in itest_main.cpp).

TEST_CASE("sample_gfx boots and renders with zero app-specific sim code") {
    sim_serial_clear();
    REQUIRE(sim_boot("sample_gfx"));
    sim_run_steps(60);
    CHECK(sim_serial_contains("sample_gfx: ready"));
    REQUIRE(sim_screenshot_png("/tmp/esprite_itest_sample.png"));
    CHECK(sim_framebuffer().pixel(50, 30) == 0xF800);   // red RGB test bar
}

TEST_CASE("cyd paints where you touch (GFX touch bus)") {
    sim_serial_clear();
    REQUIRE(sim_boot("cyd"));
    sim_run_steps(30);
    CHECK(sim_serial_contains("cyd: ready"));
    // Canvas centre starts on the dark background, not the default CYAN brush.
    CHECK(sim_framebuffer().pixel(160, 130) != 0x07FF);
    // Inject a canvas tap; the demo reads the sim touch bus and paints a dot.
    sim_input().touch_pressed = true;
    sim_input().touch_x = 160;
    sim_input().touch_y = 130;
    sim_run_steps(4);
    sim_input().touch_pressed = false;
    CHECK(sim_framebuffer().pixel(160, 130) == 0x07FF);   // painted CYAN
}

TEST_CASE("cyd_tft runs a real TFT_eSPI sketch and toggles a touch button") {
    sim_serial_clear();
    REQUIRE(sim_boot("cyd_tft"));
    sim_run_steps(20);
    CHECK(sim_serial_contains("cyd_tft: ready"));
    // LED button fill starts dark grey, not the on-colour TFT_BLUE (0x001F).
    CHECK(sim_framebuffer().pixel(30, 120) != 0x001F);
    // Tap the LED button; the edge-detected toggle fills it blue.
    sim_input().touch_pressed = true;
    sim_input().touch_x = 85;
    sim_input().touch_y = 100;
    sim_run_steps(4);
    sim_input().touch_pressed = false;
    CHECK(sim_framebuffer().pixel(30, 120) == 0x001F);   // TFT_BLUE
}
