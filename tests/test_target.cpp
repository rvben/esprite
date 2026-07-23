#include "doctest.h"
#include "target.h"
#include "runtime.h"
#include "Arduino.h"   // digitalRead

static int g_setup_calls = 0;
static int g_loop_calls = 0;
static void dummy_setup() { g_setup_calls++; }
static void dummy_loop()  { g_loop_calls++; }

static const BoardDesc kDummyBoard = {"Dummy", 240, 240, false, false, false, nullptr, 0};
static const SimTarget kDummy = {"dummy", "test target", dummy_setup, dummy_loop, &kDummyBoard};

TEST_CASE("target registry and runtime pump") {
    sim_register_target(&kDummy);
    REQUIRE(sim_target("dummy") != nullptr);
    CHECK(sim_target("nope") == nullptr);

    g_setup_calls = g_loop_calls = 0;
    REQUIRE(sim_boot("dummy"));
    CHECK(g_setup_calls == 1);
    sim_run_steps(5);
    CHECK(g_loop_calls == 5);
    CHECK(sim_active_target()->board->width == 240);
}

TEST_CASE("booting an unknown target fails") {
    CHECK_FALSE(sim_boot("does-not-exist"));
}

static int g_active_low_boot_read = -1;
static void active_low_setup() { g_active_low_boot_read = digitalRead(9); }
static void active_low_loop()  {}
static const SimButton kActiveLowBtn[] = {
    {"BOOT", ACT_GPIO, 9, 'b', EDGE_RIGHT, 0.5f, /*active_low=*/true},
};
static const BoardDesc kActiveLowBoard = {"AL", 32, 32, false, false, false, kActiveLowBtn, 1};
static const SimTarget kActiveLowTarget = {"test_active_low_boot", "active-low button board",
                                           active_low_setup, active_low_loop, &kActiveLowBoard};

TEST_CASE("boot seeds active-low GPIO buttons to released (high) before setup") {
    // Without seeding, sim_gpio_reset() leaves pin 9 at 0, which for an
    // active-low button reads as pressed. Boot must seed it high (released) so
    // setup()'s digitalRead sees the idle state - true for screenshot too, which
    // never opens a window.
    sim_register_target(&kActiveLowTarget);
    g_active_low_boot_read = -1;
    REQUIRE(sim_boot("test_active_low_boot"));
    CHECK(g_active_low_boot_read == 1);
}

TEST_CASE("SimButton defaults edge and pos when aggregate-initialized with 4 fields") {
    static const SimButton kBtn = {"BOOT", ACT_PRIMARY, 0, 'b'};
    CHECK(kBtn.edge == EDGE_RIGHT);
    CHECK(kBtn.pos == -1.0f);
}

TEST_CASE("waveshare_amoled_18 buttons declare explicit ascending pos on EDGE_RIGHT") {
    const SimTarget* t = sim_target("waveshare_amoled_18");
    if (!t) {
        MESSAGE("skipped: agentgauge firmware not present");
        return;
    }
    REQUIRE(t->board->button_count == 3);
    float last_pos = -1.0f;
    for (int i = 0; i < t->board->button_count; i++) {
        const SimButton& b = t->board->buttons[i];
        CHECK(b.edge == EDGE_RIGHT);
        CHECK(b.pos > last_pos);
        last_pos = b.pos;
    }
}
