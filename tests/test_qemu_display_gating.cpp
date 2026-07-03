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

TEST_CASE("ui remains unsupported on the rgb qemu target (no in-process widget tree)") {
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "ui", "--target", "qemu_esp32c3_rgb"}, &err) == 7);
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

TEST_CASE("input commands pass the gate on the agent-capable qemu target") {
    clear_qemu_env();
    std::string err;
    // Boot fails (no env), the gate does not: the old behavior was exit 7.
    CHECK(run_cli_err({"esprite", "tap", "10", "10", "--target", "qemu_esp32c3_rgb"}, &err) == 2);
    CHECK(err.find("\"kind\":\"backend_unavailable\"") != std::string::npos);
    err.clear();
    CHECK(run_cli_err({"esprite", "gpio", "9", "0", "--target", "qemu_esp32c3_rgb"}, &err) == 2);
    err.clear();
    CHECK(run_cli_err({"esprite", "swipe", "10", "10", "50", "50", "--target", "qemu_esp32c3_rgb"}, &err) == 2);
    err.clear();
    CHECK(run_cli_err({"esprite", "button", "BOOT", "--target", "qemu_esp32c3_rgb"}, &err) == 2);
}

TEST_CASE("input commands stay unsupported on the agent-less qemu target") {
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "tap", "10", "10", "--target", "qemu_esp32c3"}, &err) == 7);
    CHECK(err.find("agent") != std::string::npos);
    err.clear();
    CHECK(run_cli_err({"esprite", "gpio", "9", "0", "--target", "qemu_esp32c3"}, &err) == 7);
}

TEST_CASE("snapshot passes the gate on the http-capable qemu target only") {
    clear_qemu_env();
    std::string err;
    // http-capable: the gate opens, boot fails on the missing env.
    CHECK(run_cli_err({"esprite", "snapshot", "{\"color\":1}", "--target", "qemu_esp32c3_rgb"}, &err) == 2);
    CHECK(err.find("\"kind\":\"backend_unavailable\"") != std::string::npos);
    // no http capability: rejected before boot, naming what is missing.
    err.clear();
    CHECK(run_cli_err({"esprite", "snapshot", "{\"color\":1}", "--target", "qemu_esp32c3"}, &err) == 7);
    CHECK(err.find("http") != std::string::npos);
}

TEST_CASE("button accepts a board button label on a native target") {
    // cyd declares one button labeled BOOT (ACT_PRIMARY); pressing it by
    // label must work exactly like the semantic name. Case-insensitive.
    std::string err;
    CHECK(run_cli_err({"esprite", "button", "boot", "--target", "cyd"}, &err) == 0);
    CHECK(run_cli_err({"esprite", "button", "NOSUCH", "--target", "cyd"}, &err) == 2);
    CHECK(err.find("\"kind\":\"bad_args\"") != std::string::npos);
}

TEST_CASE("wifi stays gated on qemu targets at the pre-boot gate") {
    // The simulated Wi-Fi link is a native shim; the command gate rejects it
    // for qemu targets before any boot (and the applier double-checks for
    // scenario steps).
    clear_qemu_env();
    std::string err;
    CHECK(run_cli_err({"esprite", "wifi", "down", "--target", "qemu_esp32c3_rgb"}, &err) == 7);
    CHECK(err.find("\"kind\":\"unsupported\"") != std::string::npos);
}

TEST_CASE("schema documents agent_failed with exit code 10") {
    std::string out;
    CHECK(run_cli_out({"esprite", "schema"}, &out) == 0);
    CHECK(out.find("\"kind\": \"agent_failed\"") != std::string::npos);
    CHECK(out.find("\"exit_code\": 10") != std::string::npos);
}
