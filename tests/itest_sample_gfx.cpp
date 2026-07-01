#include "doctest.h"
#include "runtime.h"
#include "screenshot.h"
#include "framebuffer.h"
#include "Print.h"

// Generality proof: a standard Arduino_GFX sketch boots and renders with no
// app-specific sim code. Shares the sim_itests binary (main is in
// itest_clawdmeter.cpp).

TEST_CASE("sample_gfx boots and renders with zero app-specific sim code") {
    sim_serial_clear();
    REQUIRE(sim_boot("sample_gfx"));
    sim_run_steps(60);
    CHECK(sim_serial_contains("sample_gfx: ready"));
    REQUIRE(sim_screenshot_png("/tmp/esp32sim_itest_sample.png"));
    CHECK(sim_framebuffer().pixel(50, 30) == 0xF800);   // red RGB test bar
}
