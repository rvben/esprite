# Integrating esprite into your firmware project

The proven pattern: your firmware repo treats esprite as a **test harness**.
UI tests live next to the firmware as scenario files, your Makefile builds
esprite pointed at your source, and CI runs each scenario headless. This is
how the agentgauge firmware is tested daily.

## The harness Makefile

```makefile
ESPRITE ?= $(HOME)/Projects/esprite

esprite:
	$(MAKE) -C "$(ESPRITE)" build MYAPP_SRC="$(CURDIR)/firmware/src"

test-ui: esprite
	@for f in uitests/*.json; do echo "== $$f"; \
	  ESPRITE_HTTP_PORT=0 "$(ESPRITE)/build/esprite" scenario "$$f" || exit 1; done
```

Notes that keep this reliable:

- `MYAPP_SRC` is whatever cache variable your `firmwares/<app>/CMakeLists`
  declares (see [onboarding.md](onboarding.md), path B). esprite reads the
  source read-only; your repo stays the single source of truth.
- `ESPRITE_HTTP_PORT=0` binds an ephemeral port for the firmware's simulated
  webserver - a fixed port turns a leftover listener into flaky tests.
- `ESPRITE_STATE_DIR` isolates NVS/Preferences state per test run if your
  firmware persists anything.

## Scenario files

One JSON file per behavior, ordered steps, structured failures (the first
failing step's kind and message become the exit):

```json
{
  "target": "my_board",
  "steps": [
    { "cmd": "snapshot", "data": { "temp": 21.5 } },
    { "cmd": "expect", "text": "21.5" },
    { "cmd": "tap", "x": 230, "y": 100 },
    { "cmd": "pixel", "x": 70, "y": 100, "value": 2016, "timeout_ms": 5000 },
    { "cmd": "screenshot", "out": "after-tap.png" }
  ]
}
```

### Step reference

| Step | Fields | Native | QEMU |
|---|---|---|---|
| `snapshot` | `data` (JSON body), `path` (default `/snapshot`) | yes | needs `http` capability |
| `screenshot` | `out` | yes | needs `display` |
| `pixel` | `x`, `y`, `value` (RGB565 0..65535), `timeout_ms` (default 5000) - retries until match | yes | needs `display` |
| `tap` / `swipe` | `x`,`y` / `x1`,`y1`,`x2`,`y2` | yes | needs `agent` |
| `button` | `which`: `primary`\|`secondary`\|`pwr`\|`pwr-long`\|`pwr-release` or a board button label | yes | labels, needs `agent` |
| `gpio` | `pin`, `level` | yes | needs `agent` |
| `serial` | `expect` and/or `absent` (regex over captured output) | yes | yes |
| `settle` | `ms` (1..60000) - the portable time verb | yes | yes |
| `steps` | `n` loop() iterations (exact virtual time) | yes | no (use `settle`) |
| `expect` | `text`/`absent`, `match`: `exact`\|`contains` (reads the LVGL widget tree) | yes | no (use `pixel`/`serial`) |
| `battery` / `rotate` / `motion` | `pct`+flags / `q` / - | board-gated | no |
| `wifi` | `state`: `up`\|`down` | yes | no |
| `ble` | `sub`: `connect`\|`pair`\|`disconnect`\|`send` | firmware-gated | no |

`pixel` is the golden primitive for QEMU targets (wall-clock guests redraw
asynchronously; the retry deadline absorbs that). For byte-exact image
goldens, capture `screenshot` steps after a `pixel` barrier and compare the
files - esprite's own emulator goldens (`tests/goldens/qemu/`) work exactly
this way and have proven byte-stable across independent boots, antialiased
text included.

## Driving it from an agent or by hand

`esprite schema` prints the machine-readable contract (commands, arguments,
error kinds, exit codes). The `run` session keeps one boot alive across
newline-delimited JSON commands, and on LVGL targets `ui` returns widget
refs you can tap directly:

```
{"cmd":"boot","target":"my_board"}
{"cmd":"ui"}                       -> [{"ref":"e6","type":"bar","value":42}, ...]
{"cmd":"tap","ref":"e6"}
{"cmd":"screenshot","out":"out.png"}
{"cmd":"quit"}
```

For an interactive view while developing, `serve --window` opens the bezel
window against the same live instance.

## CI

Host-native (your firmware compiles into esprite):

```yaml
jobs:
  ui-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/checkout@v4
        with: { repository: rvben/esprite, path: esprite }
      - run: sudo apt-get update && sudo apt-get install -y cmake g++
      - run: make -C esprite build MYAPP_SRC="$GITHUB_WORKSPACE/firmware/src"
      - run: |
          for f in uitests/*.json; do
            ESPRITE_HTTP_PORT=0 esprite/build/esprite scenario "$f"
          done
```

QEMU (your CI already builds a flash image): the release binary carries the
qemu board specs, and the esprite checkout provides the emulator-fetch
script. Pin both to the same version:

```yaml
      - uses: actions/checkout@v4
        with: { repository: rvben/esprite, path: esprite, ref: v0.4.5 }
      - run: |
          V=0.4.5
          curl -sL "https://github.com/rvben/esprite/releases/download/v$V/esprite-$V-linux-x86_64.tar.gz" | tar xz
          sudo apt-get update && sudo apt-get install -y libsdl2-2.0-0 libslirp0 libpixman-1-0
      - run: make -C esprite qemu-fetch          # cache esprite/.qemu between runs
      - run: |
          . esprite/.qemu/env.sh
          ESPRITE_QEMU_IMAGE=build/my_firmware.bin \
            esprite-0.4.5-linux-x86_64/esprite scenario uitests/smoke.json
```

esprite's own informational workflow (`.github/workflows/qemu.yml`) is a
complete worked example of the caching (emulator tarball + docker-built
images keyed on their input hashes).

## Optional HTML front ends

The SDL window is optional; an application can keep an HTML device view and
use an Esprite runner as its backend. Run one persistent `esprite run` process
per simulated device and serialize JSON request/reply transactions from a local
HTTP bridge. Use `button`/`gpio` for physical controls, `serial send` for the
firmware protocol, `steps` for virtual time, `logs` for captured events, and
`screenshot` for the actual firmware framebuffer. Serve the completed PNG via
an atomic file replacement so browser requests never see a partial capture.

Keep the application-specific HTML, weather/data sources and firmware protocol
adapter in the consumer project. This preserves its product design while
Esprite supplies the same firmware execution backend used by headless tests
and the SDL window. Bind the HTTP bridge to loopback, validate Host/Origin for
mutating requests, use separate simulator storage, and label injected hardware
values and unsupported peripherals explicitly. Do not label host-native audio
or radio behavior as physically validated.

Daemon requests are bounded at 128 KiB, allowing a complete 40 KiB base64
frame in one `serial send` transaction. A larger request is rejected as one
error and drained before the next request, preserving response pairing.

A complete runnable bridge is included in [examples/html-studio](../examples/html-studio/README.md):

```sh
python3 examples/html-studio/server.py --runner ./build/esprite --target cyd
```

It discovers native display targets and their physical controls, serializes
runner transactions, bounds requests and replies, handles runner failure and
recovery, and serves a local HTML interface without third-party Python packages.
Use `--runner` with a per-project executable to keep your firmware out of Esprite.
