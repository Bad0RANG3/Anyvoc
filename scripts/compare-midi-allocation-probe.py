"""Compare two isolated MIDI diagnostic captures, never real-time deadlines.

Requires exact fixtures/audio and retained allocator-positive controls. Only the
bounded ARP fixtures' MidiBuffer-owned calls are required to disappear; this does
not establish a zero-allocation callback or intercept every CRT allocation.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys


CASE_IDS = (
    "fresh-cold", "fresh-warmed", "rotation", "long-cc", "lifecycle",
    "keyboard-off", "keyboard-on", "sysex-off", "sysex-on",
)
SITES = {"heapMalloc", "heapCalloc", "heapRealloc", "midiMessageMalloc"}
OWNERS = {"host", "other-MidiBuffer", "none"}
ORACLE_IDS = {"fresh-warmed", "rotation", "long-cc"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, minimum=0):
    return type(value) is int and value >= minimum


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"Non-finite JSON: {value}")))


def indexed(items, field, expected, label):
    require(isinstance(items, list), f"{label}: expected list")
    result = {}
    for item in items:
        key = item[field]
        require(key not in result, f"{label}: duplicate {key}")
        result[key] = item
    require(set(result) == set(expected), f"{label}: unexpected member set")
    return result


def audio_name(value):
    require(isinstance(value, str) and value.endswith(".f32le")
            and value not in (".", "..") and not any(c in value for c in "/\\:"), "Unsafe audio filename")
    return value


def check_totals(reported, events):
    expected = {}
    for event in events:
        key = event["site"], event["owner"]
        count, size = expected.get(key, (0, 0))
        expected[key] = count + 1, size + event["bytes"]
    actual = {}
    for item in reported:
        key = item["site"], item["owner"]
        require(key not in actual and key[0] in SITES and key[1] in OWNERS
                and integer(item["count"], 1) and integer(item["bytes"], 1), "Invalid/duplicate allocation aggregate")
        actual[key] = item["count"], item["bytes"]
    require(actual == expected, "Allocation aggregates differ from raw events")


def load_capture(directory):
    directory = directory.resolve(strict=True)
    manifest_bytes = (directory / "fixture-manifest.json").read_bytes()
    manifest = read_json(directory / "fixture-manifest.json")
    report = read_json(directory / "allocation-report.json")
    metadata = read_json(directory / "audio-meta.json")
    require(all(type(value["schema"]) is int and value["schema"] == 1 for value in (manifest, report, metadata)), "Unsupported schema")
    require(manifest["format"] == "f32le-interleaved-stereo" and type(manifest["sample_rate"]) is int and manifest["sample_rate"] == 48000, "Unexpected audio format")
    fixtures = indexed(manifest["cases"], "id", CASE_IDS, "fixtures")
    cases = indexed(report["cases"], "id", CASE_IDS, "allocation cases")
    require(report["valid"] is True and report["controls_passed"] is True, "Invalid capture/controls")
    require(bool(report["controls"]) and all(c["passed"] is True for c in report["controls"]), "Failed or absent instrumentation controls")
    control_sites = set()
    for control in report["controls"]:
        require(integer(control["count"]) and control["count"] == len(control["events"])
                and integer(control["overflow"]) and control["overflow"] == 0, "Invalid instrumentation control trace")
        for event in control["events"]:
            require(event["site"] in SITES and integer(event["bytes"], 1), "Invalid control allocation event")
            control_sites.add(event["site"])
    require(control_sites == SITES, "Not all four real allocator hooks were observed")
    expected_files = {}
    for case_id, fixture in fixtures.items():
        require(type(fixture["channels"]) is int and fixture["channels"] == 2 and integer(fixture["frames"], 1), "Invalid frame count")
        audio_name(fixture["audio_file"])
        require((fixture["oracle_file"] is not None) == (case_id in ORACLE_IDS), "Manual oracle set differs")
        require(isinstance(fixture["blocks"], list) and fixture["blocks"], "Empty block timeline")
        start = 0
        for index, block in enumerate(fixture["blocks"]):
            require(integer(block["index"]) and block["index"] == index and integer(block["start"]) and block["start"] == start
                    and integer(block["samples"]), "Invalid/discontinuous fixture timeline")
            start += block["samples"]
        require(start == fixture["frames"], "Block/frame total mismatch")
        for role, field in (("actual", "audio_file"), ("manual-reference", "oracle_file")):
            if fixture[field] is None:
                continue
            name = audio_name(fixture[field])
            require(name not in expected_files, "Duplicate audio filename")
            expected_files[name] = (case_id, role, fixture["frames"])
        case = cases[case_id]
        require(case["valid"] is True and case["host_midi_empty"] is True
                and integer(case["overflow"]) and case["overflow"] == 0, f"Invalid case: {case_id}")
        require(case["manual_reference_checked"] is (fixture["oracle_file"] is not None), "Oracle check status differs")
        events = case["allocation_events"]
        require(integer(case["allocation_count"]) and case["allocation_count"] == len(events), f"Event total mismatch: {case_id}")
        require(integer(case["requested_bytes"]) and case["requested_bytes"] == sum(e["bytes"] for e in events), "Requested-byte total mismatch")
        blocks = indexed(case["blocks"], "index", range(len(fixture["blocks"])), "allocation blocks")
        for event in events:
            require(event["site"] in SITES and event["owner"] in OWNERS, "Unknown allocation site/owner")
            require(integer(event["bytes"], 1) and integer(event["block"]) and event["block"] in blocks, "Invalid allocation event")
        for index, block in blocks.items():
            matching = [e for e in events if e["block"] == index]
            require(integer(index) and integer(block["count"]) and block["count"] == len(matching)
                    and integer(block["overflow"]) and block["overflow"] == 0, "Block event count/overflow mismatch")
            require(integer(block["requested_bytes"]) and block["requested_bytes"] == sum(e["bytes"] for e in matching), "Block byte total mismatch")
            require(integer(block["start"]) and integer(block["samples"])
                    and block["start"] == fixture["blocks"][index]["start"] and block["samples"] == fixture["blocks"][index]["samples"], "Block identity mismatch")
            check_totals(block["by_site_owner"], matching)
        check_totals(case["by_site_owner"], events)
    metas = indexed(metadata["files"], "file", expected_files, "audio metadata")
    expected_set = set(expected_files) | {"fixture-manifest.json", "allocation-report.json", "audio-meta.json"}
    require({p.name for p in directory.iterdir()} == expected_set, "Unexpected capture file/directory set")
    audio = {}
    for name, (case_id, role, frames) in expected_files.items():
        path = directory / name
        require(path.is_file() and not path.is_symlink(), "Audio must be a regular file")
        data = path.read_bytes()
        meta = metas[name]
        require(meta["case_id"] == case_id and meta["role"] == role and integer(meta["frames"], 1)
                and meta["frames"] == frames and type(meta["channels"]) is int and meta["channels"] == 2, "Audio metadata identity mismatch")
        require(integer(meta["bytes"], 1) and len(data) == meta["bytes"] == frames * 2 * 4, "Wrong audio length")
        require(hashlib.sha256(data).hexdigest() == meta["sha256"], "Audio SHA mismatch")
        values = [v[0] for v in struct.iter_unpack("<f", data)]
        require(meta["finite"] is True and all(math.isfinite(v) for v in values), "Non-finite audio")
        require(any(v != 0 for v in values), "Silent fixture audio")
        rms = math.sqrt(sum(v * v for v in values) / len(values))
        peak = max(abs(v) for v in values)
        require(math.isclose(meta["rms"], rms, rel_tol=1e-9, abs_tol=1e-12)
                and math.isclose(meta["peak"], peak, rel_tol=1e-9, abs_tol=1e-12), "Audio RMS/peak metadata differs")
        audio[name] = data
    for case_id, fixture in fixtures.items():
        if fixture["oracle_file"] is not None:
            require(audio[fixture["audio_file"]] == audio[fixture["oracle_file"]], f"Manual oracle differs: {case_id}")
    for case_id in ("keyboard-off", "keyboard-on"):
        require(any(e["owner"] == "host" and e["site"] in SITES - {"midiMessageMalloc"} for e in cases[case_id]["allocation_events"]), "Keyboard host-growth positive control absent")
    for case_id in ("sysex-off", "sysex-on"):
        require(any(e["site"] == "midiMessageMalloc" for e in cases[case_id]["allocation_events"]), "Long-SysEx positive control absent")
    return manifest_bytes, fixtures, cases, audio


def compare(before, after):
    old_manifest, fixtures, old_cases, old_audio = load_capture(before)
    new_manifest, _, new_cases, new_audio = load_capture(after)
    require(old_manifest == new_manifest, "Fixture manifests differ byte-for-byte")
    require(old_audio == new_audio, "Old/new raw audio differs")
    private_before = private_after = 0
    summaries = []
    for case_id in CASE_IDS:
        def private(case):
            return sum(e["owner"] == "other-MidiBuffer" and e["site"] != "midiMessageMalloc" for e in case["allocation_events"])
        old_count, new_count = private(old_cases[case_id]), private(new_cases[case_id])
        if case_id in CASE_IDS[:5]:
            private_before += old_count
            private_after += new_count
            require(new_count == 0, f"Bounded ARP fixture still allocates a private MIDI buffer: {case_id}")
        summaries.append({"id": case_id, "frames": fixtures[case_id]["frames"],
                          "old_allocation_calls": old_cases[case_id]["allocation_count"],
                          "new_allocation_calls": new_cases[case_id]["allocation_count"],
                          "old_other_midi_buffer_calls": old_count,
                          "new_other_midi_buffer_calls": new_count, "audio_byte_identical": True})
    require(private_before > 0, "Baseline did not reproduce private MIDI-buffer allocations")
    return {"schema": 1, "passed": True, "case_count": len(CASE_IDS), "fixture_byte_identical": True,
            "audio_file_count": len(old_audio), "compared_audio_bytes_per_side": sum(map(len, old_audio.values())),
            "bounded_private_calls_before": private_before, "bounded_private_calls_after": private_after,
            "cases": summaries,
            "limitations": ["Only instrumented JUCE HeapBlock wrappers and MidiMessage::allocateSpace malloc are counted.",
                            "Other MidiBuffer owner is the private ARP buffer in these single-thread fixtures.",
                            "Keyboard and SysEx controls intentionally retain real callback allocations.",
                            "No audio device, latency, deadline, global zero-allocation or zero-lock conclusion."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new JSON file; never overwritten")
    args = parser.parse_args()
    try:
        # Reserve before expensive validation; keep failure evidence without overwriting.
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            try:
                result = compare(args.before, args.after)
            except Exception as error:
                json.dump({"schema": 1, "passed": False, "error": f"{type(error).__name__}: {error}"}, stream, indent=2)
                stream.write("\n")
                raise
            json.dump(result, stream, indent=2)
            stream.write("\n")
        print(f"PASS: {result['case_count']} exact audio cases; bounded private calls {result['bounded_private_calls_before']} -> {result['bounded_private_calls_after']}")
        return 0
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
