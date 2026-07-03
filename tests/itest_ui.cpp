#include "cli_test_helpers.h"
#include <string>

// One-shot `ui` against the real agentgauge firmware: exercises the bounded
// array envelope (offset/limit slicing) over a live widget tree. Own
// executable: one esprite_main invocation = one LVGL boot per process.

TEST_CASE("one-shot ui slices the live widget tree with offset/limit") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    std::string out;
    // Freshly booted agentgauge (Wi-Fi auto-connected, no snapshot posted
    // yet) sits in STALE state: 4 elements on the active screen (e0 title,
    // e1 idle overlay, e2 idle label, e3 status label). offset=1/limit=2
    // exercises both the offset skip and real truncation against that tree.
    CHECK(run_cli_out({"esprite", "ui", "--target", "waveshare_amoled_18",
                       "--offset", "1", "--limit", "2"}, &out) == 0);
    unsetenv("ESPRITE_HTTP_PORT");

    CHECK(out.find("\"offset\":1") != std::string::npos);
    CHECK(out.find("\"count\":2") != std::string::npos);
    CHECK(out.find("\"truncated\":true") != std::string::npos);
    // Slicing must preserve the elements' own refs: the first item after
    // skipping e0 is e1.
    CHECK(out.find("\"items\":[{\"ref\":\"e1\"") != std::string::npos);
    // e3 exists in the tree but is past the limit, so it must not appear.
    CHECK(out.find("\"ref\":\"e3\"") == std::string::npos);
}
