"""Independently measure the macro demo and a supplied legacy regression baseline.

Uses only standard-library WAV decoding and the shared numerical utilities in
analyze-chaos-demo.py. No plugin implementation or audio generation is imported.
"""

import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "anyvoc_pcm_analysis", Path(__file__).with_name("analyze-chaos-demo.py"))
pcm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pcm)

LABELS = [
    "Fixed Macro 1 base 0.4; three outgoing routes",
    "Add Trigger Triangle LFO 1 (0.4 Hz, unipolar) to Macro 1, amount 0.4",
    "Also add LFO 2 (1.2 Hz) to Pan, amount 0.65, effective Macro 1 as AUX",
    "Also add Macro 1 to Macro 8 (base 0.1, amount 0.5), Macro 8 to Macro 1 (0.2); Pan AUX becomes Macro 8",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--legacy-before", type=Path, required=True)
    parser.add_argument("--legacy-after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="0.19.0",
                        help="Version of the tested binary (default: 0.19.0)")
    parser.add_argument("--baseline-version", default="0.18.0",
                        help="Version of the supplied legacy reference (default: 0.18.0)")
    args = parser.parse_args()
    for path in (args.demo, args.legacy_before, args.legacy_after):
        if not path.is_file():
            raise RuntimeError(f"Completed input WAV is missing: {path}")

    data = pcm.decode(args.demo)  # Requires stereo 24-bit PCM, 48 kHz, 20 seconds.
    sections = [[channel[index * 240000:(index + 1) * 240000] for channel in data]
                for index in range(4)]
    segments = [{"index": index, "start_seconds": index * 5,
                 "end_seconds": (index + 1) * 5, "label": LABELS[index],
                 **pcm.metrics(section)} for index, section in enumerate(sections)]
    comparisons = []
    for first, second in ((0, 1), (1, 2), (2, 3)):
        differences = [a - b for channel in range(2)
                       for a, b in zip(sections[first][channel], sections[second][channel])]
        comparisons.append({
            "first_segment": first, "second_segment": second,
            "comparison": "Add one macro input route" if first == 0 else "Documented multi-parameter routing change",
            "waveform_difference_rms": pcm.rms(differences),
            "waveform_difference_peak": max(abs(value) for value in differences),
            "decoded_samples_identical": all(value == 0 for value in differences),
        })
    before_sha, after_sha = pcm.digest(args.legacy_before), pcm.digest(args.legacy_after)
    identical = args.legacy_before.read_bytes() == args.legacy_after.read_bytes()
    overall = pcm.metrics(data)
    failures = []
    if not identical or before_sha != after_sha:
        failures.append(f"Legacy WAV bytes / SHA-256 changed from supplied {args.baseline_version} baseline")
    if overall["decoded_nonfinite_samples"] != 0:
        failures.append("Non-finite decoded samples")
    if overall["full_scale_or_clipped_samples"] != 0:
        failures.append("Decoded samples reached PCM full scale")
    for segment in segments:
        if segment["rms"] <= .001:
            failures.append(f"Segment {segment['index']} is silent or unexpectedly quiet")
    for comparison in comparisons:
        if comparison["decoded_samples_identical"]:
            failures.append(f"Segments {comparison['first_segment']}/{comparison['second_segment']} are identical")
    report = {
        "version": args.version, "generated_utc": datetime.now(timezone.utc).isoformat(),
        "status": "PASS" if not failures else "FAIL", "failures": failures,
        "scope": "Independent PCM measurements and legacy byte comparison; no synthesizer helpers used",
        "limitations": [
            "Integer PCM cannot contain NaN/Inf; this does not inspect pre-encoding floating-point buffers.",
            "No PCM full-scale sample does not certify absence of upstream limiting.",
            "Waveform differences do not establish subjective sound quality or Serum equivalence.",
            "Only segments 0/1 isolate a single added route; later segments change the documented routing combination.",
        ],
        "demo": {
            "path": str(args.demo.resolve()), "file_bytes": args.demo.stat().st_size,
            "sha256": pcm.digest(args.demo), "encoding": "signed little-endian integer PCM",
            "bits_per_sample": 24, "sample_rate_hz": 48000, "channel_count": 2,
            "frames": 960000, "duration_seconds": 20.0,
            "overall": overall, "segments": segments, "segment_comparisons": comparisons,
        },
        "legacy_regression": {
            "baseline_version": args.baseline_version,
            "before_path": str(args.legacy_before.resolve()), "after_path": str(args.legacy_after.resolve()),
            "before_sha256": before_sha, "after_sha256": after_sha,
            "before_bytes": args.legacy_before.stat().st_size, "after_bytes": args.legacy_after.stat().st_size,
            "sha256_identical": before_sha == after_sha, "byte_identical": identical,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"{report['status']} Macro demo: stereo 24-bit PCM, 48000 Hz, 20.000 seconds")
    print(f"Overall RMS={overall['rms']:.9f}, peak={overall['peak']:.9f}; full-scale={overall['full_scale_or_clipped_samples']}")
    for comparison in comparisons:
        print(f"Segments {comparison['first_segment']}/{comparison['second_segment']} difference RMS={comparison['waveform_difference_rms']:.9f}")
    print(f"Legacy byte-identical={identical}; SHA-256={after_sha}")
    print(f"Report: {args.output.resolve()}")
    if failures:
        raise SystemExit("; ".join(failures))


if __name__ == "__main__":
    main()
