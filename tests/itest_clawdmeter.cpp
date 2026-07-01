#include "doctest.h"
#include "runtime.h"
#include "screenshot.h"
#include "scenario.h"
#include "framebuffer.h"
#include "Print.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>

// Integration test for the Clawdmeter firmware on the Waveshare AMOLED 2.16 C6
// board. Isolated in its own executable so LVGL global state is clean (no prior
// lv_init from unit tests).

int sim_http_bind_status();   // shims/net/WebServer.h: >0 = the port the target bound

static uint32_t fb_hash() {
    const uint16_t* d = sim_framebuffer().data();
    int n = sim_framebuffer().w() * sim_framebuffer().h();
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; }
    return h;
}

// Send a raw HTTP request to the booted target's webserver and return the
// response status code. The sim webserver is single-threaded, so the sequence
// is: send the whole request, half-close (so the server sees EOF and processes
// it), pump loop() a few times to run handleClient(), then read the buffered
// response. Returns -1 if the request could not be delivered.
static int http_status(int port, const std::string& req) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    if (connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { close(fd); return -1; }
    size_t off = 0;
    while (off < req.size()) {
        ssize_t n = ::send(fd, req.data() + off, req.size() - off, 0);
        if (n <= 0) break;
        off += (size_t)n;
    }
    shutdown(fd, SHUT_WR);
    sim_run_steps(8);   // pump handleClient() so the server processes + responds
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    int code = -1;
    sscanf(buf, "HTTP/1.1 %d", &code);
    return code;
}

// Build a minimal multipart/form-data body carrying one firmware file part.
static std::string multipart(const std::string& boundary, const std::string& file) {
    return "--" + boundary + "\r\n"
           "Content-Disposition: form-data; name=\"firmware\"; filename=\"fw.bin\"\r\n"
           "Content-Type: application/octet-stream\r\n\r\n" + file + "\r\n"
           "--" + boundary + "--\r\n";
}

TEST_CASE("waveshare_amoled_216_c6 boots, renders, serves snapshots, and gates OTA /update") {
    // Bind an ephemeral port so a leftover listener can never collide with a
    // fixed one; sim_wifi_post and the OTA sockets both use the bound port.
    setenv("ESPRITE_HTTP_PORT", "0", 1);
    sim_serial_clear();

    REQUIRE(sim_boot("waveshare_amoled_216_c6"));
    sim_run_steps(60);

    // The firmware prints its ready banner once LVGL and the transport are up.
    CHECK(sim_serial_contains("Dashboard ready"));

    // A real 480x480 screenshot is produced (the C6 cannot do this on hardware).
    REQUIRE(sim_screenshot_png("/tmp/esprite_itest_boot.png"));
    uint32_t before = fb_hash();

    // Inject a limits snapshot through the genuine HTTP -> apply_lim path.
    // Settle by time, not step count: the render only lands after LVGL's next
    // refresh cycle. sim_itests_daemon asserts the bars are actually painted.
    sim_wifi_post("/snapshot",
        "{\"lim\":1,\"s5\":42,\"s5r\":180,\"s7\":10,\"s7r\":6000,\"ctx\":55,\"cost\":1.5,\"model\":\"opus\"}");
    sim_settle_ms();
    sim_screenshot_png("/tmp/esprite_itest_limits.png");

    // The injected data changed what is rendered.
    CHECK(fb_hash() != before);

    // OTA /update: the firmware compiled from source registers this route. Drive
    // the three faithful outcomes end to end. Ephemeral bind gives the real port.
    int port = sim_http_bind_status();
    REQUIRE(port > 0);

    const std::string boundary = "SIMBOUND";
    const std::string body = multipart(boundary, "FAKEFIRMWAREBYTES-0123456789");
    const std::string mp_headers =
        "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";

    // 1) Unauthenticated upload (no X-Clawdmeter): rejected before touching flash.
    CHECK(http_status(port,
        "POST /update HTTP/1.1\r\nHost: x\r\n" + mp_headers + body) == 403);

    // 2) Authorized but no firmware file: nothing to flash.
    CHECK(http_status(port,
        "POST /update HTTP/1.1\r\nHost: x\r\nX-Clawdmeter: 1\r\nContent-Length: 0\r\n\r\n") == 400);

    // 3) Authorized with a firmware file: reaches the success/reboot path. Run
    //    last, as the firmware requests a (simulated) restart afterwards.
    CHECK(http_status(port,
        "POST /update HTTP/1.1\r\nHost: x\r\nX-Clawdmeter: 1\r\n" + mp_headers + body) == 200);
}
