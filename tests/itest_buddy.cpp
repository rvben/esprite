#include "doctest.h"
#include "cli.h"
#include "Print.h"
#include "runtime.h"
#include "sim_input.h"
#include "sim_ble.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// End-to-end integration of the BLE Hardware Buddy flavor: the real
// app_buddy.cpp + protocol.cpp compiled from source, driven through the run
// session exactly as an agent would. One executable, one boot (lv_init runs
// once per process); the whole flow runs in one session.

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

TEST_CASE("the buddy firmware pairs, applies heartbeats, answers prompts, and sends HID") {
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    sim_serial_clear();

    std::string out = run_daemon(
        // Boot and pair: connect with a passkey, confirm like the desktop.
        "{\"cmd\":\"boot\",\"target\":\"waveshare_amoled_216_c6_buddy\"}\n"
        "{\"cmd\":\"ble\",\"sub\":\"connect\",\"passkey\":123456}\n"
        "{\"cmd\":\"ui\"}\n"                          // passkey overlay visible
        "{\"cmd\":\"ble\",\"sub\":\"pair\"}\n"
        // Heartbeat snapshot -> the real protocol.cpp parser -> the model.
        "{\"cmd\":\"ble\",\"sub\":\"send\",\"data\":{\"total\":3,\"running\":1,"
        "\"waiting\":1,\"tokens\":123,\"tokens_today\":4567,\"msg\":\"3 sessions\"}}\n"
        // Status poll: the device answers over the link.
        "{\"cmd\":\"ble\",\"sub\":\"send\",\"data\":{\"cmd\":\"status\"}}\n"
        "{\"cmd\":\"ble\",\"sub\":\"recv\"}\n"
        // Permission prompt -> approve with the PRIMARY button -> decision out.
        "{\"cmd\":\"ble\",\"sub\":\"send\",\"data\":{\"prompt\":{\"id\":\"p1\","
        "\"tool\":\"Bash\",\"hint\":\"ls -la\"}}}\n"
        "{\"cmd\":\"ui\"}\n"                          // prompt overlay visible
        "{\"cmd\":\"button\",\"which\":\"primary\"}\n"
        "{\"cmd\":\"ble\",\"sub\":\"recv\"}\n"
        // No prompt pending: PRIMARY becomes the HID Space key.
        "{\"cmd\":\"ble\",\"sub\":\"send\",\"data\":{\"total\":3,\"running\":1}}\n"
        "{\"cmd\":\"button\",\"which\":\"primary\"}\n"
        "{\"cmd\":\"ble\",\"sub\":\"hid\"}\n"
        "{\"cmd\":\"quit\"}\n");
    unsetenv("ESPRITE_HTTP_PORT");

    // Pairing: the 6-digit key was displayed on the passkey overlay.
    CHECK(out.find("123456") != std::string::npos);

    // Status response: real protocol.cpp output over the virtual link, secure
    // link and the sim battery default visible in the payload.
    CHECK(out.find("\"ack\":\"status\"") != std::string::npos);
    CHECK(out.find("\"sec\":true") != std::string::npos);
    CHECK(out.find("\"pct\":75") != std::string::npos);

    // The prompt reached the UI (tool + hint rendered as widgets).
    CHECK(out.find("Bash") != std::string::npos);
    CHECK(out.find("ls -la") != std::string::npos);

    // Approving sent the permission decision for the prompt's id.
    CHECK(out.find("\"cmd\":\"permission\",\"id\":\"p1\",\"decision\":\"once\"")
          != std::string::npos);

    // With no prompt pending, PRIMARY produced a HID Space press + release.
    CHECK(out.find("\"press\":true,\"key\":44") != std::string::npos);
    CHECK(out.find("\"press\":false") != std::string::npos);
}

TEST_CASE("the hold-to-pair gesture clears bonds and re-advertises") {
    // Continues under the same live boot (a run session admits one boot, so
    // this drives the input bus directly): long-press PWR, hold past the 3 s
    // arm window in virtual time, release -> the firmware clears bonds.
    sim_input().pwr_events.push_back(2);   // long-press edge
    sim_run_steps(5);
    sim_settle_ms(2000, 500);              // hold ~2 s more of virtual time
    sim_input().pwr_events.push_back(3);   // release
    sim_run_steps(10);
    CHECK(sim_serial_contains("Pair: armed"));
    CHECK(sim_serial_contains("clearing bonds"));
    CHECK(sim_ble_link_state() == SIM_BLE_ADVERTISING);
}
