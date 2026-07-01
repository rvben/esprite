#include "cli_test_helpers.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

// End-to-end scenario run against the real clawdmeter firmware, through the
// production entry point (esprite_main scenario). Own executable: the scenario
// boots an LVGL target, and lv_init runs once per process.

static std::string slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    REQUIRE(f != nullptr);
    std::string data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    return data;
}

TEST_CASE("a scenario boots, injects data, and captures distinct settled frames") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    const char* dir = "/tmp/esprite_itest_scenario";
    system("rm -rf /tmp/esprite_itest_scenario && mkdir -p /tmp/esprite_itest_scenario");

    // Self-contained scenario with absolute output paths: waiting screen,
    // inject the limits snapshot, limits screen.
    const char* scn = "/tmp/esprite_itest_scenario/scn.json";
    FILE* f = fopen(scn, "w");
    REQUIRE(f != nullptr);
    fputs("{\"target\":\"waveshare_amoled_216_c6\",\"steps\":["
          "{\"cmd\":\"screenshot\",\"out\":\"/tmp/esprite_itest_scenario/01.png\"},"
          "{\"cmd\":\"snapshot\",\"data\":{\"lim\":1,\"s5\":42,\"s5r\":180,"
          "\"s7\":10,\"s7r\":6000,\"ctx\":55,\"cost\":1.5,\"model\":\"opus\"}},"
          "{\"cmd\":\"screenshot\",\"out\":\"/tmp/esprite_itest_scenario/02.png\"}]}", f);
    fclose(f);

    CHECK(run_cli({"esprite", "scenario", scn}) == 0);

    std::string before = slurp("/tmp/esprite_itest_scenario/01.png");
    std::string after  = slurp("/tmp/esprite_itest_scenario/02.png");
    CHECK(before.size() > 1000);   // real PNGs, not error stubs
    CHECK(after.size() > 1000);
    // The injected data changed what the second frame shows. This is the
    // scenario-level guard for the settle regression (a stale second frame
    // reproduced the pre-injection pixels).
    CHECK(before != after);

    unsetenv("ESPRITE_HTTP_PORT");
    system("rm -rf /tmp/esprite_itest_scenario");
}
