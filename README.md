# esprite

A host-native ESP32 simulator. It boots ESP32 / Arduino firmware **compiled from
source** on your machine, renders the device display into an offscreen
framebuffer, and drives it with an agent-device-style CLI: screenshots, input
injection (GPIO, buttons, touch), serial, and scripted scenarios.

It is a reusable tool, not tied to any one app. The first onboarded target is the
Clawdmeter `waveshare_amoled_216_c6_wifi` firmware (a board that cannot even
screenshot on real hardware, since it has no PSRAM). A second target, a minimal
generic Arduino_GFX sketch, ships to show the framework runs standard-library
apps with zero app-specific code.

## Quick start

```bash
make build
./build/esprite list-targets
make screenshot TARGET=clawdmeter     # writes clawdmeter.png
make screenshot TARGET=sample_gfx     # writes sample_gfx.png
make test                             # unit + integration tests
```

Requirements: CMake >= 3.20 and a C++17 compiler (Apple clang or gcc/clang on
Linux). LVGL and ArduinoJson are fetched automatically; doctest and stb are
vendored.

## Screenshots

The Clawdmeter target boots the real firmware, so injecting a limits snapshot
drives the genuine data path (HTTP POST to the on-device server, parsed by the
firmware's own handler) and the real UI updates:

```bash
./build/esprite snapshot \
  '{"lim":1,"s5":42,"s5r":180,"s7":10,"s7r":6000,"ctx":55,"cost":1.5,"model":"opus"}' \
  --target clawdmeter --shot limits.png
```

## Live window

For an interactive, iOS-Simulator-style view, run `serve` with `--window`:

```bash
esprite serve --target clawdmeter --port 8080 --window
```

This opens a native SDL2 window that presents the device framebuffer live and
lets you drive it: **mouse** = touch (click and drag), **space** = PRIMARY
button, **tab** = SECONDARY, **p** = PWR, **Esc** = quit. Point a live bridge at
the same port and the real data updates in the window in real time.

The window is optional: it is only compiled when SDL2 is found at configure time
(`brew install sdl2` on macOS). Without SDL2, everything else builds and runs
headless, and `--window` prints a hint. `--scale N` enlarges the window N times.

## CLI

```
esprite <command> [--target NAME] [args]

list-targets                     list onboarded targets
schema                           machine-readable JSON of all commands
screenshot OUT.png [--steps N]   boot, run, write a PNG
snapshot '<json>' [--path P] [--shot OUT]   POST to the device webserver
tap X Y [--shot OUT]             inject a touch
button primary|secondary|pwr [--shot OUT]   press a button
battery PCT [--charging] [--no-vbus] [--shot OUT]
rotate 0..3 [--shot OUT]         set IMU rotation quadrant
gpio PIN LEVEL                   set a GPIO level
serial send 'TEXT'               feed the device serial input
serial expect 'REGEX'            match against captured serial output
logs                             print captured serial output
scenario FILE.json               run a scripted scenario
serve [--window] [--scale N]     boot and keep pumping for a live bridge; --window
                                 opens an interactive SDL window (mouse/keys drive it)
run                              daemon: newline-delimited JSON commands on stdin
```

Scenarios are ordered JSON steps, useful in CI:

```json
{
  "target": "clawdmeter",
  "steps": [
    { "cmd": "screenshot", "out": "01-waiting.png" },
    { "cmd": "snapshot", "data": {"lim":1,"s5":42,"s7":10,"ctx":55,"cost":1.5,"model":"opus"} },
    { "cmd": "screenshot", "out": "02-limits.png" }
  ]
}
```

## Driving it (agent-facing)

`esprite schema` prints the machine-readable clispec contract (commands, args,
output fields, error kinds, exit codes). Commands emit JSON on stdout; logs go to
stderr.

For LVGL targets there is a **snapshot-ref model** like a browser page snapshot:

```bash
esprite ui --target clawdmeter
# [{"ref":"e6","type":"bar","x":36,"y":168,"w":408,"h":24,"value":42}, ...]
esprite tap --ref e6 --target clawdmeter     # tap that widget, not a pixel
```

`ui` returns the live widget tree (refs, type, coords, text, bar/arc values), so
an agent reads the UI structurally instead of guessing pixels. `tap --ref` acts
on a ref; `tap X Y` is the pixel fallback.

The `run` daemon is a persistent session where refs from `ui` stay valid across
the session:

```
{"cmd":"boot","target":"clawdmeter"}
{"cmd":"snapshot","data":{"lim":1,"s5":42,"s7":10,"ctx":55,"cost":1.5,"model":"opus"}}
{"cmd":"ui"}                              # read the updated tree, get refs
{"cmd":"tap","ref":"e6"}                  # act on a ref
{"cmd":"screenshot","out":"out.png"}
{"cmd":"quit"}
```

Raw-GFX targets (no widget tree) return `[]` from `ui` and are driven by pixels +
screenshots.

## How it works

The firmware's own source files are compiled unchanged. Only two things are
swapped: a set of host shims that stand in for the Arduino / ESP-IDF APIs, and a
board layer that binds the app's hardware calls to virtual peripherals.

```
core/          virtual clock + setup()/loop() pump + target registry
shims/         Arduino, ESP-IDF, and networking APIs (host-backed)
peripherals/   framebuffer + PNG screenshot, LVGL glue, Arduino_GFX shim,
               injected input bus
cli/           the esprite CLI
targets/       one folder per onboarded app
```

The runtime is single-threaded and deterministic: `millis()` is driven by
`delay()` and the step count, so a given number of `loop()` iterations always
produces the same frame. Injected input and data are applied between steps.

## Onboarding a target

Add `targets/<name>/` with a `CMakeLists.txt`, a `board.cpp` that registers the
target, and either:

- **nothing else**, if the app only uses APIs the shims already cover (Serial,
  Wire, WiFi, Arduino_GFX). See `targets/sample_gfx/`.
- **a small board adapter**, if the app hides hardware behind its own HAL. See
  `targets/clawdmeter/board_sim/`, which implements Clawdmeter's six HAL
  interfaces against the framebuffer and the injected input bus.

Register the target in `board.cpp`:

```cpp
#include "target.h"
void mysketch_setup();
void mysketch_loop();
static const BoardDesc kBoard = {"My Board", 320, 240, 0, false, false, false};
static const SimTarget kTarget = {"myapp", "description", mysketch_setup, mysketch_loop, &kBoard};
namespace { struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg; }
```

Entry points are renamed at compile time (`setup` -> `myapp_setup`) so multiple
targets coexist in one binary; see the target CMakeLists for the two-line rule.

## Fidelity

**Faithful:** application logic, UI rendering (fonts, icons, animations,
responsive layout), screen and state machines, the app's real data-parsing paths
(for example its HTTP handlers), GPIO and peripheral behavior at the API level,
and NVS persistence.

**Approximated, not faithful:** virtual time rather than RTOS scheduling; instant
faked Wi-Fi with no real radio; no QSPI or panel electrical quirks; no PSRAM
exhaustion; audio is silent.

This is not a substitute for on-hardware QA of timing, radio, or panel behavior.
It is authoritative for the UI, layout, and data-path rendering. A future backend
that runs the real compiled binary (Renode or Espressif-QEMU) behind the same CLI
is possible but not built.

## Notes

- LVGL is pinned to 9.5.0 to match the version the Clawdmeter fonts were patched
  for. LVGL-based targets currently share one LVGL version across the build.
- The Clawdmeter firmware source is referenced read-only from
  `../waveshare/Clawdmeter/firmware/src`; override with `-DCLAWDMETER_SRC=...`.
