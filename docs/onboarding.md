# Onboarding your firmware

esprite runs firmware two ways, and onboarding differs accordingly:

- **Host-native**: your firmware's C++ source compiles into esprite against
  host shims. Fast, deterministic, deep introspection (`ui` widget refs, all
  injection commands). Requires source and a few lines of glue.
- **QEMU**: esprite boots your compiled flash image under Espressif's QEMU
  fork. Real RTOS/heap/toolchain behavior, works on images you cannot build,
  but display/input/networking need the firmware built with cooperation
  components. No esprite code changes at all - a board spec JSON is enough.

The README's fidelity matrix ("What each backend is authoritative for") is
the decision table. Many projects want both.

## Host-native, path A: a standard sketch (no custom HAL)

If the app only uses APIs the shims already cover (Serial, Wire, GPIO,
Preferences, WiFi/WebServer, Arduino_GFX, TFT_eSPI, LVGL), onboarding is a
directory with two files plus one list edit. `targets/sample_gfx/` and
`targets/cyd/` are living examples.

1. **`targets/<name>/board.cpp`** - describes the hardware and registers the
   target (this is real, current code from `targets/sample_gfx/board.cpp`):

```cpp
#include "target.h"

// Entry points from sketch.cpp, renamed at compile time so they do not clash
// with other targets in the same binary. See this target's CMakeLists.txt.
void samplegfx_setup();
void samplegfx_loop();

// A bare sketch with no physical controls: the window shows just the screen.
static const BoardDesc kBoard = {
    "Generic GFX 320x240", 320, 240,
    false, false, false,   // has_rotation, has_battery, has_imu
    nullptr, 0,            // no buttons
};

static const SimTarget kTarget = {
    "sample_gfx",
    "Minimal Arduino_GFX sketch (generality proof, zero app-specific sim code)",
    samplegfx_setup,
    samplegfx_loop,
    &kBoard,
};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
```

   Physical controls are declared as `SimButton`s and become clickable bezel
   nubs in `--window`, keyboard shortcuts, and `button <label>` targets:

```cpp
static const SimButton kButtons[] = {
    // label      action         gpio  key   edge        pos (0..1 along edge)
    {"PRIMARY",   ACT_PRIMARY,   0,    ' ',  EDGE_RIGHT, 0.30f},
    {"BOOT",      ACT_GPIO,      9,    'b',  EDGE_RIGHT, 0.70f},
};
// then in BoardDesc: ..., kButtons, 2,
```

   Actions: `ACT_PRIMARY`/`ACT_SECONDARY` feed the input bus buttons,
   `ACT_PWR` emits power-button edges (press/long/release), `ACT_GPIO`
   drives the pin so `digitalRead(gpio)` sees it. `edge` and `pos` place the
   nub; a trailing `active_low` field (default `false`) sets the press
   polarity for `ACT_GPIO`.

2. **`targets/<name>/CMakeLists.txt`** (from `targets/sample_gfx/`):

```cmake
add_sim_target(sample_gfx
    ${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp
)

# Rename the Arduino entry points so they do not clash with other targets.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/sketch.cpp
    TARGET_DIRECTORY target_sample_gfx
    PROPERTIES COMPILE_DEFINITIONS "setup=samplegfx_setup;loop=samplegfx_loop")
```

3. **Register the directory** in the root `CMakeLists.txt` target list
   (`foreach(_t waveshare_amoled_18 sample_gfx cyd cyd_tft)` - add yours).

4. `make build && ./build/esprite screenshot --target <name>` - done.

## Host-native, path B: firmware with its own HAL

A real product usually hides hardware behind its own interfaces (a
`hal/display.h`, a board-init file). Then the firmware compiles ONCE into a
library and a small **board adapter** implements those interfaces against
esprite's virtual peripherals. `firmwares/agentgauge/` is the living
example; the shape:

- **`firmwares/<app>/CMakeLists.txt`** builds `<app>_fw` from the firmware's
  own sources, referenced read-only from an out-of-tree checkout via a cache
  variable (`set(<APP>_SRC ... CACHE PATH ...)` in the root CMakeLists, so
  `make build <APP>_SRC=/path/to/src` works). List the simulated sources
  explicitly and keep a second list of deliberately unsimulated ones (the
  real board bring-up and hardware drivers your adapter replaces); a
  configure-time check that warns about unlisted new files turns firmware
  drift into a readable message instead of a linker error.
- **`firmwares/<app>/board_sim/`** implements the firmware's HAL headers
  against `sim_framebuffer()`, the injected input bus (`sim_input()`), and
  the shims. This is the only app-specific simulator code.
- **`targets/<board>/`** is then just a `board.cpp` (as in path A) plus
  `add_board_variant(<board> <app>_fw)` - and because the adapter reads the
  active target's `BoardDesc` at runtime (`board_caps()` pattern), several
  boards of the same firmware coexist in one binary.
- Entry points rename exactly as in path A (`setup=<app>_setup`).

Point of the split: the firmware never changes for the simulator, and a new
board variant is one small `board.cpp`.

## QEMU: any firmware, no esprite changes

A qemu target is data. Boot any ESP32-C3 image on the built-in serial-only
board today:

```bash
make qemu-fetch
ESPRITE_QEMU_IMAGE=path/to/flash.bin ./build/esprite serial expect 'boot' --target qemu_esp32c3
```

For display/input/networking (tier 2), build the firmware with the
cooperation components - the README's "Running a real LVGL app under the
emulator" section is the complete recipe, with `tools/qemu/lvgl_demo` as the
living example - and describe the board in JSON:

```json
{
  "key": "my_board",
  "name": "My Board (QEMU)",
  "description": "My firmware on the emulated C3",
  "machine": "esp32c3",
  "arch": "riscv32",
  "display": { "width": 320, "height": 240 },
  "agent": true,
  "buttons": [ { "label": "BOOT", "gpio": 9, "key": "b" } ],
  "http": { "guest_port": 80 }
}
```

Every capability block is optional and gates the matching commands (no
`display` = no screenshot, no `agent` = no tap/gpio/button, no `http` = no
snapshot). Register it without rebuilding esprite:

```bash
ESPRITE_QEMU_BOARD=/path/to/my_board.json esprite list-targets
```

Ship-with-the-binary specs live in `targets/qemu/*.json`.

## Checklist

- [ ] `list-targets` shows the target with the right dimensions and buttons.
- [ ] `screenshot` renders; `serve --window` shows the bezel and nubs.
- [ ] A scenario with a `pixel` assertion passes (see
      [integration.md](integration.md) for the step reference).
- [ ] Native targets: `ESPRITE_HTTP_PORT=0` in tests (ephemeral port);
      state isolation via `ESPRITE_STATE_DIR` if the firmware uses NVS.
