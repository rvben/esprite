#include "doctest.h"
#include "qemu_process.h"
#include "screendump.h"
#include "framebuffer.h"
#include "cli_test_helpers.h"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

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

// Returns the last `n` chars of `s` (or all of it if shorter), for compact
// failure diagnostics: shows whether the guest hung early (empty/boot-banner
// only) or was merely slow (ticks present but late) without dumping the
// full, potentially large, serial capture into the failure message.
std::string tail(const std::string& s, size_t n = 300) {
    return s.size() <= n ? s : s.substr(s.size() - n);
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
        // The actual boots-and-orderly-shutdown gate: without this, a
        // regression that hangs partway through boot (e.g. right after
        // printing "Hello world!" but before completing the countdown)
        // would still pass, since pump_until returns the partial output on
        // a timeout rather than failing outright.
        // Comma-separated arguments (not a `+`-concatenated string): doctest's
        // MESSAGE/INFO macros chain arguments via operator*/operator,, which
        // bind tighter than `+`, so "text" + out would parse as
        // (mb * "text") + out and fail to compile.
        REQUIRE_MESSAGE(out.find("Restarting now.") != std::string::npos,
                         "never reached the restart marker within 30s; last 300 chars: ", tail(out));
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

// The esp32 (xtensa) machine runs wall-clock, unlike the c3/icount case
// above: guest progress tracks real elapsed time rather than a deterministic
// instruction count, so it is directly sensitive to host scheduling load.
// Measured flake under load was about 1 in 6 runs at a 30s deadline; 90s
// gives enough headroom to absorb that without masking a genuine hang (which
// still shows up as "arduino_tick boot" present but no "tick 2" in the
// captured tail).
constexpr int kXtensaPumpDeadlineMs = 90000;

namespace {

// Channel-dominance helpers: assert quadrant color identity without pinning
// the device's exact 565->888->565 round-trip values.
bool redish(uint16_t c)   { return (c >> 11) > 24 && ((c >> 5) & 0x3F) < 16 && (c & 0x1F) < 8; }
bool greenish(uint16_t c) { return (c >> 11) < 8 && ((c >> 5) & 0x3F) > 48 && (c & 0x1F) < 8; }
bool blueish(uint16_t c)  { return (c >> 11) < 8 && ((c >> 5) & 0x3F) < 16 && (c & 0x1F) > 24; }
bool whitish(uint16_t c)  { return (c >> 11) > 24 && ((c >> 5) & 0x3F) > 48 && (c & 0x1F) > 24; }

}  // namespace

TEST_CASE("C3 rgb fixture: screendump captures the quadrant pattern") {
    std::string bin, img;
    if (!qemu_env(&bin, &img, "ESPRITE_QEMU_RISCV32", "rgb_c3.bin")) {
        MESSAGE("skipped: QEMU env/fixtures missing (ESPRITE_QEMU_RISCV32 / rgb_c3.bin)");
        return;
    }
    QemuProcess p;
    std::string err;
    std::string sock = tmp_sock_path();
    QemuSpec s{bin, "esp32c3", img, true, sock};
    REQUIRE_MESSAGE(p.start(s, &err), err);
    // The fixture prints the drawing marker, then blocks in the driver's
    // frame-consumed busy-wait until a screendump pumps the console.
    std::string out = pump_until(p, "rgb_demo drawing", 60000);
    REQUIRE_MESSAGE(out.find("rgb_demo drawing") != std::string::npos,
                     "fixture never started drawing; last 300 chars: ", tail(out));
    std::string dump = sock.substr(0, sock.rfind('/')) + "/screen.ppm";
    std::string result, qerr;
    REQUIRE_MESSAGE(p.qmp.execute("screendump", "{\"filename\":\"" + dump + "\"}", &result, &qerr), qerr);
    FILE* f = fopen(dump.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::string ppm;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) ppm.append(buf, n);
    fclose(f);
    unlink(dump.c_str());
    // The dump also released the guest: the post-draw state marker follows.
    out = pump_until(p, "rgb_demo state touch=0 inv=0", 15000);
    p.stop();
    CHECK_MESSAGE(out.find("rgb_demo state touch=0 inv=0") != std::string::npos,
                   "screendump did not release the guest; last 300 chars: ", tail(out));
    int w = 0, h = 0;
    std::vector<uint16_t> px;
    REQUIRE_MESSAGE(ppm_decode_rgb565(ppm, &w, &h, &px, &err), err);
    CHECK(w == 320);
    CHECK(h == 240);
    REQUIRE((int)px.size() == w * h);
    CHECK_MESSAGE(redish(px[60 * w + 80]),     "TL not red: ",   px[60 * w + 80]);
    CHECK_MESSAGE(greenish(px[60 * w + 240]),  "TR not green: ", px[60 * w + 240]);
    CHECK_MESSAGE(blueish(px[180 * w + 80]),   "BL not blue: ",  px[180 * w + 80]);
    CHECK_MESSAGE(whitish(px[180 * w + 240]),  "BR not white: ", px[180 * w + 240]);
}

TEST_CASE("CLI run session: screenshot on qemu_esp32c3_rgb lands guest pixels in the framebuffer") {
    std::string bin, img;
    if (!qemu_env(&bin, &img, "ESPRITE_QEMU_RISCV32", "rgb_c3.bin")) {
        MESSAGE("skipped: QEMU env/fixtures missing (ESPRITE_QEMU_RISCV32 / rgb_c3.bin)");
        return;
    }
    setenv("ESPRITE_QEMU_IMAGE", img.c_str(), 1);
    // One daemon session, one qemu boot; screenshot repeatedly until the
    // fixture has drawn (the guest runs in real time, so the exact wall-clock
    // moment its first frame lands is not fixed). Each screenshot syncs via
    // screendump, which is also what releases the guest's blocked draw. The
    // daemon path exercises boot -> gate -> sync -> encode end to end.
    system("rm -rf /tmp/esprite_itest_rgb && mkdir -p /tmp/esprite_itest_rgb");
    std::string cmds = "{\"cmd\":\"boot\",\"target\":\"qemu_esp32c3_rgb\"}\n";
    for (int i = 0; i < 40; i++)
        cmds += "{\"cmd\":\"screenshot\",\"out\":\"/tmp/esprite_itest_rgb/s" + std::to_string(i) + ".png\"}\n";
    cmds += "{\"cmd\":\"quit\"}\n";
    std::string out = run_daemon(cmds);
    unsetenv("ESPRITE_QEMU_IMAGE");
    CHECK(out.find("\"ok\":true") != std::string::npos);
    CHECK_MESSAGE(out.find("\"w\":320") != std::string::npos, "no 320-wide capture in: ", tail(out));
    // The session's final framebuffer state must carry the pattern; check it
    // directly rather than re-decoding a PNG.
    Framebuffer& fb = sim_framebuffer();
    REQUIRE(fb.w() == 320);
    REQUIRE(fb.h() == 240);
    CHECK_MESSAGE(redish(fb.pixel(80, 60)),    "TL not red: ",   fb.pixel(80, 60));
    CHECK_MESSAGE(greenish(fb.pixel(240, 60)), "TR not green: ", fb.pixel(240, 60));
    CHECK_MESSAGE(blueish(fb.pixel(80, 180)),  "BL not blue: ",  fb.pixel(80, 180));
    CHECK_MESSAGE(whitish(fb.pixel(240, 180)), "BR not white: ", fb.pixel(240, 180));
}

#include "agent_link.h"
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

// Reads a whole file; empty string when missing (asserted by callers).
std::string slurp_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The committed scenario next to the repo's other scenarios, resolved from
// the ctest build-dir cwd exactly like the fixtures.
std::string resolve_repo_file(const std::string& rel) {
    for (const char* prefix : {"../", ""}) {
        std::string p = std::string(prefix) + rel;
        if (access(p.c_str(), R_OK) == 0) return p;
    }
    return "";
}

}  // namespace

TEST_CASE("agent transport: ping, gpio, and protocol errors over the second chardev") {
    std::string bin, img;
    if (!qemu_env(&bin, &img, "ESPRITE_QEMU_RISCV32", "rgb_c3.bin")) {
        MESSAGE("skipped: QEMU env/fixtures missing (ESPRITE_QEMU_RISCV32 / rgb_c3.bin)");
        return;
    }
    QemuProcess p;
    std::string err;
    std::string sock = tmp_sock_path();
    std::string agent_sock = sock.substr(0, sock.rfind('/')) + "/agent.sock";
    QemuSpec s{bin, "esp32c3", img, true, sock};
    s.agent_socket = agent_sock;
    REQUIRE_MESSAGE(p.start(s, &err), err);
    std::string out = pump_until(p, "rgb_demo drawing", 60000);
    REQUIRE_MESSAGE(out.find("rgb_demo drawing") != std::string::npos,
                     "fixture never started; last 300 chars: ", tail(out));
    AgentLink link;
    REQUIRE_MESSAGE(link.connect_unix(agent_sock, 2000, &err), err);
    std::string reply;
    CHECK_MESSAGE(link.request("ping", &reply, &err), err);
    CHECK(reply == "ok esprite-agent v1");
    CHECK(link.request("gpio 9 0", &reply, &err));
    CHECK_FALSE(link.request("bogus 1 2", &reply, &err));
    CHECK(err.find("unknown command") != std::string::npos);
    link.close();
    p.stop();
}

TEST_CASE("CLI scenario: tap and button drive the guest, goldens match byte-exact") {
    std::string bin, img;
    if (!qemu_env(&bin, &img, "ESPRITE_QEMU_RISCV32", "rgb_c3.bin")) {
        MESSAGE("skipped: QEMU env/fixtures missing (ESPRITE_QEMU_RISCV32 / rgb_c3.bin)");
        return;
    }
    std::string scn_src = resolve_repo_file("scenarios/qemu_esp32c3_rgb.json");
    REQUIRE_MESSAGE(!scn_src.empty(), "scenarios/qemu_esp32c3_rgb.json not found");
    std::string dir = "/tmp/esprite_itest_qemu_scn";
    system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    // Same steps as the committed scenario, with the relative screenshot
    // outputs redirected to the scratch dir (ctest's cwd is the build dir).
    std::string body = slurp_file(scn_src);
    for (const char* name : {"01-quadrants.png", "02-inverted.png"}) {
        size_t at = body.find(name);
        REQUIRE(at != std::string::npos);
        body.replace(at, strlen(name), dir + "/" + name);
    }
    std::string scn = dir + "/scn.json";
    FILE* f = fopen(scn.c_str(), "w");
    REQUIRE(f != nullptr);
    fputs(body.c_str(), f);
    fclose(f);

    setenv("ESPRITE_QEMU_IMAGE", img.c_str(), 1);
    static std::string kept;   // outlives run_cli's argv
    kept = scn;
    int rc = run_cli({"esprite", "scenario", kept.c_str()});
    unsetenv("ESPRITE_QEMU_IMAGE");
    REQUIRE(rc == 0);

    // Byte-exact goldens: the fixture's states are flat colors and the PNG
    // encoder is deterministic, so any pixel drift is a real regression.
    for (const char* name : {"01-quadrants.png", "02-inverted.png"}) {
        std::string got = slurp_file(dir + "/" + name);
        std::string want = slurp_file(resolve_repo_file(std::string("tests/goldens/qemu/") + name));
        REQUIRE_MESSAGE(!got.empty(), "scenario did not write ", name);
        REQUIRE_MESSAGE(!want.empty(), "golden missing for ", name, " (run make qemu-goldens)");
        CHECK_MESSAGE(got == want, name, " differs from its golden");
    }
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
    std::string out = pump_until(p, "tick 2", kXtensaPumpDeadlineMs);
    p.stop();
    // Require the actual marker pump_until was told to wait for, not just
    // the earlier boot banner: pump_until returns the partial output on a
    // timeout rather than failing, so asserting only on "arduino_tick boot"
    // would let a hang between boot and the second tick pass silently.
    REQUIRE_MESSAGE(out.find("tick 2") != std::string::npos,
                     "never reached 'tick 2' within 90s; last 300 chars: ", tail(out));
    CHECK(out.find("arduino_tick boot") != std::string::npos);
}
