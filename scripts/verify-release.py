"""Verify completed Windows/source packages against the current release inputs.

Run from any directory with Python 3 after both package commands have finished:
    python scripts/verify-release.py

Uses only the Python standard library and Windows PowerShell for PE file versions.
It does not build, package, extract archives, run audio tests, or modify inputs.
The JSON report is written to dist, outside both ZIPs, only after all checks pass.
Run --self-test-paths to check archive-name validation without release artifacts.
"""

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import stat
import subprocess
import wave
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
HISTORICAL_DEMOS = (
    "Anyvoc-Demo.wav", "Anyvoc-v0.2-Demo.wav", "Anyvoc-v0.6-Warp-Demo.wav",
    "Anyvoc-v0.8-Voicing-Demo.wav", "Anyvoc-v0.12-Crossmod-Demo.wav",
    "Anyvoc-v0.13-Matrix-Demo.wav", "Anyvoc-v0.14-Sources-Demo.wav",
    "Anyvoc-v0.15-Envelopes-Demo.wav",
)
MOTION_DEMO = "Anyvoc-v0.16-Lfo-Motion-Demo.wav"
CHAOS_DEMO = "Anyvoc-v0.17-Chaos-Demo.wav"
MACRO_DEMO = "Anyvoc-v0.19-Macro-Demo.wav"
# Explicit version policies prevent a future release from claiming that its new
# features were checked by this script merely because the generic checks pass.
FEATURE_POLICIES = {
    "0.16.0": {
        "demos": (MOTION_DEMO,),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h", "Source/LfoModel.h"),
    },
    "0.17.0": {
        "demos": (MOTION_DEMO, CHAOS_DEMO),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h",
                    "Source/LfoModel.h", "Source/ChaosLfoModel.h"),
    },
    "0.18.0": {
        "demos": (MOTION_DEMO, CHAOS_DEMO),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h",
                    "Source/LfoModel.h", "Source/ChaosLfoModel.h",
                    "Source/LfoCurveDocument.h", "Source/LfoCurveDocument.cpp"),
    },
    "0.19.0": {
        "demos": (MOTION_DEMO, CHAOS_DEMO, MACRO_DEMO),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h",
                    "Source/LfoModel.h", "Source/ChaosLfoModel.h",
                    "Source/LfoCurveDocument.h", "Source/LfoCurveDocument.cpp"),
    },
    "0.20.0": {
        "demos": (MOTION_DEMO, CHAOS_DEMO, MACRO_DEMO),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h",
                    "Source/LfoModel.h", "Source/ChaosLfoModel.h",
                    "Source/LfoCurveDocument.h", "Source/LfoCurveDocument.cpp",
                    "Source/PluginProcessor.h", "Source/PluginProcessor.cpp",
                    "Source/PluginEditor.cpp"),
    },
    "0.20.1": {
        "demos": (MOTION_DEMO, CHAOS_DEMO, MACRO_DEMO),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h",
                    "Source/LfoModel.h", "Source/ChaosLfoModel.h",
                    "Source/LfoCurveDocument.h", "Source/LfoCurveDocument.cpp",
                    "Source/PluginProcessor.h", "Source/PluginProcessor.cpp",
                    "Source/PluginEditor.cpp"),
    },
    "0.20.2": {
        "demos": (MOTION_DEMO, CHAOS_DEMO, MACRO_DEMO),
        "headers": ("Source/ModulationRoute.h", "Source/EnvelopeModel.h",
                    "Source/LfoModel.h", "Source/ChaosLfoModel.h",
                    "Source/LfoCurveDocument.h", "Source/LfoCurveDocument.cpp",
                    "Source/PluginProcessor.h", "Source/PluginProcessor.cpp",
                    "Source/PluginEditor.cpp"),
    },
}
CHUNK_SIZE = 1024 * 1024


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def release_version():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(Anyvoc\s+VERSION\s+(\d+\.\d+\.\d+)(?:\s|\))", text)
    require(match is not None, "Could not read Anyvoc's version from CMakeLists.txt")
    return match.group(1)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK_SIZE), b""):
            value.update(block)
    return value.hexdigest()


def streams_equal(first, second):
    while True:
        left, right = first.read(CHUNK_SIZE), second.read(CHUNK_SIZE)
        if left != right:
            return False
        if not left:
            return True


def files_equal(first, second):
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as left, second.open("rb") as right:
        return streams_equal(left, right)


def normalize_name(name):
    """Canonical relative Windows-safe ZIP name; accepts either ZIP separator.

    Reject aliases instead of silently collapsing '.', empty components, drive
    prefixes, trailing spaces/dots, or Windows device names. A final separator is
    allowed for a directory entry and removed from its canonical name.
    """
    require(isinstance(name, str) and bool(name), f"Empty archive name: {name!r}")
    normalized = name.replace("\\", "/")
    require(not normalized.startswith("/"), f"Absolute archive name: {name!r}")
    if normalized.endswith("/"):
        normalized = normalized[:-1]
    parts = normalized.split("/")
    for part in parts:
        require(part not in ("", ".", ".."), f"Unsafe archive component: {name!r}")
        require(not any(ord(char) < 32 or char in '<>:"|?*' for char in part),
                f"Invalid Windows archive name: {name!r}")
        require(part == part.rstrip(" ."), f"Aliased Windows archive name: {name!r}")
        stem = part.split(".", 1)[0].rstrip(" ").upper()
        require(not re.fullmatch(r"CON|PRN|AUX|NUL|COM[1-9\u00b9\u00b2\u00b3]|LPT[1-9\u00b9\u00b2\u00b3]", stem),
                f"Reserved Windows device name: {name!r}")
    return normalized


def unique_names(names, label):
    duplicates = sorted(name for name, count in Counter(name.casefold() for name in names).items()
                        if count > 1)
    require(not duplicates, f"{label}: duplicate or case-aliased paths: {duplicates!r}")


def tree_files(path):
    require(path.is_dir(), f"Missing folder: {path}")
    files = {item.relative_to(path).as_posix(): item
             for item in path.rglob("*") if item.is_file()}
    for name in files:
        require(normalize_name(name) == name, f"Noncanonical input path: {name!r}")
    unique_names(files, str(path))
    return files


def exact_keys(actual, expected, label):
    actual, expected = set(actual), set(expected)
    missing, extra = sorted(expected - actual), sorted(actual - expected)
    require(not missing and not extra,
            f"{label}: missing={missing!r}; unexpected={extra!r}")


def verify_archive(path, expected):
    require(path.is_file(), f"Missing archive: {path}")
    unique_names(expected, f"Expected inputs for {path.name}")
    with ZipFile(path) as archive:
        entries = archive.infolist()
        names = [normalize_name(entry.filename) for entry in entries]
        unique_names(names, path.name)
        file_entries = []
        for entry, name in zip(entries, names):
            mode = stat.S_IFMT(entry.external_attr >> 16)
            require(mode in (0, stat.S_IFREG, stat.S_IFDIR),
                    f"Archive contains a link or special file: {path.name}: {name}")
            is_directory = entry.filename.endswith(("/", "\\"))
            require(mode != stat.S_IFDIR or is_directory,
                    f"Ambiguous directory entry: {path.name}: {name}")
            if is_directory:
                require(entry.file_size == 0, f"Nonempty directory entry: {path.name}: {name}")
            else:
                file_entries.append((entry, name))
        files = {name.casefold() for _, name in file_entries}
        for name in names:
            parts = name.casefold().split("/")
            require(not any("/".join(parts[:end]) in files for end in range(1, len(parts))),
                    f"Archive file is also a parent directory: {path.name}: {name}")
        exact_keys((name for _, name in file_entries), expected, str(path))
        # Validate every compressed entry, including any directory records.
        require(archive.testzip() is None, f"CRC failed: {path}")
        for entry, name in file_entries:
            original = expected[name]
            require(original.is_file(), f"Missing input: {original}")
            require(entry.file_size == original.stat().st_size,
                    f"Archive file size differs: {path.name}: {name}")
            with archive.open(entry) as packed, original.open("rb") as source:
                require(streams_equal(packed, source), f"Archive bytes differ: {path.name}: {name}")
    return {
        "path": str(path), "files": len(expected), "bytes": path.stat().st_size,
        "sha256": digest(path), "crc": "PASS", "safe_unique_paths": "PASS",
        "every_file_matches": "PASS", "exact_file_set": "PASS",
    }


def file_versions(paths):
    quoted = ",".join("'" + str(path).replace("'", "''") + "'" for path in paths)
    command = ("$ErrorActionPreference = 'Stop'; [Console]::OutputEncoding = "
               "[System.Text.UTF8Encoding]::new($false); $taskPaths = @(" + quoted + "); "
               "@($taskPaths | ForEach-Object { (Get-Item -LiteralPath $_).VersionInfo.FileVersion }) "
               "| ConvertTo-Json -Compress")
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                            check=True, capture_output=True, encoding="utf-8-sig")
    return json.loads(result.stdout)


def verify_demo(path):
    require(path.is_file(), f"Required release demo is missing: {path}")
    with wave.open(str(path), "rb") as audio:
        actual = (audio.getframerate(), audio.getsampwidth(), audio.getnchannels(), audio.getnframes())
        require(actual == (48000, 3, 2, 960000),
                f"Unexpected demo format/duration: {path.name}: {actual!r}")
        require(audio.getcomptype() == "NONE", f"Demo is not PCM: {path.name}")
        # A correct WAV header alone is insufficient: detect a truncated data chunk.
        frames = 0
        while True:
            block = audio.readframes(8192)
            if not block:
                break
            require(len(block) % 6 == 0, f"Incomplete PCM frame: {path.name}")
            frames += len(block) // 6
        require(frames == 960000, f"Truncated demo audio: {path.name}: {frames} frames")
    return {"path": str(path), "sample_rate": 48000, "bits_per_sample": 24,
            "channels": 2, "frames": 960000, "duration_seconds": 20,
            "bytes": path.stat().st_size, "sha256": digest(path)}


def self_test_paths():
    for raw, canonical in (("Anyvoc.exe", "Anyvoc.exe"),
                           ("Anyvoc.vst3\\Contents\\x86_64-win\\Anyvoc.vst3",
                            "Anyvoc.vst3/Contents/x86_64-win/Anyvoc.vst3"),
                           ("Anyvoc-Source/.github/", "Anyvoc-Source/.github"),
                           ("docs/快速开始.txt", "docs/快速开始.txt")):
        require(normalize_name(raw) == canonical, f"Path self-test failed: {raw!r}")
    invalid = ("", "/absolute", "\\\\host\\share", "C:/drive", "a/../b", "./a", "a//b",
               "a/./b", "a/", "a//", "a\\..\\b", "a:b", "a. ", "a.", "a ", "NUL.txt",
               "con", "folder/LPT1.log", "COM\u00b9.txt", "a\x00b", "a\nb", "a?b")
    # A single trailing separator is a valid directory record, unlike doubled ones.
    invalid = tuple(name for name in invalid if name != "a/")
    for name in invalid:
        try:
            normalize_name(name)
        except RuntimeError:
            continue
        raise RuntimeError(f"Unsafe path was accepted: {name!r}")
    try:
        unique_names(["docs/README.md", "DOCS/readme.md"], "self-test")
    except RuntimeError:
        pass
    else:
        raise RuntimeError("Case-aliased names were accepted")
    print(f"Path self-tests passed: 4 canonical names, {len(invalid)} rejected names, case alias rejection.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test-paths", action="store_true",
                        help="test path normalization only; do not inspect release artifacts")
    arguments = parser.parse_args()
    if arguments.self_test_paths:
        self_test_paths()
        return

    version = release_version()
    policy = FEATURE_POLICIES.get(version)
    release = DIST / f"Anyvoc-{version}-Windows"
    report_path = DIST / f"Anyvoc-{version}-PackageVerification.json"
    bundle = ROOT / "build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3"
    standalone = ROOT / "build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe"
    module = bundle / "Contents/x86_64-win/Anyvoc.vst3"

    windows_inputs = {"Anyvoc.exe": standalone}
    windows_inputs.update({"Anyvoc.vst3/" + name: path for name, path in tree_files(bundle).items()})
    windows_inputs.update({name: ROOT / name for name in
                           ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md")})
    windows_inputs["Install-Anyvoc.ps1"] = ROOT / "scripts/Install-Anyvoc.ps1"
    windows_inputs.update({"docs/" + name: path for name, path in tree_files(ROOT / "docs").items()})
    windows_inputs["快速开始.txt"] = ROOT / "docs/快速开始.txt"
    demos = [verify_demo(DIST / name) for name in policy["demos"]] if policy else []
    # Optional historical demos keep their old packaging rules. New feature
    # demos belong only to versions with an explicit acceptance policy.
    packaged_demos = dict.fromkeys((*HISTORICAL_DEMOS, MOTION_DEMO, CHAOS_DEMO,
                                   *(policy["demos"] if policy else ())))
    for name in packaged_demos:
        if (DIST / name).is_file():
            windows_inputs[name] = DIST / name
    windows_inputs["Licenses/JUCE-LICENSE.md"] = ROOT / "third_party/JUCE/LICENSE.md"
    windows_inputs["Licenses/VST3-LICENSE.txt"] = (
        ROOT / "third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt")

    folder_files = tree_files(release)
    exact_keys(folder_files, windows_inputs, "Unpacked Windows release")
    for name, original in windows_inputs.items():
        require(original.is_file(), f"Missing Windows package input: {original}")
        require(files_equal(folder_files[name], original),
                f"Unpacked Windows release differs from current input: {name}")

    source_files = [ROOT / name for name in ("CMakeLists.txt", "README.md", "LICENSE",
                    "THIRD_PARTY_NOTICES.md", ".gitignore", ".gitattributes", ".gitmodules")]
    for folder in ("Source", "Tests", "scripts", "docs", "third_party/JUCE", ".github"):
        source_files.extend(path for name, path in tree_files(ROOT / folder).items()
                            if ".git" not in path.relative_to(ROOT).parts
                            and "__pycache__" not in path.relative_to(ROOT).parts)
    source_inputs = {"Anyvoc-Source/" + path.relative_to(ROOT).as_posix(): path for path in source_files}
    require(len(source_inputs) == len(source_files), "Duplicate expected source file")
    require(any(name.startswith("Anyvoc-Source/.github/") for name in source_inputs),
            "Source package must include the repository's .github files")
    for path in source_inputs.values():
        require(path.is_file(), f"Missing source package input: {path}")
    for name in policy["headers"] if policy else ():
        require("Anyvoc-Source/" + name in source_inputs, f"Required implementation is missing: {name}")
    require(report_path not in windows_inputs.values() and report_path not in source_inputs.values(),
            "Verification report must remain outside the archives")

    archives = [
        verify_archive(DIST / f"Anyvoc-{version}-Windows-x64.zip", windows_inputs),
        verify_archive(DIST / f"Anyvoc-{version}-Source.zip", source_inputs),
    ]
    binary_pairs = [(release / "Anyvoc.exe", standalone),
                    (release / "Anyvoc.vst3/Contents/x86_64-win/Anyvoc.vst3", module)]
    versions = file_versions([path for pair in binary_pairs for path in pair])
    require(versions == [version] * 4, f"Unexpected binary versions: {versions!r}; expected {version}")
    binaries = []
    for index, (packaged, built) in enumerate(binary_pairs):
        sha = digest(packaged)
        require(sha == digest(built), f"Packaged binary differs from build: {packaged}")
        binaries.append({"path": str(packaged), "build_path": str(built),
                         "bytes": packaged.stat().st_size, "sha256": sha,
                         "file_version": versions[index * 2], "build_file_version": versions[index * 2 + 1],
                         "matches_current_build": True})
    report = {
        "version": version, "checked_at": datetime.now().astimezone().isoformat(),
        "archives": archives, "binaries": binaries, "required_demos": demos,
        "feature_policy": "defined" if policy else "not_defined_for_this_version",
        "scope": "Package integrity and current input equality only; no build, DSP, host, or performance tests run.",
        "windows_folder_exact_file_set": True, "windows_folder_every_file_matches_inputs": True,
        "current_docs_match": True, "report_external_to_archives": True,
    }
    if policy is None:
        report["feature_policy_note"] = (
            "No version-specific demo format or implementation-header requirements were checked. "
            "Add an explicit policy before claiming feature-specific release acceptance.")
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
