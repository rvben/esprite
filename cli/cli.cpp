#include "cli.h"
#include "cli_internal.h"
#include "actions.h"
#include "runtime.h"
#include "target.h"
#include "backend.h"
#include "qemu_backend.h"
#include "screenshot.h"
#include "scenario.h"
#include "sim_input.h"
#include "sim_ble.h"
#include "ble_bridge.h"
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
#include <csignal>
#include <unistd.h>

// Steps to run after a serial injection before checking for a match: enough
// for the firmware to react and print, without an unbounded wait. Same value
// as a backend's post-boot warmup (core/backend.cpp), reused here because
// both are "give the firmware a beat to produce output" pumps, not boots.
static const int SERIAL_SETTLE_STEPS = 60;

// Wall-clock equivalent for a qemu target: sim_run_steps() only advances a
// target's loop(), which a qemu target never has (it boots a child process
// instead of calling Arduino code in this one), so it is a silent no-op
// there. Route the same "give the firmware a beat" wait through the active
// backend's settle_ms() instead, which for qemu actually pumps the child's
// I/O for real wall-clock time.
static const unsigned SERIAL_SETTLE_QEMU_MS = 500;

// Gives the active target time to react to a serial send/before a serial
// expect. Native targets step their in-process loop(); a qemu target has no
// loop() to step, so it settles through the backend's own wall-clock pump
// instead (see SERIAL_SETTLE_QEMU_MS). Checked via sim_backend().name(), not
// sim_active_target(): a qemu boot never calls core's sim_boot() (it manages
// its own child process instead), so core's notion of the active target
// stays stale/null for a qemu-only process - the backend seam is the only
// reliable source for "which implementation is actually running".
static void serial_settle(int native_steps) {
    if (std::string(sim_backend().name()) == "qemu")
        sim_backend().settle_ms(SERIAL_SETTLE_QEMU_MS);
    else
        sim_run_steps(native_steps);
}

// Ensures the active backend's child process (if any) never outlives one CLI
// invocation or run session: destroyed on every exit path - success, an
// error envelope's early return, daemon EOF/quit, and the serve
// SIGINT/SIGTERM loop break, since all of those unwind through the scope
// this guard is declared in. No-op for native; qemu's shutdown() stops the
// QEMU child and removes its scratch socket dir. shutdown() is idempotent,
// so it is safe to declare one of these in both esprite_main (which calls
// esprite_daemon for the `run` command) and esprite_daemon itself (tests
// call it directly, bypassing esprite_main).
//
// For that guard to actually run on Ctrl-C/SIGTERM, default signal
// disposition (terminate without unwinding the stack) must never be allowed
// to fire while a qemu child might be alive - which spans the whole process,
// not just the serve loop, since a plain `esprite serial ...` on a qemu
// target also spawns one during boot. g_interrupted is the process-wide flag
// the handlers below set; sim_interrupted() is the accessor every bounded
// wait loop in this process (this file's serial_settle, and - via the
// function pointer qemu_backend_install() takes, since backends/qemu must
// not include cli headers - the qemu backend's own boot/settle loops) polls
// to bail out early so control returns here and the guard's destructor runs.
struct BackendShutdownGuard {
    ~BackendShutdownGuard() { sim_backend().shutdown(); }
};

static volatile sig_atomic_t g_interrupted = 0;
static bool sim_interrupted() { return g_interrupted != 0; }

// Uses sigaction (not plain signal()) so SA_RESTART can be left unset: with
// it set - the default plain signal() gives on both Linux and macOS - a
// blocking call interrupted by one of these signals is transparently resumed
// by the kernel instead of returning, which left esprite_daemon's fgets()
// loop (its steady state; see esprite_daemon) unable to ever notice
// g_interrupted while waiting for the next command line. Without
// SA_RESTART, that same read fails with EINTR and stdio surfaces it as
// fgets() returning NULL, which the daemon loop already treats like EOF.
// Idempotent: sigaction with the same disposition is a harmless no-op on
// repeat, so this is safe to call from both esprite_main and esprite_daemon
// (a public entry point tests call directly, bypassing esprite_main) and
// safe across the repeated in-process esprite_main calls the test suite
// makes.
static void install_signal_handlers() {
    struct sigaction sa {};
    sa.sa_handler = [](int) { g_interrupted = 1; };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // deliberately no SA_RESTART - see comment above
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

// Commands a qemu-backed target currently supports (tier 1: serial
// output/input and the serve lifecycle, plus session bookkeeping). Everything
// else assumes an in-process framebuffer/widget-tree/GPIO/BLE surface that no
// qemu target has, so it is gated to `unsupported` before boot - no QEMU
// process is ever spawned for a command this returns false for.
static bool qemu_tier1_command(const std::string& cmd) {
    return cmd == "serial" || cmd == "logs" || cmd == "quit" || cmd == "exit";
}

// Stamped by the build from the CMake project version: one source of truth
// for --version, the schema, and the release tag.
#ifndef ESPRITE_VERSION
#define ESPRITE_VERSION "dev"
#endif

static const std::string kSchema = std::string(R"JSON({
  "clispec": "0.2",
  "name": "esprite",
  "version": ")JSON") + ESPRITE_VERSION + R"JSON(",
  "description": "Host-native ESP32 simulator. Boots a target's firmware, renders its UI, and drives it: snapshot-ref UI reads, input injection, screenshots, and a persistent JSON session. Results are JSON on stdout; logs go to stderr.",
  "global_args": [
    { "name": "--output", "description": "Output format. auto = text on a TTY, JSON when piped.", "type": "string", "enum": ["auto", "json", "text"], "default": "auto" },
    { "name": "--json", "description": "Shorthand for --output json.", "type": "boolean" },
    { "name": "--version", "description": "Print name and version.", "type": "boolean" },
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
        { "name": "rotation", "description": "Board supports rotation.", "type": "boolean" },
        { "name": "backend", "description": "native (runs in-process) or qemu (boots in a child QEMU process; serial/logs/serve only, and serve itself rejects --shot/--window/--ble-port there since there is no framebuffer or native BLE link - see backend_unavailable for the env var contract).", "type": "string" }
      ] },
    { "name": "ui", "description": "Snapshot the active LVGL widget tree as an array of elements; act on the refs with 'tap --ref'. Empty for non-LVGL targets.", "mutating": false, "stability": "stable",
      "example": { "args": ["--target", "agentgauge"], "stdin": "" },
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
      "example": { "args": ["out.png", "--target", "agentgauge"], "stdin": "" },
      "output_fields": [
        { "name": "ok", "description": "Wrote the file.", "type": "boolean" },
        { "name": "path", "description": "PNG path.", "type": "string" },
        { "name": "w", "description": "Width.", "type": "number" },
        { "name": "h", "description": "Height.", "type": "number" }
      ] },
    { "name": "snapshot", "description": "POST a JSON body to the device's /snapshot HTTP endpoint (data the firmware parses).", "mutating": true, "stability": "stable",
      "args": [ { "name": "json", "description": "Wire JSON to POST.", "type": "string", "required": true } ],
      "example": { "args": ["{\"lim\":1,\"s5\":42}", "--target", "agentgauge"], "stdin": "" } },
    { "name": "tap", "description": "Inject a touch, by widget ref (--ref e3, from ui) or by pixel (x y).", "mutating": true, "stability": "stable",
      "args": [
        { "name": "x", "description": "X pixel (omit when using --ref).", "type": "number", "required": false },
        { "name": "y", "description": "Y pixel.", "type": "number", "required": false }
      ],
      "example": { "args": ["240", "240", "--target", "agentgauge"], "stdin": "" },
      "output_fields": [
        { "name": "ok", "description": "Tap injected.", "type": "boolean" },
        { "name": "x", "description": "Resolved x.", "type": "number" },
        { "name": "y", "description": "Resolved y.", "type": "number" }
      ] },
    { "name": "swipe", "description": "Inject a swipe from (x1,y1) to (x2,y2) as a moving press, so LVGL registers a gesture (e.g. page navigation).", "mutating": true, "stability": "stable",
      "args": [
        { "name": "x1", "description": "Start X pixel.", "type": "number", "required": true },
        { "name": "y1", "description": "Start Y pixel.", "type": "number", "required": true },
        { "name": "x2", "description": "End X pixel.", "type": "number", "required": true },
        { "name": "y2", "description": "End Y pixel.", "type": "number", "required": true }
      ],
      "example": { "args": ["400", "240", "100", "240", "--target", "agentgauge"], "stdin": "" },
      "output_fields": [
        { "name": "ok", "description": "Swipe injected.", "type": "boolean" }
      ] },
    { "name": "battery", "description": "Set battery level; --charging and --no-vbus set the flags.", "mutating": true, "stability": "stable",
      "args": [ { "name": "pct", "description": "0-100.", "type": "number", "required": true } ],
      "example": { "args": ["50", "--target", "agentgauge"], "stdin": "" } },
    { "name": "rotate", "description": "Set the IMU rotation quadrant (0-3).", "mutating": true, "stability": "stable",
      "args": [ { "name": "quadrant", "description": "0-3.", "type": "number", "required": true } ],
      "example": { "args": ["1", "--target", "agentgauge"], "stdin": "" } },
    { "name": "gpio", "description": "Set a GPIO pin level (read back by digitalRead).", "mutating": true, "stability": "stable",
      "args": [
        { "name": "pin", "description": "GPIO number.", "type": "number", "required": true },
        { "name": "level", "description": "0 or 1.", "type": "number", "required": true }
      ],
      "example": { "args": ["9", "1", "--target", "agentgauge"], "stdin": "" } },
    { "name": "wifi", "description": "Set the simulated Wi-Fi link up or down (read back by WiFi.status()); does not affect first-time provisioning.", "mutating": true, "stability": "stable",
      "args": [ { "name": "state", "description": "up or down.", "type": "string", "required": true, "enum": ["up", "down"] } ],
      "example": { "args": ["down", "--target", "agentgauge"], "stdin": "" } },
    { "name": "ble", "description": "Drive the virtual BLE link of a BLE firmware, standing in for a central/host app. One-shot `ble send` completes the round trip itself: it connects (bonded), delivers the JSON line, and returns the device's replies (add --shot to capture the resulting frame). connect/pair (passkey pairing via --passkey N), disconnect, recv, and hid hold state across commands only inside a run session or scenario. Requires a target whose firmware binds the virtual BLE link; returns bad_args otherwise.", "mutating": true, "stability": "stable",
      "args": [
        { "name": "sub", "description": "connect|pair|disconnect|send|recv|hid.", "type": "string", "required": true, "enum": ["connect", "pair", "disconnect", "send", "recv", "hid"] },
        { "name": "json", "description": "For send: one JSON line for the device.", "type": "string", "required": false }
      ],
      "example": { "args": ["send", "{\"cmd\":\"status\"}", "--target", "<ble-firmware-target>"], "stdin": "" },
      "output_fields": [
        { "name": "ok", "description": "Delivered.", "type": "boolean" },
        { "name": "replies", "description": "For send: JSON lines the device sent back.", "type": "string" }
      ] },
    { "name": "button", "description": "Press a physical button. pwr-long and pwr-release inject the power button's long-press and release edges for hold gestures (advance time with steps between them).", "mutating": true, "stability": "stable",
      "args": [ { "name": "which", "description": "primary|secondary|pwr|pwr-long|pwr-release.", "type": "string", "required": true, "enum": ["primary", "secondary", "pwr", "pwr-long", "pwr-release"] } ],
      "example": { "args": ["primary", "--target", "agentgauge"], "stdin": "" } },
    { "name": "serial", "description": "serial send TEXT feeds device input; serial expect REGEX matches captured output (exit 1 on no match).", "mutating": false, "stability": "stable",
      "args": [
        { "name": "sub", "description": "send or expect.", "type": "string", "required": true, "enum": ["send", "expect"] },
        { "name": "arg", "description": "Text to send, or regex to expect.", "type": "string", "required": true }
      ],
      "example": { "args": ["expect", "ready", "--target", "agentgauge"], "stdin": "" } },
    { "name": "logs", "description": "Print captured device serial output.", "mutating": false, "stability": "stable",
      "example": { "args": ["--target", "agentgauge"], "stdin": "" },
      "output_fields": [ { "name": "serial", "description": "Captured serial text.", "type": "string" } ] },
    { "name": "scenario", "description": "Run a JSON scenario file (ordered steps) headless.", "mutating": true, "stability": "stable",
      "args": [ { "name": "file", "description": "Scenario JSON path.", "type": "string", "required": true } ] },
    { "name": "serve", "description": "Boot and keep pumping so a live bridge can drive the device. HTTP: a bridge POSTs to the firmware's webserver (--port). BLE: --ble-port N exposes the virtual BLE link as newline-delimited JSON on a localhost TCP socket (connect = bonded central, lines in = host->device, device lines stream back; one client at a time). --window opens an interactive SDL window (mouse=touch, on-screen buttons + battery/USB/rotate controls). Human logs on stderr. On a qemu-backed target, serve only boots and pumps serial I/O until interrupted: there is no framebuffer, HTTP webserver, or native BLE link, so --shot, --window, and --ble-port are all rejected as unsupported (see the backend field on list-targets).", "mutating": true, "stability": "stable" },
    { "name": "run", "description": "Persistent agent session: newline-delimited JSON commands on stdin, one JSON reply per line. cmds: boot, ui, tap (ref|x,y), swipe (x1,y1,x2,y2), expect (text/absent/match), button, battery, rotate, gpio, wifi, ble (sub+data/passkey), snapshot, screenshot, steps, serial, logs, quit. One boot per session (a second boot replies already_booted). Error replies use {\"error\":{\"kind\":...,\"message\":...}} with the kinds from errors, plus not_booted and already_booted. Refs from ui stay valid within the session.", "mutating": true, "stability": "stable" }
  ],
  "errors": [
    { "kind": "no_target", "description": "No --target and more than one target registered.", "exit_code": 2 },
    { "kind": "unknown_target", "description": "The --target is not registered (see list-targets).", "exit_code": 2 },
    { "kind": "bind_failed", "description": "serve could not bind the HTTP port (already in use).", "exit_code": 3 },
    { "kind": "ref_not_found", "description": "tap --ref referenced a widget not in the current ui snapshot.", "exit_code": 4 },
    { "kind": "conflict", "description": "An argument or option conflicts with another (e.g. tap given both --ref and x/y).", "exit_code": 5 },
    { "kind": "post_failed", "description": "snapshot could not be delivered to the running target (connect failed or body exceeds the server read size).", "exit_code": 6 },
    { "kind": "unsupported", "description": "The command targets a capability the active board lacks (e.g. battery/rotate on a board without it, or a button the board does not have), or a non-tier-1 command (anything but serial/logs/serve) on a qemu-backed target.", "exit_code": 7 },
    { "kind": "bad_args", "description": "Missing or invalid arguments for the command.", "exit_code": 2 },
    { "kind": "expect_failed", "description": "A scenario/run 'expect' assertion did not hold (text present/absent mismatch).", "exit_code": 8 },
    { "kind": "backend_unavailable", "description": "A qemu-backed target could not boot: no qemu-system-<arch> binary configured (set ESPRITE_QEMU_BIN, or ESPRITE_QEMU_RISCV32/ESPRITE_QEMU_XTENSA per the target's architecture), ESPRITE_QEMU_IMAGE unset or not found (the flash image path), or the child process failed to spawn or never reached a running QMP state.", "exit_code": 2 }
  ]
})JSON";

// Recognized options, shared by option lookup, positional parsing, and the
// strict pre-dispatch validation. A bare `--` ends option parsing, so free-form
// positionals (serial send text, for example) can themselves start with dashes.
static const char* kValOpts[]  = {"--target", "--steps", "--path", "--shot",
                                  "--port", "--interval-ms", "--scale", "--ref",
                                  "--passkey", "--ble-port",
                                  "--output", "-o", "--limit", "--offset", "--fields"};
static const char* kFlagOpts[] = {"--charging", "--no-vbus", "--window", "--json", "--help", "--version"};

static bool is_val_opt(const char* a)  { for (auto* o : kValOpts)  if (!strcmp(a, o)) return true; return false; }
static bool is_flag_opt(const char* a) { for (auto* o : kFlagOpts) if (!strcmp(a, o)) return true; return false; }

static const char* opt_val(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc - 1; ++i) {
        if (!strcmp(argv[i], "--")) break;
        if (!strcmp(argv[i], name)) return argv[i + 1];
    }
    return nullptr;
}
static bool opt_flag(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--")) break;
        if (!strcmp(argv[i], name)) return true;
    }
    return false;
}

// Positional args after the command. Recognized options are skipped; single-dash
// tokens other than -o are preserved (negative coordinates, `serial send -AT`).
static std::string positional(int argc, char** argv, int index) {
    int seen = 0;
    bool opts_ended = false;
    for (int i = 2; i < argc; ++i) {
        if (!opts_ended && !strcmp(argv[i], "--")) { opts_ended = true; continue; }
        if (!opts_ended) {
            if (is_val_opt(argv[i])) { ++i; continue; }
            if (is_flag_opt(argv[i])) continue;
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

// Enforce the schema's argument contract before dispatch: unknown --options are
// rejected instead of silently read as positionals, value options must have a
// value, and numeric option values must parse within range.
static int validate_options(int argc, char** argv) {
    static const struct { const char* name; long min, max; } kNumericOpts[] = {
        {"--steps", 0, 1000000}, {"--port", 0, 65535}, {"--interval-ms", 1, 3600000},
        {"--scale", 1, 16}, {"--limit", 0, 1000000}, {"--offset", 0, 1000000},
        {"--passkey", 1, 999999}, {"--ble-port", 0, 65535},
    };
    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "--")) break;
        bool double_dash = a[0] == '-' && a[1] == '-';
        if (!double_dash && strcmp(a, "-o") != 0) continue;   // free-form positional
        if (!is_val_opt(a) && !is_flag_opt(a))
            return fail("bad_args", std::string("unknown option '") + a + "' (see: esprite schema)", 2);
        if (is_val_opt(a)) {
            if (i + 1 >= argc)
                return fail("bad_args", std::string("option '") + a + "' needs a value", 2);
            for (auto& n : kNumericOpts) {
                long v;
                if (!strcmp(a, n.name) && !to_long(argv[i + 1], n.min, n.max, &v))
                    return fail("bad_args", std::string("option '") + a + "' needs an integer " +
                                std::to_string(n.min) + ".." + std::to_string(n.max), 2);
            }
            ++i;
        }
    }
    return 0;
}

// Boots the resolved target. Returns 0 on success, or a non-zero exit code after
// emitting the error envelope (2 for target problems).
static int boot_or_die(int argc, char** argv) {
    std::string t = resolve_target(argc, argv);
    if (t.empty())
        return fail("no_target", "no --target and more than one target registered", 2);
    const SimTarget* target = sim_target(t);
    if (!target)
        return fail("unknown_target", "unknown target '" + t + "'", 2);
    sim_backend_select(target);
    std::string err;
    // Routes to the backend selected above: native boots in-process and
    // resets UI refs via its boot hook (sim_boot); qemu spawns a child and
    // confirms it over QMP instead. Either way "false" also covers a SIGINT/
    // SIGTERM landing mid-boot (err == "interrupted"), not just a real
    // failure - see sim_interrupted().
    if (!sim_backend().boot(target, &err))
        // target exists, so this is a real boot failure (e.g. qemu env/spawn/QMP),
        // not an unknown key - surface the backend's own message.
        return fail("backend_unavailable", err.empty() ? ("could not boot '" + t + "'") : err, 2);
    return 0;
}

static void maybe_shot(int argc, char** argv) {
    if (const char* out = opt_val(argc, argv, "--shot")) sim_screenshot_png(out);
}

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
int esprite_main(int argc, char** argv) {
    g_interrupted = 0;         // each invocation starts fresh; tests call this repeatedly in-process
    install_signal_handlers(); // before qemu_backend_install: a qemu boot's wait loops must see g_interrupted
    qemu_backend_install(sim_interrupted);   // core never links qemu code directly; this is the one wiring point
    BackendShutdownGuard backend_guard;
    set_output_mode(argc, argv);
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        printf("%s\n", kSchema.c_str());
        return argc < 2 ? 1 : 0;
    }
    std::string cmd = argv[1];

    if (cmd == "schema") { printf("%s\n", kSchema.c_str()); return 0; }
    if (cmd == "--version" || cmd == "version" || opt_flag(argc, argv, "--version")) {
        emit(std::string("{\"name\":\"esprite\",\"version\":\"") + ESPRITE_VERSION + "\"}",
             std::string("esprite ") + ESPRITE_VERSION);
        return 0;
    }
    if (opt_flag(argc, argv, "--help")) { printf("%s\n", kSchema.c_str()); return 0; }
    if (int rc = validate_options(argc, argv)) return rc;

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
            if (want("backend")) o += "\"backend\":\"" + std::string(t->backend == BACKEND_QEMU ? "qemu" : "native") + "\",";
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

    if (cmd == "run") return esprite_daemon(stdin, stdout);

    if (cmd == "scenario") {
        std::string file = positional(argc, argv, 0);
        if (file.empty()) return fail("bad_args", "scenario needs a file path", 2);
        std::string def = resolve_target(argc, argv);
        std::string effective = def.empty() ? "agentgauge" : def;
        // scenario_run stays native-only (it drives the in-process
        // framebuffer/widget tree); gate before ever opening the file, so no
        // QEMU process is spawned for a target this can never support.
        if (const SimTarget* target = sim_target(effective))
            if (target->backend == BACKEND_QEMU)
                return fail("unsupported", "scenario is native-only; '" + effective + "' boots via qemu", 7);
        return scenario_run(file, effective);
    }

    if (cmd == "serve") {
        // Persistent: bind the device webserver on --port, boot, then pump loop()
        // in real time so a live bridge can POST snapshots. Refresh a screenshot
        // every --interval-ms if --shot is given.
        if (const char* p = opt_val(argc, argv, "--port")) setenv("ESPRITE_HTTP_PORT", p, 1);
        std::string t = resolve_target(argc, argv);
        if (t.empty()) return fail("no_target", "no --target and more than one target registered", 2);
        const SimTarget* target = sim_target(t);
        if (!target) return fail("unknown_target", "unknown target '" + t + "'", 2);
        bool is_qemu = target->backend == BACKEND_QEMU;
        // A qemu target has no in-process framebuffer, window, or BLE link to
        // serve - reject before boot so no QEMU process is ever spawned for a
        // request this can never satisfy.
        if (is_qemu && opt_val(argc, argv, "--shot"))
            return fail("unsupported", "--shot needs a framebuffer; '" + t + "' boots via qemu", 7);
        if (is_qemu && opt_flag(argc, argv, "--window"))
            return fail("unsupported", "--window needs a framebuffer; '" + t + "' boots via qemu", 7);
        if (is_qemu && opt_val(argc, argv, "--ble-port"))
            return fail("unsupported", "--ble-port needs the native BLE link; '" + t + "' boots via qemu", 7);
        sim_backend_select(target);
        std::string boot_err;
        if (!sim_backend().boot(target, &boot_err))
            return fail("backend_unavailable", boot_err.empty() ? ("could not boot '" + t + "'") : boot_err, 2);

        const char* shot = opt_val(argc, argv, "--shot");
        const char* port = getenv("ESPRITE_HTTP_PORT");
        // If the target ran an HTTP server but its bind failed (port in use), a
        // live bridge could never reach it. Fail loudly instead of looping. A
        // qemu target never runs the native webserver, so there is nothing to
        // check here.
        if (!is_qemu && sim_http_bind_status() == 0)
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

        if (is_qemu) {
            fprintf(stderr, "esprite: serving '%s' via qemu (serial/logs only; no HTTP snapshot)\n",
                    t.c_str());
        } else {
            // Report the port the target actually bound (matters when --port 0
            // asked the OS for an ephemeral port), not the raw requested value.
            int bound = sim_http_bind_status();
            std::string bound_port = bound > 0 ? std::to_string(bound) : (port ? port : "8080");
            fprintf(stderr, "esprite: serving '%s' at http://127.0.0.1:%s/snapshot%s\n",
                    t.c_str(), bound_port.c_str(),
                    shot ? "" : " (pass --shot to capture frames)");
            if (shot) sim_screenshot_png(shot);
        }

        // Live BLE bridge: expose the virtual link on a localhost socket so a
        // real host process can drive a BLE firmware (newline-delimited JSON).
        // Gated above for qemu targets, so this only ever runs for native.
        BleBridge* ble = nullptr;
        if (const char* bp = opt_val(argc, argv, "--ble-port")) {
            if (!sim_ble_available())
                return fail("unsupported", "this firmware has no BLE (boot a buddy target)", 7);
            ble = ble_bridge_open(atoi(bp));
            if (!ble) return fail("bind_failed", std::string("could not bind BLE bridge port ") + bp, 3);
            fprintf(stderr, "esprite: BLE bridge at tcp://127.0.0.1:%d (newline-delimited JSON)\n",
                    ble_bridge_port(ble));
        }

        // Ctrl-C / SIGTERM exit the loop for an orderly shutdown (window
        // closed, exit code 0) instead of dying mid-frame. Handlers are
        // already installed process-wide (esprite_main, before this command
        // even resolved its target); g_interrupted is guaranteed 0 here since
        // an interrupted boot above would have already returned
        // backend_unavailable instead of reaching this loop.
        auto last = std::chrono::steady_clock::now();
        while (!sim_interrupted()) {
            sim_backend().tick();          // pump handleClient() so POSTs land
            ble_bridge_tick(ble);          // null-safe: shuttle BLE lines
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
        ble_bridge_close(ble);
        return 0;
    }

    // Remaining commands boot a target first. Validate the command name before
    // resolving the target, so a typo'd command is reported as bad_args rather
    // than a misleading target error.
    static const char* kBootCommands[] = {"ui", "screenshot", "snapshot", "tap", "swipe", "button",
                                          "battery", "rotate", "gpio", "wifi", "ble", "serial", "logs"};
    bool known = false;
    for (auto* k : kBootCommands) if (cmd == k) { known = true; break; }
    if (!known)
        return fail("bad_args", "unknown command '" + cmd + "' (try: esprite schema)", 2);
    // Gate before boot_or_die spawns anything: a qemu target only supports
    // serial/logs right now (see qemu_tier1_command), so a command like
    // `screenshot --target qemu_esp32c3` never gets as far as starting QEMU.
    {
        std::string rt = resolve_target(argc, argv);
        if (const SimTarget* rtarget = rt.empty() ? nullptr : sim_target(rt))
            if (rtarget->backend == BACKEND_QEMU && !qemu_tier1_command(cmd))
                return fail("unsupported", "'" + cmd + "' is not supported on qemu-backed target '" + rt + "' (serial/logs only)", 7);
    }
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
        JsonDocument body;
        if (json.empty() || deserializeJson(body, json))
            return fail("bad_args", "snapshot needs a valid JSON body", 2);
        bool ok = sim_wifi_post(opt_val(argc, argv, "--path") ? opt_val(argc, argv, "--path") : "/snapshot", json);
        if (!ok) return fail("post_failed", "snapshot not delivered (target server unbound/unreachable or body too large)", 6);
        sim_settle_ms();   // let the firmware render the injected data
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
            long lx, ly;
            if (!to_long(positional(argc, argv, 0), 0, 100000, &lx) ||
                !to_long(positional(argc, argv, 1), 0, 100000, &ly))
                return fail("bad_args", "tap needs integer x y coordinates (or --ref)", 2);
            x = (int)lx; y = (int)ly;
        }
        if (ActionError e = apply_tap(x, y)) return fail(e.kind, e.msg, kind_exit(e.kind));
        maybe_shot(argc, argv);
        emit("{\"ok\":true" + extra + ",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}",
             "tapped " + std::to_string(x) + "," + std::to_string(y));
        return 0;
    }
    if (cmd == "swipe") {
        long v[4];
        for (int i = 0; i < 4; i++)
            if (!to_long(positional(argc, argv, i), 0, 100000, &v[i]))
                return fail("bad_args", "swipe needs integer x1 y1 x2 y2", 2);
        if (ActionError e = apply_swipe((int)v[0], (int)v[1], (int)v[2], (int)v[3]))
            return fail(e.kind, e.msg, kind_exit(e.kind));
        maybe_shot(argc, argv);
        emit("{\"ok\":true}", "swiped");
        return 0;
    }
    if (cmd == "button") {
        std::string which = positional(argc, argv, 0);
        if (ActionError e = apply_button(which)) return fail(e.kind, e.msg, kind_exit(e.kind));
        maybe_shot(argc, argv);
        emit("{\"ok\":true,\"button\":\"" + json_esc(which) + "\"}", "pressed " + which);
        return 0;
    }
    if (cmd == "battery") {
        long pct;
        if (!to_long(positional(argc, argv, 0), 0, 100, &pct))
            return fail("bad_args", "battery needs a percentage 0-100", 2);
        static const bool kOn = true, kOff = false;
        const bool* charging = opt_flag(argc, argv, "--charging") ? &kOn  : nullptr;
        const bool* vbus     = opt_flag(argc, argv, "--no-vbus")  ? &kOff : nullptr;
        if (ActionError e = apply_battery((int)pct, charging, vbus))
            return fail(e.kind, e.msg, kind_exit(e.kind));
        maybe_shot(argc, argv);
        emit("{\"ok\":true,\"pct\":" + std::to_string(sim_input().battery_pct) + ",\"charging\":" +
             jbool(sim_input().charging) + ",\"vbus\":" + jbool(sim_input().vbus) + "}",
             "battery " + std::to_string(sim_input().battery_pct) + "%");
        return 0;
    }
    if (cmd == "rotate") {
        long q;
        if (!to_long(positional(argc, argv, 0), -1000, 1000, &q))
            return fail("bad_args", "rotate needs a quadrant 0-3", 2);
        if (ActionError e = apply_rotate((int)q)) return fail(e.kind, e.msg, kind_exit(e.kind));
        maybe_shot(argc, argv);
        emit("{\"ok\":true,\"quadrant\":" + std::to_string(sim_input().quadrant) + "}",
             "quadrant " + std::to_string(sim_input().quadrant));
        return 0;
    }
    if (cmd == "gpio") {
        long pin, lvl;
        if (!to_long(positional(argc, argv, 0), -1000, 1000, &pin) ||
            !to_long(positional(argc, argv, 1), -1000, 1000, &lvl))
            return fail("bad_args", "gpio needs a pin 0-63 and a level 0|1", 2);
        if (ActionError e = apply_gpio((int)pin, (int)lvl)) return fail(e.kind, e.msg, kind_exit(e.kind));
        emit("{\"ok\":true,\"pin\":" + std::to_string(pin) + ",\"level\":" + std::to_string(lvl ? 1 : 0) + "}",
             "gpio " + std::to_string(pin) + "=" + std::to_string(lvl ? 1 : 0));
        return 0;
    }
    if (cmd == "wifi") {
        std::string state = positional(argc, argv, 0);
        if (ActionError e = apply_wifi(state)) return fail(e.kind, e.msg, kind_exit(e.kind));
        maybe_shot(argc, argv);
        emit("{\"ok\":true,\"state\":\"" + json_esc(state) + "\"}", "wifi " + state);
        return 0;
    }
    if (cmd == "ble") {
        std::string sub = positional(argc, argv, 0);
        if (sub == "connect") {
            long pk = 0;
            if (const char* p = opt_val(argc, argv, "--passkey")) to_long(p, 1, 999999, &pk);
            if (ActionError e = apply_ble_connect((unsigned)pk)) return fail(e.kind, e.msg, kind_exit(e.kind));
            emit(std::string("{\"ok\":true,\"secure\":") + jbool(sim_ble_secure()) +
                 ",\"passkey\":" + std::to_string(sim_ble_passkey()) + "}",
                 sim_ble_secure() ? "connected (bonded)" : "connected, pairing");
            return 0;
        }
        if (sub == "pair") {
            if (ActionError e = apply_ble_pair()) return fail(e.kind, e.msg, kind_exit(e.kind));
            emit("{\"ok\":true,\"secure\":true}", "paired");
            return 0;
        }
        if (sub == "disconnect") {
            if (ActionError e = apply_ble_disconnect()) return fail(e.kind, e.msg, kind_exit(e.kind));
            emit("{\"ok\":true}", "disconnected");
            return 0;
        }
        if (sub == "send") {
            std::string line = positional(argc, argv, 1);
            JsonDocument probe;
            if (line.empty() || deserializeJson(probe, line))
                return fail("bad_args", "ble send needs a valid JSON line", 2);
            // One-shot invocations boot fresh, so no prior connect can exist:
            // complete the round trip here over the bonded fast path, and
            // return whatever the device sent back.
            if (sim_ble_link_state() != SIM_BLE_CONNECTED)
                if (ActionError e = apply_ble_connect(0)) return fail(e.kind, e.msg, kind_exit(e.kind));
            if (ActionError e = apply_ble_send(line)) return fail(e.kind, e.msg, kind_exit(e.kind));
            std::string replies = "[", text;
            bool first = true;
            for (const std::string& l : sim_ble_host_drain()) {
                JsonDocument p2;
                replies += (first ? "" : ",");
                replies += deserializeJson(p2, l) ? "\"" + json_esc(l) + "\"" : l;
                first = false;
                text += l + "\n";
            }
            replies += "]";
            maybe_shot(argc, argv);
            emit("{\"ok\":true,\"replies\":" + replies + "}", "sent" + (text.empty() ? "" : "\n" + text));
            return 0;
        }
        if (sub == "recv") {
            if (ActionError e = ble_guarded()) return fail(e.kind, e.msg, kind_exit(e.kind));
            std::string items = "[", text;
            bool first = true;
            for (const std::string& l : sim_ble_host_drain()) {
                JsonDocument probe;   // firmware lines are JSON; quote any that are not
                items += (first ? "" : ",");
                items += deserializeJson(probe, l) ? "\"" + json_esc(l) + "\"" : l;
                first = false;
                text += l + "\n";
            }
            items += "]";
            emit("{\"items\":" + items + "}", text.empty() ? "(no lines)" : text);
            return 0;
        }
        if (sub == "hid") {
            if (ActionError e = ble_guarded()) return fail(e.kind, e.msg, kind_exit(e.kind));
            std::string items = "[", text;
            bool first = true;
            for (const SimBleHidEvent& ev : sim_ble_hid_drain()) {
                items += (first ? "" : ",");
                items += std::string("{\"press\":") + jbool(ev.press) +
                         ",\"key\":" + std::to_string(ev.key) +
                         ",\"modifier\":" + std::to_string(ev.modifier) + "}";
                first = false;
                text += std::string(ev.press ? "press" : "release") + " key=" +
                        std::to_string(ev.key) + " mod=" + std::to_string(ev.modifier) + "\n";
            }
            items += "]";
            emit("{\"items\":" + items + "}", text.empty() ? "(no hid events)" : text);
            return 0;
        }
        return fail("bad_args", "ble takes connect|pair|disconnect|send|recv|hid", 2);
    }
    if (cmd == "serial") {
        std::string sub = positional(argc, argv, 0), arg = positional(argc, argv, 1);
        if (sub == "send") {
            sim_backend().serial_inject(arg + "\n"); serial_settle(5);
            emit("{\"ok\":true}", "sent");
            return 0;
        }
        if (sub == "expect") {
            if (!sim_serial_regex_valid(arg))
                return fail("bad_args", "invalid regex '" + arg + "'", 2);
            serial_settle(SERIAL_SETTLE_STEPS);
            bool matched = sim_serial_regex_search(sim_backend().serial_output(), arg);
            emit("{\"matched\":" + std::string(jbool(matched)) + "}", matched ? "matched" : "no match");
            return matched ? 0 : 1;
        }
        return fail("bad_args", "serial takes 'send TEXT' or 'expect REGEX'", 2);
    }
    if (cmd == "logs") {
        std::string s = sim_backend().serial_output();
        emit("{\"serial\":\"" + json_esc(s) + "\"}", s);
        return 0;
    }

    return fail("bad_args", "unknown command '" + cmd + "' (try: esprite schema)", 2);
}

// One error reply on the session stream, same kind/message envelope as the
// one-shot CLI's stderr errors.
static void session_err(FILE* out, const char* kind, const std::string& msg) {
    fprintf(out, "{\"error\":{\"kind\":\"%s\",\"message\":\"%s\"}}\n", kind, json_esc(msg).c_str());
}

// Daemon: a persistent agent session. Read newline-delimited JSON commands from
// `in`, emit one JSON reply per line on `out`. Refs from {"cmd":"ui"} stay valid
// for {"cmd":"tap","ref":...} within the session (the snapshot-ref model).
// Streams are injected so tests can drive a full session in-process.
int esprite_daemon(FILE* in, FILE* out) {
    // esprite_main also does this, but esprite_daemon is a public entry point
    // tests (and future callers) invoke directly - without this, a qemu boot
    // in such a process would find BACKEND_QEMU unregistered and silently
    // fall back to native (core/backend.cpp's sim_backend_select), which
    // trivially "succeeds" instead of surfacing backend_unavailable. The same
    // reasoning applies to the signal-flag wiring: a direct caller's qemu
    // boot needs g_interrupted reset and the handlers installed too, or a
    // SIGINT during it would have nothing to bail its wait loops out early.
    // Idempotent: safe alongside esprite_main's own calls in the same process.
    g_interrupted = 0;
    install_signal_handlers();
    qemu_backend_install(sim_interrupted);
    BackendShutdownGuard backend_guard;   // tests call esprite_daemon directly, bypassing esprite_main
    char line[16384];
    bool booted = false;
    bool is_qemu = false;
    // fgets() below blocks waiting for the next command line, which is where
    // this loop spends nearly all of its time. install_signal_handlers()
    // installs SIGINT/SIGTERM via sigaction with SA_RESTART cleared (not
    // plain signal(), which defaults to SA_RESTART on both Linux and macOS),
    // so a signal landing while fgets() is blocked interrupts its underlying
    // read() with EINTR instead of transparently resuming it; stdio
    // propagates that as fgets() returning NULL, same as real EOF, which
    // this loop already treats as "stop serving" further down. That relies
    // on fgets() itself noticing the interrupt rather than a separate
    // readiness check on the raw fd - an earlier version of this fix polled
    // the fd directly, but that ignored bytes fgets() had already pulled
    // into stdio's internal buffer, deadlocking a session that pipes several
    // commands in one write() without an intervening EOF or signal.
    while (fgets(line, sizeof(line), in)) {
        // A line beyond the buffer would otherwise be parsed as two commands,
        // desyncing 1-in/1-out reply pairing. Drain it and reply with one error.
        if (!strchr(line, '\n') && !feof(in)) {
            int c;
            while ((c = fgetc(in)) != EOF && c != '\n') {}
            session_err(out, "bad_args", "line too long (max " + std::to_string(sizeof(line) - 1) + " bytes)");
            fflush(out);
            continue;
        }
        JsonDocument doc;
        if (deserializeJson(doc, line)) {
            session_err(out, "bad_args", "line is not valid JSON");
            fflush(out);
            continue;
        }
        std::string cmd = doc["cmd"] | "";

        if (cmd == "boot") {
            // One boot per session: re-running a firmware's setup() duplicates
            // its UI state, and lv_init cannot run twice in one process.
            if (booted) {
                session_err(out, "already_booted", "one boot per run session; start a new session to boot again");
            } else {
                std::string t = doc["target"] | "";
                const SimTarget* target = sim_target(t);
                std::string boot_err;
                if (target) {
                    sim_backend_select(target);
                    booted = sim_backend().boot(target, &boot_err);   // sim_boot resets UI refs
                    is_qemu = target->backend == BACKEND_QEMU;
                } else {
                    booted = false;
                }
                if (booted) {
                    fprintf(out, "{\"ok\":true}\n");
                } else if (target) {
                    // target exists, so this is a real boot failure (e.g. qemu
                    // env/spawn/QMP), not an unknown key.
                    session_err(out, "backend_unavailable", boot_err.empty() ? ("could not boot '" + t + "'") : boot_err);
                } else {
                    session_err(out, "unknown_target", "unknown target '" + t + "'");
                }
            }
        } else if (cmd == "quit" || cmd == "exit") {
            break;
        } else if (!booted) {
            session_err(out, "not_booted", "boot a target first");
        } else if (is_qemu && !qemu_tier1_command(cmd)) {
            session_err(out, "unsupported", "'" + cmd + "' is not supported on a qemu-backed target (serial/logs only)");
        } else if (cmd == "ui") {
            fprintf(out, "%s\n", lvgl_snapshot_json().c_str());
        } else if (cmd == "screenshot") {
            const char* path = doc["out"] | "esprite.png";
            sim_settle_ms();   // capture a settled frame, whatever ran before
            bool ok = sim_screenshot_png(path);
            fprintf(out, "{\"ok\":%s,\"path\":\"%s\",\"w\":%d,\"h\":%d}\n",
                    jbool(ok), json_esc(path).c_str(), sim_framebuffer().w(), sim_framebuffer().h());
        } else if (cmd == "tap") {
            int x, y;
            if (doc["ref"].is<const char*>()) {
                std::string ref = doc["ref"] | "";
                if (!lvgl_ref_center(ref, &x, &y)) {
                    session_err(out, "ref_not_found", "no widget with ref " + ref + " in the current ui snapshot");
                    fflush(out);
                    continue;
                }
            } else { x = doc["x"] | 0; y = doc["y"] | 0; }
            if (ActionError e = apply_tap(x, y)) session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true,\"x\":%d,\"y\":%d}\n", x, y);
        } else if (cmd == "swipe") {
            if (ActionError e = apply_swipe(doc["x1"] | 0, doc["y1"] | 0, doc["x2"] | 0, doc["y2"] | 0))
                session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "expect") {
            std::string m = doc["match"] | "exact";
            if (m != "exact" && m != "contains")
                session_err(out, "bad_args", "expect match must be 'exact' or 'contains'");
            else if (ActionError e = apply_expect(doc["text"] | "", doc["absent"] | "", m == "exact"))
                session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "button") {
            if (ActionError e = apply_button(doc["which"] | "primary")) session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "battery") {
            bool charging = doc["charging"] | false, vbus = doc["vbus"] | true;
            if (ActionError e = apply_battery(doc["pct"] | 75,
                                              doc["charging"].is<bool>() ? &charging : nullptr,
                                              doc["vbus"].is<bool>() ? &vbus : nullptr))
                session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "rotate") {
            if (ActionError e = apply_rotate(doc["q"] | 0)) session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "gpio") {
            if (ActionError e = apply_gpio(doc["pin"] | 0, doc["level"] | 0)) session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "wifi") {
            if (ActionError e = apply_wifi(doc["state"] | "up")) session_err(out, e.kind, e.msg);
            else fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "snapshot") {
            std::string body; serializeJson(doc["data"], body);
            bool ok = sim_wifi_post(doc["path"] | "/snapshot", body);
            if (ok) { sim_settle_ms(); fprintf(out, "{\"ok\":true}\n"); }
            else session_err(out, "post_failed", "snapshot not delivered (target server unbound/unreachable or body too large)");
        } else if (cmd == "steps") {
            sim_run_steps(doc["n"] | 10); fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "ble") {
            std::string sub = doc["sub"] | "";
            ActionError e;
            if (sub == "connect")         e = apply_ble_connect(doc["passkey"] | 0u);
            else if (sub == "pair")       e = apply_ble_pair();
            else if (sub == "disconnect") e = apply_ble_disconnect();
            else if (sub == "send") {
                std::string body; serializeJson(doc["data"], body);
                e = (body.empty() || body == "null")
                        ? ActionError{"bad_args", "ble send needs a data object"}
                        : apply_ble_send(body);
            } else if (sub == "recv" || sub == "hid") {
                e = ble_guarded();
                if (!e) {
                    std::string items = "[";
                    bool first = true;
                    if (sub == "recv") {
                        for (const std::string& l : sim_ble_host_drain()) {
                            JsonDocument probe;
                            items += (first ? "" : ",");
                            items += deserializeJson(probe, l) ? "\"" + json_esc(l) + "\"" : l;
                            first = false;
                        }
                    } else {
                        for (const SimBleHidEvent& ev : sim_ble_hid_drain()) {
                            items += (first ? "" : ",");
                            items += std::string("{\"press\":") + jbool(ev.press) +
                                     ",\"key\":" + std::to_string(ev.key) +
                                     ",\"modifier\":" + std::to_string(ev.modifier) + "}";
                            first = false;
                        }
                    }
                    fprintf(out, "{\"items\":%s]}\n", items.c_str());
                }
            } else {
                e = {"bad_args", "ble takes sub connect|pair|disconnect|send|recv|hid"};
            }
            if (e) session_err(out, e.kind, e.msg);
            else if (sub != "recv" && sub != "hid") fprintf(out, "{\"ok\":true}\n");
        } else if (cmd == "serial") {
            std::string sub = doc["sub"] | "";
            if (sub == "send") {
                std::string text = doc["text"] | "";
                sim_backend().serial_inject(text + "\n"); serial_settle(5); fprintf(out, "{\"ok\":true}\n");
            } else if (sub == "expect") {
                std::string rx = doc["regex"] | "";
                if (!sim_serial_regex_valid(rx)) {
                    session_err(out, "bad_args", "invalid regex '" + rx + "'");
                } else {
                    serial_settle(SERIAL_SETTLE_STEPS);
                    fprintf(out, "{\"matched\":%s}\n", jbool(sim_serial_regex_search(sim_backend().serial_output(), rx)));
                }
            } else session_err(out, "bad_args", "serial takes sub send|expect");
        } else if (cmd == "logs") {
            fprintf(out, "{\"serial\":\"%s\"}\n", json_esc(sim_backend().serial_output()).c_str());
        } else {
            session_err(out, "bad_args", "unknown cmd '" + cmd + "'");
        }
        fflush(out);
    }
    return 0;
}
