#include "scenario.h"
#include "runtime.h"
#include "target.h"
#include "screenshot.h"
#include "sim_input.h"
#include <ArduinoJson.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static int http_port() {
    const char* p = getenv("CLAWDSIM_HTTP_PORT");
    return p ? atoi(p) : 8080;
}

void sim_wifi_post(const std::string& path, const std::string& body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)http_port());
    if (connect(fd, (sockaddr*)&a, sizeof(a)) == 0) {
        // Build in a std::string so an arbitrarily large body can never overrun a
        // fixed request buffer. The X-Clawdmeter header satisfies the Clawdmeter
        // firmware's CSRF guard on /snapshot; other targets ignore it.
        std::string req = "POST " + path + " HTTP/1.1\r\nHost: x\r\n"
            "X-Clawdmeter: 1\r\nContent-Length: "
            + std::to_string(body.size()) + "\r\n\r\n" + body;
        ::send(fd, req.data(), req.size(), 0);
        shutdown(fd, SHUT_WR);
    }
    close(fd);
    sim_run_steps(5);   // let handleClient() + ui_update run
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

    for (JsonObject step : doc["steps"].as<JsonArray>()) {
        const char* cmd = step["cmd"];
        if (!cmd) continue;
        if (!strcmp(cmd, "snapshot")) {
            std::string body; serializeJson(step["data"], body);
            sim_wifi_post(step["path"] | "/snapshot", body);
        } else if (!strcmp(cmd, "screenshot")) {
            sim_screenshot_png(step["out"] | "esp32sim.png");
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
    return 0;
}
