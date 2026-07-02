#include "cli_test_helpers.h"
#include "Arduino.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <csignal>

static bool file_exists(const char* p) { struct stat st; return stat(p, &st) == 0; }

TEST_CASE("schema, help, and list-targets succeed without a booted target") {
    CHECK(run_cli({"esprite", "schema"}) == 0);
    CHECK(run_cli({"esprite", "--help"}) == 0);
    CHECK(run_cli({"esprite", "list-targets"}) == 0);
}

TEST_CASE("no arguments is a usage error") {
    CHECK(run_cli({"esprite"}) == 1);
}

TEST_CASE("commands reject capabilities the active board lacks") {
    // sample_gfx has no battery, no rotation, and no buttons, so these must fail
    // with the 'unsupported' exit code (7) rather than silently succeeding.
    CHECK(run_cli({"esprite", "battery", "50", "--target", "sample_gfx"}) == 7);
    CHECK(run_cli({"esprite", "rotate", "1", "--target", "sample_gfx"}) == 7);
    CHECK(run_cli({"esprite", "button", "primary", "--target", "sample_gfx"}) == 7);
}

TEST_CASE("every documented error kind fires with its schema exit code") {
    std::string err;

    CHECK(run_cli_err({"esprite", "screenshot", "x.png", "--target", "nope"}, &err) == 2);
    CHECK(err.find("\"kind\":\"unknown_target\"") != std::string::npos);

    // Five targets registered: booting without --target cannot resolve.
    CHECK(run_cli_err({"esprite", "logs"}, &err) == 2);
    CHECK(err.find("\"kind\":\"no_target\"") != std::string::npos);

    CHECK(run_cli_err({"esprite", "tap", "--ref", "e99", "--target", "cyd"}, &err) == 4);
    CHECK(err.find("\"kind\":\"ref_not_found\"") != std::string::npos);

    CHECK(run_cli_err({"esprite", "tap", "--ref", "e1", "5", "5", "--target", "cyd"}, &err) == 5);
    CHECK(err.find("\"kind\":\"conflict\"") != std::string::npos);

    // sample_gfx runs no HTTP server, so a snapshot has nowhere to land.
    CHECK(run_cli_err({"esprite", "snapshot", "{\"a\":1}", "--target", "sample_gfx"}, &err) == 6);
    CHECK(err.find("\"kind\":\"post_failed\"") != std::string::npos);

    CHECK(run_cli_err({"esprite", "battery", "50", "--target", "sample_gfx"}, &err) == 7);
    CHECK(err.find("\"kind\":\"unsupported\"") != std::string::npos);
}

TEST_CASE("ble commands reject targets whose firmware has no BLE") {
    // cyd runs a plain GFX sketch: no BLE stack ever attaches, so the whole
    // ble surface is 'unsupported' (exit 7), like battery on a battery-less
    // board. A bogus subcommand is a usage error.
    std::string err;
    CHECK(run_cli_err({"esprite", "ble", "connect", "--target", "cyd"}, &err) == 7);
    CHECK(err.find("\"kind\":\"unsupported\"") != std::string::npos);
    CHECK(run_cli({"esprite", "ble", "send", "{\"a\":1}", "--target", "cyd"}) == 7);
    CHECK(run_cli({"esprite", "ble", "recv", "--target", "cyd"}) == 7);
    CHECK(run_cli({"esprite", "ble", "bogus", "--target", "cyd"}) == 2);
}

TEST_CASE("pwr long-press and release are injectable like a short press") {
    // sample_gfx has no PWR control, so all three reject with unsupported;
    // previously the long/release forms did not exist at all (bad_args).
    CHECK(run_cli({"esprite", "button", "pwr-long", "--target", "sample_gfx"}) == 7);
    CHECK(run_cli({"esprite", "button", "pwr-release", "--target", "sample_gfx"}) == 7);
}

TEST_CASE("serve shuts down cleanly on SIGTERM") {
    // Regression: serve's loop had no signal handling, so Ctrl-C/SIGTERM
    // killed the process (signal death) instead of an orderly exit 0.
    if (access("./esprite", X_OK) != 0) return;   // run from the build dir (ctest)
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        setenv("ESPRITE_HTTP_PORT", "0", 1);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl("./esprite", "esprite", "serve", "--target", "cyd", (char*)nullptr);
        _exit(127);
    }
    usleep(400000);   // let it boot and enter the pump loop
    kill(pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 40 && waitpid(pid, &status, WNOHANG) == 0; ++i) usleep(100000);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("gpio injection is readable through the Arduino API after the command") {
    CHECK(run_cli({"esprite", "gpio", "5", "1", "--target", "cyd"}) == 0);
    CHECK(digitalRead(5) == 1);
    CHECK(run_cli({"esprite", "gpio", "5", "0", "--target", "cyd"}) == 0);
    CHECK(digitalRead(5) == 0);
}

TEST_CASE("ui on a non-LVGL target returns an empty bounded envelope") {
    std::string out;
    CHECK(run_cli_out({"esprite", "ui", "--target", "cyd"}, &out) == 0);
    CHECK(out.find("\"items\":[]") != std::string::npos);
    CHECK(out.find("\"total\":0") != std::string::npos);
    CHECK(out.find("\"truncated\":false") != std::string::npos);
}

TEST_CASE("--version prints the tool name and version") {
    std::string out;
    CHECK(run_cli_out({"esprite", "--version"}, &out) == 0);
    CHECK(out.find("\"name\":\"esprite\"") != std::string::npos);
    CHECK(out.find("\"version\"") != std::string::npos);

    // It is documented as a global flag, so it wins in any position.
    CHECK(run_cli_out({"esprite", "list-targets", "--version"}, &out) == 0);
    CHECK(out.find("\"name\":\"esprite\"") != std::string::npos);
    CHECK(out.find("\"items\"") == std::string::npos);
}

TEST_CASE("the schema version matches the binary's --version") {
    std::string schema, version;
    REQUIRE(run_cli_out({"esprite", "schema"}, &schema) == 0);
    REQUIRE(run_cli_out({"esprite", "--version"}, &version) == 0);
    // Extract "version":"X" from --version output and require the schema to
    // carry the same string: one source of truth, no drift.
    size_t k = version.find("\"version\":\"");
    REQUIRE(k != std::string::npos);
    size_t start = k + 11, end = version.find('"', start);
    std::string v = version.substr(start, end - start);
    CHECK(schema.find("\"version\": \"" + v + "\"") != std::string::npos);
}

TEST_CASE("an unknown command is bad_args, not a misleading target error") {
    // Regression: a typo'd command without --target fell through target
    // resolution first and reported no_target ("no --target and more than one
    // target registered") instead of naming the real problem.
    std::string err;
    CHECK(run_cli_err({"esprite", "nonsense-cmd"}, &err) == 2);
    CHECK(err.find("\"kind\":\"bad_args\"") != std::string::npos);
    CHECK(err.find("unknown command") != std::string::npos);
}

TEST_CASE("unknown --options are rejected, never absorbed as positionals") {
    // Regression: a typo'd flag fell through to positional parsing, so
    // `battery --charge 50` silently became pct=atoi("--charge")=0, ok:true.
    CHECK(run_cli({"esprite", "battery", "--charge", "50", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "screenshot", "--oops", "x.png", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "tap", "--refx", "e3", "--target", "sample_gfx"}) == 2);
}

TEST_CASE("a value option missing its value is bad_args") {
    CHECK(run_cli({"esprite", "screenshot", "out.png", "--target"}) == 2);
}

TEST_CASE("numeric arguments are validated and range-checked") {
    // Garbage and out-of-range numbers previously became atoi()=0 -> ok:true.
    CHECK(run_cli({"esprite", "battery", "abc", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "battery", "150", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "battery", "--target", "sample_gfx"}) == 2);        // missing pct
    CHECK(run_cli({"esprite", "rotate", "9", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "gpio", "99", "1", "--target", "sample_gfx"}) == 2); // pin > 63
    CHECK(run_cli({"esprite", "gpio", "5", "2", "--target", "sample_gfx"}) == 2);  // level not 0/1
    CHECK(run_cli({"esprite", "gpio", "5", "--target", "sample_gfx"}) == 2);       // missing level
    CHECK(run_cli({"esprite", "screenshot", "s.png", "--steps", "abc", "--target", "sample_gfx"}) == 2);
}

TEST_CASE("tap coordinates outside the board are bad_args") {
    // sample_gfx is 320x240; a tap at (10000,10000) previously returned ok:true.
    CHECK(run_cli({"esprite", "tap", "10000", "10000", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "tap", "abc", "10", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "tap", "10", "--target", "sample_gfx"}) == 2);       // missing y
    CHECK(run_cli({"esprite", "tap", "5", "5", "--target", "sample_gfx"}) == 0);   // in-bounds still works
}

TEST_CASE("snapshot requires a valid JSON body") {
    // 'not-json' was previously posted anyway and reported {"ok":true}.
    CHECK(run_cli({"esprite", "snapshot", "not-json", "--target", "sample_gfx"}) == 2);
    CHECK(run_cli({"esprite", "snapshot", "--target", "sample_gfx"}) == 2);        // missing body
}

TEST_CASE("a bare -- ends option parsing for free-form positionals") {
    // serial send text that itself starts with dashes must be expressible.
    CHECK(run_cli({"esprite", "serial", "send", "--target", "sample_gfx", "--", "--weird"}) == 0);
    CHECK(run_cli({"esprite", "serial", "send", "--weird", "--target", "sample_gfx"}) == 2);
}

static void write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    REQUIRE(f != nullptr);
    fputs(content, f);
    fclose(f);
}

TEST_CASE("scenario errors use the same envelope and exit codes as the CLI") {
    std::string err;

    // Regression: a missing file printed bare text and never emitted the
    // structured envelope every other command path produces.
    CHECK(run_cli_err({"esprite", "scenario", "/nonexistent-scenario.json"}, &err) == 2);
    CHECK(err.find("\"kind\":\"bad_args\"") != std::string::npos);

    // Regression: an undeliverable snapshot step exited 3, while the schema
    // documents post_failed as exit 6 (and the one-shot command honors that).
    write_file("/tmp/esprite_scn_post.json",
               "{\"target\":\"cyd\",\"steps\":[{\"cmd\":\"snapshot\",\"data\":{\"a\":1}}]}");
    CHECK(run_cli_err({"esprite", "scenario", "/tmp/esprite_scn_post.json"}, &err) == 6);
    CHECK(err.find("\"kind\":\"post_failed\"") != std::string::npos);
    std::remove("/tmp/esprite_scn_post.json");
}

TEST_CASE("scenario steps respect board capabilities like the other dialects") {
    // Regression: a scenario could set battery or rotation on a board with
    // neither and exit 0; the one-shot and run dialects reject with exit 7.
    std::string err;
    write_file("/tmp/esprite_scn_cap.json",
               "{\"target\":\"cyd\",\"steps\":[{\"cmd\":\"battery\",\"pct\":50}]}");
    CHECK(run_cli_err({"esprite", "scenario", "/tmp/esprite_scn_cap.json"}, &err) == 7);
    CHECK(err.find("\"kind\":\"unsupported\"") != std::string::npos);
    std::remove("/tmp/esprite_scn_cap.json");
}

TEST_CASE("serial expect with an invalid regex is bad_args, not a crash") {
    // Regression: a malformed pattern threw std::regex_error out of esprite_main
    // and aborted the whole process instead of reporting a structured error.
    CHECK(run_cli({"esprite", "serial", "expect", "(", "--target", "sample_gfx"}) == 2);
}

TEST_CASE("--json is a global flag, never consumed as a positional") {
    // Regression: `screenshot --json` used to treat --json as the output-path
    // positional, writing a file literally named "--json" instead of the default.
    // --json must select JSON output and be skipped by positional parsing.
    std::remove("--json");
    std::remove("esprite.png");
    int rc = run_cli({"esprite", "screenshot", "--target", "sample_gfx", "--json"});
    CHECK(rc == 0);
    CHECK_FALSE(file_exists("--json"));   // must NOT create a file named after the flag
    CHECK(file_exists("esprite.png"));    // default output path used instead
    std::remove("--json");
    std::remove("esprite.png");
}
