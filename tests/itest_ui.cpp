#include "cli_test_helpers.h"
#include <string>

// One-shot `ui` against the real clawdmeter firmware: exercises the bounded
// array envelope (offset/limit slicing) over a live widget tree. Own
// executable: one esprite_main invocation = one LVGL boot per process.

TEST_CASE("one-shot ui slices the live widget tree with offset/limit") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    std::string out;
    CHECK(run_cli_out({"esprite", "ui", "--target", "waveshare_amoled_216_c6",
                       "--offset", "1", "--limit", "3"}, &out) == 0);
    unsetenv("ESPRITE_HTTP_PORT");

    CHECK(out.find("\"offset\":1") != std::string::npos);
    CHECK(out.find("\"count\":3") != std::string::npos);
    CHECK(out.find("\"truncated\":true") != std::string::npos);
    // Slicing must preserve the elements' own refs: the first item after
    // skipping e0 is e1 (the title label).
    CHECK(out.find("\"items\":[{\"ref\":\"e1\"") != std::string::npos);
    // Widgets past the limit are not in the payload.
    CHECK(out.find("\"ref\":\"e5\"") == std::string::npos);
}
