#include "doctest.h"
#include "target.h"
#include "runtime.h"

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

TEST_CASE("SimButton defaults edge and pos when aggregate-initialized with 4 fields") {
    static const SimButton kBtn = {"BOOT", ACT_PRIMARY, 0, 'b'};
    CHECK(kBtn.edge == EDGE_RIGHT);
    CHECK(kBtn.pos == -1.0f);
}

TEST_CASE("agentgauge buttons declare explicit ascending pos on EDGE_RIGHT") {
    const SimTarget* t = sim_target("agentgauge");
    REQUIRE(t != nullptr);
    REQUIRE(t->board->button_count == 3);
    float last_pos = -1.0f;
    for (int i = 0; i < t->board->button_count; i++) {
        const SimButton& b = t->board->buttons[i];
        CHECK(b.edge == EDGE_RIGHT);
        CHECK(b.pos > last_pos);
        last_pos = b.pos;
    }
}
