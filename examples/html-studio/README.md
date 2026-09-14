# HTML studio

An optional, dependency-free browser front end for a **host-native** Esprite
runner. It executes the same firmware as the CLI and SDL window, displays the
actual framebuffer, and discovers physical button labels from board metadata.
The Paperplane desk companion inspired this integration pattern; this example
contains no Paperplane firmware, device assumptions, or weather service.

## Run

Requires Python 3.9+ and an Esprite runner built from this version or newer.
From the checkout:

```sh
make build
python3 examples/html-studio/server.py --runner ./build/esprite --target cyd
```

Open the printed loopback URL. An available port is chosen automatically. To
keep the address and simulated NVS stable across launches:

```sh
python3 examples/html-studio/server.py --runner ./build/esprite \
  --target cyd --port 8765 --state-dir ./html-state
```

For your own firmware, substitute the executable produced by
`esprite_add_runner` and its target key. No source changes to the bridge are
needed. A release tarball includes this folder under `examples/html-studio`.
The bridge uses the binary supplied to `--runner`; install no Python packages.

## Use

- Choose a board to start a fresh runner process. Restart also creates a fresh
  process, so LVGL initializes only once. Each target has separate storage.
- Click the display to inject touch, or use the keyboard-accessible X/Y form.
  Buttons come from the runner's `list-targets` metadata. Firmware decides what
  a touch or button does; a board need not consume every injected input.
- Pause automatic stepping, then Step once to call the firmware loop. Screenshot
  capture also settles the display. Virtual time depends on firmware behavior;
  this is not a wall-clock simulator or a CPU debugger.
- Send serial text using your firmware's own protocol. Input is bounded to
  60,000 UTF-8 bytes. Output shows the latest 16 KB captured by the runner.
- Save PNG exports the actual framebuffer. Battery input appears only on boards
  declaring battery support; it is an injected value, not a physical measurement.

The example intentionally supports native display targets. QEMU lifecycle,
capability discovery and guest pacing need a different integration; use its CLI
or SDL viewer. Radio, microphone and audio fidelity depend on the runner shims.
A successful UI interaction does not establish electrical or timing accuracy.

## Bridge contract and customization

`Session` owns one child process. A lock serializes complete command/capture/log
transactions so browser requests and the automatic pump cannot interleave RPC
replies. Replies have a deadline and size cap; a stalled or malformed runner is
terminated and can be restarted from the page. PNG bytes are replaced under the
same lock, so readers receive complete captures. Shutting down the server also
reaps its child. Temporary state is removed on exit unless `--state-dir` is set.

`GET /api/state` returns target metadata, session state, bounded serial output,
a framebuffer revision and an ephemeral request token. `GET /frame.png` returns
the latest image. `POST /api/action` accepts the explicit actions in
`Session.action`; it does not forward arbitrary daemon commands or file paths.
The HTTP server binds to IPv4 loopback, checks Host and Origin, requires a token
for mutations, limits request bodies and serves an explicit asset allowlist.
It is a local development tool, not an Internet-facing service. Do not expose it
through a proxy or tunnel.

Customize `index.html`, `style.css` and `app.js` to give your firmware its own
interface. Keep application-specific data and protocol adapters in your firmware
project. All assets here are local; there are no fonts, analytics or CDNs to load.

## Tests

The real-runner integration tests are part of CTest when Python 3.9+ is found:

```sh
make test
# Or run just the bridge checks against a chosen runner:
ESPRITE_TEST_RUNNER="$PWD/build/esprite" python3 tests/test_html_studio.py
```

They cover metadata, actual PNG capture, touch changing pixels, board buttons,
a 40 KB serial payload, request rejection, target switching, runner failure and
recovery. No physical device is required.
