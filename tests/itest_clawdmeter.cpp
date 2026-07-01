#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "runtime.h"
#include "screenshot.h"
#include "scenario.h"
#include "framebuffer.h"
#include "Print.h"
#include <cstdlib>
#include <cstdint>

// Integration test for the clawdmeter target. Isolated in its own executable so
// LVGL global state is clean (no prior lv_init from unit tests).

static uint32_t fb_hash() {
    const uint16_t* d = sim_framebuffer().data();
    int n = sim_framebuffer().w() * sim_framebuffer().h();
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; }
    return h;
}

TEST_CASE("clawdmeter boots, renders, and the limits data path updates the UI") {
    setenv("CLAWDSIM_HTTP_PORT", "18100", 1);
    sim_serial_clear();

    REQUIRE(sim_boot("clawdmeter"));
    sim_run_steps(60);

    // The firmware prints its ready banner once LVGL and the transport are up.
    CHECK(sim_serial_contains("Dashboard ready"));

    // A real 480x480 screenshot is produced (the C6 cannot do this on hardware).
    REQUIRE(sim_screenshot_png("/tmp/esp32sim_itest_boot.png"));
    uint32_t before = fb_hash();

    // Inject a limits snapshot through the genuine HTTP -> apply_lim path.
    sim_wifi_post("/snapshot",
        "{\"lim\":1,\"s5\":42,\"s5r\":180,\"s7\":10,\"s7r\":6000,\"ctx\":55,\"cost\":1.5,\"model\":\"opus\"}");
    sim_run_steps(10);
    sim_screenshot_png("/tmp/esp32sim_itest_limits.png");

    // The injected data changed what is rendered.
    CHECK(fb_hash() != before);
}
