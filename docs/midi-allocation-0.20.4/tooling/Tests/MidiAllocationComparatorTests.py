"""Synthetic filesystem tests for the MIDI-allocation comparison tool.

Run: python Tests/MidiAllocationComparatorTests.py
These nine-case captures contain hand-authored two-frame audio and allocator
records. They validate the comparison tool, not production DSP or CRT hooks,
and are separate from the native CTest/DSP group count.
"""
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPARATOR_PATH = ROOT / "scripts" / "compare-midi-allocation-probe.py"
CASE_IDS = (
    "fresh-cold", "fresh-warmed", "rotation", "long-cc", "lifecycle",
    "keyboard-off", "keyboard-on", "sysex-off", "sysex-on",
)
ORACLE_IDS = {"fresh-warmed", "rotation", "long-cc"}
SITE_IDS = ("heapMalloc", "heapCalloc", "heapRealloc", "midiMessageMalloc")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def read_json(directory, filename):
    return json.loads((directory / filename).read_text(encoding="utf-8"))


def event(site="heapRealloc", owner="other-MidiBuffer", size=96, block=0):
    return {"site": site, "owner": owner, "bytes": size, "block": block}


def set_case_events(case, events):
    """Keep totals internally consistent when testing a different invariant."""
    case["allocation_events"] = events
    case["allocation_count"] = len(events)
    case["requested_bytes"] = sum(e["bytes"] for e in events)
    case["by_site_owner"] = aggregates(events)
    for block in case["blocks"]:
        selected = [e for e in events if e["block"] == block["index"]]
        block["count"] = len(selected)
        block["requested_bytes"] = sum(e["bytes"] for e in selected)
        block["by_site_owner"] = aggregates(selected)


def aggregates(events):
    grouped = {}
    for item in events:
        key = item["site"], item["owner"]
        group = grouped.setdefault(key, {"site": key[0], "owner": key[1], "count": 0, "bytes": 0})
        group["count"] += 1
        group["bytes"] += item["bytes"]
    return list(grouped.values())


def make_capture(directory, before):
    directory.mkdir()
    manifest = {"schema": 1, "format": "f32le-interleaved-stereo", "sample_rate": 48000, "cases": []}
    report = {"schema": 1, "valid": True, "controls_passed": True, "controls": [], "cases": []}
    metadata = {"schema": 1, "files": []}
    # All four real-site names must be observed. These synthetic events only
    # exercise evidence validation, and are never presented as a real capture.
    report["controls"].append({"id": "scope-noop", "passed": True, "count": 0, "overflow": 0, "events": []})
    for control_id, site, size in (
        ("heap-malloc", "heapMalloc", 32), ("heap-calloc", "heapCalloc", 64),
        ("heap-realloc-same-size", "heapRealloc", 64),
        ("heap-realloc-grow", "heapRealloc", 128),
        ("midi-message-sysex", "midiMessageMalloc", 1026),
    ):
        report["controls"].append({
            "id": control_id, "passed": True, "count": 1, "overflow": 0,
            "events": [dict(event(site, "none", size), before_nonnull=site == "heapRealloc",
                            after_nonnull=True, same_address=control_id == "heap-realloc-same-size")],
        })
    report["recorder_self_tests"] = {
        "nested_capture_and_owner_restore": True,
        "capacity_overflow": {"passed": True, "count": 8192, "overflow": 1, "expected_overflow": 1},
    }
    for index, case_id in enumerate(CASE_IDS):
        fixture = {
            "id": case_id, "frames": 2, "channels": 2,
            "audio_file": case_id + ".f32le",
            "oracle_file": case_id + "-oracle.f32le" if case_id in ORACLE_IDS else None,
            "blocks": [{"index": 0, "start": 0, "samples": 1},
                       {"index": 1, "start": 1, "samples": 1}],
        }
        manifest["cases"].append(fixture)
        case = {
            "id": case_id, "valid": True, "host_midi_empty": True, "overflow": 0,
            "manual_reference_checked": case_id in ORACLE_IDS,
            "blocks": [dict(b, count=0, requested_bytes=0, overflow=0) for b in fixture["blocks"]],
        }
        events = []
        if before and index < 5:
            events = [event(size=96 + index * 8)]
        elif case_id.startswith("keyboard-"):
            events = [event("heapRealloc", "host", 128)]
        elif case_id.startswith("sysex-"):
            events = [event("midiMessageMalloc", "none", 128)]
        set_case_events(case, events)
        report["cases"].append(case)
        # Two frames, two channels, finite and nonzero; cases are distinguishable.
        data = struct.pack("<4f", (index + 1) / 32, -0.25, 0.5, -0.75)
        for role, key in (("actual", "audio_file"), ("manual-reference", "oracle_file")):
            name = fixture[key]
            if name is None:
                continue
            (directory / name).write_bytes(data)
            metadata["files"].append({
                "file": name, "case_id": case_id, "role": role, "frames": 2,
                "channels": 2, "bytes": len(data), "finite": True,
                "sha256": hashlib.sha256(data).hexdigest(),
                "rms": math.sqrt(((index + 1) ** 2 / 1024 + 0.0625 + 0.25 + 0.5625) / 4),
                "peak": 0.75,
            })
    write_json(directory / "fixture-manifest.json", manifest)
    write_json(directory / "allocation-report.json", report)
    write_json(directory / "audio-meta.json", metadata)


class MidiAllocationComparatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Execute the actual tool without creating __pycache__ in scripts/.
        cls.comparator = types.ModuleType("anyvoc_midi_comparator_under_test")
        cls.comparator.__file__ = str(COMPARATOR_PATH)
        exec(compile(COMPARATOR_PATH.read_bytes(), str(COMPARATOR_PATH), "exec"), cls.comparator.__dict__)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="Anyvoc-midi-comparator-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.pair_index = 0
        self.before, self.after = self.new_pair()

    def new_pair(self):
        self.pair_index += 1
        pair = self.root / str(self.pair_index)
        pair.mkdir()
        before, after = pair / "before", pair / "after"
        make_capture(before, True)
        make_capture(after, False)
        return before, after

    def edit(self, directory, filename, mutate):
        data = read_json(directory, filename)
        mutate(data)
        write_json(directory / filename, data)

    def reject(self, before=None, after=None, message=None):
        context = self.assertRaisesRegex(ValueError, message) if message else self.assertRaises(ValueError)
        with context:
            self.comparator.compare(before or self.before, after or self.after)

    def replace_audio(self, directory, name, data):
        (directory / name).write_bytes(data)
        def update(metadata):
            entry = next(e for e in metadata["files"] if e["file"] == name)
            entry["sha256"] = hashlib.sha256(data).hexdigest()
            entry["bytes"] = len(data)
            if len(data) % 4 == 0:
                values = struct.unpack("<" + "f" * (len(data) // 4), data)
                if values and all(math.isfinite(v) for v in values):
                    entry["rms"] = math.sqrt(sum(v * v for v in values) / len(values))
                    entry["peak"] = max(abs(v) for v in values)
        self.edit(directory, "audio-meta.json", update)

    def invoke(self, output, before=None, after=None):
        return subprocess.run(
            [sys.executable, str(COMPARATOR_PATH), "--before", str(before or self.before),
             "--after", str(after or self.after), "--output", str(output)],
            capture_output=True, text=True, timeout=30,
        )

    def test_complete_nine_case_capture_passes_with_exact_summary(self):
        summary = self.comparator.compare(self.before, self.after)
        self.assertIs(summary["passed"], True)
        self.assertEqual(summary["case_count"], 9)
        self.assertEqual(summary["audio_file_count"], 12)
        self.assertEqual(summary["compared_audio_bytes_per_side"], 192)
        self.assertEqual(summary["bounded_private_calls_before"], 5)
        self.assertEqual(summary["bounded_private_calls_after"], 0)
        self.assertEqual([c["id"] for c in summary["cases"]], list(CASE_IDS))
        self.assertTrue(all(c["audio_byte_identical"] for c in summary["cases"]))

    def test_one_byte_audio_change_with_new_correct_sha_is_rejected(self):
        name = "fresh-cold.f32le"
        original = (self.after / name).read_bytes()
        changed = bytes([original[0] ^ 1]) + original[1:]
        self.assertEqual(sum(a != b for a, b in zip(original, changed)), 1)
        self.replace_audio(self.after, name, changed)
        self.reject(message="raw audio differs")

    def test_nan_with_correct_sha_is_rejected_even_when_both_captures_match(self):
        for side in (self.before, self.after):
            self.replace_audio(side, "fresh-cold.f32le", struct.pack("<4f", float("nan"), 0.25, 0.5, 0.75))
        self.reject(message="Non-finite audio")

    def test_silent_audio_with_correct_sha_is_rejected(self):
        for side in (self.before, self.after):
            self.replace_audio(side, "fresh-cold.f32le", b"\0" * 16)
        self.reject(message="Silent fixture")

    def test_truncated_audio_with_updated_length_and_sha_is_rejected(self):
        data = (self.after / "fresh-cold.f32le").read_bytes()[:-1]
        self.replace_audio(self.after, "fresh-cold.f32le", data)
        self.reject(message="audio length")

    def test_missing_extra_and_directory_audio_members_are_rejected(self):
        for mutation in ("missing", "extra", "directory"):
            with self.subTest(mutation=mutation):
                before, after = self.new_pair()
                path = after / "fresh-cold.f32le"
                if mutation == "extra":
                    (after / "unlisted.f32le").write_bytes(b"extra")
                else:
                    path.unlink()
                    if mutation == "directory":
                        path.mkdir()
                self.reject(before, after)

    def test_duplicate_json_keys_are_rejected(self):
        for filename in ("fixture-manifest.json", "allocation-report.json", "audio-meta.json"):
            with self.subTest(filename=filename):
                before, after = self.new_pair()
                path = after / filename
                text = path.read_text(encoding="utf-8")
                path.write_text(text.replace('"schema": 1', '"schema": 1, "schema": 1', 1), encoding="utf-8")
                self.reject(before, after, "Duplicate JSON key")

    def test_duplicate_case_and_metadata_members_are_rejected(self):
        for filename, key in (("fixture-manifest.json", "cases"), ("allocation-report.json", "cases"), ("audio-meta.json", "files")):
            with self.subTest(filename=filename):
                before, after = self.new_pair()
                self.edit(after, filename, lambda data: data[key].append(copy.deepcopy(data[key][0])))
                self.reject(before, after, "duplicate")

    def test_fixture_difference_is_rejected_even_if_audio_matches(self):
        self.edit(self.after, "fixture-manifest.json", lambda data: data.update(synthetic_fixture_revision=2))
        self.reject(message="Fixture manifests differ")

    def test_fake_event_counts_requested_bytes_and_block_totals_are_rejected(self):
        mutations = (
            ("allocation_count", lambda c: c.update(allocation_count=2)),
            ("requested_bytes", lambda c: c.update(requested_bytes=97)),
            ("block_count", lambda c: c["blocks"][0].update(count=2)),
            ("block_bytes", lambda c: c["blocks"][0].update(requested_bytes=97)),
            ("missing_block", lambda c: c["blocks"].pop()),
            ("event_block", lambda c: c["allocation_events"][0].update(block=99)),
            ("event_site", lambda c: c["allocation_events"][0].update(site="operatorNew")),
            ("event_owner", lambda c: c["allocation_events"][0].update(owner="unknown")),
        )
        for label, mutation in mutations:
            with self.subTest(label=label):
                before, after = self.new_pair()
                self.edit(before, "allocation-report.json", lambda r: mutation(r["cases"][0]))
                self.reject(before, after)

    def test_overflow_at_case_block_and_control_levels_is_rejected(self):
        mutations = (
            lambda r: r["cases"][0].update(overflow=1),
            lambda r: r["cases"][0]["blocks"][0].update(overflow=1),
            lambda r: r["controls"][0].update(overflow=1),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(level=index):
                before, after = self.new_pair()
                self.edit(after, "allocation-report.json", mutation)
                self.reject(before, after)

    def test_baseline_without_private_buffer_growth_is_rejected(self):
        def remove(report):
            for case in report["cases"][:5]:
                set_case_events(case, [])
        self.edit(self.before, "allocation-report.json", remove)
        self.reject(message="Baseline did not reproduce")

    def test_after_private_buffer_growth_is_rejected_in_each_bounded_case(self):
        for index in range(5):
            with self.subTest(case=CASE_IDS[index]):
                before, after = self.new_pair()
                self.edit(after, "allocation-report.json", lambda r: set_case_events(r["cases"][index], [event()]))
                self.reject(before, after, "still allocates")

    def test_keyboard_host_positive_control_is_required_on_both_sides(self):
        for side in ("before", "after"):
            for index in (5, 6):
                with self.subTest(side=side, case=CASE_IDS[index]):
                    before, after = self.new_pair()
                    directory = before if side == "before" else after
                    self.edit(directory, "allocation-report.json", lambda r: set_case_events(r["cases"][index], []))
                    self.reject(before, after, "Keyboard host-growth")

    def test_sysex_positive_control_is_required_on_both_sides(self):
        for side in ("before", "after"):
            for index in (7, 8):
                with self.subTest(side=side, case=CASE_IDS[index]):
                    before, after = self.new_pair()
                    directory = before if side == "before" else after
                    self.edit(directory, "allocation-report.json", lambda r: set_case_events(r["cases"][index], []))
                    self.reject(before, after, "Long-SysEx")

    def test_absent_failed_and_incomplete_hook_controls_are_rejected(self):
        mutations = (
            ("absent", lambda r: r.update(controls=[])),
            ("failed", lambda r: r["controls"][0].update(passed=False)),
            ("failed_summary", lambda r: r.update(controls_passed=False)),
            ("missing_site", lambda r: r["controls"].pop()),
            ("wrong_count", lambda r: r["controls"][0].update(count=2)),
            ("invalid_bytes", lambda r: r["controls"][1]["events"][0].update(bytes=0)),
        )
        for label, mutation in mutations:
            with self.subTest(label=label):
                before, after = self.new_pair()
                self.edit(after, "allocation-report.json", mutation)
                self.reject(before, after)

    def test_manual_reference_cannot_be_mismatched_missing_or_added_to_wrong_case(self):
        data = struct.pack("<4f", 0.25, -0.25, 0.5, -0.75)
        self.replace_audio(self.after, "fresh-warmed-oracle.f32le", data)
        self.reject(message="Manual oracle differs")
        for case_index in (0, 1):
            with self.subTest(case=CASE_IDS[case_index]):
                before, after = self.new_pair()
                for directory in (before, after):
                    self.edit(directory, "fixture-manifest.json", lambda m: m["cases"][case_index].update(
                        oracle_file=None if case_index == 1 else "fresh-cold-oracle.f32le"))
                self.reject(before, after, "Manual oracle set")

    def test_actual_audio_is_required_even_when_manifest_meta_and_files_agree(self):
        for directory in (self.before, self.after):
            self.edit(directory, "fixture-manifest.json", lambda m: m["cases"][0].update(audio_file=None))
            self.edit(directory, "audio-meta.json", lambda m: m.update(files=[e for e in m["files"] if e["file"] != "fresh-cold.f32le"]))
            (directory / "fresh-cold.f32le").unlink()
        self.reject()

    def test_invalid_timeline_is_rejected_even_when_both_sides_and_reports_agree(self):
        for mutation in ("discontinuous", "negative", "boolean"):
            with self.subTest(mutation=mutation):
                before, after = self.new_pair()
                for directory in (before, after):
                    def change(blocks):
                        if mutation == "discontinuous":
                            blocks[1]["start"] = 99
                        elif mutation == "negative":
                            blocks[0]["samples"], blocks[1]["samples"] = -1, 3
                        else:
                            blocks[0]["samples"] = True
                    self.edit(directory, "fixture-manifest.json", lambda m: change(m["cases"][0]["blocks"]))
                    self.edit(directory, "allocation-report.json", lambda r: change(r["cases"][0]["blocks"]))
                self.reject(before, after)

    def test_zero_sample_block_is_valid_and_does_not_invent_audio_frames(self):
        for directory in (self.before, self.after):
            def insert_zero(blocks, report=False):
                for b in blocks:
                    b["index"] += 1
                zero = {"index": 0, "start": 0, "samples": 0}
                if report:
                    zero.update(count=0, requested_bytes=0, overflow=0, by_site_owner=[])
                blocks.insert(0, zero)
            self.edit(directory, "fixture-manifest.json", lambda m: insert_zero(m["cases"][0]["blocks"]))
            def report_change(report):
                case = report["cases"][0]
                insert_zero(case["blocks"], True)
                for e in case["allocation_events"]:
                    e["block"] += 1
            self.edit(directory, "allocation-report.json", report_change)
        self.assertIs(self.comparator.compare(self.before, self.after)["passed"], True)

    def test_boolean_and_fractional_integer_fields_are_rejected(self):
        report_mutations = (
            ("block_count_bool", lambda r: r["cases"][0]["blocks"][0].update(count=True)),
            ("block_index_float", lambda r: r["cases"][0]["blocks"][0].update(index=0.0)),
            ("block_bytes_float", lambda r: r["cases"][0]["blocks"][0].update(requested_bytes=96.0)),
            ("case_overflow_bool", lambda r: r["cases"][0].update(overflow=False)),
            ("control_overflow_float", lambda r: r["controls"][0].update(overflow=0.0)),
            ("event_bytes_float", lambda r: r["cases"][0]["allocation_events"][0].update(bytes=96.0)),
        )
        for label, mutation in report_mutations:
            with self.subTest(label=label):
                before, after = self.new_pair()
                self.edit(before, "allocation-report.json", mutation)
                self.reject(before, after)
        for key, value in (("frames", 2.0), ("channels", 2.0), ("bytes", 16.0)):
            with self.subTest(metadata=key):
                before, after = self.new_pair()
                self.edit(after, "audio-meta.json", lambda m: m["files"][0].update({key: value}))
                self.reject(before, after)

    def test_nonfinite_json_constants_are_rejected(self):
        self.edit(self.after, "allocation-report.json", lambda r: r.update(valid=float("nan")))
        self.reject(message="Non-finite JSON")

    def test_case_and_block_aggregates_must_match_raw_events(self):
        mutations = (
            ("case_missing", lambda c: c.update(by_site_owner=[])),
            ("case_duplicate", lambda c: c["by_site_owner"].append(copy.deepcopy(c["by_site_owner"][0]))),
            ("case_bytes", lambda c: c["by_site_owner"][0].update(bytes=97)),
            ("case_owner", lambda c: c["by_site_owner"][0].update(owner="host")),
            ("block_missing", lambda c: c["blocks"][0].update(by_site_owner=[])),
            ("block_count", lambda c: c["blocks"][0]["by_site_owner"][0].update(count=2)),
        )
        for label, mutation in mutations:
            with self.subTest(label=label):
                before, after = self.new_pair()
                self.edit(before, "allocation-report.json", lambda r: mutation(r["cases"][0]))
                self.reject(before, after)

    def test_rms_and_peak_metadata_are_recomputed_from_audio(self):
        for key in ("rms", "peak"):
            with self.subTest(key=key):
                before, after = self.new_pair()
                self.edit(after, "audio-meta.json", lambda m: m["files"][0].update({key: 0.123}))
                self.reject(before, after, "RMS/peak")

    def test_midi_message_malloc_cannot_impersonate_private_buffer_growth(self):
        def change(report):
            for case in report["cases"][:5]:
                case["allocation_events"][0]["site"] = "midiMessageMalloc"
                set_case_events(case, case["allocation_events"])
        self.edit(self.before, "allocation-report.json", change)
        self.reject()

    def test_cli_writes_success_and_never_overwrites_existing_output(self):
        output = self.root / "comparison.json"
        result = self.invoke(output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIs(json.loads(output.read_bytes())["passed"], True)
        saved = output.read_bytes()
        result = self.invoke(output)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), saved)

    def test_cli_keeps_failure_report_and_does_not_replace_it_on_retry(self):
        output = self.root / "failure.json"
        (self.after / "fresh-cold.f32le").unlink()
        result = self.invoke(output)
        self.assertNotEqual(result.returncode, 0)
        saved = output.read_bytes()
        failure = json.loads(saved)
        self.assertIs(failure["passed"], False)
        self.assertIn("error", failure)
        result = self.invoke(output)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main(verbosity=2)
