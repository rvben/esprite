#include "cli.h"
#include "runtime.h"
#include "target.h"
#include "screenshot.h"
#include "scenario.h"
#include "sim_input.h"
#include "Print.h"
#include "WebServer.h"
#ifdef HAVE_SDL2
#include "sim_window.h"
#endif
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

static const int WARMUP_STEPS = 60;

static const char* kSchema = R"JSON({
  "name": "esp32sim",
  "description": "Host-native ESP32 simulator: boot targets, inject input, screenshot",
  "global_opts": { "--target": "NAME (defaults to the only registered target)" },
  "commands": {
    "list-targets": {},
    "schema": {},
    "screenshot": { "args": ["out.png"], "opts": ["--steps N"] },
    "snapshot":    { "args": ["json"], "opts": ["--path P", "--shot OUT"] },
    "tap":         { "args": ["x", "y"], "opts": ["--shot OUT"] },
    "button":      { "args": ["primary|secondary|pwr"], "opts": ["--shot OUT"] },
    "battery":     { "args": ["pct"], "opts": ["--charging", "--no-vbus", "--shot OUT"] },
    "rotate":      { "args": ["0..3"], "opts": ["--shot OUT"] },
    "gpio":        { "args": ["pin", "level"] },
    "serial":      { "args": ["send TEXT | expect REGEX"] },
    "logs":        {},
    "scenario":    { "args": ["file.json"] },
    "serve":       { "opts": ["--port P", "--shot OUT", "--interval-ms N", "--window", "--scale N"], "desc": "boot and keep pumping so a live bridge can POST; --window opens a live SDL window (mouse=touch, space/tab/p=buttons)" },
    "run":         { "desc": "daemon: newline-delimited JSON commands on stdin" }
  }
})JSON";

static const char* opt_val(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc - 1; ++i)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return nullptr;
}
static bool opt_flag(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i) if (!strcmp(argv[i], name)) return true;
    return false;
}

// Positional args after the command, skipping options and their values.
static std::string positional(int argc, char** argv, int index) {
    static const char* val_opts[] = {"--target", "--steps", "--path", "--shot",
                                     "--port", "--interval-ms", "--scale"};
    int seen = 0;
    for (int i = 2; i < argc; ++i) {
        bool is_opt = argv[i][0] == '-' && argv[i][1] == '-';
        if (is_opt) {
            for (auto* vo : val_opts) if (!strcmp(argv[i], vo)) { ++i; break; }
            continue;
        }
        if (seen++ == index) return argv[i];
    }
    return "";
}

static std::string resolve_target(int argc, char** argv) {
    if (const char* t = opt_val(argc, argv, "--target")) return t;
    if (sim_target_count() == 1) return sim_target_at(0)->key;
    return "";
}

static int boot_or_die(int argc, char** argv) {
    std::string t = resolve_target(argc, argv);
    if (t.empty()) {
        fprintf(stderr, "no --target given and %d targets registered\n", sim_target_count());
        return -1;
    }
    if (!sim_boot(t)) { fprintf(stderr, "unknown target '%s'\n", t.c_str()); return -1; }
    sim_run_steps(WARMUP_STEPS);
    return 0;
}

static void maybe_shot(int argc, char** argv) {
    if (const char* out = opt_val(argc, argv, "--shot")) sim_screenshot_png(out);
}

static int cmd_daemon();

int esp32sim_main(int argc, char** argv) {
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        printf("%s\n", kSchema);
        return argc < 2 ? 1 : 0;
    }
    std::string cmd = argv[1];

    if (cmd == "schema") { printf("%s\n", kSchema); return 0; }

    if (cmd == "list-targets") {
        for (int i = 0; i < sim_target_count(); ++i) {
            const SimTarget* t = sim_target_at(i);
            printf("%-16s %dx%d  %s\n", t->key, t->board->width, t->board->height,
                   t->description ? t->description : "");
        }
        return 0;
    }

    if (cmd == "run") return cmd_daemon();

    if (cmd == "scenario") {
        std::string file = positional(argc, argv, 0);
        if (file.empty()) { fprintf(stderr, "scenario: need a file\n"); return 2; }
        std::string def = resolve_target(argc, argv);
        return scenario_run(file, def.empty() ? "clawdmeter" : def);
    }

    if (cmd == "serve") {
        // Persistent: bind the device webserver on --port, boot, then pump loop()
        // in real time so a live bridge (clawdmeter serve) can POST snapshots.
        // Refresh a screenshot every --interval-ms if --shot is given.
        if (const char* p = opt_val(argc, argv, "--port")) setenv("CLAWDSIM_HTTP_PORT", p, 1);
        std::string t = resolve_target(argc, argv);
        if (t.empty()) { fprintf(stderr, "serve: need --target\n"); return 2; }
        if (!sim_boot(t)) { fprintf(stderr, "serve: unknown target '%s'\n", t.c_str()); return 2; }
        sim_run_steps(WARMUP_STEPS);

        const char* shot = opt_val(argc, argv, "--shot");
        const char* port = getenv("CLAWDSIM_HTTP_PORT");
        // If the target ran an HTTP server but its bind failed (port in use), a
        // live bridge could never reach it. Fail loudly instead of looping.
        if (sim_http_bind_status() == 0) {
            fprintf(stderr, "serve: failed to bind HTTP port %s (already in use?)\n",
                    port ? port : "8080");
            return 3;
        }
        int interval = 1000;
        if (const char* iv = opt_val(argc, argv, "--interval-ms")) interval = atoi(iv);

        bool want_window = opt_flag(argc, argv, "--window");
#ifdef HAVE_SDL2
        SimWindow* win = nullptr;
        if (want_window) {
            const SimTarget* at = sim_active_target();
            int scale = 1;
            if (const char* sc = opt_val(argc, argv, "--scale")) scale = atoi(sc);
            win = sim_window_open(at->key, at->board, scale);
            if (!win) fprintf(stderr, "serve: could not open a window; continuing headless\n");
        }
#else
        if (want_window)
            fprintf(stderr, "serve: built without SDL2; --window unavailable "
                            "(install SDL2 and rebuild)\n");
#endif

        fprintf(stderr, "esp32sim: serving '%s' at http://127.0.0.1:%s/snapshot%s\n",
                t.c_str(), port ? port : "8080",
                shot ? "" : " (pass --shot to capture frames)");
        if (shot) sim_screenshot_png(shot);

        auto last = std::chrono::steady_clock::now();
        for (;;) {
            sim_run_steps(4);              // pump handleClient() so POSTs land
            bool paced = false;
#ifdef HAVE_SDL2
            if (win) {
                if (!sim_window_tick(win)) break;   // window closed / Escape
                paced = true;                        // vsync paces the frame
            }
#endif
            if (!paced) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (shot) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() >= interval) {
#ifdef HAVE_SDL2
                    if (win) sim_window_request_capture(win, shot);   // full window (with buttons)
                    else
#endif
                    sim_screenshot_png(shot);                          // device only
                    last = now;
                }
            }
        }
#ifdef HAVE_SDL2
        if (win) sim_window_close(win);
#endif
        return 0;
    }

    // Remaining commands boot a target first.
    if (boot_or_die(argc, argv) != 0) return 2;

    if (cmd == "screenshot") {
        std::string out = positional(argc, argv, 0);
        if (out.empty()) out = "esp32sim.png";
        if (const char* s = opt_val(argc, argv, "--steps")) sim_run_steps(atoi(s));
        return sim_screenshot_png(out.c_str()) ? 0 : 1;
    }
    if (cmd == "snapshot") {
        std::string json = positional(argc, argv, 0);
        const char* path = opt_val(argc, argv, "--path");
        sim_wifi_post(path ? path : "/snapshot", json);
        maybe_shot(argc, argv);
        return 0;
    }
    if (cmd == "tap") {
        sim_input().touch_pressed = true;
        sim_input().touch_x = atoi(positional(argc, argv, 0).c_str());
        sim_input().touch_y = atoi(positional(argc, argv, 1).c_str());
        sim_run_steps(4);
        sim_input().touch_pressed = false;
        sim_run_steps(4);
        maybe_shot(argc, argv);
        return 0;
    }
    if (cmd == "button") {
        std::string which = positional(argc, argv, 0);
        if (which == "pwr") { sim_input().pwr_events.push_back(1); sim_run_steps(5); }
        else {
            int idx = (which == "secondary") ? 1 : 0;
            sim_input().button[idx] = true;  sim_run_steps(5);
            sim_input().button[idx] = false; sim_run_steps(3);
        }
        maybe_shot(argc, argv);
        return 0;
    }
    if (cmd == "battery") {
        sim_input().battery_pct = atoi(positional(argc, argv, 0).c_str());
        if (opt_flag(argc, argv, "--charging")) sim_input().charging = true;
        if (opt_flag(argc, argv, "--no-vbus"))  sim_input().vbus = false;
        sim_run_steps(5);
        maybe_shot(argc, argv);
        return 0;
    }
    if (cmd == "rotate") {
        sim_input().quadrant = atoi(positional(argc, argv, 0).c_str());
        sim_run_steps(5);
        maybe_shot(argc, argv);
        return 0;
    }
    if (cmd == "gpio") {
        int pin = atoi(positional(argc, argv, 0).c_str());
        int lvl = atoi(positional(argc, argv, 1).c_str());
        sim_gpio_set(pin, lvl);
        sim_run_steps(5);
        return 0;
    }
    if (cmd == "serial") {
        std::string sub = positional(argc, argv, 0);
        std::string arg = positional(argc, argv, 1);
        if (sub == "send") { sim_serial_inject(arg + "\n"); sim_run_steps(5); return 0; }
        if (sub == "expect") { sim_run_steps(WARMUP_STEPS); return sim_serial_contains(arg) ? 0 : 1; }
        fprintf(stderr, "serial: use 'send TEXT' or 'expect REGEX'\n");
        return 2;
    }
    if (cmd == "logs") { printf("%s", sim_serial_output().c_str()); return 0; }

    fprintf(stderr, "unknown command '%s' (try schema)\n", cmd.c_str());
    return 2;
}

// Daemon: read newline-delimited JSON commands on stdin, emit JSON per line.
static int cmd_daemon() {
    char line[8192];
    bool booted = false;
    while (fgets(line, sizeof(line), stdin)) {
        JsonDocument doc;
        if (deserializeJson(doc, line)) { printf("{\"error\":\"bad json\"}\n"); fflush(stdout); continue; }
        std::string cmd = doc["cmd"] | "";
        if (cmd == "boot") {
            std::string t = doc["target"] | "";
            booted = sim_boot(t);
            if (booted) sim_run_steps(WARMUP_STEPS);
            printf("{\"ok\":%s}\n", booted ? "true" : "false");
        } else if (!booted) {
            printf("{\"error\":\"not booted\"}\n");
        } else if (cmd == "screenshot") {
            const char* out = doc["out"] | "esp32sim.png";
            printf("{\"ok\":%s}\n", sim_screenshot_png(out) ? "true" : "false");
        } else if (cmd == "snapshot") {
            std::string body; serializeJson(doc["data"], body);
            sim_wifi_post(doc["path"] | "/snapshot", body);
            printf("{\"ok\":true}\n");
        } else if (cmd == "steps") {
            sim_run_steps(doc["n"] | 10);
            printf("{\"ok\":true}\n");
        } else if (cmd == "logs") {
            printf("{\"logs\":\"seen\"}\n");
        } else {
            printf("{\"error\":\"unknown cmd\"}\n");
        }
        fflush(stdout);
    }
    return 0;
}
