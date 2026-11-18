"""Independent, read-only 0.20.3 input checks; writes three new JSON reports.

No build, renderer, executable, device, or package command is launched. Git is
used only to read the frozen source commit. PE versions use Windows version.dll.
Run only after all final binaries and four freshly rendered WAVs are frozen.
"""

import argparse
import contextlib
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import subprocess
import sys
import wave
import zipfile


ROOT = Path(__file__).resolve().parents[2]
BINARIES = {
    "Standalone": "build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe",
    "VST3": "build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3/Contents/x86_64-win/Anyvoc.vst3",
    "EngineTests": "build/AnyvocEngineTests_artefacts/Release/AnyvocEngineTests.exe",
    "ProcessorTests": "build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe",
    "WavetableTests": "build/AnyvocWavetableTests_artefacts/Release/AnyvocWavetableTests.exe",
    "PresetLibraryTests": "build/AnyvocPresetLibraryTests_artefacts/Release/AnyvocPresetLibraryTests.exe",
    "RealtimeProbe": "build/AnyvocRealtimeProbe_artefacts/Release/AnyvocRealtimeProbe.exe",
    "pluginval": "tools/pluginval/pluginval.exe",
}
DEMOS = {
    "Legacy": ("Anyvoc-Demo.wav", 720000),
    "Motion": ("Anyvoc-v0.16-Lfo-Motion-Demo.wav", 960000),
    "Chaos": ("Anyvoc-v0.17-Chaos-Demo.wav", 960000),
    "Macro": ("Anyvoc-v0.19-Macro-Demo.wav", 960000),
}
EDITOR_FOOTER_BEFORE = b'        drawText (g, "ANYVOC  /  0.20.2", { 955, 744, 144, 13 }, 8.5f, secondaryText, false, juce::Justification::centredRight);'
EDITOR_FOOTER_AFTER = b'        drawText (g, "ANYVOC  /  " JucePlugin_VersionString, { 955, 744, 144, 13 }, 8.5f, secondaryText, false, juce::Justification::centredRight);'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_stable(path):
    before = path.stat()
    data = path.read_bytes()
    after = path.stat()
    require((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns)
            and len(data) == after.st_size, f"Input changed while reading: {path}")
    return data


def version_parts(text):
    require(re.fullmatch(r"\d+(?:\.\d+){2,3}", text) is not None,
            f"Unexpected version string: {text!r}")
    values = tuple(map(int, text.split(".")))
    return values + (0,) * (4 - len(values))


def pe_versions(path):
    require(os.name == "nt", "PE version extraction requires Windows")
    api = ctypes.WinDLL("version", use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    api.GetFileVersionInfoW.restype = wintypes.BOOL
    api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR,
                                   ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    api.VerQueryValueW.restype = wintypes.BOOL
    unused = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(str(path), ctypes.byref(unused))
    require(size > 0, f"No PE version resource: {path}")
    buffer = ctypes.create_string_buffer(size)
    require(api.GetFileVersionInfoW(str(path), 0, size, buffer), f"Cannot read PE version: {path}")

    def query(key):
        pointer, length = ctypes.c_void_p(), wintypes.UINT()
        require(api.VerQueryValueW(buffer, key, ctypes.byref(pointer), ctypes.byref(length)),
                f"Missing PE version key {key}: {path}")
        return pointer.value, length.value

    pointer, length = query("\\")
    require(length >= 52, f"Truncated VS_FIXEDFILEINFO: {path}")
    fixed = struct.unpack("<13I", ctypes.string_at(pointer, 52))
    require(fixed[0] == 0xFEEF04BD, f"Wrong PE version signature: {path}")
    fixed_file = (fixed[2] >> 16, fixed[2] & 65535, fixed[3] >> 16, fixed[3] & 65535)
    fixed_product = (fixed[4] >> 16, fixed[4] & 65535, fixed[5] >> 16, fixed[5] & 65535)
    pointer, length = query("\\VarFileInfo\\Translation")
    require(length >= 4 and length % 4 == 0, f"Malformed PE translation table: {path}")
    translations = struct.iter_unpack("<HH", ctypes.string_at(pointer, length))
    localized = []
    for language, codepage in translations:
        values = {}
        for key in ("FileVersion", "ProductVersion"):
            pointer, length = query(f"\\StringFileInfo\\{language:04x}{codepage:04x}\\{key}")
            require(length > 0, f"Empty {key}: {path}")
            values[key] = ctypes.wstring_at(pointer, length).rstrip("\x00").strip()
        require(version_parts(values["FileVersion"]) == fixed_file
                and version_parts(values["ProductVersion"]) == fixed_product,
                f"Fixed/localized PE versions disagree: {path}")
        localized.append({"language": language, "codepage": codepage, **values})
    return {"file_version": localized[0]["FileVersion"],
            "product_version": localized[0]["ProductVersion"],
            "fixed_file_version": ".".join(map(str, fixed_file)),
            "fixed_product_version": ".".join(map(str, fixed_product)),
            "localized_versions": localized}


def check_binaries(args):
    records = []
    for role, relative in BINARIES.items():
        path = args.root / relative
        data = read_stable(path)
        require(len(data) >= 64 and data[:2] == b"MZ", f"Not a DOS/PE image: {path}")
        offset = struct.unpack_from("<I", data, 60)[0]
        require(offset + 6 <= len(data) and data[offset:offset + 4] == b"PE\0\0", f"Not PE: {path}")
        versions = pe_versions(path)
        require(read_stable(path) == data, f"PE changed during version extraction: {path}")
        expected = args.pluginval_version if role == "pluginval" else args.version
        require(version_parts(versions["file_version"]) == version_parts(expected)
                and version_parts(versions["product_version"]) == version_parts(expected),
                f"Wrong {role} version: {versions}")
        records.append({"role": role, "path": relative, "bytes": len(data), "sha256": digest(data),
                        "pe_machine": hex(struct.unpack_from("<H", data, offset + 4)[0]),
                        "expected_version": expected, **versions})
    return {"version": args.version, "binary_count": len(records), "binaries": records,
            "scope": "PE identity and version resources only; no executable was launched."}


def git(args, *command):
    return subprocess.check_output(["git", "-C", str(args.root), *command])


def normalize_newlines(data):
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def check_sources(args):
    commit = git(args, "rev-parse", "--verify", args.baseline_commit + "^{commit}").decode().strip()
    paths = sorted(p.decode("utf-8") for p in git(args, "ls-tree", "-rz", "--name-only", commit, "--", "Source").split(b"\0")
                   if p and PurePosixPath(p.decode("utf-8")).suffix in (".cpp", ".h"))
    current = sorted(p.relative_to(args.root).as_posix() for p in (args.root / "Source").rglob("*")
                     if p.is_file() and p.suffix in (".cpp", ".h"))
    require(current == paths, "Production .cpp/.h file set differs from baseline")
    records, raw_changed, normalized_changed = [], [], []
    editor_footer_verified = False
    for path in paths:
        old, new = git(args, "show", f"{commit}:{path}"), read_stable(args.root / path)
        normalized_old, normalized_new = normalize_newlines(old), normalize_newlines(new)
        if old != new:
            raw_changed.append(path)
        if normalized_old != normalized_new:
            normalized_changed.append(path)
        if path == "Source/PluginEditor.cpp":
            require(old.count(EDITOR_FOOTER_BEFORE) == 1 and new.count(EDITOR_FOOTER_AFTER) == 1,
                    "Editor version-display replacement is missing or ambiguous")
            require(old.replace(EDITOR_FOOTER_BEFORE, EDITOR_FOOTER_AFTER, 1) == new,
                    "Editor differs beyond the exact version-display replacement")
            editor_footer_verified = True
        records.append({"path": path, "baseline_bytes": len(old), "current_bytes": len(new),
                        "baseline_sha256": digest(old), "current_sha256": digest(new),
                        "baseline_lf_sha256": digest(normalized_old), "current_lf_sha256": digest(normalized_new),
                        "byte_identical": old == new, "lf_identical": normalized_old == normalized_new})
    allowed = ["Source/PluginEditor.cpp", "Source/SynthEngine.cpp"]
    require(raw_changed == allowed and normalized_changed == raw_changed and editor_footer_verified,
            f"Unexpected production changes: raw={raw_changed}, LF={normalized_changed}")
    return {"baseline_commit": commit, "source_file_count": len(records),
            "raw_changed_files": raw_changed, "lf_changed_files": normalized_changed,
            "allowed_changed_files": allowed, "files": records,
            "editor_version_display_fix": {"path": "Source/PluginEditor.cpp",
                                           "before": EDITOR_FOOTER_BEFORE.decode("ascii"),
                                           "after": EDITOR_FOOTER_AFTER.decode("ascii"),
                                           "exact_replacement_verified": editor_footer_verified,
                                           "all_remaining_bytes_identical": editor_footer_verified},
            "normalization": "CRLF and standalone CR converted to LF; raw equality checked separately.",
            "scope": "All production Source .cpp/.h bytes. Only the exact Editor footer replacement and SynthEngine cache implementation may differ; no semantic-equivalence claim for the latter."}


def safe_archive_names(infos):
    entries = {}
    for info in infos:
        name = info.orig_filename
        require(name == info.filename and name and "\\" not in name and "\x00" not in name and not name.startswith("/"),
                f"Unsafe ZIP spelling: {name!r}")
        parts = (name[:-1] if info.is_dir() else name).split("/")
        for component in parts:
            require(component not in ("", ".", "..") and component.rstrip(" .") == component
                    and not any(ord(c) < 32 or c in ':<>"|?*' for c in component), f"Unsafe ZIP member: {name!r}")
            require(not re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])", component.split(".")[0], re.I),
                    f"Reserved Windows ZIP member: {name!r}")
        require(stat.S_IFMT(info.external_attr >> 16) != stat.S_IFLNK, f"Symlink ZIP member: {name!r}")
        key = "/".join(parts).casefold()
        require(key not in entries, f"Duplicate/aliased ZIP member: {name!r}")
        entries[key] = info.is_dir()
    for key in entries:
        parts = key.split("/")
        for length in range(1, len(parts)):
            parent = "/".join(parts[:length])
            require(parent not in entries or entries[parent], f"ZIP file used as directory: {key!r}")
    return len(entries)


def wav_format(data, expected_frames):
    with wave.open(io.BytesIO(data), "rb") as wav:
        channels, rate, width, frames = wav.getnchannels(), wav.getframerate(), wav.getsampwidth(), wav.getnframes()
        require(wav.getcomptype() == "NONE" and (channels, rate, width, frames) == (2, 48000, 3, expected_frames),
                f"Unexpected WAV format: {(channels, rate, width, frames)}")
        require(len(wav.readframes(frames + 1)) == frames * channels * width, "Truncated WAV data")
    return {"channels": channels, "sample_rate_hz": rate, "bits_per_sample": width * 8,
            "frames": frames, "duration_seconds": frames / rate}


def check_demos(args):
    archive_bytes = read_stable(args.baseline_zip)
    report_bytes = read_stable(args.baseline_report)
    report = json.loads(report_bytes.decode("utf-8-sig"))
    candidates = [item for item in report["archives"] if Path(item["path"]).name == args.baseline_zip.name]
    require(report["version"] == args.baseline_version and len(candidates) == 1, "Wrong/ambiguous frozen package report")
    expected = candidates[0]
    require(digest(archive_bytes) == expected["sha256"] and len(archive_bytes) == expected["bytes"],
            "Frozen ZIP does not match its archived verification report")
    records = []
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
        infos = archive.infolist()
        unique_count = safe_archive_names(infos)
        require(archive.testzip() is None, "Frozen ZIP CRC failed")
        require(sum(not entry.is_dir() for entry in infos) == expected["files"], "Frozen ZIP file count differs")
        for demo, (member, expected_frames) in DEMOS.items():
            previous = archive.read(member)
            path = args.wave_dir / f"Anyvoc-v{args.version}-{demo}.wav"
            current = read_stable(path)
            format_info = wav_format(current, expected_frames)
            wav_format(previous, expected_frames)
            require(current == previous, f"{demo} newly rendered WAV differs from frozen ZIP")
            records.append({"demo": demo, "current_file": str(path), "archive_member": member,
                            "current_bytes": len(current), "baseline_bytes": len(previous),
                            "current_sha256": digest(current), "baseline_sha256": digest(previous),
                            "byte_identical": True, "format_pass": True, **format_info})
        retained = read_stable(args.retained_legacy)
        require(retained == archive.read(DEMOS["Legacy"][0]), "Retained old Legacy is not bound to frozen ZIP")
    return {"version": args.version, "baseline_version": args.baseline_version,
            "baseline_zip": str(args.baseline_zip), "baseline_zip_bytes": len(archive_bytes),
            "baseline_zip_sha256": digest(archive_bytes), "baseline_report_sha256": digest(report_bytes),
            "baseline_zip_matches_original_report": True, "baseline_zip_crc": "PASS",
            "baseline_zip_safe_unique_paths": "PASS", "baseline_zip_entries": unique_count,
            "demos": records,
            "retained_previous_legacy": {"file": str(args.retained_legacy), "bytes": len(retained),
                                         "sha256": digest(retained), "byte_identical_to_frozen_zip": True,
                                         "rendered_again_this_run": False},
            "scope": "Four provided fresh WAVs checked against the frozen 0.20.2 ZIP. The retained old Legacy is historical evidence; no old renderer was rerun and no listening was performed."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--version", default="0.20.3")
    parser.add_argument("--baseline-version", default="0.20.2")
    parser.add_argument("--baseline-commit", default="63a8192")
    parser.add_argument("--pluginval-version", default="1.0.4")
    parser.add_argument("--wave-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--baseline-zip", type=Path)
    parser.add_argument("--baseline-report", type=Path)
    parser.add_argument("--retained-legacy", type=Path)
    args = parser.parse_args()
    args.root = args.root.resolve(strict=True)
    args.wave_dir = (args.wave_dir or args.root / "build/validation-cache-optimization").resolve(strict=True)
    args.output_dir = (args.output_dir or args.wave_dir).resolve(strict=True)
    args.baseline_zip = args.baseline_zip or args.root / "dist" / f"Anyvoc-{args.baseline_version}-Windows-x64.zip"
    args.baseline_report = args.baseline_report or args.root / "dist" / f"Anyvoc-{args.baseline_version}-PackageVerification.json"
    args.retained_legacy = args.retained_legacy or args.root / "build/validation-0.20.2/Anyvoc-v0.20.2-Legacy.wav"
    names = ("binaries", "source-compatibility", "demo-compatibility")
    outputs = [args.output_dir / f"{name}-{args.version}.json" for name in names]
    require(not any(path.exists() for path in outputs), "Refusing to overwrite an existing evidence report")
    # Reserve the outputs exclusively before verification, preserving failures.
    with contextlib.ExitStack() as stack:
        streams = [stack.enter_context(path.open("x", encoding="utf-8", newline="\n")) for path in outputs]
        failed = False
        for name, check, stream in zip(names, (check_binaries, check_sources, check_demos), streams):
            try:
                result = {"status": "PASS", **check(args)}
            except Exception as error:
                failed = True
                result = {"status": "FAIL", "error_type": type(error).__name__, "error": str(error)}
            result["checked_utc"] = datetime.now(timezone.utc).isoformat()
            result["verifier_sha256"] = digest(Path(__file__).read_bytes())
            json.dump(result, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            print(f"{name}: {result['status']}")
        return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
