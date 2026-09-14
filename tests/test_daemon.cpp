#include "doctest.h"
#include "cli.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <chrono>
#include <thread>
#include <unistd.h>

// Drive a full `run` session in-process: feed newline-delimited JSON commands,
// return the concatenated replies. Uses the real esprite_daemon entry point.
static std::string run_daemon(const std::string& input) {
    FILE* in = fmemopen((void*)input.data(), input.size(), "r");
    char* buf = nullptr;
    size_t len = 0;
    FILE* out = open_memstream(&buf, &len);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    esprite_daemon(in, out);
    fclose(in);
    fclose(out);
    std::string reply(buf, len);
    free(buf);
    return reply;
}

// Same as run_daemon, but supplies a default boot target for lines that omit one.
static std::string run_daemon_default(const std::string& input, const char* default_target) {
    FILE* in = fmemopen((void*)input.data(), input.size(), "r");
    char* buf = nullptr;
    size_t len = 0;
    FILE* out = open_memstream(&buf, &len);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    esprite_daemon(in, out, default_target);
    fclose(in);
    fclose(out);
    std::string reply(buf, len);
    free(buf);
    return reply;
}

// Count newline-terminated replies.
static int reply_count(const std::string& out) {
    int n = 0;
    for (char c : out) if (c == '\n') ++n;
    return n;
}

TEST_CASE("run session: a signal while idle on a real fd ends the session promptly") {
    // The session's steady state blocks waiting for the next command line on
    // a pipe that never delivers one. One SIGTERM - whether it lands while
    // blocked (EINTR) or in the check-then-block gap (self-pipe wake-up) -
    // must end the session promptly; hanging until the next input line is
    // the failure this guards against. The safety writer unblocks a hung
    // daemon after 5 s so a regression fails fast instead of wedging ctest.
    int fds[2];
    REQUIRE(pipe(fds) == 0);
    FILE* in = fdopen(fds[0], "r");
    REQUIRE(in != nullptr);
    char* buf = nullptr;
    size_t len = 0;
    FILE* out = open_memstream(&buf, &len);
    REQUIRE(out != nullptr);

    std::thread killer([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        raise(SIGTERM);
    });
    bool done = false;
    std::thread safety([&] {
        for (int i = 0; i < 50 && !done; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!done) { ssize_t ignored = write(fds[1], "\n", 1); (void)ignored; }
    });

    auto start = std::chrono::steady_clock::now();
    esprite_daemon(in, out);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count();
    done = true;
    killer.join();
    safety.join();
    fclose(in);
    close(fds[1]);
    fclose(out);
    free(buf);
    CHECK_MESSAGE(elapsed < 3000, "session ignored the signal for ", elapsed, " ms");
}

TEST_CASE("run session: error replies use the kind/message envelope of the one-shot CLI") {
    // Regression: the session spoke a second, flat error vocabulary
    // ({"error":"bad_json"}) undocumented by the schema.
    std::string out = run_daemon(
        "garbage line\n"
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n"
        "{\"cmd\":\"battery\",\"pct\":50}\n");
    CHECK(out.find("\"kind\":\"bad_args\"") != std::string::npos);      // garbage line
    CHECK(out.find("\"kind\":\"unsupported\"") != std::string::npos);   // cyd has no battery
}

TEST_CASE("run session: an oversized line yields one error reply, not a desync") {
    // Regression: a line beyond the read buffer was consumed as two commands,
    // producing two bad_json replies and desyncing request/reply pairing.
    std::string big = "{\"cmd\":\"snapshot\",\"data\":{\"pad\":\"";
    big.append(140000, 'x');
    big += "\"}}\n";
    std::string out = run_daemon(
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n" + big + "{\"cmd\":\"logs\"}\n");
    CHECK(reply_count(out) == 3);                                  // boot, error, logs
    CHECK(out.find("line too long") != std::string::npos);
}

TEST_CASE("run session: commands before boot reply not_booted") {
    std::string out = run_daemon("{\"cmd\":\"ui\"}\n");
    CHECK(out.find("\"kind\":\"not_booted\"") != std::string::npos);
}

TEST_CASE("run session: a second boot is rejected, not silently corrupting") {
    // Regression: re-booting re-ran the firmware's setup() (for LVGL targets
    // duplicating the whole widget tree; lv_init cannot run twice per process).
    std::string out = run_daemon(
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n"
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n");
    CHECK(out.find("\"kind\":\"already_booted\"") != std::string::npos);
}

TEST_CASE("run session: booting a qemu target installs the qemu backend even when esprite_main never ran") {
    // Regression: esprite_daemon() is a public entry point this test (and
    // others) call directly, bypassing esprite_main's qemu_backend_install()
    // call. Without esprite_daemon also installing it, BACKEND_QEMU is
    // unregistered and sim_backend_select falls back to native
    // (core/backend.cpp), which trivially "succeeds" for qemu_esp32c3 (its
    // setup() is null, so native's sim_boot() does nothing and still returns
    // true) even though no qemu binary or image is configured.
    unsetenv("ESPRITE_QEMU_BIN");
    unsetenv("ESPRITE_QEMU_RISCV32");
    unsetenv("ESPRITE_QEMU_XTENSA");
    unsetenv("ESPRITE_QEMU_IMAGE");
    std::string out = run_daemon("{\"cmd\":\"boot\",\"target\":\"qemu_esp32c3\"}\n");
    CHECK(out.find("\"kind\":\"backend_unavailable\"") != std::string::npos);
    CHECK(out.find("\"ok\":true") == std::string::npos);
}

TEST_CASE("run session: boots the default target when the boot line omits target") {
    // A boot line with no "target" uses the daemon's default (threaded from the
    // front-end's --target), instead of returning unknown_target.
    std::string out = run_daemon_default(
        "{\"cmd\":\"boot\"}\n{\"cmd\":\"quit\"}\n", "cyd");
    CHECK(out.find("\"ok\":true") != std::string::npos);
    CHECK(out.find("unknown_target") == std::string::npos);
}

TEST_CASE("run session: an explicit target still overrides any default") {
    // The default only fills an omitted target; an explicit one wins.
    std::string out = run_daemon_default(
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n{\"cmd\":\"quit\"}\n", "sample_gfx");
    CHECK(out.find("\"ok\":true") != std::string::npos);
}

TEST_CASE("run session: an invalid serial-expect regex is an error reply, not a crash") {
    // Regression: a malformed pattern threw std::regex_error out of the session
    // loop, killing the whole persistent session (and process).
    std::string out = run_daemon(
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n"
        "{\"cmd\":\"serial\",\"sub\":\"expect\",\"regex\":\"(\"}\n"
        "{\"cmd\":\"logs\"}\n");
    CHECK(out.find("\"error\"") != std::string::npos);   // the bad regex is reported
    CHECK(out.find("\"serial\"") != std::string::npos);  // the session survives to answer logs
}

TEST_CASE("run session: serial accepts a complete base64 display frame") {
    std::string frame(40000, 'A');
    std::string out = run_daemon(
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n"
        "{\"cmd\":\"serial\",\"sub\":\"send\",\"text\":\"" + frame + "\"}\n"
        "{\"cmd\":\"logs\"}\n");
    CHECK(reply_count(out) == 3);
    CHECK(out.find("\"error\"") == std::string::npos);
}
