#include "doctest.h"
#include "cli.h"
#include <vector>
#include <string>
#include <initializer_list>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

static int run_cli(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    static std::vector<std::string> storage;
    storage.clear();
    for (auto* a : args) storage.emplace_back(a);
    for (auto& s : storage) argv.push_back(const_cast<char*>(s.c_str()));
    return esprite_main((int)argv.size(), argv.data());
}

static bool file_exists(const char* p) { struct stat st; return stat(p, &st) == 0; }

// Like run_cli, but also captures what the command wrote to stderr (the error
// envelope), so tests can assert the error *kind*, not just the exit code.
static int run_cli_err(std::initializer_list<const char*> args, std::string* err) {
    fflush(stderr);
    int saved = dup(fileno(stderr));
    FILE* tmp = tmpfile();
    REQUIRE(tmp != nullptr);
    dup2(fileno(tmp), fileno(stderr));
    int rc = run_cli(args);
    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    rewind(tmp);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    fclose(tmp);
    err->assign(buf, n);
    return rc;
}

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
