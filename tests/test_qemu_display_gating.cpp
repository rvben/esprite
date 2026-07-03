#include "doctest.h"
#include "cli_test_helpers.h"
#include <cstdlib>
#include <string>

// Display gating is testable without any QEMU env: rejection happens before a
// child could spawn, and a display-capable target without a configured qemu
// binary fails at boot with backend_unavailable - which proves the gate
// opened (a fixed tier-1 list would have rejected screenshot with
// unsupported before ever reaching boot).

static void clear_qemu_env() {
    unsetenv("ESPRITE_QEMU_BIN");
    unsetenv("ESPRITE_QEMU_RISCV32");
    unsetenv("ESPRITE_QEMU_XTENSA");
    unsetenv("ESPRITE_QEMU_IMAGE");
}

TEST_CASE("screenshot on a display-less qemu target is unsupported") {
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "screenshot", "--target", "qemu_esp32c3", "/tmp/x.png"}, &err) == 7);
    CHECK(err.find("\"kind\":\"unsupported\"") != std::string::npos);
    CHECK(err.find("display") != std::string::npos);
}

TEST_CASE("screenshot on the rgb qemu target passes the gate (fails at boot, not the gate)") {
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "screenshot", "--target", "qemu_esp32c3_rgb", "/tmp/x.png"}, &err) == 2);
    CHECK(err.find("\"kind\":\"backend_unavailable\"") != std::string::npos);
}

TEST_CASE("serial and logs stay allowed on both qemu targets at the gate") {
    clear_qemu_env();
    std::string err;
    // Both die at boot (no env), never at the capability gate.
    CHECK(run_cli_err({"esprite", "logs", "--target", "qemu_esp32c3_rgb"}, &err) == 2);
    CHECK(err.find("\"kind\":\"backend_unavailable\"") != std::string::npos);
}

TEST_CASE("ui and tap remain unsupported on the rgb qemu target") {
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "ui", "--target", "qemu_esp32c3_rgb"}, &err) == 7);
    CHECK(run_cli_err({"esprite", "tap", "10", "10", "--target", "qemu_esp32c3_rgb"}, &err) == 7);
}

TEST_CASE("serve --shot: display-less qemu rejected at the gate, rgb target reaches boot") {
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "serve", "--target", "qemu_esp32c3", "--shot", "/tmp/x.png"}, &err) == 7);
    CHECK(err.find("\"kind\":\"unsupported\"") != std::string::npos);
    err.clear();
    CHECK(run_cli_err({"esprite", "serve", "--target", "qemu_esp32c3_rgb", "--shot", "/tmp/x.png"}, &err) == 2);
    CHECK(err.find("\"kind\":\"backend_unavailable\"") != std::string::npos);
    // --ble-port stays rejected even on the display target.
    err.clear();
    CHECK(run_cli_err({"esprite", "serve", "--target", "qemu_esp32c3_rgb", "--ble-port", "0"}, &err) == 7);
}

TEST_CASE("schema documents capture_failed with exit code 9") {
    std::string out;
    CHECK(run_cli_out({"esprite", "schema"}, &out) == 0);
    CHECK(out.find("\"kind\": \"capture_failed\"") != std::string::npos);
    CHECK(out.find("\"exit_code\": 9") != std::string::npos);
}
