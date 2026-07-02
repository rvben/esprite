#include "doctest.h"
#include "backend.h"
#include "target.h"

TEST_CASE("native backend boots a target and exposes serial") {
    const SimTarget* t = sim_target("sample_gfx");
    REQUIRE(t != nullptr);
    sim_backend_select(t);
    CHECK(std::string(sim_backend().name()) == "native");
    std::string err;
    CHECK(sim_backend().boot(t, &err));
    CHECK(err.empty());
}

TEST_CASE("SimTarget defaults to the native backend") {
    const SimTarget* t = sim_target("sample_gfx");
    CHECK(t->backend == BACKEND_NATIVE);
    CHECK(t->qemu == nullptr);
}
