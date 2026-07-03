#include "doctest.h"
#include "backend.h"
#include "target.h"
#include "qemu_backend.h"
#include "qemu_board.h"

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

TEST_CASE("qemu_esp32c3 declares BACKEND_QEMU with an esp32c3/riscv32 machine spec") {
    // Qemu targets are data-driven (targets/qemu/*.json): registered by
    // qemu_boards_install, which esprite_main normally calls. Direct
    // sim_target lookups need it too; idempotent across cases.
    std::string board_err;
    REQUIRE_MESSAGE(qemu_boards_install(&board_err), board_err);
    const SimTarget* t = sim_target("qemu_esp32c3");
    REQUIRE(t != nullptr);
    CHECK(t->backend == BACKEND_QEMU);
    REQUIRE(t->qemu != nullptr);
    CHECK(std::string(t->qemu->machine) == "esp32c3");
    CHECK(std::string(t->qemu->arch) == "riscv32");
}

TEST_CASE("qemu_backend_install routes BACKEND_QEMU targets to the qemu backend") {
    // Selecting the target is enough to prove the registration hook wired the
    // real qemu backend in (core/backend.h's sim_backend_register) instead of
    // silently falling back to native for an unregistered kind. Never calls
    // boot(), so this passes without a real qemu-system-<arch> binary on CI.
    qemu_backend_install();
    std::string board_err;
    REQUIRE_MESSAGE(qemu_boards_install(&board_err), board_err);
    const SimTarget* t = sim_target("qemu_esp32c3");
    REQUIRE(t != nullptr);
    sim_backend_select(t);
    CHECK(std::string(sim_backend().name()) == "qemu");

    // Restore the global backend selector to native: sim_tests runs every
    // TEST_CASE in one process, and leaving this selected at qemu would make
    // sim_backend() in later cases silently resolve to a singleton this case
    // never boots or shuts down.
    const SimTarget* native = sim_target("sample_gfx");
    REQUIRE(native != nullptr);
    sim_backend_select(native);
    CHECK(std::string(sim_backend().name()) == "native");
}
