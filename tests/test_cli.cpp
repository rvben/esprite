#include "doctest.h"
#include "cli.h"
#include <vector>
#include <string>
#include <initializer_list>
#include <cstdio>
#include <sys/stat.h>

static int run_cli(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    static std::vector<std::string> storage;
    storage.clear();
    for (auto* a : args) storage.emplace_back(a);
    for (auto& s : storage) argv.push_back(const_cast<char*>(s.c_str()));
    return esprite_main((int)argv.size(), argv.data());
}

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
