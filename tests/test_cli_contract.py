"""Exercise CLI metadata against real invocations, with isolated firmware state."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

RUNNER = os.environ.get("ESPRITE_BINARY", "esprite")


class ContractTests(unittest.TestCase):
    def run_cli(self, *args, **env):
        return subprocess.run(
            [RUNNER, *args],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
            env=dict(os.environ, **env),
        )

    def test_schema_is_offline_and_describes_retry_and_output(self):
        result = self.run_cli("schema", ESPRITE_QEMU_BOARD="/missing/board.json")
        self.assertEqual(result.returncode, 0, result.stderr)
        schema = json.loads(result.stdout)
        self.assertEqual(schema["clispec"], "0.3")
        commands = {c["name"]: c for c in schema["commands"]}
        for name, command in commands.items():
            self.assertIn(
                command["effects"], ("read_only", "idempotent", "non_idempotent")
            )
            if command.get("output_kind", "data") == "data":
                self.assertIn(
                    command["cardinality"], ("single", "bounded", "unbounded")
                )
                self.assertTrue(
                    "output_fields" in command or "stdout_schema" in command, name
                )
        self.assertEqual(commands["list-targets"]["effects"], "read_only")
        # Firmware may persist boot state, handle serial input or overwrite files.
        for name in ("screenshot", "serial", "tap", "run", "ui", "logs"):
            self.assertEqual(commands[name]["effects"], "non_idempotent")
        self.assertEqual(commands["run"]["output_kind"], "stream")
        self.assertEqual(commands["serve"]["output_kind"], "opaque")
        self.assertNotIn("example", commands["ui"])  # no unavailable private probe

    def test_global_options_work_before_command_and_fields_are_exact(self):
        result = self.run_cli(
            "--json", "--limit", "1", "--fields", "key", "list-targets"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data["count"], 1)
        self.assertEqual(set(data["items"][0]), {"key"})
        result = self.run_cli("list-targets", "--fields", "notkey")
        self.assertEqual(json.loads(result.stdout)["items"][0], {})

    def test_capture_failure_is_a_declared_error(self):
        with tempfile.TemporaryDirectory() as state:
            result = self.run_cli(
                "screenshot",
                state + "/missing/frame.png",
                "--target",
                "cyd",
                ESPRITE_STATE_DIR=state,
            )
        self.assertEqual(result.returncode, 9)
        self.assertEqual(json.loads(result.stderr)["error"]["kind"], "capture_failed")
        self.assertEqual(result.stdout, "")

    def test_scenario_success_has_structured_output(self):
        with tempfile.TemporaryDirectory() as state:
            path = Path(state) / "scenario.json"
            path.write_text(json.dumps({"target": "cyd", "steps": []}))
            result = self.run_cli("scenario", str(path), ESPRITE_STATE_DIR=state)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), {"ok": True})

    def test_serial_no_match_is_a_declared_outcome(self):
        schema = json.loads(self.run_cli("schema").stdout)
        self.assertTrue(
            any(o["name"] == "no_match" and o["code"] == 1 for o in schema["outcomes"])
        )
        with tempfile.TemporaryDirectory() as state:
            result = self.run_cli(
                "serial",
                "expect",
                "^not-in-demo-output$",
                "--target",
                "cyd",
                ESPRITE_STATE_DIR=state,
            )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout), {"matched": False})
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
