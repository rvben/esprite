#include "scenario.h"
#include "runtime.h"
#include "target.h"
#include "screenshot.h"
#include "sim_input.h"
#include "WebServer.h"
#include <ArduinoJson.h>
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

static int http_port() {
    // Prefer the port the target actually bound (authoritative, and the only way
    // to reach an ephemeral bind); fall back to the requested env value.
    int bound = sim_http_bind_status();
    if (bound > 0) return bound;
    const char* p = getenv("ESPRITE_HTTP_PORT");
    return p ? atoi(p) : 8080;
}

bool sim_wifi_post(const std::string& path, const std::string& body) {
    // If the target's own HTTP server failed to bind (its port was already in
    // use), a connect here could reach that unrelated listener and complete the
    // send, making an undelivered post look successful. Refuse in that case so
    // the caller reports failure rather than a false positive.
    if (sim_http_bind_status() == 0) {
        fprintf(stderr, "sim_wifi_post: target HTTP server not bound (port in use); dropped\n");
        return false;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)http_port());
    // Enlarge the send buffer so a bounded request always fits without blocking
    // (the server is single-threaded and only drains after this returns).
    int sndbuf = 65536;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    bool ok = false;
    if (connect(fd, (sockaddr*)&a, sizeof(a)) == 0) {
        // The X-Clawdmeter header satisfies the Clawdmeter firmware's CSRF guard
        // on /snapshot; other targets ignore it.
        std::string req = "POST " + path + " HTTP/1.1\r\nHost: x\r\n"
            "X-Clawdmeter: 1\r\nContent-Length: "
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
    if (!f) { fprintf(stderr, "scenario: cannot open %s\n", path.c_str()); return 2; }
    std::stringstream ss; ss << f.rdbuf();

    JsonDocument doc;
    if (deserializeJson(doc, ss.str())) { fprintf(stderr, "scenario: bad JSON\n"); return 2; }

    std::string target = doc["target"] | default_target.c_str();
    if (!sim_boot(target)) { fprintf(stderr, "scenario: unknown target '%s'\n", target.c_str()); return 2; }
    sim_run_steps(60);

    int failures = 0;
    for (JsonObject step : doc["steps"].as<JsonArray>()) {
        const char* cmd = step["cmd"];
        if (!cmd) continue;
        if (!strcmp(cmd, "snapshot")) {
            std::string body; serializeJson(step["data"], body);
            if (!sim_wifi_post(step["path"] | "/snapshot", body)) {
                fprintf(stderr, "scenario: snapshot step not delivered (dropped)\n");
                ++failures;
            } else {
                sim_settle_ms();   // let the firmware render the injected data
            }
        } else if (!strcmp(cmd, "screenshot")) {
            sim_settle_ms();       // capture a settled frame, whatever ran before
            sim_screenshot_png(step["out"] | "esprite.png");
        } else if (!strcmp(cmd, "steps")) {
            sim_run_steps(step["n"] | 10);
        } else if (!strcmp(cmd, "battery")) {
            sim_input().battery_pct = step["pct"] | 75;
            sim_input().charging    = step["charging"] | false;
            sim_run_steps(5);
        } else if (!strcmp(cmd, "button")) {
            const char* which = step["which"] | "primary";
            if (!strcmp(which, "pwr")) sim_input().pwr_events.push_back(1);
            else sim_input().button[strcmp(which, "secondary") == 0 ? 1 : 0] = true;
            sim_run_steps(5);
            if (strcmp(which, "pwr") != 0)
                sim_input().button[strcmp(which, "secondary") == 0 ? 1 : 0] = false;
            sim_run_steps(3);
        } else if (!strcmp(cmd, "tap")) {
            sim_input().touch_pressed = true;
            sim_input().touch_x = step["x"] | 0;
            sim_input().touch_y = step["y"] | 0;
            sim_run_steps(4);
            sim_input().touch_pressed = false;
            sim_run_steps(4);
        } else if (!strcmp(cmd, "rotate")) {
            sim_input().quadrant = step["q"] | 0;
            sim_run_steps(5);
        }
    }
    return failures ? 3 : 0;   // non-zero if any snapshot step was dropped
}
