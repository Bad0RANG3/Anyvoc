"""No-device integration tests for the real realtime-probe runner CLI.

Run with: python Tests/RealtimeRunnerTests.py
Only child-process execution and Git queries are simulated. Argument parsing,
exclusive filesystem operations, manifest updates and artifact hashes use the
production runner. All output and the fake executable live in temporary paths.
These tests do not validate DSP, an audio device, or the C++ profiler itself.
"""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts" / "run_realtime_probe.py"
CASES = ("baseline", "unison", "matrix", "chaos", "chaos-rossler", "granular")
ORIGINAL_EXE = b"temporary fake executable; never executed\x00\r\n"
REPLACEMENT_EXE = b"replacement fake executable; never executed\x00\r\n"
STDOUT = b"simulated child stdout\x00\r\n"
STDERR = b"simulated child stderr\r\n"


def sha(data):
    return hashlib.sha256(data).hexdigest()


class RealtimeRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("anyvoc_runner_under_test", RUNNER_PATH)
        cls.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.runner)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="Anyvoc-runner-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.exe = self.root / "fake-probe.exe"
        self.exe.write_bytes(ORIGINAL_EXE)
        self.output = self.root / "results"
        self.manifest_path = self.output / "suite-manifest.json"
        self.console = io.StringIO()
        self.child_calls = []

    def read_manifest(self):
        return json.loads(self.manifest_path.read_bytes())

    def invoke(self, child, console=None):
        argv = [str(RUNNER_PATH), "--exe", str(self.exe), "--output", str(self.output),
                "--sample-rate", "48000", "--buffer-size", "256",
                "--seconds", "1", "--warmup", "0", "--rounds", "1"]
        # check_output uses subprocess.run internally: mock both explicitly so
        # neither Git nor the fake executable is launched by these tests.
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(self.runner.subprocess, "check_output", return_value="fixture-commit\n"), \
                mock.patch.object(self.runner.subprocess, "run", side_effect=child), \
                contextlib.redirect_stdout(console if console is not None else self.console):
            self.runner.main()

    def valid_child(self, command, **kwargs):
        self.child_calls.append(command)
        self.assertEqual(command[0], str(self.exe.resolve()))
        self.assertEqual(command[command.index("--seconds") + 1], "1")
        self.assertEqual(command[command.index("--warmup") + 1], "0")
        self.assertGreater(kwargs["timeout"], 0)
        stem = Path(command[command.index("--output") + 1])
        kwargs["stdout"].write(STDOUT)
        kwargs["stderr"].write(STDERR)
        # Minimal runner-level completion reply, deliberately not a valid raw
        # audio measurement that could pass the separate measurement analyzer.
        reply = {"status": "valid", "summary": {"simulated": True}}
        stem.with_suffix(".json").write_text(json.dumps(reply) + "\n", encoding="utf-8")
        stem.with_suffix(".csv").write_bytes(b"simulated artifact,not audio data\r\n")
        return subprocess.CompletedProcess(command, 0)

    def assert_artifacts(self, item, suffixes):
        stem = self.output / f"round-{item['round']}-{item['case']}"
        expected = {stem.with_suffix(suffix).name for suffix in suffixes}
        self.assertEqual(set(item["artifacts"]), expected)
        for name in expected:
            data = (self.output / name).read_bytes()
            self.assertEqual(item["artifacts"][name], {"bytes": len(data), "sha256": sha(data)})

    def assert_single_failure(self, failure_type):
        manifest = self.read_manifest()
        self.assertIs(manifest["complete"], False)
        self.assertNotIn("finished_utc", manifest)
        self.assertEqual(len(manifest["runs"]), 1)
        item = manifest["runs"][0]
        self.assertEqual((item["round"], item["case"], item["status"]), (1, "baseline", "invalid"))
        self.assertEqual(item["failure"]["type"], failure_type)
        self.assertTrue(item["failure"]["message"])
        self.assertIsNone(item["exit_code"])
        self.assertGreaterEqual(item["wall_seconds"], 0)
        self.assertEqual(item["exe_sha256_before"], sha(ORIGINAL_EXE))
        self.assertEqual(item["exe_sha256_after"], sha(ORIGINAL_EXE))
        return item

    def test_complete_suite_preserves_each_case_and_artifact(self):
        self.invoke(self.valid_child)
        manifest = self.read_manifest()
        self.assertIs(manifest["complete"], True)
        self.assertTrue(manifest["finished_utc"])
        self.assertEqual(manifest["exe_sha256"], sha(ORIGINAL_EXE))
        self.assertEqual([item["case"] for item in manifest["runs"]], list(CASES))
        self.assertEqual(len(self.child_calls), 6)
        for item in manifest["runs"]:
            self.assertEqual((item["status"], item["exit_code"]), ("valid", 0))
            self.assertEqual(item["exe_sha256_before"], sha(ORIGINAL_EXE))
            self.assertEqual(item["exe_sha256_after"], sha(ORIGINAL_EXE))
            self.assert_artifacts(item, (".json", ".csv", ".stdout.txt", ".stderr.txt"))
        self.assertFalse(self.manifest_path.with_suffix(".pending").exists())

    def test_timeout_keeps_failed_run_and_partial_artifacts(self):
        def timeout_child(command, **kwargs):
            self.child_calls.append(command)
            stem = Path(command[command.index("--output") + 1])
            kwargs["stdout"].write(STDOUT)
            kwargs["stderr"].write(STDERR)
            stem.with_suffix(".json").write_bytes(b'{"unfinished":')
            stem.with_suffix(".csv").write_bytes(b"partial raw rows\r\n")
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])

        with self.assertRaises(RuntimeError):
            self.invoke(timeout_child)
        item = self.assert_single_failure("TimeoutExpired")
        self.assertEqual(len(self.child_calls), 1)
        self.assert_artifacts(item, (".json", ".csv", ".stdout.txt", ".stderr.txt"))
        self.assertEqual((self.output / "round-1-baseline.stdout.txt").read_bytes(), STDOUT)

    def test_launch_oserror_keeps_failed_run_and_created_logs(self):
        def failed_launch(command, **kwargs):
            self.child_calls.append(command)
            raise OSError("simulated process creation failure")

        with self.assertRaises(RuntimeError):
            self.invoke(failed_launch)
        item = self.assert_single_failure("OSError")
        self.assertEqual(len(self.child_calls), 1)
        self.assert_artifacts(item, (".stdout.txt", ".stderr.txt"))
        self.assertTrue(all(artifact["bytes"] == 0 for artifact in item["artifacts"].values()))

    def test_executable_replaced_during_capture_invalidates_success_reply(self):
        def replacing_child(command, **kwargs):
            result = self.valid_child(command, **kwargs)
            self.exe.write_bytes(REPLACEMENT_EXE)
            return result

        with self.assertRaises(RuntimeError):
            self.invoke(replacing_child)
        manifest = self.read_manifest()
        self.assertIs(manifest["complete"], False)
        self.assertEqual(len(self.child_calls), 1)
        self.assertEqual(len(manifest["runs"]), 1)
        item = manifest["runs"][0]
        self.assertEqual((item["status"], item["exit_code"]), ("invalid", 0))
        self.assertEqual(item["exe_sha256_before"], sha(ORIGINAL_EXE))
        self.assertEqual(item["exe_sha256_after"], sha(REPLACEMENT_EXE))
        self.assertTrue(item["binary_identity_failure"])
        self.assert_artifacts(item, (".json", ".csv", ".stdout.txt", ".stderr.txt"))

    def test_executable_replaced_between_cases_is_rejected_before_launch(self):
        owner = self

        class ReplaceAfterFirstCompletion(io.StringIO):
            changed = False

            def write(self, text):
                count = super().write(text)
                # Simulate an external replacement on the public END progress
                # event, after the first capture's final hash was recorded.
                if text.startswith("END ") and not self.changed:
                    self.changed = True
                    owner.exe.write_bytes(REPLACEMENT_EXE)
                return count

        console = ReplaceAfterFirstCompletion()
        with self.assertRaises(RuntimeError):
            self.invoke(self.valid_child, console)
        self.assertTrue(console.changed)
        manifest = self.read_manifest()
        self.assertIs(manifest["complete"], False)
        self.assertEqual(len(self.child_calls), 1)
        self.assertEqual(len(manifest["runs"]), 2)
        first, rejected = manifest["runs"]
        self.assertEqual(first["status"], "valid")
        self.assertEqual(first["exe_sha256_after"], sha(ORIGINAL_EXE))
        self.assertEqual((rejected["case"], rejected["status"]), ("unison", "invalid"))
        self.assertEqual(rejected["failure"]["type"], "RuntimeError")
        self.assertIsNone(rejected["exit_code"])
        self.assertEqual(rejected["exe_sha256_before"], sha(REPLACEMENT_EXE))
        self.assertEqual(rejected["artifacts"], {})

    def test_existing_suite_is_untouched_and_never_launches(self):
        self.output.mkdir()
        sentinel = b"existing suite sentinel\r\n\x00"
        self.manifest_path.write_bytes(sentinel)
        with self.assertRaises(FileExistsError):
            self.invoke(self.valid_child)
        self.assertEqual(self.manifest_path.read_bytes(), sentinel)
        self.assertEqual(self.child_calls, [])
        self.assertEqual({p.name for p in self.output.iterdir()}, {"suite-manifest.json"})

    def test_competing_runner_cannot_enter_after_atomic_reservation(self):
        original_open = Path.open
        contender_attempted = False
        owner = self

        def open_then_contend(path, mode="r", *args, **kwargs):
            nonlocal contender_attempted
            stream = original_open(path, mode, *args, **kwargs)
            if path == owner.manifest_path and mode == "x" and not contender_attempted:
                contender_attempted = True
                # The actual exclusive create has succeeded, but its first
                # byte has not been written. Deterministically schedule a
                # second complete CLI invocation at that race boundary.
                try:
                    with owner.assertRaises(FileExistsError):
                        owner.runner.main()
                    owner.assertEqual(owner.child_calls, [])
                    owner.assertEqual(owner.manifest_path.read_bytes(), b"")
                except BaseException:
                    stream.close()
                    raise
            return stream

        with mock.patch.object(Path, "open", open_then_contend):
            self.invoke(self.valid_child)
        self.assertTrue(contender_attempted, "Runner must reserve before starting any capture")
        manifest = self.read_manifest()
        self.assertIs(manifest["complete"], True)
        self.assertEqual([item["case"] for item in manifest["runs"]], list(CASES))
        self.assertEqual(len(self.child_calls), 6)
        for item in manifest["runs"]:
            self.assert_artifacts(item, (".json", ".csv", ".stdout.txt", ".stderr.txt"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
