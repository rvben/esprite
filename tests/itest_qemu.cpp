#include "doctest.h"
#include "qemu_process.h"
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>

// Gated end-to-end tests against a real QEMU child (not the CLI backend):
// they drive QemuProcess/QmpClient directly, exactly like test_qemu_process.cpp
// but against real qemu-system-riscv32/-xtensa binaries and fixture flash
// images instead of /bin/cat. Self-skipping when the QEMU env
// (.qemu/env.sh, see the repo root README/Makefile qemu-fetch target) or the
// fixture images (Makefile qemu-fixtures) are not present, so `make test`
// stays green on a checkout that never ran either.

namespace {

// Fixture images live at tests/fixtures/qemu/ (repo root), but ctest's
// working directory is the build directory: `make test`/`make qemu-test` run
// `ctest --test-dir $(BUILD)`, and $(BUILD) sits directly under the repo
// root, so the fixture is one level up from there. Try, in order: an
// explicit override (for callers with a nonstandard layout), the
// build-dir-relative path (the normal ctest flow), and the repo-root-relative
// path (covers running the test binary directly from the repo root).
std::string resolve_fixture(const std::string& name) {
    if (const char* dir = getenv("ESPRITE_QEMU_FIXTURES")) {
        std::string p = std::string(dir) + "/" + name;
        if (access(p.c_str(), R_OK) == 0) return p;
    }
    for (const char* prefix : {"../tests/fixtures/qemu/", "tests/fixtures/qemu/"}) {
        std::string p = std::string(prefix) + name;
        if (access(p.c_str(), R_OK) == 0) return p;
    }
    return "";
}

// True (and fills bin/img) only when both the qemu binary env var and the
// fixture image are actually present; false otherwise so the caller can
// MESSAGE + skip instead of failing a build where the QEMU env was never
// sourced.
bool qemu_env(std::string* bin, std::string* img, const char* bvar, const char* fixture_name) {
    const char* b = getenv(bvar);
    if (!b) return false;
    std::string p = resolve_fixture(fixture_name);
    if (p.empty()) return false;
    *bin = b;
    *img = p;
    return true;
}

// A short, collision-free scratch path for the QMP unix socket: sun_path is
// 104 bytes on macOS, so this stays directly under /tmp rather than any
// longer per-test directory (same rationale as QemuBackend::socket_dir_ in
// backends/qemu/qemu_backend.cpp).
std::string tmp_sock_path() {
    char tmpl[] = "/tmp/esprite_itest_qemu.XXXXXX";
    char* dir = mkdtemp(tmpl);
    REQUIRE(dir != nullptr);
    return std::string(dir) + "/qmp.sock";
}

// Polls QemuProcess::pump() until `marker` shows up in the captured serial
// output or `deadline_ms` elapses. QEMU under host/CI load can be slow to
// boot, hence the generous deadline (see the brief: 30s).
std::string pump_until(QemuProcess& p, const std::string& marker, int deadline_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms);
    for (;;) {
        p.pump();
        if (p.serial_output().find(marker) != std::string::npos) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return p.serial_output();
}

}  // namespace

TEST_CASE("C3 hello_world: two icount runs produce identical serial") {
    std::string bin, img;
    if (!qemu_env(&bin, &img, "ESPRITE_QEMU_RISCV32", "hello_c3.bin")) {
        MESSAGE("skipped: QEMU env/fixtures missing (ESPRITE_QEMU_RISCV32 / hello_c3.bin)");
        return;
    }
    auto run_once = [&]() -> std::string {
        QemuProcess p;
        std::string err;
        QemuSpec s{bin, "esp32c3", img, true, tmp_sock_path()};
        REQUIRE_MESSAGE(p.start(s, &err), err);
        std::string out = pump_until(p, "Restarting now.", 30000);
        p.stop();
        REQUIRE(out.find("Hello world!") != std::string::npos);
        return out.substr(0, out.find("Restarting now."));
    };
    std::string a = run_once();
    std::string b = run_once();
    // Measurement, not a gate: a mismatch is a valid M0 finding to record
    // in Task 8, not a broken build. doctest WARN reports without failing.
    WARN_MESSAGE(a == b, "C3 icount runs differ: determinism verdict is NO");
    // std::string, not a raw const char* ternary: doctest's default
    // toString(T*) prints pointer-typed values (including const char*) as a
    // hex address, not their contents - only string literals of preserved
    // array type (e.g. a bare "..." macro argument) print as text.
    std::string verdict = a == b ? "DETERMINISTIC" : "NON-DETERMINISTIC";
    MESSAGE("determinism verdict (C3, icount): ", verdict);
}

TEST_CASE("ESP32 arduino image boots and ticks") {
    std::string bin, img;
    if (!qemu_env(&bin, &img, "ESPRITE_QEMU_XTENSA", "arduino_esp32.bin")) {
        MESSAGE("skipped: QEMU env/fixtures missing (ESPRITE_QEMU_XTENSA / arduino_esp32.bin)");
        return;
    }
    QemuProcess p;
    std::string err;
    QemuSpec s{bin, "esp32", img, false, tmp_sock_path()};
    REQUIRE_MESSAGE(p.start(s, &err), err);
    std::string out = pump_until(p, "tick 2", 30000);
    p.stop();
    CHECK(out.find("arduino_tick boot") != std::string::npos);
}
