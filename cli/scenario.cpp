#include "scenario.h"
#include "cli_internal.h"
#include "actions.h"
#include "runtime.h"
#include "target.h"
#include "backend.h"
#include "screenshot.h"
#include "framebuffer.h"
#include "sim_input.h"
#include "WebServer.h"
#include <ArduinoJson.h>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

// Upper bound on a POSTed request, kept under the WebServer shim's single-recv
// buffer (16 KB) so a bounded body is received whole. Snapshots are tiny.
static const size_t MAX_REQUEST = 15000;

bool sim_wifi_post(const std::string& path, const std::string& body) {
    // Deliver only to the port the active backend reports for the booted
    // firmware's HTTP server (native: the webserver shim's bound port; qemu:
    // the user-net hostfwd port). A blind connect against a configured port
    // could reach an unrelated host process and make an undelivered post
    // look successful. Refuse instead.
    int port = sim_backend().http_port();
    if (port <= 0) {
        fprintf(stderr, "sim_wifi_post: target has no reachable HTTP server; dropped\n");
        return false;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    // Enlarge the send buffer so a bounded request always fits without blocking
    // (the server is single-threaded and only drains after this returns).
    int sndbuf = 65536;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    bool ok = false;
    if (connect(fd, (sockaddr*)&a, sizeof(a)) == 0) {
        // The X-Agentgauge header satisfies the agentgauge firmware's CSRF guard
        // on /snapshot; other targets ignore it.
        std::string req = "POST " + path + " HTTP/1.1\r\nHost: x\r\n"
            "X-Agentgauge: 1\r\nContent-Length: "
            + std::to_string(body.size()) + "\r\n\r\n" + body;
        // The single-threaded loopback server reads one bounded chunk, so a body
        // beyond that would be received truncated. Reject oversized posts cleanly
        // rather than delivering a partial request that looks valid.
        if (req.size() > MAX_REQUEST) {
            fprintf(stderr, "sim_wifi_post: body too large (%zu > %zu bytes); dropped\n",
                    req.size(), MAX_REQUEST);
        } else {
            // Bounded request fits the send buffer, so the blocking loop completes
            // without ever blocking or short-writing.
            size_t off = 0;
            while (off < req.size()) {
                ssize_t n = ::send(fd, req.data() + off, req.size() - off, 0);
                if (n <= 0) break;
                off += (size_t)n;
            }
            shutdown(fd, SHUT_WR);
            ok = (off == req.size());   // full request delivered
        }
    }
    close(fd);
    sim_run_steps(5);   // let handleClient() + ui_update run
    return ok;
}

int scenario_run(const std::string& path, const std::string& default_target) {
    std::ifstream f(path);
    if (!f) return fail("bad_args", "scenario: cannot open " + path, 2);
    std::stringstream ss; ss << f.rdbuf();

    JsonDocument doc;
    if (deserializeJson(doc, ss.str()))
        return fail("bad_args", "scenario: " + path + " is not valid JSON", 2);

    std::string target = doc["target"] | default_target.c_str();
    const SimTarget* t = sim_target(target);
    if (!t) return fail("unknown_target", "unknown target '" + target + "'", 2);
    // Boot through the backend seam: native wraps sim_boot plus its warmup
    // steps (the historical behavior), qemu boots the child process. Steps
    // that assume the in-process surface gate themselves per backend below.
    sim_backend_select(t);
    std::string boot_err;
    if (!sim_backend().boot(t, &boot_err))
        return fail("backend_unavailable",
                    boot_err.empty() ? ("could not boot '" + target + "'") : boot_err, 2);
    bool is_qemu = t->backend == BACKEND_QEMU;

    // Run every step; remember the first failure and report it as the result,
    // with the same kind/message envelope and exit codes as the other dialects.
    int step_no = 0, failures = 0;
    std::string first_kind, first_msg;
    auto step_failed = [&](const std::string& kind, const std::string& msg) {
        fprintf(stderr, "scenario: step %d failed: %s\n", step_no, msg.c_str());
        if (!failures++) { first_kind = kind; first_msg = msg; }
    };

    for (JsonObject step : doc["steps"].as<JsonArray>()) {
        ++step_no;
        const char* cmd = step["cmd"];
        if (!cmd) continue;
        if (!strcmp(cmd, "snapshot")) {
            std::string body; serializeJson(step["data"], body);
            if (!sim_wifi_post(step["path"] | "/snapshot", body))
                step_failed("post_failed", "snapshot step not delivered (dropped)");
            else
                sim_backend().settle_ms(50);   // let the firmware render the injected data
        } else if (!strcmp(cmd, "screenshot")) {
            if (!is_qemu) sim_settle_ms();   // capture a settled frame, whatever ran before
            // Sync-then-capture, one code path for both backends (native
            // sync is a successful no-op).
            std::string sync_err;
            if (!sim_backend().sync_framebuffer(&sync_err))
                step_failed("capture_failed", "could not capture the display: " + sync_err);
            else
                sim_screenshot_png(step["out"] | "esprite.png");
        } else if (!strcmp(cmd, "steps")) {
            if (is_qemu) step_failed("unsupported", "steps is native-only (loop iterations); use settle");
            else sim_run_steps(step["n"] | 10);
        } else if (!strcmp(cmd, "settle")) {
            // The portable time verb: native pumps loop() wall-bounded, qemu
            // pumps the child's I/O for real wall-clock time.
            int ms = step["ms"] | 0;
            if (ms < 1 || ms > 60000)
                step_failed("bad_args", "settle needs ms in 1..60000");
            else
                sim_backend().settle_ms((unsigned)ms);
        } else if (!strcmp(cmd, "pixel")) {
            // Framebuffer assertion with a retry deadline: eventually
            // consistent by design, since a qemu guest redraws in wall time
            // and each sync also releases its next pending frame. Exact
            // matches suit flat-color fixtures; this is the emulator golden
            // primitive.
            int x = step["x"] | -1, y = step["y"] | -1;
            long want = step["value"] | -1L;
            int timeout_ms = step["timeout_ms"] | 5000;
            if (x < 0 || y < 0 || want < 0 || want > 0xFFFF || timeout_ms < 1 || timeout_ms > 120000) {
                step_failed("bad_args", "pixel needs x, y, value 0..65535, and optional timeout_ms 1..120000");
            } else {
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
                uint16_t got = 0;
                bool matched = false, oob = false;
                for (;;) {
                    std::string sync_err;
                    sim_backend().sync_framebuffer(&sync_err);   // transient failures just retry
                    Framebuffer& fb = sim_framebuffer();
                    if (x >= fb.w() || y >= fb.h()) { oob = true; }
                    else {
                        oob = false;
                        got = fb.pixel(x, y);
                        if (got == (uint16_t)want) { matched = true; break; }
                    }
                    if (std::chrono::steady_clock::now() >= deadline) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (!is_qemu) sim_settle_ms();   // let native frames advance between polls
                }
                char buf[128];
                if (oob) {
                    snprintf(buf, sizeof(buf), "pixel (%d,%d) is outside the %dx%d framebuffer",
                             x, y, sim_framebuffer().w(), sim_framebuffer().h());
                    step_failed("bad_args", buf);
                } else if (!matched) {
                    snprintf(buf, sizeof(buf), "pixel (%d,%d) = 0x%04X, expected 0x%04X",
                             x, y, got, (unsigned)want);
                    step_failed("expect_failed", buf);
                }
            }
        } else if (!strcmp(cmd, "battery")) {
            bool charging = step["charging"] | false;
            if (ActionError e = apply_battery(step["pct"] | 75,
                                              step["charging"].is<bool>() ? &charging : nullptr, nullptr))
                step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "button")) {
            if (ActionError e = apply_button(step["which"] | "primary")) step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "tap")) {
            if (ActionError e = apply_tap(step["x"] | 0, step["y"] | 0)) step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "swipe")) {
            if (ActionError e = apply_swipe(step["x1"] | 0, step["y1"] | 0, step["x2"] | 0, step["y2"] | 0))
                step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "expect")) {
            const char* m = step["match"] | "exact";
            if (is_qemu)
                step_failed("unsupported", "expect reads the native widget tree; use pixel or serial on qemu targets");
            else if (strcmp(m, "exact") != 0 && strcmp(m, "contains") != 0)
                step_failed("bad_args", "expect match must be 'exact' or 'contains'");
            else if (ActionError e = apply_expect(step["text"] | "", step["absent"] | "", !strcmp(m, "exact")))
                step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "rotate")) {
            if (ActionError e = apply_rotate(step["q"] | 0)) step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "motion")) {
            if (ActionError e = apply_motion()) step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "serial")) {
            if (ActionError e = apply_serial_expect(step["expect"] | "", step["absent"] | ""))
                step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "gpio")) {
            if (ActionError e = apply_gpio(step["pin"] | 0, step["level"] | 0)) step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "wifi")) {
            if (ActionError e = apply_wifi(step["state"] | "up")) step_failed(e.kind, e.msg);
        } else if (!strcmp(cmd, "ble")) {
            const char* sub = step["sub"] | "";
            ActionError e;
            if (!strcmp(sub, "connect"))         e = apply_ble_connect(step["passkey"] | 0u);
            else if (!strcmp(sub, "pair"))       e = apply_ble_pair();
            else if (!strcmp(sub, "disconnect")) e = apply_ble_disconnect();
            else if (!strcmp(sub, "send")) {
                std::string body; serializeJson(step["data"], body);
                e = (body.empty() || body == "null")
                        ? ActionError{"bad_args", "ble send step needs a data object"}
                        : apply_ble_send(body);
            } else e = {"bad_args", std::string("unknown ble sub '") + sub + "'"};
            if (e) step_failed(e.kind, e.msg);
        } else {
            step_failed("bad_args", std::string("unknown step cmd '") + cmd + "'");
        }
    }
    if (failures)
        return fail(first_kind.c_str(),
                    std::to_string(failures) + " step(s) failed, first: " + first_msg,
                    kind_exit(first_kind));
    return 0;
}
