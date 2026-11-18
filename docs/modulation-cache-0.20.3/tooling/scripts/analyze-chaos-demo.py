"""Independently decode completed release WAVs; no plugin code or audio is changed.

Run only after the Chaos and legacy demo renderers have completed.
Historical defaults target v0.17; pass --version and explicit paths for later releases.
The JSON report measures decoded PCM, not subjective sound quality or the
pre-encoding floating-point buffers (integer PCM cannot contain NaN/Inf).
"""

import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
import re
from pathlib import Path
import wave


ROOT = Path(__file__).resolve().parents[1]
LABELS = [
    "Lorenz: C4, nominal speed 2, Trigger",
    "Rossler: C4, nominal speed 2, Trigger",
]
CONTROLLED_PAIRS = {
    (0, 1): "Lorenz versus Rossler, same voice, MIDI, speed and gain",
}


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def db(value):
    return 20.0 * math.log10(value) if value > 0 else None


def rms(values):
    return math.sqrt(math.fsum(value * value for value in values) / len(values))


def metrics(channels):
    count = sum(len(channel) for channel in channels)
    peak = max(abs(value) for channel in channels for value in channel)
    energy = math.fsum(value * value for channel in channels for value in channel)
    level = math.sqrt(energy / count)
    nonfinite = sum(not math.isfinite(value) for channel in channels for value in channel)
    full_scale = sum(value <= -1.0 or value >= 8388607.0 / 8388608.0
                     for channel in channels for value in channel)
    return {
        "rms": level,
        "rms_dbfs": db(level),
        "peak": peak,
        "peak_dbfs": db(peak),
        "decoded_nonfinite_samples": nonfinite,
        "full_scale_or_clipped_samples": full_scale,
        "channels": [
            {
                "channel": "left" if index == 0 else "right",
                "rms": rms(values),
                "peak": max(abs(value) for value in values),
                "dc_mean": math.fsum(values) / len(values),
            }
            for index, values in enumerate(channels)
        ],
    }


def decode(path):
    with wave.open(str(path), "rb") as stream:
        shape = (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(),
                 stream.getnframes(), stream.getcomptype())
        if shape != (2, 3, 48000, 960000, "NONE"):
            raise RuntimeError(f"Expected stereo 24-bit PCM, 48 kHz, 20 seconds; got {shape!r}")
        raw = stream.readframes(shape[3])
    if len(raw) != shape[0] * shape[1] * shape[3]:
        raise RuntimeError("Truncated WAV PCM payload")
    data = [[], []]
    for offset in range(0, len(raw), 6):
        for channel in (0, 1):
            begin = offset + channel * 3
            integer = int.from_bytes(raw[begin:begin + 3], "little", signed=True)
            data[channel].append(integer / 8388608.0)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="0.17.0",
                        help="Release version recorded in the report (default: historical 0.17.0)")
    parser.add_argument("--demo", type=Path, default=ROOT / "dist/Anyvoc-v0.17-Chaos-Demo.wav")
    parser.add_argument("--legacy-before", type=Path,
                        default=ROOT / "build/validation-baseline/Anyvoc-v0.16-before-v0.17.wav")
    parser.add_argument("--legacy-after", type=Path,
                        default=ROOT / "build/validation-baseline/Anyvoc-v0.17-legacy-demo.wav")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/audio-analysis-0.17.json")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("--version must have three numeric components, e.g. 0.18.0")
    for path in (args.demo, args.legacy_before, args.legacy_after):
        if not path.is_file():
            raise RuntimeError(f"Completed input WAV is missing: {path}")

    data = decode(args.demo)
    sections = [[channel[index * 480000:(index + 1) * 480000] for channel in data]
                for index in range(2)]
    segments = [{"index": index, "start_seconds": index * 10, "end_seconds": (index + 1) * 10,
                 "label": LABELS[index], **metrics(section)}
                for index, section in enumerate(sections)]
    comparisons = []
    for first, second in itertools.combinations(range(2), 2):
        difference = [a - b for channel in range(2)
                      for a, b in zip(sections[first][channel], sections[second][channel])]
        comparisons.append({
            "first_segment": first,
            "second_segment": second,
            "controlled_comparison": CONTROLLED_PAIRS.get((first, second)),
            "waveform_difference_rms": rms(difference),
            "waveform_difference_peak": max(abs(value) for value in difference),
            "decoded_samples_identical": all(value == 0 for value in difference),
            "rms_level_delta_db": db(segments[second]["rms"] / segments[first]["rms"])
                if segments[first]["rms"] > 0 and segments[second]["rms"] > 0 else None,
        })

    before_sha, after_sha = digest(args.legacy_before), digest(args.legacy_after)
    identical = args.legacy_before.read_bytes() == args.legacy_after.read_bytes()
    overall = metrics(data)
    failures = []
    if not identical or before_sha != after_sha:
        failures.append("Legacy WAV bytes / SHA-256 changed from the supplied baseline")
    if overall["decoded_nonfinite_samples"] != 0:
        failures.append("Non-finite decoded samples")
    if overall["full_scale_or_clipped_samples"] != 0:
        failures.append("Decoded samples reached PCM full scale")
    for segment in segments:
        if segment["rms"] <= .001:
            failures.append(f"Segment {segment['index']} is silent or unexpectedly quiet")
    for comparison in comparisons:
        if comparison["controlled_comparison"] and comparison["decoded_samples_identical"]:
            failures.append(f"Controlled comparison {comparison['first_segment']}/{comparison['second_segment']} is identical")
    report = {
        "version": args.version,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "status": "PASS" if not failures else "FAIL",
        "failures": failures,
        "scope": "Independent standard-library WAV decode and file comparison; no synthesizer helpers used",
        "limitations": [
            "Integer PCM cannot contain NaN/Inf. This does not independently inspect pre-encoding float buffers.",
            "No full-scale sample means no observed PCM clipping; it does not certify absence of upstream limiting.",
            "Waveform differences are numerical measurements, not a subjective listening evaluation.",
        ],
        "demo": {
            "path": str(args.demo.resolve()), "file_bytes": args.demo.stat().st_size,
            "sha256": digest(args.demo), "encoding": "signed little-endian integer PCM",
            "bits_per_sample": 24, "sample_rate_hz": 48000, "channel_count": 2,
            "frames": 960000, "duration_seconds": 20.0,
            "render_notes": "Fixed render gain; each 10-second section has 20 ms fade-in and 100 ms fade-out",
            "overall": overall, "segments": segments, "segment_comparisons": comparisons,
        },
        "legacy_regression": {
            "before_path": str(args.legacy_before.resolve()), "after_path": str(args.legacy_after.resolve()),
            "before_sha256": before_sha, "after_sha256": after_sha,
            "before_bytes": args.legacy_before.stat().st_size, "after_bytes": args.legacy_after.stat().st_size,
            "sha256_identical": before_sha == after_sha, "byte_identical": identical,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"{report['status']} Chaos demo: stereo 24-bit PCM, 48000 Hz, 20.000 seconds")
    print(f"Overall RMS={overall['rms']:.9f}, peak={overall['peak']:.9f}; decoded nonfinite=0; full-scale={overall['full_scale_or_clipped_samples']}")
    for segment in segments:
        print(f"{segment['start_seconds']:2}-{segment['end_seconds']:2}s RMS={segment['rms']:.9f} peak={segment['peak']:.9f}: {segment['label']}")
    for comparison in comparisons:
        if comparison["controlled_comparison"]:
            print(f"Segments {comparison['first_segment']}/{comparison['second_segment']} difference RMS={comparison['waveform_difference_rms']:.9f}")
    print(f"Legacy byte-identical={identical}; SHA-256={after_sha}")
    print(f"Report: {args.output.resolve()}")
    if failures:
        raise SystemExit("; ".join(failures))


if __name__ == "__main__":
    main()
