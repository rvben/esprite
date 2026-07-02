#include "cli_test_helpers.h"
#include <cstdlib>
#include <string>

// One-shot `ble send` against the buddy firmware: each CLI invocation boots
// fresh (the BLE bus resets on boot), so send must complete the whole round
// trip itself: connect over the bonded fast path, deliver the line, settle,
// and return the device's replies. Own executable: one boot per process.

TEST_CASE("one-shot ble send auto-connects and returns the device's replies") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    std::string out;
    CHECK(run_cli_out({"esprite", "ble", "send", "{\"cmd\":\"status\"}",
                       "--target", "waveshare_amoled_216_c6_buddy"}, &out) == 0);
    unsetenv("ESPRITE_HTTP_PORT");

    // The reply carries the firmware's status response from the same process.
    CHECK(out.find("\"ok\":true") != std::string::npos);
    CHECK(out.find("\"ack\":\"status\"") != std::string::npos);
    CHECK(out.find("\"sec\":true") != std::string::npos);   // bonded fast path
}
