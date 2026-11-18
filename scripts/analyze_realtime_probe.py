"""Independently check a real-device probe JSON report and its raw callback CSV.

Uses only the Python standard library. PASS means the supplied measurement is
internally consistent, complete, finite, non-silent before clearing and silent
after clearing. It does not mean that a driver had no dropouts, that the callback
never allocated, or that the instrument has passed a DAW/hardware listening test.
Percentiles use nearest rank: sorted_values[ceil(p * count) - 1].
"""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path
import sys


CSV_FIELDS = (
    "index", "phase", "start_sample", "num_samples", "sample_rate",
    "entry_ticks", "process_start_ticks", "process_end_ticks", "body_end_ticks",
    "interval_ticks", "host_time_available", "host_time_ns", "finite_samples",
    "nonfinite_samples", "nonzero_samples", "sum_squares", "peak",
    "hardware_nonzero_after_clear",
)
FLOAT_FIELDS = {"sample_rate", "sum_squares", "peak"}
PARAMETER_COUNT = 1256
MAX_REPORT_BYTES = 4 * 1024 * 1024
MAX_CSV_BYTES = 512 * 1024 * 1024
MAX_ROWS = 2_000_000
FLOAT_REL_TOLERANCE = 1.0e-9
FLOAT_ABS_TOLERANCE = 1.0e-10


class AnalysisError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise AnalysisError(message)


def number(value, label, minimum=None, positive=False):
    require(type(value) in (int, float) and math.isfinite(value),
            f"{label} must be a finite number")
    if minimum is not None:
        require(value >= minimum, f"{label} must be >= {minimum}")
    if positive:
        require(value > 0, f"{label} must be positive")
    return value


def integer(value, label, minimum=0):
    require(type(value) is int and value >= minimum,
            f"{label} must be an integer >= {minimum}")
    return value


def mapping(value, label):
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def close(actual, expected, label):
    number(actual, label)
    require(math.isclose(actual, expected, rel_tol=FLOAT_REL_TOLERANCE,
                         abs_tol=FLOAT_ABS_TOLERANCE),
            f"{label} differs: reported {actual!r}, recomputed {expected!r}")


def reject_constant(value):
    raise AnalysisError(f"Non-finite JSON constant is not permitted: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_bounded(path, maximum):
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    require(0 < len(data) <= maximum,
            f"File must contain 1..{maximum} bytes: {path}")
    return data


def parse_csv(data):
    try:
        reader = csv.DictReader(io.StringIO(data.decode("utf-8-sig"), newline=""),
                                strict=True)
        require(reader.fieldnames == list(CSV_FIELDS),
                "CSV header does not match realtime probe schema 1")
        rows = []
        for line, raw in enumerate(reader, start=2):
            require(len(rows) < MAX_ROWS, "CSV exceeds the row limit")
            require(None not in raw and all(value is not None for value in raw.values()),
                    f"CSV line {line} has missing or extra fields")
            row = {"phase": raw["phase"]}
            for field in CSV_FIELDS:
                if field == "phase":
                    continue
                value = raw[field]
                if field in FLOAT_FIELDS:
                    row[field] = float(value)
                    number(row[field], f"CSV line {line} {field}")
                else:
                    require(value.isascii() and value.isdecimal(),
                            f"CSV line {line} {field} must be a nonnegative decimal integer")
                    row[field] = int(value)
            rows.append(row)
        require(rows, "CSV has no callback rows")
        return rows
    except (UnicodeError, csv.Error, ValueError) as error:
        if isinstance(error, AnalysisError):
            raise
        raise AnalysisError(f"Invalid CSV: {error}") from error


def statistics(values, overruns=None):
    ordered = sorted(values)

    def percentile(p):
        if not ordered:
            return 0.0
        return ordered[max(0, math.ceil(p * len(ordered)) - 1)]

    return {
        "count": len(values), "mean_ms": math.fsum(values) / len(values) if values else 0.0,
        "p50_ms": percentile(.50), "p95_ms": percentile(.95),
        "p99_ms": percentile(.99), "max_ms": ordered[-1] if ordered else 0.0,
        "over_budget_callbacks": overruns,
    }


def compare_statistics(reported, computed, label):
    mapping(reported, label)
    require(integer(reported.get("count"), f"{label}.count") == computed["count"],
            f"{label}.count differs from CSV")
    for key in ("mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms"):
        close(reported.get(key), computed[key], f"{label}.{key}")
    expected = computed["over_budget_callbacks"]
    actual = reported.get("over_budget_callbacks")
    if expected is None:
        require(actual is None, f"{label}.over_budget_callbacks must be null")
    else:
        require(integer(actual, f"{label}.over_budget_callbacks") == expected,
                f"{label}.over_budget_callbacks differs from CSV")


def analyse(report, rows):
    mapping(report, "report")
    require(type(report.get("schema_version")) is int and report["schema_version"] == 1,
            "Unsupported probe schema_version")
    require(report.get("status") == "valid", "Probe did not report a valid capture")
    require(report.get("errors") == [], "Probe reported errors")
    require(isinstance(report.get("case"), str) and report["case"], "Missing workload case")
    require(isinstance(report.get("anyvoc_version"), str) and report["anyvoc_version"]
            and report["anyvoc_version"] != "unknown",
            "Missing Anyvoc version")
    frequency = number(report.get("ticks_per_second"), "ticks_per_second", positive=True)
    requested = mapping(report.get("requested"), "requested")
    actual = mapping(report.get("actual"), "actual")
    recording = mapping(report.get("recording"), "recording")
    summary = mapping(report.get("summary"), "summary")
    seconds = number(requested.get("seconds"), "requested.seconds", positive=True)
    warmup_seconds = number(requested.get("warmup_seconds"), "requested.warmup_seconds", minimum=0)
    requested_rate = number(requested.get("sample_rate"), "requested.sample_rate", minimum=0)
    requested_block = integer(requested.get("buffer_size"), "requested.buffer_size")
    rate = number(actual.get("sample_rate"), "actual.sample_rate", positive=True)
    block_size = integer(actual.get("buffer_size"), "actual.buffer_size", 1)
    require(requested_rate == 0 or requested_rate == rate,
            "Actual sample rate did not honour the explicit requested rate")
    require(requested_block == 0 or requested_block == block_size,
            "Actual buffer size did not honour the explicit requested size")
    require(actual.get("input_channels") == 0 and actual.get("output_channels") == 2,
            "Expected the probe's zero-input, two-output device configuration")
    for field in ("device_type", "device_name"):
        require(isinstance(actual.get(field), str) and actual[field], f"Missing actual.{field}")
    integer(actual.get("output_latency_samples"), "actual.output_latency_samples")
    warmup_blocks = math.ceil(warmup_seconds * rate / block_size)
    measured_blocks = math.ceil(seconds * rate / block_size)
    expected_blocks = warmup_blocks + measured_blocks
    require(1 <= expected_blocks <= MAX_ROWS, "Requested recording exceeds supported bounds")
    expected_recording = {
        "warmup_blocks_requested": warmup_blocks, "measured_blocks_requested": measured_blocks,
        "capacity": expected_blocks, "recorded_blocks": expected_blocks,
        "warmup_blocks": warmup_blocks, "measured_blocks": measured_blocks,
    }
    for key, expected in expected_recording.items():
        require(integer(recording.get(key), f"recording.{key}") == expected,
                f"recording.{key} differs from requested duration / actual device")
    require(recording.get("complete") is True, "Recording was not completed")
    require(len(rows) == expected_blocks, "CSV callback row count differs from recording")

    parameters = report.get("parameters")
    require(isinstance(parameters, list) and len(parameters) == PARAMETER_COUNT,
            f"Expected a complete snapshot of {PARAMETER_COUNT} parameters")
    ids = set()
    for parameter in parameters:
        mapping(parameter, "parameter")
        name = parameter.get("id")
        require(isinstance(name, str) and name and name not in ids,
                "Parameter IDs must be nonempty and unique")
        ids.add(name)
        number(parameter.get("plain"), f"parameter {name} plain")
        normalised = number(parameter.get("normalised"), f"parameter {name} normalised", minimum=0)
        require(normalised <= 1, f"parameter {name} normalised exceeds 1")
    workload = mapping(report.get("workload"), "workload")
    require(isinstance(workload.get("description"), str) and workload["description"],
            "Missing workload description")

    process_ms, body_ms, interval_ms, jitter_ms = [], [], [], []
    process_over = body_over = 0
    measured = []
    previous = None
    for index, row in enumerate(rows):
        require(row["index"] == index, f"CSV row {index}: index is not consecutive")
        require(row["start_sample"] == index * block_size,
                f"CSV row {index}: start_sample is not consecutive")
        require(row["num_samples"] == block_size, f"CSV row {index}: block size changed")
        close(row["sample_rate"], rate, f"CSV row {index} sample_rate")
        expected_phase = "warmup" if index < warmup_blocks else "measured"
        require(row["phase"] == expected_phase, f"CSV row {index}: wrong warmup / measured phase")
        entry, start, end, body = (row[key] for key in (
            "entry_ticks", "process_start_ticks", "process_end_ticks", "body_end_ticks"))
        require(0 <= entry <= start <= end <= body, f"CSV row {index}: timing boundaries are not nested")
        interval = 0 if previous is None else entry - previous["entry_ticks"]
        require(row["interval_ticks"] == interval, f"CSV row {index}: interval_ticks differs from entries")
        if previous is not None:
            require(interval > 0, f"CSV row {index}: callback entries are not strictly increasing")
            require(entry >= previous["body_end_ticks"], f"CSV row {index}: callback bodies overlap")
        require(row["host_time_available"] in (0, 1), f"CSV row {index}: invalid host timestamp flag")
        if not row["host_time_available"]:
            require(row["host_time_ns"] == 0, f"CSV row {index}: absent host timestamp is not zero")
        samples = 2 * block_size
        require(row["finite_samples"] + row["nonfinite_samples"] == samples,
                f"CSV row {index}: pre-clear sample count is inconsistent")
        require(row["nonfinite_samples"] == 0, f"CSV row {index}: pre-clear samples are non-finite")
        require(0 <= row["nonzero_samples"] <= row["finite_samples"],
                f"CSV row {index}: nonzero sample count is inconsistent")
        require(row["hardware_nonzero_after_clear"] == 0,
                f"CSV row {index}: hardware output was not fully silenced")
        energy, peak = row["sum_squares"], row["peak"]
        number(energy, f"CSV row {index} sum_squares", minimum=0)
        number(peak, f"CSV row {index} peak", minimum=0)
        require(peak * peak <= energy + FLOAT_ABS_TOLERANCE
                and energy <= row["nonzero_samples"] * peak * peak + FLOAT_ABS_TOLERANCE,
                f"CSV row {index}: peak / energy / nonzero count are inconsistent")
        if row["nonzero_samples"] == 0:
            require(energy == 0 and peak == 0, f"CSV row {index}: silent samples have nonzero metrics")
        if row["phase"] == "measured":
            measured.append(row)
            process_duration = (end - start) * 1000.0 / frequency
            body_duration = (body - entry) * 1000.0 / frequency
            budget_ms = block_size * 1000.0 / rate
            process_ms.append(process_duration)
            body_ms.append(body_duration)
            process_over += process_duration > budget_ms
            body_over += body_duration > budget_ms
            if interval > 0:
                interval_ms.append(interval * 1000.0 / frequency)
                jitter_ms.append(interval * 1000.0 / frequency - budget_ms)
        previous = row

    computed = {
        "process": statistics(process_ms, process_over),
        "body": statistics(body_ms, body_over),
        "interval": statistics(interval_ms),
        "measured_frames": sum(row["num_samples"] for row in measured),
        "finite_samples": sum(row["finite_samples"] for row in measured),
        "nonfinite_samples": sum(row["nonfinite_samples"] for row in measured),
        "nonzero_samples": sum(row["nonzero_samples"] for row in measured),
        "sum_squares": math.fsum(row["sum_squares"] for row in measured),
        "peak": max(row["peak"] for row in measured),
        "hardware_nonzero_after_clear": sum(row["hardware_nonzero_after_clear"] for row in measured),
    }
    computed["measured_audio_seconds"] = computed["measured_frames"] / rate
    computed["rms"] = math.sqrt(computed["sum_squares"] / computed["finite_samples"])
    require(computed["nonzero_samples"] > 0 and computed["rms"] > 1.0e-8,
            "Measured workload was silent or below the probe's RMS floor")
    require(seconds <= computed["measured_audio_seconds"] < seconds + block_size / rate + 1.0e-12,
            "Measured audio duration does not match requested block rounding")
    for key in ("process", "body", "interval"):
        compare_statistics(summary.get(key), computed[key], f"summary.{key}")
    for key in ("measured_frames", "finite_samples", "nonfinite_samples", "nonzero_samples",
                "hardware_nonzero_after_clear"):
        require(integer(summary.get(key), f"summary.{key}") == computed[key],
                f"summary.{key} differs from CSV")
    for key in ("measured_audio_seconds", "sum_squares", "rms", "peak"):
        close(summary.get(key), computed[key], f"summary.{key}")

    xruns = mapping(report.get("device_xruns"), "device_xruns")
    native_start = integer(xruns.get("native_count_start"), "native_count_start", -1)
    native_end = integer(xruns.get("native_count_end"), "native_count_end", -1)
    supported = native_start >= 0 and native_end >= 0
    require(type(xruns.get("native_supported")) is bool and xruns["native_supported"] == supported,
            "Native xrun support flag disagrees with the returned counts")
    require(supported or native_start == native_end == -1,
            "Native xrun support changed during the capture")
    native_delta = native_end - native_start if supported else None
    require(native_delta is None or native_delta >= 0, "Native xrun counter was reset")
    require(xruns.get("native_delta") == native_delta
            and (native_delta is None or type(xruns.get("native_delta")) is int),
            "Native xrun delta disagrees with support / source counters")
    manager_start = integer(xruns.get("manager_count_start"), "manager_count_start")
    manager_end = integer(xruns.get("manager_count_end"), "manager_count_end")
    require(manager_end >= manager_start, "Manager counter was reset")
    estimated = manager_end - manager_start - (native_delta or 0)
    require(estimated >= 0, "Manager delta is smaller than native xrun delta")
    require(integer(xruns.get("manager_estimated_overruns"), "manager_estimated_overruns") == estimated,
            "Manager estimated overrun count disagrees with source counters")
    return {
        "measurementValid": True,
        "case": report["case"], "anyvoc_version": report["anyvoc_version"],
        "percentile_method": "nearest-rank: sorted[ceil(p * N) - 1]",
        "summary_comparison_tolerance": {"relative": FLOAT_REL_TOLERANCE, "absolute": FLOAT_ABS_TOLERANCE},
        "recording": {**expected_recording, "warmup_excluded": True,
                      "warmup_audio_seconds": warmup_blocks * block_size / rate,
                      "capture_body_span_seconds": (rows[-1]["body_end_ticks"] - rows[0]["entry_ticks"]) / frequency},
        "actual": actual,
        "observedPerformance": {
            **computed,
            "interval_jitter_ms": {"count": len(jitter_ms),
                                   "mean_signed": math.fsum(jitter_ms) / len(jitter_ms) if jitter_ms else None,
                                   "minimum_signed": min(jitter_ms) if jitter_ms else None,
                                   "maximum_signed": max(jitter_ms) if jitter_ms else None,
                                   "maximum_absolute": max(abs(value) for value in jitter_ms) if jitter_ms else None},
            "native_xruns": {"supported": supported, "count_delta": native_delta,
                             "interpretation": "Device-reported counter" if supported else "Unsupported; not a zero-dropout result"},
            "manager_estimated_overruns": estimated,
            "budget_interpretation": "Strict elapsed > num_samples / actual_sample_rate; not a hardware dropout detector",
            "no_dropouts_proven": False,
        },
    }


def self_test_fixture():
    """Handwritten timing oracle: one excluded warmup, then four 1-ms blocks."""
    rows = []
    # The 999-us warmup process time must never contaminate measured percentiles.
    for index, (entry, process, body) in enumerate((
        (1000, 999, 1100), (3000, 100, 200), (5000, 200, 1200),
        (7000, 300, 400), (9000, 1100, 1300),
    )):
        rows.append({
            "index": index, "phase": "warmup" if index == 0 else "measured",
            "start_sample": index * 48, "num_samples": 48, "sample_rate": 48000.0,
            "entry_ticks": entry, "process_start_ticks": entry + 10,
            "process_end_ticks": entry + 10 + process, "body_end_ticks": entry + body,
            "interval_ticks": 0 if index == 0 else 2000,
            "host_time_available": 0, "host_time_ns": 0,
            "finite_samples": 96, "nonfinite_samples": 0, "nonzero_samples": 96,
            "sum_squares": 24.0, "peak": .5, "hardware_nonzero_after_clear": 0,
        })
    report = {
        "schema_version": 1, "status": "valid", "case": "handwritten-oracle",
        "anyvoc_version": "0.20.2", "ticks_per_second": 1_000_000,
        "requested": {"seconds": .004, "warmup_seconds": .001, "sample_rate": 48000, "buffer_size": 48},
        "actual": {"device_type": "synthetic fixture (no device)", "device_name": "none",
                   "sample_rate": 48000.0, "buffer_size": 48, "input_channels": 0,
                   "output_channels": 2, "output_latency_samples": 48},
        "recording": {"warmup_blocks_requested": 1, "measured_blocks_requested": 4,
                      "capacity": 5, "recorded_blocks": 5, "warmup_blocks": 1,
                      "measured_blocks": 4, "complete": True},
        "summary": {
            "process": {"count": 4, "mean_ms": .425, "p50_ms": .2, "p95_ms": 1.1,
                        "p99_ms": 1.1, "max_ms": 1.1, "over_budget_callbacks": 1},
            "body": {"count": 4, "mean_ms": .775, "p50_ms": .4, "p95_ms": 1.3,
                     "p99_ms": 1.3, "max_ms": 1.3, "over_budget_callbacks": 2},
            "interval": {"count": 4, "mean_ms": 2.0, "p50_ms": 2.0, "p95_ms": 2.0,
                         "p99_ms": 2.0, "max_ms": 2.0, "over_budget_callbacks": None},
            "measured_frames": 192, "measured_audio_seconds": .004,
            "finite_samples": 384, "nonfinite_samples": 0, "nonzero_samples": 384,
            "sum_squares": 96.0, "rms": .5, "peak": .5, "hardware_nonzero_after_clear": 0,
        },
        "device_xruns": {"native_supported": False, "native_count_start": -1, "native_count_end": -1,
                         "native_delta": None, "manager_count_start": 3, "manager_count_end": 5,
                         "manager_estimated_overruns": 2},
        "errors": [], "parameters": [{"id": f"fixture-{index}", "plain": .5, "normalised": .5}
                                      for index in range(PARAMETER_COUNT)],
        "workload": {"description": "Synthetic arithmetic fixture only; no audio device was used"},
        "csv_file": "synthetic.csv",
    }
    return report, rows


def encode_fixture_csv(rows):
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue().encode("utf-8")


def run_self_tests():
    results = []

    def passed(name, action):
        action()
        results.append({"name": name, "status": "PASS"})

    def rejected(name, mutate):
        report, rows = self_test_fixture()
        mutate(report, rows)
        try:
            analyse(report, parse_csv(encode_fixture_csv(rows)))
        except AnalysisError as error:
            results.append({"name": name, "status": "PASS", "expected_rejection": str(error)})
            return
        raise AnalysisError(f"Self-test accepted invalid input: {name}")

    def check_oracle():
        report, rows = self_test_fixture()
        result = analyse(report, parse_csv(encode_fixture_csv(rows)))
        measured = result["observedPerformance"]
        require(result["measurementValid"], "Handwritten trace was rejected")
        require(measured["process"]["p50_ms"] == .2 and measured["process"]["p99_ms"] == 1.1,
                "Handwritten process percentile oracle failed")
        require(measured["body"]["p50_ms"] == .4, "Handwritten body percentile oracle failed")
        require(measured["process"]["over_budget_callbacks"] == 1
                and measured["body"]["over_budget_callbacks"] == 2,
                "Different process/body over-budget counts were lost")
        require(measured["native_xruns"]["count_delta"] is None and not measured["no_dropouts_proven"],
                "Unsupported native counter became a dropout claim")

    passed("handwritten timing, nearest-rank, warmup exclusion and separate budget counts", check_oracle)

    def check_percentile_edges():
        # Explicit order statistics: 20 values make rank 95% = 19 and rank 99% = 20.
        values = [20, 3, 18, 5, 16, 7, 14, 9, 12, 1, 10, 11, 8, 13, 6, 15, 4, 17, 2, 19]
        result = statistics(values)
        require(result["mean_ms"] == 10.5 and result["p50_ms"] == 10
                and result["p95_ms"] == 19 and result["p99_ms"] == 20 and result["max_ms"] == 20,
                "Independent 20-value percentile oracle failed")
        require(statistics([7])["p99_ms"] == 7, "One-element percentile failed")

    passed("independent 20-value percentile boundaries and singleton", check_percentile_edges)

    def check_actual_deadline():
        report, rows = self_test_fixture()
        report["requested"].update(sample_rate=0, buffer_size=0)
        rows[-1]["process_end_ticks"] = rows[-1]["process_start_ticks"] + 1000
        report["summary"]["process"].update(mean_ms=.4, p95_ms=1.0, p99_ms=1.0, max_ms=1.0,
                                            over_budget_callbacks=0)
        result = analyse(report, parse_csv(encode_fixture_csv(rows)))
        require(result["observedPerformance"]["process"]["over_budget_callbacks"] == 0,
                "Equality to actual 1-ms deadline was counted, or requested settings were used")

    passed("actual device deadline and strict greater-than boundary", check_actual_deadline)

    def check_native_supported():
        report, rows = self_test_fixture()
        report["device_xruns"].update(native_supported=True, native_count_start=5,
                                     native_count_end=7, native_delta=2,
                                     manager_count_start=10, manager_count_end=15,
                                     manager_estimated_overruns=3)
        result = analyse(report, parse_csv(encode_fixture_csv(rows)))
        require(result["observedPerformance"]["native_xruns"]["count_delta"] == 2,
                "Native device delta oracle failed")
        require(result["observedPerformance"]["manager_estimated_overruns"] == 3,
                "Manager delta did not subtract actual native counter delta")

    passed("supported native counter separated from manager estimate", check_native_supported)

    def check_single_callback():
        report, rows = self_test_fixture()
        # Legal large device block: a 0.1-s request rounds up to one 8192-frame block.
        row = rows[1]
        row.update(index=0, start_sample=0, num_samples=8192, interval_ticks=0,
                   finite_samples=16384, nonzero_samples=16384, sum_squares=4096)
        report["requested"].update(seconds=.1, warmup_seconds=0, buffer_size=8192)
        report["actual"].update(buffer_size=8192)
        report["recording"].update(warmup_blocks_requested=0, measured_blocks_requested=1,
                                   capacity=1, recorded_blocks=1, warmup_blocks=0, measured_blocks=1)
        report["summary"].update(
            process={"count": 1, "mean_ms": .1, "p50_ms": .1, "p95_ms": .1,
                     "p99_ms": .1, "max_ms": .1, "over_budget_callbacks": 0},
            body={"count": 1, "mean_ms": .2, "p50_ms": .2, "p95_ms": .2,
                  "p99_ms": .2, "max_ms": .2, "over_budget_callbacks": 0},
            interval={"count": 0, "mean_ms": 0, "p50_ms": 0, "p95_ms": 0,
                      "p99_ms": 0, "max_ms": 0, "over_budget_callbacks": None},
            measured_frames=8192, measured_audio_seconds=8192 / 48000,
            finite_samples=16384, nonzero_samples=16384, sum_squares=4096)
        result = analyse(report, parse_csv(encode_fixture_csv([row])))
        require(result["observedPerformance"]["interval"]["count"] == 0
                and result["observedPerformance"]["interval_jitter_ms"]["mean_signed"] is None,
                "Absent interval was treated as a measured zero-jitter observation")

    passed("single callback without inventing interval or jitter observations", check_single_callback)
    rejected("explicit sample-rate request was not honoured", lambda report, rows: report["requested"].update(sample_rate=44100))
    rejected("explicit buffer-size request was not honoured", lambda report, rows: report["requested"].update(buffer_size=256))
    rejected("nonfinite CSV metric", lambda report, rows: rows[2].update(sum_squares=float("nan")))
    rejected("missing callback row", lambda report, rows: rows.pop(2))
    rejected("tampered summary percentile", lambda report, rows: report["summary"]["process"].update(p50_ms=.3))
    rejected("unsupported native counter labelled supported", lambda report, rows: report["device_xruns"].update(native_supported=True))
    rejected("unsupported native counter falsely reported as zero", lambda report, rows: report["device_xruns"].update(native_delta=0))
    rejected("warmup row relabelled measured", lambda report, rows: rows[0].update(phase="measured"))
    rejected("hardware output not silenced", lambda report, rows: rows[3].update(hardware_nonzero_after_clear=1))
    rejected("nonfinite pre-clear output", lambda report, rows: rows[3].update(finite_samples=95, nonfinite_samples=1))
    rejected("timing boundaries overlap", lambda report, rows: rows[2].update(process_start_ticks=rows[2]["entry_ticks"] - 1))
    rejected("incorrect interval", lambda report, rows: rows[2].update(interval_ticks=1999))
    rejected("tampered manager overrun estimate", lambda report, rows: report["device_xruns"].update(manager_estimated_overruns=0))
    rejected("duplicate parameter ID", lambda report, rows: report["parameters"][1].update(id="fixture-0"))

    def silent(report, rows):
        for row in rows:
            row.update(nonzero_samples=0, sum_squares=0, peak=0)
        report["summary"].update(nonzero_samples=0, sum_squares=0, rms=0, peak=0)

    rejected("silent workload despite internally consistent summary", silent)
    return {"status": "PASS", "self_test": True, "passed": len(results), "failed": 0,
            "cases": results, "scope": "Synthetic script validation only; no audio device or production DSP was run"}


def assert_distinct_output(output, inputs):
    resolved = output.resolve()
    for input_path in inputs:
        require(resolved != input_path.resolve(), "Output must not overwrite an input")
        if output.exists() and input_path.exists():
            require(not output.samefile(input_path), "Output must not overwrite an input alias")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, help="Original probe JSON (schema 1)")
    parser.add_argument("--csv", type=Path, help="Original raw callback CSV; explicitly required with --report")
    parser.add_argument("--output", type=Path, required=True, help="Separate analysis JSON; never an input path")
    parser.add_argument("--self-test", action="store_true", help="Run only synthetic arithmetic and negative controls")
    args = parser.parse_args()
    if args.self_test:
        if args.report or args.csv:
            parser.error("--self-test cannot be combined with --report/--csv")
    elif args.report is None or args.csv is None:
        parser.error("--report and --csv are required unless --self-test is used")
    inputs = [path for path in (args.report, args.csv) if path is not None]
    try:
        assert_distinct_output(args.output, inputs)
    except AnalysisError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    result = {"status": "FAIL", "measurementValid": False, "failures": []}
    try:
        if args.self_test:
            result = run_self_tests()
        else:
            report_bytes = read_bounded(args.report, MAX_REPORT_BYTES)
            csv_bytes = read_bounded(args.csv, MAX_CSV_BYTES)
            result.update({"inputs": {
                "report": {"path": str(args.report.resolve()), "bytes": len(report_bytes),
                           "sha256": hashlib.sha256(report_bytes).hexdigest()},
                "csv": {"path": str(args.csv.resolve()), "bytes": len(csv_bytes),
                        "sha256": hashlib.sha256(csv_bytes).hexdigest()},
            }})
            original = json.loads(report_bytes.decode("utf-8-sig"), object_pairs_hook=unique_object,
                                  parse_constant=reject_constant)
            result.update(analyse(original, parse_csv(csv_bytes)))
            result["status"] = "PASS"
    except (AnalysisError, OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError, OverflowError) as error:
        result["status"] = "FAIL"
        result["measurementValid"] = False
        result.setdefault("failures", []).append(str(error))
    result["generated_utc"] = datetime.now(timezone.utc).isoformat()
    result["limitations"] = [
        "Checks consistency of supplied records; hashes bind these inputs but do not independently attest the binary or physical device.",
        "Body timing ends before final record publication and excludes device/driver work outside the probe callback.",
        "Interval jitter is callback-entry spacing, not a hardware clock or proof of audio dropouts.",
        "Native xrun -1 means unsupported; manager estimates and elapsed-time budgets are different observations.",
        "Pre-clear finite/nonzero checks inspect processor output, not all internal DSP values; production output already sanitises non-finite values.",
        "Output was intentionally silenced; no listening, playback PCM capture, DAW validation or zero-allocation claim is made.",
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"{result['status']}: {'self-test' if args.self_test else 'measurement consistency'}")
    if result.get("observedPerformance"):
        performance = result["observedPerformance"]
        print(f"Process/body over budget: {performance['process']['over_budget_callbacks']}/"
              f"{performance['body']['over_budget_callbacks']}; native xrun support: "
              f"{performance['native_xruns']['supported']}")
    for failure in result.get("failures", []):
        print(f"FAIL: {failure}", file=sys.stderr)
    print(f"Report: {args.output.resolve()}")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
