#include "doctest.h"
#include "runtime.h"
#include "screenshot.h"
#include "framebuffer.h"
#include "actions.h"
#include "Print.h"

// The Clawdmeter firmware on the Waveshare AMOLED 2.16 (ESP32-S3). Own
// executable: every LVGL board needs its own process (lv_init runs once).
// This board is the rotation-capable variant, so it also pins the capability
// contrast: rotate is accepted here and rejected on the C6.

TEST_CASE("waveshare_amoled_216 boots the shared firmware with rotation support") {
    sim_serial_clear();
    REQUIRE(sim_boot("waveshare_amoled_216"));
    sim_run_steps(60);

    CHECK(sim_serial_contains("Dashboard ready"));
    CHECK(sim_framebuffer().w() == 480);
    CHECK(sim_framebuffer().h() == 480);

    // Rotation is a real capability of this board: the injection is accepted
    // (the C6 variant rejects the same action with 'unsupported').
    CHECK_FALSE(apply_rotate(1));

    // The HAL sees two held buttons (BOOT + FWD; PWR is an edge event), the
    // same derivation the real board's caps.cpp declares.
    CHECK(board_has_action(ACT_PRIMARY));
    CHECK(board_has_action(ACT_SECONDARY));
    CHECK(board_has_action(ACT_PWR));

    REQUIRE(sim_screenshot_png("/tmp/esprite_itest_amoled216.png"));
}
