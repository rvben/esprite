#include "doctest.h"
#include "runtime.h"
#include "screenshot.h"
#include "framebuffer.h"
#include "Print.h"

// The Clawdmeter firmware on the Waveshare AMOLED 1.8 (368x448). This lives in a
// SEPARATE executable from the 2.16 C6 integration test: both boards run the
// LVGL firmware and lv_init runs once per process, so each LVGL board needs its
// own binary. It is the runtime board-selection proof - one compiled firmware,
// a different panel chosen at boot via board_caps().

TEST_CASE("waveshare_amoled_18 boots the shared firmware at 368x448") {
    sim_serial_clear();
    REQUIRE(sim_boot("waveshare_amoled_18"));
    sim_run_steps(60);

    CHECK(sim_serial_contains("Dashboard ready"));

    // board_caps() reported the 1.8 panel at runtime, so the framebuffer sized
    // itself to 368x448 - not the C6's 480x480 - from the very same firmware.
    CHECK(sim_framebuffer().w() == 368);
    CHECK(sim_framebuffer().h() == 448);

    REQUIRE(sim_screenshot_png("/tmp/esprite_itest_amoled18.png"));
}
