#include "doctest.h"
#include "target.h"
#include "runtime.h"

static int g_setup_calls = 0;
static int g_loop_calls = 0;
static void dummy_setup() { g_setup_calls++; }
static void dummy_loop()  { g_loop_calls++; }

static const BoardDesc kDummyBoard = {"Dummy", 240, 240, 1, false, false, false};
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
