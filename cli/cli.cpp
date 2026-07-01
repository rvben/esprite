#include "cli.h"
#include "runtime.h"
#include "target.h"
#include "screenshot.h"
#include "scenario.h"
#include "sim_input.h"
#include "Print.h"
#include "WebServer.h"
#include "lvgl_snapshot.h"
#include "framebuffer.h"
#ifdef HAVE_SDL2
#include "sim_window.h"
#endif
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cctype>
#include <unistd.h>

static const int WARMUP_STEPS = 60;

static const char* kSchema = R"JSON({
  "clispec": "0.2",
  "name": "esprite",
  "version": "0.1.0",
  "description": "Host-native ESP32 simulator. Boots a target's firmware, renders its UI, and drives it: snapshot-ref UI reads, input injection, screenshots, and a persistent JSON session. Results are JSON on stdout; logs go to stderr.",
  "global_args": [
    { "name": "--output", "description": "Output format. auto = text on a TTY, JSON when piped.", "type": "string", "enum": ["auto", "json", "text"], "default": "auto" },
    { "name": "--json", "description": "Shorthand for --output json.", "type": "boolean" },
    { "name": "--target", "description": "Target to boot (see list-targets). Optional when exactly one is registered.", "type": "string" },
    { "name": "--limit", "description": "Max items to return from list commands (list-targets, ui).", "type": "number" },
    { "name": "--offset", "description": "Items to skip from list commands (pagination).", "type": "number" },
    { "name": "--fields", "description": "Comma-separated fields to include from list-targets items.", "type": "string" }
  ],
  "commands": [
    { "name": "schema", "description": "Print this clispec contract as JSON.", "mutating": false, "stability": "stable" },
    { "name": "list-targets", "description": "List onboarded targets with board metadata.", "mutating": false, "stability": "stable",
      "output_fields": [
        { "name": "key", "description": "Target id to pass to --target.", "type": "string" },
        { "name": "width", "description": "Screen width in px.", "type": "number" },
        { "name": "height", "description": "Screen height in px.", "type": "number" },
        { "name": "buttons", "description": "Physical button count.", "type": "number" },
        { "name": "battery", "description": "Board has a battery.", "type": "boolean" },
        { "name": "rotation", "description": "Board supports rotation.", "type": "boolean" }
      ] },
    { "name": "ui", "description": "Snapshot the active LVGL widget tree as an array of elements; act on the refs with 'tap --ref'. Empty for non-LVGL targets.", "mutating": false, "stability": "stable",
      "example": { "args": ["--target", "waveshare_amoled_216_c6"], "stdin": "" },
      "output_fields": [
        { "name": "ref", "description": "Stable element handle, e.g. e3.", "type": "string" },
        { "name": "type", "description": "label|bar|arc|button|image|line|obj.", "type": "string" },
        { "name": "x", "description": "Absolute x.", "type": "number" },
        { "name": "y", "description": "Absolute y.", "type": "number" },
        { "name": "text", "description": "Label text, when present.", "type": "string" },
        { "name": "value", "description": "Bar/arc value, when present.", "type": "number" }
      ] },
    { "name": "screenshot", "description": "Boot, render, and write a PNG of the device screen.", "mutating": false, "stability": "stable",
      "args": [ { "name": "out", "description": "Output PNG path (default esprite.png).", "type": "string", "required": false } ],
      "example": { "args": ["out.png", "--target", "waveshare_amoled_216_c6"], "stdin": "" },
      "output_fields": [
        { "name": "ok", "description": "Wrote the file.", "type": "boolean" },
        { "name": "path", "description": "PNG path.", "type": "string" },
        { "name": "w", "description": "Width.", "type": "number" },
        { "name": "h", "description": "Height.", "type": "number" }
      ] },
    { "name": "snapshot", "description": "POST a JSON body to the device's /snapshot HTTP endpoint (data the firmware parses).", "mutating": true, "stability": "stable",
      "args": [ { "name": "json", "description": "Wire JSON to POST.", "type": "string", "required": true } ],
      "example": { "args": ["{\"lim\":1,\"s5\":42}", "--target", "waveshare_amoled_216_c6"], "stdin": "" } },
    { "name": "tap", "description": "Inject a touch, by widget ref (--ref e3, from ui) or by pixel (x y).", "mutating": true, "stability": "stable",
      "args": [
        { "name": "x", "description": "X pixel (omit when using --ref).", "type": "number", "required": false },
        { "name": "y", "description": "Y pixel.", "type": "number", "required": false }
      ],
      "example": { "args": ["240", "240", "--target", "waveshare_amoled_216_c6"], "stdin": "" },
      "output_fields": [
        { "name": "ok", "description": "Tap injected.", "type": "boolean" },
        { "name": "x", "description": "Resolved x.", "type": "number" },
        { "name": "y", "description": "Resolved y.", "type": "number" }
      ] },
    { "name": "button", "description": "Press a physical button.", "mutating": true, "stability": "stable",
      "args": [ { "name": "which", "description": "Button to press.", "type": "string", "required": true, "enum": ["primary", "secondary", "pwr"] } ],
      "example": { "args": ["primary", "--target", "waveshare_amoled_216_c6"], "stdin": "" } },
    { "name": "battery", "description": "Set battery level; --charging and --no-vbus set the flags.", "mutating": true, "stability": "stable",
      "args": [ { "name": "pct", "description": "0-100.", "type": "number", "required": true } ],
      "example": { "args": ["50", "--target", "waveshare_amoled_216_c6"], "stdin": "" } },
    { "name": "rotate", "description": "Set the IMU rotation quadrant (0-3).", "mutating": true, "stability": "stable",
      "args": [ { "name": "quadrant", "description": "0-3.", "type": "number", "required": true } ],
      "example": { "args": ["1", "--target", "waveshare_amoled_216_c6"], "stdin": "" } },
    { "name": "gpio", "description": "Set a GPIO pin level (read back by digitalRead).", "mutating": true, "stability": "stable",
      "args": [
        { "name": "pin", "description": "GPIO number.", "type": "number", "required": true },
        { "name": "level", "description": "0 or 1.", "type": "number", "required": true }
      ],
      "example": { "args": ["9", "1", "--target", "waveshare_amoled_216_c6"], "stdin": "" } },
    { "name": "serial", "description": "serial send TEXT feeds device input; serial expect REGEX matches captured output (exit 1 on no match).", "mutating": false, "stability": "stable",
      "args": [
        { "name": "sub", "description": "send or expect.", "type": "string", "required": true, "enum": ["send", "expect"] },
        { "name": "arg", "description": "Text to send, or regex to expect.", "type": "string", "required": true }
      ],
      "example": { "args": ["expect", "ready", "--target", "waveshare_amoled_216_c6"], "stdin": "" } },
    { "name": "logs", "description": "Print captured device serial output.", "mutating": false, "stability": "stable",
      "example": { "args": ["--target", "waveshare_amoled_216_c6"], "stdin": "" },
      "output_fields": [ { "name": "serial", "description": "Captured serial text.", "type": "string" } ] },
    { "name": "scenario", "description": "Run a JSON scenario file (ordered steps) headless.", "mutating": true, "stability": "stable",
      "args": [ { "name": "file", "description": "Scenario JSON path.", "type": "string", "required": true } ] },
    { "name": "serve", "description": "Boot and keep pumping so a live bridge can POST; --window opens an interactive SDL window (mouse=touch, on-screen buttons + battery/USB/rotate controls). Human logs on stderr.", "mutating": true, "stability": "stable" },
    { "name": "run", "description": "Persistent agent session: newline-delimited JSON commands on stdin, one JSON reply per line. cmds: boot, ui, tap (ref|x,y), button, battery, rotate, gpio, snapshot, screenshot, steps, serial, logs, quit. Refs from ui stay valid within the session.", "mutating": true, "stability": "stable" }
  ],
  "errors": [
    { "kind": "no_target", "description": "No --target and more than one target registered.", "exit_code": 2 },
    { "kind": "unknown_target", "description": "The --target is not registered (see list-targets).", "exit_code": 2 },
    { "kind": "bind_failed", "description": "serve could not bind the HTTP port (already in use).", "exit_code": 3 },
    { "kind": "ref_not_found", "description": "tap --ref referenced a widget not in the current ui snapshot.", "exit_code": 4 },
    { "kind": "conflict", "description": "An argument or option conflicts with another (e.g. tap given both --ref and x/y).", "exit_code": 5 },
    { "kind": "post_failed", "description": "snapshot could not be delivered to the running target (connect failed or body exceeds the server read size).", "exit_code": 6 },
    { "kind": "bad_args", "description": "Missing or invalid arguments for the command.", "exit_code": 2 }
  ]
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

// Positional args after the command. Only *recognized* options are skipped, so
// free-form positionals that start with a dash (e.g. `serial send -AT`, negative
// coordinates) are preserved.
static std::string positional(int argc, char** argv, int index) {
    static const char* val_opts[] = {"--target", "--steps", "--path", "--shot",
                                     "--port", "--interval-ms", "--scale", "--ref",
                                     "--output", "-o", "--limit", "--offset", "--fields"};
    static const char* flag_opts[] = {"--charging", "--no-vbus", "--window", "--json"};
    int seen = 0;
    for (int i = 2; i < argc; ++i) {
        bool matched = false;
        for (auto* vo : val_opts) if (!strcmp(argv[i], vo)) { ++i; matched = true; break; }
        if (matched) continue;
        for (auto* fo : flag_opts) if (!strcmp(argv[i], fo)) { matched = true; break; }
        if (matched) continue;
        if (seen++ == index) return argv[i];
    }
    return "";
}

static std::string resolve_target(int argc, char** argv) {
    if (const char* t = opt_val(argc, argv, "--target")) return t;
    if (sim_target_count() == 1) return sim_target_at(0)->key;
    return "";
}

static int fail(const char* kind, const std::string& msg, int code);  // defined below

// Boots the resolved target. Returns 0 on success, or a non-zero exit code after
// emitting the error envelope (2 for target problems).
static int boot_or_die(int argc, char** argv) {
    std::string t = resolve_target(argc, argv);
    if (t.empty())
        return fail("no_target", "no --target and more than one target registered", 2);
    if (!sim_boot(t))
        return fail("unknown_target", "unknown target '" + t + "'", 2);
    sim_run_steps(WARMUP_STEPS);   // sim_boot resets UI refs via its boot hook
    return 0;
}

static void maybe_shot(int argc, char** argv) {
    if (const char* out = opt_val(argc, argv, "--shot")) sim_screenshot_png(out);
}

static int cmd_daemon();

// JSON-escape a string into out (for structured stdout results).
static std::string json_esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}
static const char* jbool(bool b) { return b ? "true" : "false"; }

// Output mode: JSON when piped or forced with --output json; text on a TTY or
// with --output text. Explicit --output wins over TTY detection.
static bool g_json = true;
static void set_output_mode(int argc, char** argv) {
    const char* om = opt_val(argc, argv, "--output");
    if (!om) om = opt_val(argc, argv, "-o");
    std::string m = om ? om : "auto";
    if (opt_flag(argc, argv, "--json")) m = "json";   // convenience alias for --output json
    if (m == "json")      g_json = true;
    else if (m == "text") g_json = false;
    else                  g_json = !isatty(fileno(stdout));
}
static void emit(const std::string& json, const std::string& text) {
    printf("%s\n", (g_json ? json : text).c_str());
}
// Slice a JSON array-of-objects string by --offset/--limit and wrap it with
// truncation metadata: {"items":[...],"total","offset","count","truncated"}.
static std::string bounded_array(const std::string& arr, int offset, int limit) {
    std::vector<std::string> elems;
    int depth = 0; size_t start = std::string::npos;
    bool in_str = false, esc = false;
    for (size_t i = 0; i < arr.size(); ++i) {
        char c = arr[i];
        if (esc) { esc = false; continue; }         // skip the char after a backslash
        if (in_str) {
            if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;                                // braces inside strings don't count
        }
        if (c == '"') { in_str = true; }
        else if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}') {
            if (--depth == 0 && start != std::string::npos) {
                elems.push_back(arr.substr(start, i - start + 1)); start = std::string::npos;
            }
        }
    }
    if (offset < 0) offset = 0;
    int total = (int)elems.size(), shown = 0;
    std::string items = "[";
    for (int i = offset; i < total && (limit < 0 || shown < limit); ++i, ++shown)
        items += (shown ? "," : "") + elems[i];
    items += "]";
    bool truncated = (offset + shown) < total;
    return "{\"items\":" + items + ",\"total\":" + std::to_string(total) +
           ",\"offset\":" + std::to_string(offset) + ",\"count\":" + std::to_string(shown) +
           ",\"truncated\":" + jbool(truncated) + "}";
}
// Structured error envelope as the last line of stderr; returns the exit code.
static int fail(const char* kind, const std::string& msg, int code) {
    fprintf(stderr, "{\"error\":{\"kind\":\"%s\",\"message\":\"%s\"}}\n", kind, json_esc(msg).c_str());
    return code;
}

int esprite_main(int argc, char** argv) {
    set_output_mode(argc, argv);
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        printf("%s\n", kSchema);
        return argc < 2 ? 1 : 0;
    }
    std::string cmd = argv[1];

    if (cmd == "schema") { printf("%s\n", kSchema); return 0; }

    if (cmd == "list-targets") {
        int offset = opt_val(argc, argv, "--offset") ? atoi(opt_val(argc, argv, "--offset")) : 0;
        int limit  = opt_val(argc, argv, "--limit")  ? atoi(opt_val(argc, argv, "--limit"))  : -1;
        if (offset < 0) offset = 0;   // never index the registry out of bounds
        const char* fields = opt_val(argc, argv, "--fields");
        auto want = [&](const char* f) { return !fields || strstr(fields, f) != nullptr; };
        int total = sim_target_count();
        std::string json = "{\"items\":[", text;
        int shown = 0;
        for (int i = offset; i < total && (limit < 0 || shown < limit); ++i, ++shown) {
            const SimTarget* t = sim_target_at(i);
            std::string o = "{";
            if (want("key"))    o += "\"key\":\"" + std::string(t->key) + "\",";
            if (want("name"))   o += "\"name\":\"" + json_esc(t->board->name) + "\",";
            if (want("width"))  o += "\"width\":" + std::to_string(t->board->width) + ",";
            if (want("height")) o += "\"height\":" + std::to_string(t->board->height) + ",";
            if (want("buttons"))o += "\"buttons\":" + std::to_string(t->board->button_count) + ",";
            if (want("battery"))o += std::string("\"battery\":") + jbool(t->board->has_battery) + ",";
            if (want("rotation"))o += std::string("\"rotation\":") + jbool(t->board->has_rotation) + ",";
            if (!o.empty() && o.back() == ',') o.pop_back();
            o += "}";
            json += (shown ? "," : "") + o;
            text += std::string(t->key) + "  " + std::to_string(t->board->width) + "x" +
                    std::to_string(t->board->height) + "  " + t->board->name + "\n";
        }
        bool truncated = (offset + shown) < total;
        json += "],\"total\":" + std::to_string(total) + ",\"offset\":" + std::to_string(offset) +
                ",\"count\":" + std::to_string(shown) + ",\"truncated\":" + jbool(truncated) + "}";
        emit(json, text.empty() ? "(no targets)" : text.substr(0, text.size() - 1));
        return 0;
    }

    if (cmd == "run") return cmd_daemon();

    if (cmd == "scenario") {
        std::string file = positional(argc, argv, 0);
        if (file.empty()) return fail("bad_args", "scenario needs a file path", 2);
        std::string def = resolve_target(argc, argv);
        return scenario_run(file, def.empty() ? "waveshare_amoled_216_c6" : def);
    }

    if (cmd == "serve") {
        // Persistent: bind the device webserver on --port, boot, then pump loop()
        // in real time so a live bridge (clawdmeter serve) can POST snapshots.
        // Refresh a screenshot every --interval-ms if --shot is given.
        if (const char* p = opt_val(argc, argv, "--port")) setenv("ESPRITE_HTTP_PORT", p, 1);
        std::string t = resolve_target(argc, argv);
        if (t.empty()) return fail("no_target", "no --target and more than one target registered", 2);
        if (!sim_boot(t)) return fail("unknown_target", "unknown target '" + t + "'", 2);
        sim_run_steps(WARMUP_STEPS);

        const char* shot = opt_val(argc, argv, "--shot");
        const char* port = getenv("ESPRITE_HTTP_PORT");
        // If the target ran an HTTP server but its bind failed (port in use), a
        // live bridge could never reach it. Fail loudly instead of looping.
        if (sim_http_bind_status() == 0)
            return fail("bind_failed", std::string("could not bind HTTP port ") + (port ? port : "8080"), 3);
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

        // Report the port the target actually bound (matters when --port 0 asked
        // the OS for an ephemeral port), not the raw requested value.
        int bound = sim_http_bind_status();
        std::string bound_port = bound > 0 ? std::to_string(bound) : (port ? port : "8080");
        fprintf(stderr, "esprite: serving '%s' at http://127.0.0.1:%s/snapshot%s\n",
                t.c_str(), bound_port.c_str(),
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

    if (cmd == "ui") {
        // Snapshot-ref model: the LVGL widget tree of the current screen. Bounded
        // by --offset/--limit; --fields not applied (elements are already compact).
        int offset = opt_val(argc, argv, "--offset") ? atoi(opt_val(argc, argv, "--offset")) : 0;
        int limit  = opt_val(argc, argv, "--limit")  ? atoi(opt_val(argc, argv, "--limit"))  : -1;
        std::string tree = lvgl_snapshot_json();
        emit(bounded_array(tree, offset, limit), tree);
        return 0;
    }
    if (cmd == "screenshot") {
        std::string out = positional(argc, argv, 0);
        if (out.empty()) out = "esprite.png";
        if (const char* s = opt_val(argc, argv, "--steps")) sim_run_steps(atoi(s));
        bool ok = sim_screenshot_png(out.c_str());
        int w = sim_framebuffer().w(), h = sim_framebuffer().h();
        emit("{\"ok\":" + std::string(jbool(ok)) + ",\"path\":\"" + json_esc(out) + "\",\"w\":" +
             std::to_string(w) + ",\"h\":" + std::to_string(h) + "}",
             (ok ? "wrote " : "FAILED ") + out + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
        return ok ? 0 : 1;
    }
    if (cmd == "snapshot") {
        std::string json = positional(argc, argv, 0);
        bool ok = sim_wifi_post(opt_val(argc, argv, "--path") ? opt_val(argc, argv, "--path") : "/snapshot", json);
        if (!ok) return fail("post_failed", "snapshot not delivered (target server unbound/unreachable or body too large)", 6);
        maybe_shot(argc, argv);
        emit("{\"ok\":true}", "posted snapshot");
        return 0;
    }
    if (cmd == "tap") {
        bool has_ref = opt_val(argc, argv, "--ref") != nullptr;
        bool has_xy  = !positional(argc, argv, 0).empty();
        if (has_ref && has_xy) return fail("conflict", "tap takes --ref or x/y, not both", 5);
        int x, y;
        std::string extra;
        if (has_ref) {
            std::string ref = opt_val(argc, argv, "--ref");
            if (!lvgl_ref_center(ref, &x, &y))
                return fail("ref_not_found", "no widget with ref " + ref + " in the current ui snapshot", 4);
            extra = ",\"ref\":\"" + json_esc(ref) + "\"";
        } else {
            x = atoi(positional(argc, argv, 0).c_str());
            y = atoi(positional(argc, argv, 1).c_str());
        }
        sim_input().touch_pressed = true; sim_input().touch_x = x; sim_input().touch_y = y;
        sim_run_steps(4);
        sim_input().touch_pressed = false; sim_run_steps(4);
        maybe_shot(argc, argv);
        emit("{\"ok\":true" + extra + ",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}",
             "tapped " + std::to_string(x) + "," + std::to_string(y));
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
        emit("{\"ok\":true,\"button\":\"" + json_esc(which) + "\"}", "pressed " + which);
        return 0;
    }
    if (cmd == "battery") {
        sim_input().battery_pct = atoi(positional(argc, argv, 0).c_str());
        if (opt_flag(argc, argv, "--charging")) sim_input().charging = true;
        if (opt_flag(argc, argv, "--no-vbus"))  sim_input().vbus = false;
        sim_run_steps(5);
        maybe_shot(argc, argv);
        emit("{\"ok\":true,\"pct\":" + std::to_string(sim_input().battery_pct) + ",\"charging\":" +
             jbool(sim_input().charging) + ",\"vbus\":" + jbool(sim_input().vbus) + "}",
             "battery " + std::to_string(sim_input().battery_pct) + "%");
        return 0;
    }
    if (cmd == "rotate") {
        sim_input().quadrant = atoi(positional(argc, argv, 0).c_str());
        sim_run_steps(5);
        maybe_shot(argc, argv);
        emit("{\"ok\":true,\"quadrant\":" + std::to_string(sim_input().quadrant) + "}",
             "quadrant " + std::to_string(sim_input().quadrant));
        return 0;
    }
    if (cmd == "gpio") {
        int pin = atoi(positional(argc, argv, 0).c_str());
        int lvl = atoi(positional(argc, argv, 1).c_str());
        sim_gpio_set(pin, lvl);
        sim_run_steps(5);
        emit("{\"ok\":true,\"pin\":" + std::to_string(pin) + ",\"level\":" + std::to_string(lvl ? 1 : 0) + "}",
             "gpio " + std::to_string(pin) + "=" + std::to_string(lvl ? 1 : 0));
        return 0;
    }
    if (cmd == "serial") {
        std::string sub = positional(argc, argv, 0), arg = positional(argc, argv, 1);
        if (sub == "send") {
            sim_serial_inject(arg + "\n"); sim_run_steps(5);
            emit("{\"ok\":true}", "sent");
            return 0;
        }
        if (sub == "expect") {
            sim_run_steps(WARMUP_STEPS);
            bool matched = sim_serial_contains(arg);
            emit("{\"matched\":" + std::string(jbool(matched)) + "}", matched ? "matched" : "no match");
            return matched ? 0 : 1;
        }
        return fail("bad_args", "serial takes 'send TEXT' or 'expect REGEX'", 2);
    }
    if (cmd == "logs") {
        std::string s = sim_serial_output();
        emit("{\"serial\":\"" + json_esc(s) + "\"}", s);
        return 0;
    }

    return fail("bad_args", "unknown command '" + cmd + "' (try: esprite schema)", 2);
}

// Daemon: a persistent agent session. Read newline-delimited JSON commands on
// stdin, emit one JSON reply per line. Refs from {"cmd":"ui"} stay valid for
// {"cmd":"tap","ref":...} within the session (the snapshot-ref model).
static int cmd_daemon() {
    char line[16384];
    bool booted = false;
    while (fgets(line, sizeof(line), stdin)) {
        JsonDocument doc;
        if (deserializeJson(doc, line)) { printf("{\"error\":\"bad_json\"}\n"); fflush(stdout); continue; }
        std::string cmd = doc["cmd"] | "";

        if (cmd == "boot") {
            std::string t = doc["target"] | "";
            booted = sim_boot(t);
            if (booted) sim_run_steps(WARMUP_STEPS);   // sim_boot resets UI refs
            printf("{\"ok\":%s}\n", jbool(booted));
        } else if (!booted) {
            printf("{\"error\":\"not_booted\"}\n");
        } else if (cmd == "ui") {
            printf("%s\n", lvgl_snapshot_json().c_str());
        } else if (cmd == "screenshot") {
            const char* out = doc["out"] | "esprite.png";
            bool ok = sim_screenshot_png(out);
            printf("{\"ok\":%s,\"path\":\"%s\",\"w\":%d,\"h\":%d}\n",
                   jbool(ok), json_esc(out).c_str(), sim_framebuffer().w(), sim_framebuffer().h());
        } else if (cmd == "tap") {
            int x, y;
            if (doc["ref"].is<const char*>()) {
                std::string ref = doc["ref"] | "";
                if (!lvgl_ref_center(ref, &x, &y)) {
                    printf("{\"ok\":false,\"error\":\"ref_not_found\"}\n"); fflush(stdout); continue;
                }
            } else { x = doc["x"] | 0; y = doc["y"] | 0; }
            sim_input().touch_pressed = true; sim_input().touch_x = x; sim_input().touch_y = y;
            sim_run_steps(4);
            sim_input().touch_pressed = false; sim_run_steps(4);
            printf("{\"ok\":true,\"x\":%d,\"y\":%d}\n", x, y);
        } else if (cmd == "button") {
            std::string which = doc["which"] | "primary";
            if (which == "pwr") { sim_input().pwr_events.push_back(1); sim_run_steps(5); }
            else {
                int idx = (which == "secondary") ? 1 : 0;
                sim_input().button[idx] = true;  sim_run_steps(5);
                sim_input().button[idx] = false; sim_run_steps(3);
            }
            printf("{\"ok\":true}\n");
        } else if (cmd == "battery") {
            sim_input().battery_pct = doc["pct"] | 75;
            if (doc["charging"].is<bool>()) sim_input().charging = doc["charging"];
            if (doc["vbus"].is<bool>())     sim_input().vbus = doc["vbus"];
            sim_run_steps(5);
            printf("{\"ok\":true}\n");
        } else if (cmd == "rotate") {
            sim_input().quadrant = doc["q"] | 0; sim_run_steps(5); printf("{\"ok\":true}\n");
        } else if (cmd == "gpio") {
            sim_gpio_set(doc["pin"] | 0, doc["level"] | 0); sim_run_steps(5); printf("{\"ok\":true}\n");
        } else if (cmd == "snapshot") {
            std::string body; serializeJson(doc["data"], body);
            bool ok = sim_wifi_post(doc["path"] | "/snapshot", body);
            if (ok) printf("{\"ok\":true}\n");
            else printf("{\"ok\":false,\"error\":\"post_failed\"}\n");
        } else if (cmd == "steps") {
            sim_run_steps(doc["n"] | 10); printf("{\"ok\":true}\n");
        } else if (cmd == "serial") {
            std::string sub = doc["sub"] | "";
            if (sub == "send") {
                std::string text = doc["text"] | "";
                sim_serial_inject(text + "\n"); sim_run_steps(5); printf("{\"ok\":true}\n");
            } else if (sub == "expect") {
                std::string rx = doc["regex"] | "";
                sim_run_steps(WARMUP_STEPS);
                printf("{\"matched\":%s}\n", jbool(sim_serial_contains(rx)));
            } else printf("{\"error\":\"serial_sub\"}\n");
        } else if (cmd == "logs") {
            printf("{\"serial\":\"%s\"}\n", json_esc(sim_serial_output()).c_str());
        } else if (cmd == "quit" || cmd == "exit") {
            break;
        } else {
            printf("{\"error\":\"unknown_cmd\"}\n");
        }
        fflush(stdout);
    }
    return 0;
}
