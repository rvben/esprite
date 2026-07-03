# esprite

An ESP32 firmware simulator driven by an agent-device-style CLI, with two
backends behind one contract:

- **Host-native (default):** boots ESP32 / Arduino firmware **compiled from
  source** on your machine, renders the device display into an offscreen
  framebuffer, and drives it: screenshots, input injection (GPIO, buttons,
  touch), serial, and scripted scenarios. Fast, deterministic, no
  cross-toolchain needed.
- **QEMU (optional):** boots a **real compiled flash image** under Espressif's
  QEMU fork and drives it through the same CLI: serial for any image, plus
  screenshots and the live window for firmware built against the fork's
  virtual RGB panel. See "The QEMU backend" below.

It is a reusable tool, not tied to any one app. A **firmware** is compiled once
and is board-agnostic (it renders itself from `board_caps()` at runtime); a
**board** target selects the panel it runs on. The first onboarded firmware is
agentgauge, a Wi-Fi Claude usage-limit desk gauge, shown here on its Waveshare
ESP32-S3-Touch-AMOLED-1.8 board (`waveshare_amoled_18`, 480x480). Three more
targets show the breadth, all
with zero app-specific sim code: `sample_gfx` (a generic Arduino_GFX sketch),
and two takes on the Cheap Yellow Display (ESP32-2432S028R, 320x240) - `cyd`,
an Arduino_GFX touch-paint demo, and `cyd_tft`, a touch-button UI written
against the real **TFT_eSPI** library that runs unmodified. Supported display
libraries: LVGL, Arduino_GFX, and TFT_eSPI, plus a touch bus (`sim_touch`) for
non-LVGL sketches.

## Quick start

```bash
make build
./build/esprite list-targets
make screenshot TARGET=sample_gfx     # writes sample_gfx.png
make screenshot TARGET=cyd_tft        # writes cyd_tft.png
make test                             # unit + integration tests
make install PREFIX=~/.local          # optimized build onto your PATH
```

Requirements: CMake >= 3.20 and a C++17 compiler (Apple clang or gcc/clang on
Linux). LVGL and ArduinoJson are fetched automatically; doctest and stb are
vendored.

The `waveshare_amoled_18` target runs the agentgauge firmware, which lives in a separate
checkout: without it the build skips the target (with a CMake warning) and
everything else works. Point `-DAGENTGAUGE_SRC=/path/to/firmware/src` (or
`make build AGENTGAUGE_SRC=...`) at it to enable it. Prebuilt release
binaries carry the generic targets only, for the same reason.

## Screenshots

The `waveshare_amoled_18` target boots the real agentgauge firmware, so injecting a limits snapshot
drives the genuine data path (HTTP POST to the on-device server, parsed by the
firmware's own handler) and the real UI updates:

```bash
./build/esprite snapshot \
  '{"lim":1,"s5":42,"s5r":180,"s7":10,"s7r":6000}' \
  --target waveshare_amoled_18 --shot limits.png
```

## Live window

For an interactive, iOS-Simulator-style view, run `serve` with `--window`:

```bash
esprite serve --target waveshare_amoled_18 --port 8080 --window
```

This opens a native SDL2 window: the device screen, pixel-exact, inside a slim
device bezel. The board's physical buttons appear as clickable nubs on the
bezel edge at their declared positions (each target's `board.cpp` places
them); hover a nub for its label and keyboard shortcut, press `?` for the
full key list, and press `` ` `` (backtick) for the hardware-controls panel
(battery level and charging/USB toggles, rotation) on boards that have them.
**Mouse on the screen** = touch (click and drag); each button also has a
board-declared key (waveshare_amoled_18: **space** = PRIMARY, **tab** = SECONDARY,
**p** = PWR); **Esc** closes an open overlay, then quits. PWR follows the
hardware's hold semantics: a quick press or click is a short press; holding
past 1.5 s emits the long-press edge (for a firmware's hold-release gesture).
Bezel chrome renders at desktop density, so `--scale N` enlarges the screen
without blowing up the controls. Ctrl-C stops `serve` cleanly.

For BLE firmwares, `serve --ble-port N` additionally exposes the virtual BLE
link as newline-delimited JSON on a localhost TCP socket: connecting acts as a
bonded central, lines written go to the device, and the device's lines stream
back. Any host process (a companion app via a small adapter, a script, even
`nc`) can drive the simulated device live (point `--target` at a BLE firmware
that binds the virtual link):

```bash
esprite serve --target <ble-firmware-target> --ble-port 9091 --window &
printf '{"cmd":"status"}\n' | nc 127.0.0.1 9091
``` Point a live bridge at
the same port and the real data updates in the window in real time.

The window is optional: it is only compiled when SDL2 is found at configure time
(`brew install sdl2` on macOS). Without SDL2, everything else builds and runs
headless, and `--window` prints a hint. `--scale N` enlarges the window N times.

## CLI

```
esprite <command> [--target NAME] [args]

list-targets                     list onboarded targets
schema                           machine-readable JSON of all commands
--version                        print name and version
ui                               snapshot the LVGL widget tree (refs for tap --ref)
screenshot OUT.png [--steps N]   boot, run, write a PNG
snapshot '<json>' [--path P] [--shot OUT]   POST to the device webserver
tap X Y | tap --ref eN [--shot OUT]         inject a touch
button primary|secondary|pwr [--shot OUT]   press a button; pwr-long / pwr-release
                                 inject the power button's hold-gesture edges
battery PCT [--charging] [--no-vbus] [--shot OUT]
rotate 0..3 [--shot OUT]         set IMU rotation quadrant
motion [--shot OUT]              inject one accelerometer wake nudge (needs an IMU board)
gpio PIN LEVEL                   set a GPIO level
ble connect|pair|disconnect|send|recv|hid   drive a BLE firmware's virtual link
                                 (connect [--passkey N], send '<json>', recv lines,
                                 hid captured keyboard reports)
serial send 'TEXT'               feed the device serial input
serial expect 'REGEX'            match against captured serial output
logs                             print captured serial output
scenario FILE.json               run a scripted scenario
serve [--window] [--scale N]     boot and keep pumping for a live bridge; --window
                                 opens an interactive SDL window (mouse/keys drive it)
run                              daemon: newline-delimited JSON commands on stdin
```

Errors are structured (`{"error":{"kind":...,"message":...}}` on stderr) with
documented exit codes per kind; see `esprite schema`.

Scenarios are ordered JSON steps, useful in CI:

```json
{
  "target": "waveshare_amoled_18",
  "steps": [
    { "cmd": "screenshot", "out": "01-waiting.png" },
    { "cmd": "snapshot", "data": {"lim":1,"s5":42,"s5r":180,"s7":10,"s7r":6000} },
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
esprite ui --target waveshare_amoled_18
# [{"ref":"e6","type":"bar","x":36,"y":168,"w":408,"h":24,"value":42}, ...]
esprite tap --ref e6 --target waveshare_amoled_18   # tap that widget, not a pixel
```

`ui` returns the live widget tree (refs, type, coords, text, bar/arc values), so
an agent reads the UI structurally instead of guessing pixels. `tap --ref` acts
on a ref; `tap X Y` is the pixel fallback.

The `run` daemon is a persistent session where refs from `ui` stay valid across
the session (one boot per session; `steps` advances virtual time explicitly):

```
{"cmd":"boot","target":"waveshare_amoled_18"}
{"cmd":"snapshot","data":{"lim":1,"s5":42,"s5r":180,"s7":10,"s7r":6000}}
{"cmd":"ui"}                              # read the updated tree, get refs
{"cmd":"tap","ref":"e6"}                  # act on a ref
{"cmd":"steps","n":50}                    # run 50 loop() iterations
{"cmd":"screenshot","out":"out.png"}
{"cmd":"quit"}
```

Raw-GFX targets (no widget tree) return `[]` from `ui` and are driven by pixels +
screenshots.

## The QEMU backend

Every target above runs host-native. The `qemu_esp32c3` target instead boots a
real ESP32-C3 flash image under Espressif's QEMU fork. It sees what the
host-native backend cannot: the real RTOS scheduler, real heap pressure,
watchdogs, binary-only components, and it runs images you did not build
yourself.

```bash
make qemu-fetch        # pinned prebuilt Espressif QEMU (no source build)
ESPRITE_QEMU_IMAGE=path/to/flash.bin \
  ./build/esprite serial expect 'Hello world' --target qemu_esp32c3
make qemu-fixtures     # scripted demo images (needs docker + arduino-cli)
make qemu-test         # gated integration tests (self-skip without QEMU)
```

Tier 1 (any image): `serial`, `logs`, and headless `serve` work; every other
command degrades explicitly to `unsupported`, exactly like a board without a
battery rejects `battery`. Execution is deterministic on ESP32-C3 (icount:
same image, same serial bytes, every run); ESP32/S3 (Xtensa) run wall-clock
only in the current fork release. `list-targets` reports each target's
`backend`, and a missing emulator or image yields the `backend_unavailable`
error kind with the missing piece named.

Tier 2 (cooperating firmware) adds the display and input: build the firmware
against Espressif's `esp_lcd_qemu_rgb` component and boot it on
`qemu_esp32c3_rgb` (320x240 virtual RGB panel), and `screenshot`,
`serve --shot`, and the live `--window` work exactly as on native targets,
fed by QMP screendump. Draw full frames: the virtual panel consumes one
pending draw per host-side capture, so per-line drawing stalls headless
firmware.

Input is the same cooperation model: the firmware runs esprite's tiny
`esprite_qemu_agent` component (tools/qemu/esprite_qemu_agent, one task on
UART1), and `tap`, `swipe`, `gpio`, and `button` inject through it - the
fork emulates no GPIO or touch hardware, so the firmware polls the agent's
APIs (`esprite_agent_touch_events`, `esprite_agent_gpio_events`) instead of
the hardware drivers. `scenario` runs on qemu targets too: `settle` is the
portable time verb, and the `pixel` step (a framebuffer assertion with a
retry deadline) plus byte-exact screenshot goldens make emulator UI tests
deterministic; see `scenarios/qemu_esp32c3_rgb.json` for a
tap-press-and-post example against the bundled fixture.

Networking closes the loop: the machine emulates an OpenCores ethernet, and
a board spec with `"http": {"guest_port": N}` gets user-mode networking with
a localhost port forwarded into the guest, so `snapshot` POSTs into the
firmware's real HTTP server (lwIP over the emulated NIC; build with
`CONFIG_ETH_USE_OPENETH=y`). `serve` prints the forwarded URL for live
bridges.

Qemu targets are data, not code: `targets/qemu/*.json` (key, machine, arch,
optional display dimensions, agent flag, buttons, http capability) ship
inside the binary, and `ESPRITE_QEMU_BOARD=/path/to/board.json` registers
your own board at runtime without a rebuild. Board-spec buttons render as
bezel nubs in `--window` (view-only on qemu: window clicks do not route
through the agent yet).

### Running a real LVGL app under the emulator

`tools/qemu/lvgl_demo` is a genuine LVGL 9 application (a two-screen device
control panel) proving the recipe end to end - same board spec as the rgb
fixture, different image (`ESPRITE_QEMU_IMAGE` selects the firmware):

- Registry deps: `lvgl/lvgl ^9`, `espressif/esp_lvgl_port ^2`,
  `espressif/esp_lcd_qemu_rgb ^1`; esprite components:
  `esprite_qemu_agent` (input transport) and `esp_lcd_touch_esprite` (the
  standard `esp_lcd_touch` driver contract over the agent, so touch reaches
  LVGL through a normal indev driver, no esprite-specific app code).
- Display rule: full-refresh mode with a single static full-frame buffer
  (the emulated panel consumes one draw per host capture; partial flushes
  stall). On RAM-tight chips allocate the buffer statically and wire
  `lv_display_create`/`set_buffers`/`set_flush_cb` yourself -
  `lvgl_port_add_disp` only heap-allocates - keeping `esp_lvgl_port` for
  task, tick, and locking.
- esprite pumps display captures around every injection, so a UI task
  blocked in a flush still observes taps; `scenarios/qemu_esp32c3_rgb_lvgl.json`
  taps its switch and slider, presses BOOT, and byte-compares the frames.

## What each backend is authoritative for

The two backends answer different questions. Host-native compiles the
firmware's source against shims: it is the fast, deterministic authority on
the app's own behavior, and it can see inside (the `ui` snapshot-ref model
walks the live LVGL tree). QEMU runs the real compiled image: it is the
authority on everything below the app that shims cannot reproduce, at the
cost of wall-clock timing and firmware cooperation for anything beyond
serial.

| Capability | Host-native | QEMU tier 1 (any image) | QEMU tier 2 (cooperating firmware) |
|---|---|---|---|
| App logic, UI rendering, data parsing | authoritative | runs, observable via serial | runs, observable via display |
| RTOS scheduling, heap/stack pressure, watchdogs | not visible | authoritative | authoritative |
| Toolchain/arch bugs, binary-only components, unbuildable images | no | yes | yes |
| Serial, logs | yes | yes | yes |
| Display (`screenshot`, `serve --shot/--window`) | yes | no | yes (`esp_lcd_qemu_rgb`) |
| Input (`tap`, `swipe`, `gpio`, `button`) | yes | no | yes (`esprite_qemu_agent`) |
| HTTP `snapshot` | yes | no | yes (openeth + port forward) |
| `ui` widget refs | yes | no | no (out of process, inherent) |
| BLE (virtual link, `--ble-port`) | yes | no | no (not emulated upstream) |
| `battery`, `rotate`, `motion` | yes | no | no |
| Time control | `steps` (exact loop iterations) | `settle` (wall-clock) | `settle` (wall-clock) |
| Determinism | fully deterministic (virtual clock) | ESP32-C3: byte-exact serial across runs; ESP32/S3: wall-clock, load-sensitive | same per architecture |
| Boot speed | milliseconds | seconds | seconds |

Tier 2 is a firmware choice, not an esprite switch: build against the
QEMU-facing components listed above and declare the matching capabilities in
the board spec. Anything a target cannot do fails as `unsupported` with the
missing piece named, never silently.

## How it works

The firmware's own source files are compiled unchanged. Only two things are
swapped: a set of host shims that stand in for the Arduino / ESP-IDF APIs, and a
board layer that binds the app's hardware calls to virtual peripherals.

```
core/          virtual clock + setup()/loop() pump + target registry +
               the SimBackend seam (native vs qemu)
shims/         Arduino, ESP-IDF, and networking APIs (host-backed)
peripherals/   framebuffer + PNG screenshot, LVGL glue, Arduino_GFX shim,
               injected input bus
backends/      the QEMU backend: child process driven over QMP + stdio serial
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
  `firmwares/agentgauge/board_sim/`, which implements agentgauge's HAL
  interfaces (display, touch, power, input, board_caps) against the
  framebuffer and the injected input bus.

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
responsive layout), screen and state machines, the app's real data-parsing
paths (its HTTP handlers), GPIO and peripheral behavior at the API level, and
NVS persistence. A generic virtual BLE link (`peripherals/ble`) is also
available for a firmware that binds it, with its own protocol parser and HID
mapping run for real against the link; no onboarded target uses it today.

**Approximated, not faithful:** virtual time rather than RTOS scheduling; instant
faked Wi-Fi and BLE with no real radio (no MTU fragmentation, advertising
intervals, or bonding storage); no QSPI or panel electrical quirks; no PSRAM
exhaustion; audio is silent.

This is not a substitute for on-hardware QA of timing, radio, or panel behavior.
The host-native backend is authoritative for UI, layout, and data-path
rendering. The QEMU backend runs the real compiled binary and is authoritative
for serial-observable firmware behavior (RTOS scheduling, heap, watchdogs,
binary-only components); it has no display, input, or networking yet.

## Notes

- LVGL is pinned to 9.5.0 for a stable core API surface. LVGL-based targets
  currently share one LVGL version across the build.
- The agentgauge firmware source is referenced read-only from
  `../agentgauge/firmware/src`; override with `-DAGENTGAUGE_SRC=...`.
