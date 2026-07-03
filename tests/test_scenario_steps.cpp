#include "doctest.h"
#include "cli_test_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// Scenario steps added for cross-backend runs, exercised on the native
// sample_gfx target (non-LVGL, safe to boot repeatedly in this process):
// `settle` advances time portably, `pixel` asserts a framebuffer value with
// a retry deadline. The passing pixel path needs a known-stable frame and
// lives in the gated qemu tests against the quadrant fixture; here the
// failure semantics are locked in.

static int run_scenario(const char* body, std::string* err) {
    static int n = 0;
    std::string dir = "/tmp/esprite_test_scn_steps";
    if (n == 0) system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    std::string path = dir + "/s" + std::to_string(n++) + ".json";
    FILE* f = fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    fputs(body, f);
    fclose(f);
    // Leak the path string into run_cli's argv lifetime via a static copy.
    static std::string kept;
    kept = path;
    return run_cli_err({"esprite", "scenario", kept.c_str()}, err);
}

TEST_CASE("scenario settle step runs on a native target") {
    std::string err;
    CHECK(run_scenario(R"json({"target":"sample_gfx","steps":[
        {"cmd":"settle","ms":60}
    ]})json", &err) == 0);
}

TEST_CASE("scenario settle step validates its bounds") {
    std::string err;
    CHECK(run_scenario(R"json({"target":"sample_gfx","steps":[
        {"cmd":"settle","ms":0}
    ]})json", &err) == 2);
    CHECK(err.find("bad_args") != std::string::npos);
}

TEST_CASE("scenario pixel step fails with expect_failed on a wrong value") {
    std::string err;
    // 0x0001 is a color the sample_gfx demo never paints at 5,5; the tiny
    // timeout keeps the retry loop short.
    CHECK(run_scenario(R"json({"target":"sample_gfx","steps":[
        {"cmd":"pixel","x":5,"y":5,"value":1,"timeout_ms":150}
    ]})json", &err) == 8);
    CHECK(err.find("expect_failed") != std::string::npos);
}

TEST_CASE("scenario pixel step rejects out-of-range coordinates") {
    std::string err;
    CHECK(run_scenario(R"json({"target":"sample_gfx","steps":[
        {"cmd":"pixel","x":5000,"y":5,"value":0}
    ]})json", &err) == 2);
    CHECK(err.find("bad_args") != std::string::npos);
}
