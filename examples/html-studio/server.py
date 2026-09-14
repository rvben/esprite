#!/usr/bin/env python3
"""Local, dependency-free HTML front end for a host-native Esprite runner."""

import argparse
import hashlib
import json
import os
import secrets
import selectors
import shutil
import signal
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ASSETS = Path(__file__).resolve().parent
MAX_BODY = 100_000
MAX_REPLY = 4 * 1024 * 1024


class StudioError(Exception):
    pass


class Session:
    def __init__(self, runner, state_dir):
        self.runner = shutil.which(runner) or str(Path(runner).resolve())
        self.state_dir = Path(state_dir)
        self.lock = threading.RLock()
        self.process = None
        self.selector = None
        self.buffer = bytearray()
        self.reply_timeout = 10
        self.target = None
        self.running = False
        self.error = ""
        self.logs = ""
        self.png = b""
        self.frame = 0
        self.updated = None
        self.stop = threading.Event()
        try:
            result = subprocess.run(
                [self.runner, "list-targets", "--json"],
                capture_output=True,
                timeout=10,
                check=True,
            )
            self.targets = [
                t
                for t in json.loads(result.stdout)["items"]
                if t.get("backend") == "native"
                and t.get("width", 0) > 0
                and t.get("height", 0) > 0
            ]
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
            raise StudioError(
                "Cannot discover targets. Check --runner points to a working Esprite runner."
            ) from exc
        if not self.targets:
            raise StudioError(
                "This example requires a host-native target with a display."
            )
        self.worker = threading.Thread(target=self._pump, daemon=True)
        self.worker.start()

    def _close_process(self):
        if self.process:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2)
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None
        if self.selector:
            self.selector.close()
            self.selector = None
        self.buffer.clear()

    def _rpc(self, command):
        if not self.process or self.process.poll() is not None:
            raise StudioError("Runner stopped. Choose Restart to open a fresh session.")
        wire = (
            json.dumps(command, ensure_ascii=False, separators=(",", ":")) + "\n"
        ).encode()
        if len(wire) >= 131072:
            raise StudioError("Command exceeds the runner request limit.")
        deadline = time.monotonic() + self.reply_timeout
        try:
            # Unbuffered nonblocking pipes: a stalled runner must not freeze HTTP.
            at = 0
            while at < len(wire):
                try:
                    at += os.write(self.process.stdin.fileno(), wire[at:])
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise TimeoutError()
                    time.sleep(0.005)
            while b"\n" not in self.buffer:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not self.selector.select(remaining):
                    raise TimeoutError()
                chunk = os.read(self.process.stdout.fileno(), 65536)
                if not chunk:
                    raise EOFError()
                self.buffer.extend(chunk)
                if len(self.buffer) > MAX_REPLY:
                    raise ValueError("Reply too large")
            raw, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            reply = json.loads(raw)
            if not isinstance(reply, dict):
                raise TypeError("Expected an object reply")
        except (OSError, ValueError, TypeError, EOFError, TimeoutError) as exc:
            self._close_process()
            raise StudioError(
                "Runner did not return a valid reply. Choose Restart to recover."
            ) from exc
        if isinstance(reply, dict) and ("error" in reply or reply.get("ok") is False):
            raise StudioError(
                "Runner rejected the command: "
                + str(reply.get("error", "operation failed"))
            )
        return reply

    def _capture(self):
        # These paths belong exclusively to the bridge, never to an HTTP caller.
        result = self._rpc({"cmd": "screenshot", "out": str(self.capture_path)})
        png = self.capture_path.read_bytes()
        if not png.startswith(b"\x89PNG\r\n\x1a\n"):
            raise StudioError(
                "Runner did not produce a PNG. Choose Restart to recover."
            )
        if png != self.png:
            self.png = png
            self.frame += 1
        width, height = result.get("w"), result.get("h")
        if (
            type(width) is not int
            or type(height) is not int
            or width <= 0
            or height <= 0
        ):
            raise StudioError(
                "Runner returned invalid display dimensions. Choose Restart to recover."
            )
        self.width, self.height = width, height
        logs = self._rpc({"cmd": "logs"}).get("serial", "")
        if not isinstance(logs, str):
            raise StudioError(
                "Runner returned invalid serial output. Choose Restart to recover."
            )
        self.logs = logs[-16384:]
        self.updated = time.time()

    def _boot(self, key):
        board = next((t for t in self.targets if t["key"] == key), None)
        if board is None:
            raise ValueError("Choose a target listed by this runner.")
        self._close_process()
        self.target = key
        self.width = self.height = 0
        self.frame += 1
        self.board = board
        self.png = b""
        self.logs = ""
        self.running = False
        self.error = ""
        self.updated = None
        state = self.state_dir / hashlib.sha256(key.encode()).hexdigest()[:16]
        state.mkdir(parents=True, exist_ok=True)
        self.capture_path = state / "display.png"
        env = dict(os.environ, ESPRITE_HTTP_PORT="0", ESPRITE_STATE_DIR=str(state))
        self.process = subprocess.Popen(
            [self.runner, "run"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,
            env=env,
        )
        os.set_blocking(self.process.stdin.fileno(), False)
        os.set_blocking(self.process.stdout.fileno(), False)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.buffer.clear()
        self._rpc({"cmd": "boot", "target": key})
        self._capture()
        self.running = True

    def action(self, data):
        with self.lock:
            try:
                action = data.get("action")
                if action == "target":
                    self._boot(data.get("target"))
                    return
                if not self.target:
                    raise ValueError("Choose a target first.")
                if action == "running":
                    if type(data.get("value")) is not bool:
                        raise ValueError("Running must be true or false.")
                    self.running = data["value"]
                    return
                if action == "step":
                    cmd = {"cmd": "steps", "n": 1}
                elif action == "button":
                    which = data.get("which")
                    if which not in [
                        b["label"] for b in self.board.get("controls", [])
                    ]:
                        raise ValueError("Choose a physical button on this board.")
                    cmd = {"cmd": "button", "which": which}
                elif action == "tap":
                    x, y = data.get("x"), data.get("y")
                    if (
                        type(x) is not int
                        or type(y) is not int
                        or not (0 <= x < self.width and 0 <= y < self.height)
                    ):
                        raise ValueError(
                            "Touch coordinates must be inside the display."
                        )
                    cmd = {"cmd": "tap", "x": x, "y": y}
                elif action == "serial":
                    value = data.get("text")
                    if not isinstance(value, str) or len(value.encode()) > 60000:
                        raise ValueError(
                            "Serial input must be text up to 60,000 UTF-8 bytes."
                        )
                    cmd = {"cmd": "serial", "sub": "send", "text": value}
                elif action == "battery":
                    value = data.get("pct")
                    if (
                        not self.board.get("battery")
                        or type(value) is not int
                        or not 0 <= value <= 100
                    ):
                        raise ValueError(
                            "This board must support a battery level from 0 to 100."
                        )
                    cmd = {"cmd": "battery", "pct": value}
                else:
                    raise ValueError("Unknown studio action.")
                self._rpc(cmd)
                self._capture()
                self.error = ""
            except (StudioError, OSError) as exc:
                self.running = False
                self.error = str(exc)
                raise StudioError(str(exc)) from exc

    def _pump(self):
        while not self.stop.wait(0.2):
            with self.lock:
                if not self.running:
                    continue
                try:
                    self._rpc({"cmd": "steps", "n": 1})
                    self._capture()
                except (StudioError, OSError) as exc:
                    self.error = str(exc)
                    self.running = False

    def snapshot(self):
        with self.lock:
            return {
                "target": self.target,
                "targets": self.targets,
                "running": self.running,
                "alive": bool(self.process and self.process.poll() is None),
                "error": self.error,
                "logs": self.logs,
                "frame": self.frame,
                "updated": self.updated,
                "width": getattr(self, "width", 0),
                "height": getattr(self, "height", 0),
            }

    def close(self):
        self.stop.set()
        self.worker.join(timeout=12)
        with self.lock:
            self._close_process()


def make_server(session, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def reply(self, code, data, kind="application/json"):
            body = json.dumps(data).encode() if kind == "application/json" else data
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header(
                "Content-Security-Policy",
                "default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
            )
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def local(self):
            return self.headers.get("Host") in {
                f"127.0.0.1:{self.server.server_port}",
                f"localhost:{self.server.server_port}",
            }

        def do_GET(self):
            if not self.local():
                return self.reply(403, {"error": "Local host required."})
            path = self.path.split("?", 1)[0]
            if path == "/api/state":
                return self.reply(
                    200, dict(session.snapshot(), token=self.server.token)
                )
            if path == "/frame.png":
                with session.lock:
                    png = session.png
                return (
                    self.reply(200, png, "image/png")
                    if png
                    else self.reply(404, {"error": "No frame yet."})
                )
            assets = {
                "/": ("index.html", "text/html; charset=utf-8"),
                "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                "/style.css": ("style.css", "text/css; charset=utf-8"),
            }
            if path not in assets:
                return self.reply(404, {"error": "Not found."})
            name, kind = assets[path]
            return self.reply(200, (ASSETS / name).read_bytes(), kind)

        def do_POST(self):
            if (
                not self.local()
                or self.headers.get("Origin")
                not in (None, f"http://{self.headers.get('Host')}")
                or not secrets.compare_digest(
                    self.headers.get("X-Esprite-Token", ""), self.server.token
                )
            ):
                return self.reply(403, {"error": "Open the studio locally and retry."})
            if self.path != "/api/action":
                return self.reply(404, {"error": "Not found."})
            if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                return self.reply(415, {"error": "Send application/json."})
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= MAX_BODY:
                    return self.reply(413, {"error": "Request exceeds 100,000 bytes."})
                self.connection.settimeout(5)
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise TypeError("Expected a JSON object.")
                session.action(data)
                return self.reply(200, {"ok": True})
            except (ValueError, TypeError) as exc:
                return self.reply(400, {"error": str(exc)})
            except (StudioError, OSError) as exc:
                return self.reply(503, {"error": str(exc)})

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.token = secrets.token_urlsafe(32)
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--runner", default="esprite", help="Esprite binary or per-project runner"
    )
    parser.add_argument(
        "--target", help="Target to boot (default: first native display target)"
    )
    parser.add_argument(
        "--port", type=int, default=0, help="Loopback port; 0 chooses a free port"
    )
    parser.add_argument(
        "--state-dir", type=Path, help="Keep simulator state here between launches"
    )
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("Port must be from 0 to 65535.")
    with tempfile.TemporaryDirectory(prefix="esprite-html-") as temporary:
        session = None
        server = None
        try:
            session = Session(args.runner, args.state_dir or temporary)
            session.action(
                {"action": "target", "target": args.target or session.targets[0]["key"]}
            )
            server = make_server(session, args.port)
            signal.signal(
                signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt())
            )
            print(
                f"Esprite HTML studio: http://127.0.0.1:{server.server_port}",
                flush=True,
            )
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        except (StudioError, OSError, ValueError) as exc:
            parser.exit(1, f"{exc}\n")
        finally:
            if server:
                server.server_close()
            if session:
                session.close()


if __name__ == "__main__":
    main()
