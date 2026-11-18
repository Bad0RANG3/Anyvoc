"""Verify and archive a completed 0.20.2 device-callback capture; never run a device.

Run ONLY after the capture runner has exited:
  python build/validation-realtime-probe/analyze-suite.py --capture-complete

Optional --repo, --capture-root, --archive and --work paths allow relocating the
original capture directory. Original JSON may retain its historic absolute paths;
the analyzer receives explicit current paths. This script does not overwrite an
existing archive or work directory. It writes an external archive-directory.json
beside the work directory to bind every final archived byte without self-hashing.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys


CASES = ["baseline", "unison", "matrix", "chaos", "chaos-rossler", "granular"]
EXE_SHA256 = "a480fcb05a8f8279ab7356063cebb02c9938865db0023c6476bf26b7c618b027"
SUFFIXES = (".json", ".csv", ".stdout.txt", ".stderr.txt")
SUPPORT = ("environment.json", "configure.txt", "build-first.txt", "help.txt",
           "devices.txt", "analyzer-self-test.json", "analyzer-self-test-final.json",
           "suite-console.txt", "short-suite-console.txt")
SHORT_DEFAULT = ("short-default.json", "short-default.csv", "short-default-console.txt",
                 "short-default-analysis.json")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"Non-finite JSON constant: {value}")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object,
                      parse_constant=reject_constant)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def info(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def canonical_digest(value):
    data = json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def directory_info(directory):
    files = {p.relative_to(directory).as_posix(): info(p)
             for p in sorted(directory.rglob("*")) if p.is_file()}
    # One unambiguous UTF-8 record per sorted relative path, LF line endings.
    records = "".join(f"{name}\t{item['bytes']}\t{item['sha256']}\n"
                      for name, item in sorted(files.items()))
    return {"file_count": len(files), "total_bytes": sum(v["bytes"] for v in files.values()),
            "set_sha256": hashlib.sha256(records.encode("utf-8")).hexdigest(),
            "set_hash_format": "SHA256 of sorted UTF-8 relative-path TAB decimal-bytes TAB lowercase-sha256 LF",
            "files": files}


def safe_artifact(directory, name):
    require(Path(name).name == name and "/" not in name and "\\" not in name,
            f"Unsafe artifact name: {name}")
    path = directory / name
    require(path.is_file() and not path.is_symlink(), f"Missing/non-regular artifact: {path}")
    return path


def parameters(report):
    rows = report["parameters"]
    require(isinstance(rows, list) and len(rows) == 1256, "Expected all 1256 parameters")
    mapped = {}
    for row in rows:
        require(set(row) == {"id", "plain", "normalised"}, "Unexpected parameter fields")
        name = row["id"]
        require(isinstance(name, str) and name and name not in mapped, "Invalid/duplicate parameter ID")
        for key in ("plain", "normalised"):
            require(type(row[key]) in (float, int) and math.isfinite(row[key]), "Invalid parameter value")
        require(0 <= row["normalised"] <= 1, "Normalised parameter outside [0,1]")
        mapped[name] = {"plain": row["plain"], "normalised": row["normalised"]}
    return mapped


def validate_short(root):
    """Bind existing short analyses to their originals; do not mix into formal totals."""
    short = root / "short-suite-48k256"
    manifest = read_json(short / "suite-manifest.json")
    require(manifest["complete"] is True and len(manifest["runs"]) == 6, "Incomplete short suite")
    require(manifest["requested"] == {"sample_rate": 48000, "buffer_size": 256,
                                      "seconds": 3, "warmup": 1, "rounds": 1}, "Unexpected short protocol")
    require({run["case"] for run in manifest["runs"]} == set(CASES), "Short cases differ")
    result = []
    for run in manifest["runs"]:
        stem = f"round-1-{run['case']}"
        require(run["status"] == "valid" and run["exit_code"] == 0, "Invalid short capture")
        require(run["exe_sha256_before"] == run["exe_sha256_after"] == EXE_SHA256,
                "Short executable identity differs")
        require(set(run["artifacts"]) == {stem + s for s in SUFFIXES}, "Short artifact set differs")
        for name, expected in run["artifacts"].items():
            require(info(safe_artifact(short, name)) == expected, f"Short artifact mismatch: {name}")
        analysis = read_json(short / (stem + ".analysis.json"))
        require(analysis["measurementValid"] is True and analysis["status"] == "PASS", "Invalid short analysis")
        for key, suffix in (("report", ".json"), ("csv", ".csv")):
            actual = info(short / (stem + suffix))
            require(all(analysis["inputs"][key][field] == actual[field] for field in actual),
                    "Short analysis input identity differs")
        perf = analysis["observedPerformance"]
        result.append({"case": run["case"], "round": 1,
                       "process_over_budget": perf["process"]["over_budget_callbacks"],
                       "body_over_budget": perf["body"]["over_budget_callbacks"],
                       "included_in_formal_statistics": False})
    default = read_json(root / "short-default-analysis.json")
    require(default["status"] == "PASS" and default["measurementValid"] is True,
            "Invalid short-default analysis")
    for key, name in (("report", "short-default.json"), ("csv", "short-default.csv")):
        actual = info(root / name)
        require(all(default["inputs"][key][field] == actual[field] for field in actual),
                "Short-default analysis input identity differs")
    return result


def tables(aggregates, runs):
    text = ["# Real-device callback observations: 48 kHz / 256 samples", "",
            "Formal captures only: 6 cases × 3 rounds, each 3 s requested warmup and 60 s measured. "
            "The per-block deadline is 5.333333 ms. Values below are milliseconds. "
            "Percentiles use nearest-rank; warmup is excluded from callback timings. "
            "Measurement consistency does not establish real-time reliability or absence of dropouts.", "",
            "Native output xrun counters are unsupported (-1); their absence is unknown, not zero. "
            "Manager estimates include warmup, outer-manager work and a short detach margin. "
            "They are not driver xrun counts and need not equal measured over-budget counts. "
            "Short captures in short/ are excluded from all tables below.", "",
            "## Three-round case summary", "",
            "| Case | Process mean range | Max process p99 | Max process | Body mean range | Max body p99 | Max body | Process > deadline (total) | Body > deadline (total) | Manager estimate (total) |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for row in aggregates:
        p, b = row["process"], row["body"]
        text.append(f"| {row['case']} | {p['mean_min_ms']:.6f}–{p['mean_max_ms']:.6f} | "
                    f"{p['largest_p99_ms']:.6f} | {p['max_ms']:.6f} | "
                    f"{b['mean_min_ms']:.6f}–{b['mean_max_ms']:.6f} | "
                    f"{b['largest_p99_ms']:.6f} | {b['max_ms']:.6f} | "
                    f"{p['over_budget_callbacks']} | {b['over_budget_callbacks']} | {row['manager_estimated_overruns']} |")
    for metric, title in (("process", "processBlock"), ("body", "Instrumented callback body")):
        text += ["", f"## All 18 runs: {title}", "",
                 "| Round | Case | Mean | p50 | p95 | p99 | Max | Process > deadline | Body > deadline | Manager estimate |",
                 "|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for row in runs:
            perf, stats = row["observedPerformance"], row["observedPerformance"][metric]
            text.append(f"| {row['round']} | {row['case']} | {stats['mean_ms']:.6f} | "
                        f"{stats['p50_ms']:.6f} | {stats['p95_ms']:.6f} | {stats['p99_ms']:.6f} | "
                        f"{stats['max_ms']:.6f} | {perf['process']['over_budget_callbacks']} | "
                        f"{perf['body']['over_budget_callbacks']} | {perf['manager_estimated_overruns']} |")
    text += ["", "The callback body ends before final record publication and excludes the outer JUCE/driver stack. "
             "Hardware output was cleared; these records are not listening or audible-dropout evidence. "
             "No allocation, lock-freedom or DAW conclusion is made.", ""]
    return "\n".join(text)


def execute(args):
    repo = args.repo.resolve(strict=True)
    capture = (args.capture_root or repo / "build/realtime-probe-0.20.2").resolve(strict=True)
    archive = (args.archive or repo / "docs/realtime-probe-0.20.2").resolve()
    work = (args.work or repo / "build/validation-realtime-probe/suite-postprocess").resolve()
    require(not archive.exists() and not work.exists(), "Archive/work directory already exists; preserve it")
    work.mkdir(parents=True)
    formal = capture / "suite-48k256"
    manifest_path = formal / "suite-manifest.json"
    original_manifest = info(manifest_path)
    manifest = read_json(manifest_path)
    require(manifest["complete"] is True and manifest.get("finished_utc"), "Capture has not completed")
    require(manifest["requested"] == {"sample_rate": 48000, "buffer_size": 256,
                                      "seconds": 60, "warmup": 3, "rounds": 3}, "Unexpected formal protocol")
    require(manifest["cases"] == CASES and len(manifest["runs"]) == 18, "Expected six cases and 18 runs")
    require(manifest["exe_sha256"] == EXE_SHA256, "Unexpected formal executable identity")
    source_paths = [Path("Tests/RealtimeProbe.cpp"), Path("CMakeLists.txt")]
    source_paths += sorted(p.relative_to(repo) for p in (repo / "Source").glob("*.cpp"))
    source_paths += sorted(p.relative_to(repo) for p in (repo / "Source").glob("*.h"))
    current_source = {p.as_posix(): info(repo / p)["sha256"] for p in source_paths}
    require(current_source == manifest["source_sha256"], "Full source snapshot changed or is incomplete")
    require(subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
            == manifest["source_base_commit"], "Repository base commit changed")
    require(subprocess.check_output(["git", "-C", "third_party/JUCE", "rev-parse", "HEAD"],
                                    cwd=repo, text=True).strip() == manifest["juce_commit"], "JUCE identity changed")
    runner = repo / "scripts/run_realtime_probe.py"
    analyzer = repo / "scripts/analyze_realtime_probe.py"
    require(info(runner)["sha256"] == manifest["driver_sha256"], "Runner changed")
    analyzer_identity = info(analyzer)
    self_test = read_json(capture / "analyzer-self-test-final.json")
    require(self_test["status"] == "PASS" and self_test["passed"] == 20 and self_test["failed"] == 0,
            "Missing expected 20/20 analyzer self-test evidence")
    staged = work / "archive"
    staged.mkdir()
    original_copies = {}

    def copy(source, relative):
        destination = staged / relative
        require(not destination.exists(), f"Duplicate destination: {relative}")
        before = info(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        require(info(destination) == before == info(source), f"Original changed during copy: {source}")
        original_copies[relative] = {"source": str(source), **before}

    results, per_case = [], {}
    all_ids = None
    device = None
    for index, run in enumerate(manifest["runs"]):
        expected_round = index // 6 + 1
        expected_case = CASES[(index % 6 + expected_round - 1) % 6]
        require(run["round"] == expected_round and run["case"] == expected_case,
                "Missing/duplicate/reordered formal capture")
        require(run["status"] == "valid" and run["exit_code"] == 0, "Invalid formal capture")
        require(run["exe_sha256_before"] == run["exe_sha256_after"] == EXE_SHA256,
                "Executable changed during a capture")
        stem = f"round-{expected_round}-{expected_case}"
        cmd = run["command"]
        expected_tail = ["--case", expected_case, "--seconds", "60", "--warmup", "3",
                         "--sample-rate", "48000", "--buffer-size", "256", "--output", str(formal / stem)]
        require(cmd[1:] == expected_tail, f"Capture command differs: {stem}")
        require(info(Path(cmd[0]))["sha256"] == EXE_SHA256, "Current executable differs from capture")
        require(set(run["artifacts"]) == {stem + s for s in SUFFIXES}, "Raw artifact set differs")
        for name, expected in run["artifacts"].items():
            source = safe_artifact(formal, name)
            require(info(source) == expected, f"Raw artifact bytes/hash mismatch: {name}")
            copy(source, "runs/" + name)
        report = read_json(formal / (stem + ".json"))
        require(report["case"] == expected_case and report["anyvoc_version"] == "0.20.2", "Report identity differs")
        require(report["requested"] == {"seconds": 60, "warmup_seconds": 3,
                                        "sample_rate": 48000, "buffer_size": 256}, "Per-run protocol differs")
        actual = report["actual"]
        require(actual["sample_rate"] == 48000 and actual["buffer_size"] == 256
                and actual["input_channels"] == 0 and actual["output_channels"] == 2
                and actual["device_type"] == "Windows Audio", "Actual shared device configuration differs")
        if device is None:
            device = actual
        require(actual == device, "Actual device/configuration changed between formal runs")
        require(report["recording"]["warmup_blocks"] == 563
                and report["recording"]["measured_blocks"] == 11250, "Unexpected block counts")
        params = parameters(report)
        if all_ids is None:
            all_ids = sorted(params)
        require(sorted(params) == all_ids, "Parameter ID sets differ across cases")
        fixture = {"parameters": params, "workload": report["workload"]}
        if expected_case not in per_case:
            per_case[expected_case] = fixture
        require(fixture == per_case[expected_case], f"Parameter/fixture changed across rounds: {expected_case}")
        require(report["device_xruns"]["native_supported"] is False
                and report["device_xruns"]["native_count_start"] == -1
                and report["device_xruns"]["native_count_end"] == -1
                and report["device_xruns"]["native_delta"] is None, "Unexpected native xrun contract")
        out = staged / "analysis" / (stem + ".analysis.json")
        out.parent.mkdir(parents=True, exist_ok=True)
        command = [sys.executable, str(analyzer), "--report", str(formal / (stem + ".json")),
                   "--csv", str(formal / (stem + ".csv")), "--output", str(out)]
        proc = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        out.with_suffix(".stdout.txt").write_bytes(proc.stdout)
        out.with_suffix(".stderr.txt").write_bytes(proc.stderr)
        require(proc.returncode == 0, f"Independent analyzer failed for {stem}: {proc.stderr.decode(errors='replace')}")
        analysis = read_json(out)
        require(analysis["status"] == "PASS" and analysis["measurementValid"] is True,
                f"Invalid independently recomputed measurement: {stem}")
        perf = analysis["observedPerformance"]
        require(perf["measured_audio_seconds"] == 60 and perf["measured_frames"] == 2880000,
                "Incorrect measured duration")
        results.append({"round": expected_round, "case": expected_case,
                        "measurementValid": True, "analysis": "analysis/" + out.name,
                        "parameter_count": len(params), "parameters_sha256": canonical_digest(params),
                        "workload_sha256": canonical_digest(report["workload"]),
                        "observedPerformance": perf})
    require(info(analyzer) == analyzer_identity, "Analyzer changed during analysis")
    require(info(manifest_path) == original_manifest, "Manifest changed during verification")
    require({p.as_posix(): info(repo / p)["sha256"] for p in source_paths} == current_source,
            "Source changed during verification")
    sample_hash = per_case["granular"]["workload"]["sample_sha256"]
    require(isinstance(sample_hash, str) and len(sample_hash) == 64
            and all(c in "0123456789abcdef" for c in sample_hash), "Invalid granular asset SHA")
    copy(manifest_path, "suite-manifest.json")
    for name in SUPPORT:
        copy(capture / name, "support/" + name)
    short_results = validate_short(capture)
    for source in sorted((capture / "short-suite-48k256").iterdir()):
        require(source.is_file() and not source.is_symlink(), "Unexpected short evidence object")
        copy(source, "short/suite-48k256/" + source.name)
    for name in SHORT_DEFAULT:
        copy(capture / name, "short/" + name)
    for source in (analyzer, runner, Path(__file__).resolve()):
        copy(source, "tools/" + source.name)
    aggregates = []
    for case in CASES:
        selected = [r["observedPerformance"] for r in results if r["case"] == case]
        row = {"case": case, "rounds": len(selected), "measurementValid": True,
               "measured_callbacks": sum(p["process"]["count"] for p in selected),
               "measured_audio_seconds": sum(p["measured_audio_seconds"] for p in selected),
               "manager_estimated_overruns": sum(p["manager_estimated_overruns"] for p in selected),
               "native_xruns": {"supported": False, "count_delta": None}}
        for metric in ("process", "body"):
            row[metric] = {"mean_min_ms": min(p[metric]["mean_ms"] for p in selected),
                           "mean_max_ms": max(p[metric]["mean_ms"] for p in selected),
                           "largest_p99_ms": max(p[metric]["p99_ms"] for p in selected),
                           "max_ms": max(p[metric]["max_ms"] for p in selected),
                           "over_budget_callbacks": sum(p[metric]["over_budget_callbacks"] for p in selected)}
        aggregates.append(row)
    (staged / "table-rows.md").write_text(tables(aggregates, results), encoding="utf-8", newline="\n")
    verification = {
        "schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
        "measurementValid": True, "verification_status": "consistent", "real_time_pass_claim": False,
        "protocol": manifest["requested"], "actual": device, "formal_run_count": 18,
        "formal_measured_callbacks": sum(r["observedPerformance"]["process"]["count"] for r in results),
        "formal_measured_audio_seconds": sum(r["observedPerformance"]["measured_audio_seconds"] for r in results),
        "source_base_commit": manifest["source_base_commit"], "juce_commit": manifest["juce_commit"],
        "executable_sha256": EXE_SHA256, "source_snapshot_matches_current": True,
        "source_sha256": current_source, "runner_sha256": manifest["driver_sha256"],
        "analyzer": analyzer_identity, "python_version": sys.version,
        "per_case_parameters_and_workloads_identical_across_three_rounds": True,
        "parameter_count_each": 1256, "parameter_id_set_sha256": canonical_digest(all_ids),
        "granular_sample_sha256_all_rounds": sample_hash, "case_summary": aggregates, "runs": results,
        "short_captures": {"included_in_formal_statistics": False, "suite": short_results,
                           "default": "Archived separately; existing analysis input hashes verified"},
        "original_byte_copies": original_copies,
        "archive_payload_before_this_verification": directory_info(staged),
        "limitations": [
            "Consistency verification is not a real-time performance pass; over-budget observations are retained.",
            "Native output xruns are unsupported (-1); absence of dropouts cannot be established.",
            "Manager overrun estimates include warmup, outer-manager work and a short detach margin.",
            "Machine/background load was not isolated or controlled; these runs describe this environment only.",
            "Body excludes final record publication and outer JUCE/driver work; no zero-allocation or lock-freedom claim.",
            "Pre-clear output was finite and nonzero; output was intentionally silenced. No listening or DAW validation.",
            "Short captures are archived for provenance and excluded from every formal total and table.",
            "Full final directory hash is written externally to avoid a self-referential digest."
        ]
    }
    write_json(staged / "suite-verification.json", verification)
    archive.parent.mkdir(parents=True, exist_ok=True)
    staged.rename(archive)
    final = {"archive": str(archive), **directory_info(archive)}
    write_json(work / "archive-directory.json", final)
    print(json.dumps({"measurementValid": True, "real_time_pass_claim": False,
                      "archive": str(archive), "file_count": final["file_count"],
                      "total_bytes": final["total_bytes"], "set_sha256": final["set_sha256"],
                      "case_summary": aggregates}, ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-complete", action="store_true", help="Run only after the recorder has exited")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--capture-root", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()
    if not args.capture_complete:
        parser.error("Wait for recorder completion, then explicitly pass --capture-complete")
    try:
        execute(args)
    except Exception as error:
        print(f"VERIFICATION FAILED: {type(error).__name__}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
