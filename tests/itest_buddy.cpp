#include "doctest.h"
#include "cli.h"
#include "Print.h"
#include "runtime.h"
#include "sim_input.h"
#include "sim_ble.h"
#include "lvgl_snapshot.h"
#include "ble_bridge.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

TEST_CASE("time sync turns the title into a live wall clock") {
    // Reconnect (the pair gesture dropped the link) and send the one-shot
    // {"time":[epoch, tz]} the desktop sends on connect. Epoch 45240 is
    // 12:34 wall clock; the firmware replaces the "Usage" title with it.
    sim_ble_host_connect(0);
    sim_settle_ms();
    sim_ble_host_send("{\"time\":[45240,0]}");
    // The clock reaches the UI with the next heartbeat (ui_update runs when a
    // snapshot applies), exactly the desktop's connect sequence.
    sim_ble_host_send("{\"total\":1,\"running\":0}");
    sim_settle_ms(500, 200);
    CHECK(lvgl_snapshot_json().find("12:34") != std::string::npos);
}

TEST_CASE("the BLE bridge shuttles lines between a TCP client and the firmware") {
    // The bridge is what `serve --ble-port` pumps: a localhost socket where a
    // real host process speaks newline-delimited JSON to the virtual device.
    BleBridge* bridge = ble_bridge_open(0);
    REQUIRE(bridge != nullptr);
    int port = ble_bridge_port(bridge);
    REQUIRE(port > 0);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    REQUIRE(connect(fd, (sockaddr*)&a, sizeof(a)) == 0);
    timeval tv{0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const char* req = "{\"cmd\":\"status\"}\n";
    REQUIRE(send(fd, req, strlen(req), 0) == (ssize_t)strlen(req));

    // Pump bridge + firmware together until the device's reply streams back.
    std::string resp;
    char rb[2048];
    for (int i = 0; i < 30 && resp.find('\n') == std::string::npos; ++i) {
        ble_bridge_tick(bridge);
        sim_run_steps(8);
        ble_bridge_tick(bridge);
        ssize_t n = recv(fd, rb, sizeof(rb), 0);
        if (n > 0) resp.append(rb, (size_t)n);
    }
    CHECK(resp.find("\"ack\":\"status\"") != std::string::npos);

    close(fd);
    ble_bridge_tick(bridge);   // notices the EOF and drops the link
    ble_bridge_close(bridge);
}

// Real firmware state, linked straight from idle.cpp.
extern bool idle_is_asleep(void);

TEST_CASE("the device sleeps after the idle timeout and swallows the wake press") {
    // Own the link (earlier cases may have dropped it), then arm a pending
    // prompt (its arrival counts as activity) and let 30 virtual minutes pass
    // on battery power (sleep is disabled on USB).
    sim_ble_host_connect(0);
    sim_settle_ms();
    sim_ble_host_send(
        "{\"prompt\":{\"id\":\"p2\",\"tool\":\"Edit\",\"hint\":\"write file\"}}");
    sim_settle_ms();
    (void)sim_ble_host_drain();   // discard anything sent so far
    CHECK_FALSE(idle_is_asleep());

    sim_input().vbus = false;                 // unplug: sleep becomes possible
    sim_settle_ms(31 * 60 * 1000, 800000);    // 31 virtual minutes
    CHECK(idle_is_asleep());

    // First PRIMARY press only wakes: no permission decision goes out.
    sim_input().button[0] = true;  sim_run_steps(5);
    sim_input().button[0] = false; sim_run_steps(5);
    sim_settle_ms(500, 200);                  // wake fade-in
    CHECK_FALSE(idle_is_asleep());
    for (const std::string& l : sim_ble_host_drain())
        CHECK(l.find("permission") == std::string::npos);

    // The second press acts: the pending prompt is approved.
    sim_input().button[0] = true;  sim_run_steps(5);
    sim_input().button[0] = false; sim_run_steps(5);
    bool sent = false;
    for (const std::string& l : sim_ble_host_drain())
        if (l.find("\"cmd\":\"permission\",\"id\":\"p2\",\"decision\":\"once\"") != std::string::npos)
            sent = true;
    CHECK(sent);
    sim_input().vbus = true;   // restore the bus default for any later cases
}
