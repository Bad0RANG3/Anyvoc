"""Compare frozen --render-cache-regression exports without running the synth.

Standard library only. Exact audio bytes and every manifest field must match;
synthetic --self-test checks this tool, not DSP or performance.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile

PROFILES = ("all-sources", "constant-and-held", "macro-phase-feedback")
COMBINATIONS = {(p, r, b) for p in PROFILES for r in (44100, 48000, 96000) for b in (127, 256, 511)}
EDITS = (0, 317, 354, 521, 733, 997, 1034, 1103, 1201, 1301, 1338, 1559,
         3511, 4099, 4673, 5471, 6311, 7207, 7244, 8191, 9001)
FORMAT = "IEEE754 float32 little-endian; interleaved L,R; no header; no quantisation"
MANIFEST_KEYS = {"fixture_version", "format", "comparison", "duration_per_case_seconds", "total_audio_seconds",
                 "coverage", "settings_changes", "zero_length_blocks", "midi_events", "cases", "complete"}
CASE_KEYS = {"file", "profile", "sample_rate", "nominal_block", "channels", "frames", "bytes", "rms", "peak", "render_blocks"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def expected_blocks(frames, block):
    result, start = [], 0
    while start < frames:
        if start in EDITS:
            result.append([start, 0])
        end = min(start + block, frames, next((x for x in EDITS if x > start), frames))
        result.append([start, end - start])
        start = end
    return result


def read_export(directory, state, evidence):
    paths = list(directory.iterdir())
    require(all(p.is_file() and not p.is_symlink() for p in paths), "Only ordinary files are allowed")
    names = {f"{p}-{r}-{b}.f32le" for p, r, b in COMBINATIONS} | {"manifest.json"}
    require({p.name for p in paths} == names, "Unexpected file set: " + repr(sorted({p.name for p in paths} ^ names)))
    state["data"] = {}
    for path in sorted(paths):
        require(path.stat().st_size <= 2_000_000, f"Oversized input: {path.name}")
        data = path.read_bytes()
        state["data"][path.name] = data
        evidence["files"][path.name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    manifest = json.loads(state["data"]["manifest.json"].decode("utf-8"), object_pairs_hook=unique_object,
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError("Non-finite JSON: " + value)))
    state["manifest"] = manifest
    require(set(manifest) == MANIFEST_KEYS and type(manifest["fixture_version"]) is int
            and manifest["fixture_version"] == 1 and manifest["complete"] is True, "Incomplete/unsupported manifest")
    require(manifest["format"] == FORMAT and manifest["duration_per_case_seconds"] == .24
            and manifest["total_audio_seconds"] == 6.48, "Unexpected format/duration")
    require(all(isinstance(manifest[k], str) and manifest[k] for k in ("comparison", "coverage", "zero_length_blocks")),
            "Missing fixture description")
    changes = manifest["settings_changes"]
    require(isinstance(changes, list) and tuple(x["sample"] for x in changes) == EDITS
            and all(set(x) == {"sample", "action"} and type(x["sample"]) is int
                    and isinstance(x["action"], str) and x["action"] for x in changes), "Invalid edit sequence")
    events = manifest["midi_events"]
    require(isinstance(events, list) and len(events) == 100, "Expected 100 ordered MIDI events")
    previous = -1
    for event in events:
        require(set(event) == {"sample", "midi_hex"} and type(event["sample"]) is int
                and previous <= event["sample"] < 10584, "Invalid MIDI event ordering/range")
        raw = bytes.fromhex(event["midi_hex"])
        require(len(raw) == 3 and 0x80 <= raw[0] <= 0xEF and all(v < 128 for v in raw[1:]), "Invalid MIDI bytes")
        previous = event["sample"]
    cases = manifest["cases"]
    require(isinstance(cases, list) and len(cases) == 27, "Expected exactly 27 cases")
    seen, total = set(), 0
    for case in cases:
        require(set(case) == CASE_KEYS, "Incomplete case")
        combo = (case["profile"], case["sample_rate"], case["nominal_block"])
        require(combo in COMBINATIONS and combo not in seen, "Invalid/duplicate case combination")
        seen.add(combo)
        profile, rate, block = combo
        name, frames = f"{profile}-{int(rate)}-{block}.f32le", round(rate * .24)
        require(case["file"] == name and case["channels"] == 2 and type(case["frames"]) is int
                and case["frames"] == frames and case["bytes"] == frames * 8, "Invalid file name/channels/size")
        require(canonical(case["render_blocks"]) == canonical(expected_blocks(frames, block)), "Invalid render block sequence")
        data = state["data"][name]
        require(len(data) == frames * 8, f"Wrong float byte count: {name}")
        values = [v[0] for v in struct.iter_unpack("<f", data)]
        require(all(math.isfinite(v) for v in values), f"Non-finite float: {name}")
        rms, peak = math.sqrt(sum(v * v for v in values) / len(values)), max(map(abs, values))
        require(rms > 1e-6 and peak <= 1.001, f"Silent/out-of-bounds audio: {name}")
        require(type(case["rms"]) in (int, float) and type(case["peak"]) in (int, float)
                and math.isfinite(case["rms"]) and 1e-6 < case["rms"] <= case["peak"] <= 1.001, "Invalid reported metrics")
        evidence["files"][name].update(frames=frames, samples=frames * 2, measured_rms=rms, measured_peak=peak)
        total += frames
    evidence.update(valid=True, total_frames=total, total_samples=total * 2, audio_bytes=total * 8)


def compare(reference, candidate):
    report = {"status": "FAIL", "failures": [], "first_difference": None, "inputs": {},
              "scope": "Exact frozen fixture manifests and raw float audio; no performance or private-cache-key proof"}
    states = {}
    for label, directory in (("reference", reference), ("candidate", candidate)):
        evidence = report["inputs"][label] = {"path": str(directory), "valid": False, "files": {}}
        state = states[label] = {}
        try:
            read_export(directory, state, evidence)
        except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
            report["failures"].append(f"{label}: {error}")
    a, b = states["reference"], states["candidate"]
    if "manifest" in a and "manifest" in b and canonical(a["manifest"]) != canonical(b["manifest"]):
        report["failures"].append("Manifest fields differ (includes event/edit/block order and reported RMS/peak)")
        field = next((k for k in sorted(set(a["manifest"]) | set(b["manifest"]))
                      if canonical(a["manifest"].get(k)) != canonical(b["manifest"].get(k))), "missing/null field")
        report["first_difference"] = {"file": "manifest.json", "kind": "manifest_fields", "field": field}
    for name in sorted(set(a.get("data", {})) & set(b.get("data", {})) - {"manifest.json"}):
        left, right = a["data"][name], b["data"][name]
        if left != right:
            offset = next((i for i, (x, y) in enumerate(zip(left, right)) if x != y), min(len(left), len(right)))
            difference = {"file": name, "kind": "audio_bytes", "byte_offset": offset,
                          "frame": offset // 8, "channel": (offset // 4) % 2,
                          "reference_byte": left[offset] if offset < len(left) else None,
                          "candidate_byte": right[offset] if offset < len(right) else None}
            report["first_audio_difference"] = difference
            report["first_difference"] = report["first_difference"] or difference
            report["failures"].append(f"Raw float bytes differ: {name} at {offset}")
            break
    if not report["failures"]:
        report["status"] = "PASS"
        report.update(manifest_fields_exact=True, audio_bytes_exact=True)
    elif report["first_difference"] is None:
        report["first_difference"] = {"kind": "invalid_input", "detail": report["failures"][0]}
    return report


def self_test():
    results = []
    with tempfile.TemporaryDirectory(prefix="anyvoc-cache-compare-") as temp:
        base = Path(temp)
        reference, candidate = base / "reference", base / "candidate"
        reference.mkdir(); candidate.mkdir()
        manifest = dict(fixture_version=1, format=FORMAT, comparison="synthetic tool test", coverage="synthetic",
                        duration_per_case_seconds=.24, total_audio_seconds=6.48, complete=True, zero_length_blocks="synthetic",
                        settings_changes=[dict(sample=n, action="synthetic") for n in EDITS],
                        midi_events=[dict(sample=n, midi_hex="90 3c 64") for n in range(100)], cases=[])
        for profile, rate, block in sorted(COMBINATIONS):
            frames = round(rate * .24); name = f"{profile}-{rate}-{block}.f32le"
            data = struct.pack("<ff", .25, -.5) * frames
            for directory in (reference, candidate): (directory / name).write_bytes(data)
            manifest["cases"].append(dict(file=name, profile=profile, sample_rate=rate, nominal_block=block, channels=2,
                                          frames=frames, bytes=len(data), rms=math.sqrt(.15625), peak=.5,
                                          render_blocks=expected_blocks(frames, block)))
        original = canonical(manifest).encode()
        for directory in (reference, candidate): (directory / "manifest.json").write_bytes(original)
        def check(name, passed):
            require(passed, "Self-test failed: " + name); results.append(name)
        check("synthetic exact pair", compare(reference, candidate)["status"] == "PASS")
        audio = candidate / manifest["cases"][0]["file"]; raw = audio.read_bytes()
        audio.unlink(); check("missing file rejected", compare(reference, candidate)["status"] == "FAIL"); audio.write_bytes(raw)
        for name, changed in (("one audio byte", bytes([raw[0] ^ 1]) + raw[1:]),
                              ("NaN", struct.pack("<I", 0x7FC00000) + raw[4:]), ("truncation", raw[:-4])):
            audio.write_bytes(changed); result = compare(reference, candidate)
            check(name + " rejected", result["status"] == "FAIL" and result.get("first_audio_difference") is not None)
        audio.write_bytes(raw)
        audio.write_bytes(bytes(len(raw)))
        check("silent audio rejected", compare(reference, candidate)["status"] == "FAIL")
        audio.write_bytes(raw)
        manifest["settings_changes"][0]["action"] = "edited"
        (candidate / "manifest.json").write_text(canonical(manifest), encoding="utf-8")
        check("manifest-only edit rejected", compare(reference, candidate)["status"] == "FAIL")
        for name, mutation in (("reported RMS", lambda m: m["cases"][0].update(rms=.4)),
                               ("block sequence", lambda m: m["cases"][0]["render_blocks"].reverse()),
                               ("unsafe filename", lambda m: m["cases"][0].update(file="../escape.f32le")),
                               ("duplicate case", lambda m: m["cases"].__setitem__(1, m["cases"][0]))):
            changed = json.loads(original); mutation(changed)
            (candidate / "manifest.json").write_text(canonical(changed), encoding="utf-8")
            check(name + " rejected", compare(reference, candidate)["status"] == "FAIL")
        (candidate / "manifest.json").write_bytes(original)
        (candidate / "extra.bin").write_bytes(b"extra")
        check("extra file rejected", compare(reference, candidate)["status"] == "FAIL")
    return {"status": "PASS", "scope": "Synthetic comparator controls only; no DSP render", "passed": len(results), "checks": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path); parser.add_argument("--candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True); parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test and (args.reference or args.candidate) or not args.self_test and not (args.reference and args.candidate):
        parser.error("Use --self-test or both --reference/--candidate")
    output = args.output.resolve()
    if not args.self_test:
        args.reference, args.candidate = args.reference.resolve(), args.candidate.resolve()
        if args.reference == args.candidate or any(output.is_relative_to(p) for p in (args.reference, args.candidate)):
            parser.error("Use distinct input directories and an output outside both")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:  # Reserve first; never replace an existing report.
        try:
            report = self_test() if args.self_test else compare(args.reference, args.candidate)
        except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
            report = {"status": "FAIL", "failures": [str(error)]}
        stream.write(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"{report['status']}: {output}")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
