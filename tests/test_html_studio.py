"""Exercise the reusable HTML bridge against a real Esprite runner."""

import importlib.util
import json
import os
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "html_studio", ROOT / "examples/html-studio/server.py"
)
studio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(studio)
RUNNER = os.environ.get("ESPRITE_TEST_RUNNER", str(ROOT / "build/esprite"))


class StudioTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.session = studio.Session(RUNNER, Path(self.tmp.name))
        self.addCleanup(self.session.close)
        self.session.action({"action": "target", "target": "cyd"})
        self.server = studio.make_server(self.session, 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = "http://127.0.0.1:" + str(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(3)
        self.session.close()
        self.tmp.cleanup()

    def get(self, path):
        return urllib.request.urlopen(self.url + path, timeout=5)

    def post(self, body, token=True, origin=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["X-Esprite-Token"] = self.server.token
        if origin:
            headers["Origin"] = origin
        return urllib.request.urlopen(
            urllib.request.Request(
                self.url + "/api/action",
                data=json.dumps(body).encode(),
                headers=headers,
            ),
            timeout=10,
        )

    def test_discovery_real_pixels_touch_and_controls(self):
        with self.get("/api/state") as response:
            state = json.load(response)
        self.assertEqual(state["target"], "cyd")
        target = next(t for t in state["targets"] if t["key"] == "cyd")
        self.assertEqual(target["controls"][0]["label"], "BOOT")
        with self.get("/frame.png") as response:
            before = response.read()
        self.assertTrue(before.startswith(b"\x89PNG\r\n\x1a\n"))
        with self.post({"action": "tap", "x": 100, "y": 130}):
            pass
        with self.get("/frame.png") as response:
            self.assertNotEqual(before, response.read())
        with self.post({"action": "button", "which": "BOOT"}):
            pass
        with self.post({"action": "serial", "text": "A" * 40000}):
            pass

    def test_rejects_cross_origin_and_unbounded_or_arbitrary_commands(self):
        for body, token, origin, code in [
            ({"action": "step"}, False, None, 403),
            ({"action": "step"}, True, "https://example.org", 403),
            ({"action": "screenshot", "out": "/tmp/unwanted.png"}, True, None, 400),
            ({"action": "tap", "x": -1, "y": 0}, True, None, 400),
            ({"action": "serial", "text": "x" * 131072}, True, None, 413),
        ]:
            with self.assertRaises(urllib.error.HTTPError) as caught:
                self.post(body, token, origin)
            self.assertEqual(caught.exception.code, code)
            caught.exception.close()
        with self.get("/api/state") as response:
            self.assertEqual(json.load(response)["target"], "cyd")

    def test_target_switch_and_runner_failure_are_recoverable(self):
        old = self.session.process
        with self.post({"action": "target", "target": "cyd_tft"}):
            pass
        self.assertIsNotNone(old.poll())
        self.assertEqual(self.session.snapshot()["target"], "cyd_tft")
        self.session.process.kill()
        self.session.process.wait(3)
        with self.assertRaises(studio.StudioError):
            self.session.action({"action": "step"})
        with self.post({"action": "target", "target": "cyd"}):
            pass
        self.assertEqual(self.session.snapshot()["target"], "cyd")
        self.assertTrue(self.session.snapshot()["alive"])


class BrokenRunnerTests(unittest.TestCase):
    def test_stalled_or_malformed_runner_is_reaped(self):
        for behavior in ("import time; time.sleep(30)", "print('[]', flush=True)"):
            with (
                self.subTest(behavior=behavior),
                tempfile.TemporaryDirectory() as directory,
            ):
                root = Path(directory)
                runner = root / "runner"
                runner.write_text(
                    "#!/usr/bin/env python3\nimport sys,json\n"
                    "if 'list-targets' in sys.argv:\n"
                    " print(json.dumps({'items':[{'key':'test','backend':'native','width':1,'height':1}]}))\n"
                    "else:\n for line in sys.stdin:\n  " + behavior + "\n"
                )
                runner.chmod(0o755)
                session = studio.Session(str(runner), root / "state")
                session.reply_timeout = 0.2
                try:
                    with self.assertRaises(studio.StudioError):
                        session.action({"action": "target", "target": "test"})
                    self.assertIsNone(session.process)
                    self.assertFalse(session.snapshot()["alive"])
                    self.assertFalse(session.snapshot()["running"])
                finally:
                    session.close()


if __name__ == "__main__":
    unittest.main()
