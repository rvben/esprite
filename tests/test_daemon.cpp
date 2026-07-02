#include "doctest.h"
#include "cli.h"
#include <cstdio>
#include <cstdlib>
#include <string>

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

// Count newline-terminated replies.
static int reply_count(const std::string& out) {
    int n = 0;
    for (char c : out) if (c == '\n') ++n;
    return n;
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
    big.append(20000, 'x');
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
