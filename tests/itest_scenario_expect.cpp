#include "cli_test_helpers.h"
#include <cstdio>
#include <cstdlib>

// `expect` assertions in a scenario, end-to-end through esprite_main. Own
// executable: a scenario boots an LVGL target and lv_init runs once per process.

TEST_CASE("a scenario expect passes on real text and fails (exit 8) on a miss") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    system("rm -rf /tmp/esprite_itest_expect && mkdir -p /tmp/esprite_itest_expect");
    const char* scn = "/tmp/esprite_itest_expect/scn.json";
    FILE* f = fopen(scn, "w");
    REQUIRE(f != nullptr);
    // Inject limits, then a passing expect (Limits title) and a failing one. The
    // runner tallies the failure and returns the expect_failed exit code (8).
    fputs("{\"target\":\"waveshare_amoled_18\",\"steps\":["
          "{\"cmd\":\"snapshot\",\"data\":{\"lim\":1,\"s5\":42,\"s5r\":180,\"s7\":10,\"s7r\":6000}},"
          "{\"cmd\":\"steps\",\"n\":40},"
          "{\"cmd\":\"expect\",\"text\":\"Limits\"},"
          "{\"cmd\":\"expect\",\"text\":\"NoSuchLabelXYZ\"}]}", f);
    fclose(f);

    CHECK(run_cli({"esprite", "scenario", scn}) == 8);   // expect_failed exit code

    unsetenv("ESPRITE_HTTP_PORT");
    system("rm -rf /tmp/esprite_itest_expect");
}
