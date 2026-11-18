"""Compare two saved Anyvoc schema-14 presets without loading plugin code.

Checks 1,256 unique parameters, other saved state, and embedded WAV bytes.
Optionally checks an original 48-kHz PCM16 mono sample against the embedded
PCM24 sample. This verifies stored data, not audio playback or native actions.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


MAX_PRESET_BYTES = 40 * 1024 * 1024
PARAMETER_COUNT = 1256
JUCE_ALPHABET = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
IDENTITY_ATTRIBUTES = {"presetName", "presetId"}


class VerificationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_bounded(path, maximum):
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    require(0 < len(data) <= maximum, f"File must contain 1..{maximum} bytes: {path}")
    return data


def decode_juce_block(text, maximum):
    """JUCE MemoryBlock's length-prefixed, least-significant-bit-first format.

    This is not RFC Base64. Require the canonical output produced by JUCE;
    do not inherit its permissive decoder's handling of missing/trailing data.
    """
    size_text, separator, chars = text.partition(".")
    require(separator and size_text.isascii() and size_text.isdecimal(),
            "Embedded WAV has no valid JUCE byte-length prefix")
    require(len(size_text) <= 8, "Embedded WAV byte-length prefix is too large")
    size = int(size_text)
    require(size_text == str(size), "Embedded WAV byte-length prefix is not canonical")
    require(0 < size <= maximum, "Embedded WAV exceeds the permitted byte length")
    require(len(chars) == (size * 8 + 5) // 6, "Embedded WAV encoded length is inconsistent")
    values = {char: index for index, char in enumerate(JUCE_ALPHABET)}
    output = bytearray()
    accumulator = bits = 0
    for char in chars:
        require(char in values, "Embedded WAV contains an invalid JUCE encoding character")
        accumulator |= values[char] << bits
        bits += 6
        while bits >= 8:
            output.append(accumulator & 255)
            accumulator >>= 8
            bits -= 8
    require(len(output) == size and accumulator == 0, "Embedded WAV has invalid final bits")
    return bytes(output)


def parse_wav(data):
    """Read bounded RIFF/WAVE PCM or IEEE float, including extensible headers."""
    require(len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] == b"WAVE",
            "Expected a RIFF/WAVE file")
    require(struct.unpack_from("<I", data, 4)[0] + 8 == len(data),
            "WAV RIFF size does not match the complete file")
    fmt = pcm = None
    position = 12
    chunks = []
    while position < len(data):
        require(position + 8 <= len(data), "Truncated WAV chunk header")
        kind, size = struct.unpack_from("<4sI", data, position)
        start, end = position + 8, position + 8 + size
        require(end + (size & 1) <= len(data), "Truncated WAV chunk data or padding")
        chunks.append({"id": kind.decode("ascii", errors="backslashreplace"), "bytes": size})
        if kind == b"fmt ":
            require(fmt is None, "Duplicate WAV fmt chunk")
            fmt = data[start:end]
        elif kind == b"data":
            require(pcm is None, "Duplicate WAV data chunk")
            pcm = data[start:end]
        position = end + (size & 1)
    require(fmt is not None and len(fmt) >= 16 and pcm is not None, "WAV fmt/data chunk missing")
    tag, channels, rate, byte_rate, alignment, bits = struct.unpack_from("<HHIIHH", fmt)
    valid_bits, channel_mask = bits, None
    if tag == 0xFFFE:
        require(len(fmt) >= 40, "Truncated extensible WAV format")
        extension_size, valid_bits, channel_mask = struct.unpack_from("<HHI", fmt, 16)
        require(extension_size >= 22 and len(fmt) >= 18 + extension_size,
                "Invalid extensible WAV format size")
        guid = fmt[24:40]
        require(guid[4:] == bytes.fromhex("00001000800000aa00389b71"),
                "Unsupported extensible WAV subformat GUID")
        tag = struct.unpack_from("<I", guid)[0]
    require(tag in (1, 3), "Only integer PCM and IEEE-float WAV are supported")
    require(channels in (1, 2) and rate > 0, "WAV must be mono or stereo with a positive rate")
    require(bits in (8, 16, 24, 32) and valid_bits == bits, "Unsupported WAV bit depth")
    require(tag != 3 or bits == 32, "Only 32-bit IEEE-float WAV is supported")
    require(alignment == channels * (bits // 8) and byte_rate == rate * alignment,
            "WAV block alignment or byte rate is inconsistent")
    require(len(pcm) > 0 and len(pcm) % alignment == 0, "WAV PCM length is not whole frames")
    if tag == 3:
        require(all(math.isfinite(value[0]) for value in struct.iter_unpack("<f", pcm)),
                "Embedded floating-point WAV contains non-finite samples")
    info = {
        "encoding": "integer PCM" if tag == 1 else "IEEE float",
        "sample_rate_hz": rate, "channels": channels, "bits_per_sample": bits,
        "frames": len(pcm) // alignment, "pcm_bytes": len(pcm),
        "wav_bytes": len(data), "wav_sha256": digest(data), "pcm_sha256": digest(pcm),
        "channel_mask": channel_mask, "chunks": chunks,
    }
    return info, pcm


def parse_preset(path):
    data = read_bounded(path, MAX_PRESET_BYTES)
    # Files produced by savePreset are UTF-8 XML with no DTD or custom entities.
    text = data.decode("utf-8-sig")
    require("<!DOCTYPE" not in text.upper() and "<!ENTITY" not in text.upper(),
            "Preset DTD/entity declarations are not supported")
    root = ET.fromstring(text)
    require(root.tag == "ANYVOC" and root.get("schemaVersion") == "14",
            "Expected ANYVOC XML with schemaVersion=14")
    require(not (root.text or "").strip(), "Unexpected root text in preset")
    params, assets = {}, {}
    for child in root:
        require(len(child) == 0 and not (child.text or "").strip()
                and not (child.tail or "").strip(), "Unexpected nested content in preset")
        if child.tag == "PARAM":
            identifier, raw = child.get("id"), child.get("value")
            require(identifier and raw is not None and identifier not in params,
                    "Missing or duplicate PARAM id/value")
            value = float(raw)
            require(math.isfinite(value), f"Non-finite parameter: {identifier}")
            params[identifier] = {"value": value, "text": raw,
                                  "attributes": {k: v for k, v in child.attrib.items() if k != "value"}}
        elif child.tag in ("WAVETABLE", "SAMPLE"):
            oscillator = child.get("osc")
            require(oscillator in ("0", "1", "2"), "Invalid embedded oscillator index")
            key = (child.tag, int(oscillator))
            require(key not in assets and child.get("name") is not None and child.get("wav"),
                    "Duplicate or incomplete embedded asset")
            maximum = (6 if child.tag == "SAMPLE" else 4) * 1024 * 1024
            wav = decode_juce_block(child.get("wav"), maximum)
            info, pcm = parse_wav(wav)
            if child.tag == "SAMPLE":
                require(info["sample_rate_hz"] == 48000 and info["encoding"] == "integer PCM"
                        and info["bits_per_sample"] == 24 and 2 <= info["frames"] <= 960000,
                        "Expected saved SAMPLE as 48-kHz PCM24, 2..960000 frames")
            else:
                require(info["sample_rate_hz"] == 44100 and info["channels"] == 1
                        and info["encoding"] == "IEEE float" and 32 <= info["frames"] <= 65536,
                        "Expected saved WAVETABLE as mono 44.1-kHz float32, 32..65536 frames")
                require(child.get("normalise") in ("0", "1"), "Invalid WAVETABLE normalise flag")
            assets[key] = {"attributes": {k: v for k, v in child.attrib.items() if k != "wav"},
                           "wav": wav, "pcm": pcm, "info": info}
        else:
            raise VerificationError(f"Unsupported preset child: {child.tag}")
    require(len(params) == PARAMETER_COUNT,
            f"Expected {PARAMETER_COUNT} unique PARAMs, found {len(params)}")
    summary = {"path": str(path.resolve()), "file_bytes": len(data), "sha256": digest(data),
               "schema_version": 14, "parameter_count": len(params),
               "root_attributes": dict(root.attrib), "assets": [
                   {"type": kind, "osc": oscillator, "attributes": asset["attributes"],
                    "wav": asset["info"]} for (kind, oscillator), asset in sorted(assets.items())]}
    return {"root": dict(root.attrib), "params": params, "assets": assets, "summary": summary}


def compare_presets(before, after, failures):
    left_root = {k: v for k, v in before["root"].items() if k not in IDENTITY_ATTRIBUTES}
    right_root = {k: v for k, v in after["root"].items() if k not in IDENTITY_ATTRIBUTES}
    root_equal = left_root == right_root
    if not root_equal:
        failures.append("Non-identity root state attributes differ")
    left, right = before["params"], after["params"]
    missing, added = sorted(left.keys() - right.keys()), sorted(right.keys() - left.keys())
    if missing or added:
        failures.append("Parameter ID sets differ")
    differences = []
    for identifier in sorted(left.keys() & right.keys()):
        a, b = left[identifier], right[identifier]
        if a["value"] != b["value"] or a["attributes"] != b["attributes"]:
            differences.append({"id": identifier, "before": a, "after": b})
    if differences:
        failures.append(f"{len(differences)} parameters differ (no numerical tolerance applied)")
    left_assets, right_assets = before["assets"], after["assets"]
    asset_set_equal = left_assets.keys() == right_assets.keys()
    if not asset_set_equal:
        failures.append("Embedded asset type/oscillator sets differ")
    comparisons = []
    for key in sorted(left_assets.keys() & right_assets.keys()):
        a, b = left_assets[key], right_assets[key]
        identity_equal, wav_equal = a["attributes"] == b["attributes"], a["wav"] == b["wav"]
        comparisons.append({"type": key[0], "osc": key[1], "attributes_equal": identity_equal,
                            "wav_byte_identical": wav_equal,
                            "before_sha256": digest(a["wav"]), "after_sha256": digest(b["wav"])})
        if not identity_equal or not wav_equal:
            failures.append(f"Embedded {key[0]} oscillator {key[1]} metadata or WAV bytes differ")
    return {
        "root_state_equal_except_preset_name_and_id": root_equal,
        "ignored_identity_changes": {k: {"before": before["root"].get(k), "after": after["root"].get(k)}
                                     for k in sorted(IDENTITY_ATTRIBUTES)},
        "parameter_id_sets_equal": not missing and not added,
        "missing_parameter_ids": missing, "added_parameter_ids": added,
        "parameter_values_and_other_attributes_equal": not differences and not missing and not added,
        "parameter_differences": differences, "asset_sets_equal": asset_set_equal,
        "asset_comparisons": comparisons,
    }


def compare_source(path, preset, oscillator, failures):
    data = read_bounded(path, 64 * 1024 * 1024)
    source_info, source_pcm = parse_wav(data)
    require(source_info["encoding"] == "integer PCM" and source_info["sample_rate_hz"] == 48000
            and source_info["bits_per_sample"] == 16 and source_info["channels"] == 1,
            "--source-wav supports only 48-kHz PCM16 mono (no resampling)")
    asset = preset["assets"].get(("SAMPLE", oscillator))
    require(asset is not None, f"No SAMPLE asset in oscillator {oscillator}")
    info = asset["info"]
    require(info["channels"] == 1 and source_info["frames"] == info["frames"],
            "Source/embedded channel count or sample count differs")
    # Fixed JUCE 8.0.12: PCM16 read = n/32768 exactly in float32; encodeSample
    # does no resampling at 48 kHz. AudioFormatWriter::convertFloatsToInts uses
    # round-to-nearest-even((2**31-1)*sample), with -1 clamped to INT32_MIN.
    # WavAudioFormatWriter then stores the top 24 bits (arithmetic >> 8).
    # The resulting normalized error is at most ONE PCM24 LSB, not a fitted
    # tolerance. Also require every PCM24 integer to match this exact rule.
    pcm = asset["pcm"]
    maximum_error_lsb = mismatch_count = changed_samples = 0
    square_error_lsb = 0
    examples = []
    for index, (sample16,) in enumerate(struct.iter_unpack("<h", source_pcm)):
        offset = index * 3
        actual = int.from_bytes(pcm[offset:offset + 3], "little", signed=True)
        expected32 = -(1 << 31) if sample16 == -32768 else round(((1 << 31) - 1) * (sample16 / 32768))
        expected = expected32 >> 8
        error_lsb = actual - sample16 * 256
        maximum_error_lsb = max(maximum_error_lsb, abs(error_lsb))
        square_error_lsb += error_lsb * error_lsb
        changed_samples += error_lsb != 0
        if actual != expected:
            mismatch_count += 1
            if len(examples) < 16:
                examples.append({"frame": index, "source_pcm16": sample16,
                                 "expected_pcm24": expected, "actual_pcm24": actual})
    if maximum_error_lsb > 1 or mismatch_count:
        failures.append("Source PCM does not match the documented JUCE PCM16-to-PCM24 conversion")
    return {"source_path": str(path.resolve()), "source_wav": source_info, "osc": oscillator,
            "embedded_wav": info, "frames_compared": source_info["frames"],
            "maximum_normalized_error": maximum_error_lsb / (1 << 23),
            "rms_normalized_error": math.sqrt(square_error_lsb / source_info["frames"]) / (1 << 23),
            "allowed_maximum_normalized_error": 1 / (1 << 23),
            "changed_by_quantization_samples": changed_samples,
            "exact_conversion_mismatch_count": mismatch_count, "first_mismatches": examples,
            "quantization_rule": "PCM16/32768; saturate/round-even to signed PCM32 using 2147483647; arithmetic shift right 8",
            "basis": "JUCE 8.0.12 AudioFormatReader.cpp, AudioFormatWriter.cpp, AudioDataConverters.h; no imported runtime helpers"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True, help="Original saved .anyvoc")
    parser.add_argument("--after", type=Path, required=True, help="Saved again after native restore")
    parser.add_argument("--source-wav", type=Path, help="Optional original or renamed PCM16 mono WAV")
    parser.add_argument("--osc", type=int, choices=(0, 1, 2), default=0, help="Source sample oscillator, zero-based")
    parser.add_argument("--output", type=Path, required=True, help="External JSON report, never an input file")
    args = parser.parse_args()
    inputs = [args.before, args.after] + ([args.source_wav] if args.source_wav else [])
    if args.output.resolve() in [path.resolve() for path in inputs]:
        parser.error("--output must not overwrite an input file")
    failures = []
    report = {
        "generated_utc": datetime.now(timezone.utc).isoformat(), "status": "FAIL", "failures": failures,
        "scope": "Independent schema-14 stored-state and embedded-WAV comparison; Python standard library only",
        "limitations": [
            "Does not execute Anyvoc, prove native exit/restart, or prove the original path was unavailable.",
            "Does not measure rendered audio, listening quality, real-time performance, or DAW compatibility.",
            "Checks 1256 unique parameter IDs and exact saved values against each other, not a production parameter registry or ranges.",
            "Optional source comparison covers only 48-kHz PCM16 mono to embedded PCM24 mono; resampling is outside scope.",
            "Runtime voices, modulation history, GUI navigation, and undo history are not preset assets.",
        ],
    }
    parsed = {}
    for label, path in (("before", args.before), ("after", args.after)):
        try:
            parsed[label] = parse_preset(path)
            report[label] = parsed[label]["summary"]
        except (OSError, ValueError, ET.ParseError) as error:
            failures.append(f"{label}: {error}")
            report[label] = {"path": str(path.resolve()), "error": str(error)}
    if len(parsed) == 2:
        report["comparison"] = compare_presets(parsed["before"], parsed["after"], failures)
    if args.source_wav and "after" in parsed:
        try:
            report["source_pcm_comparison"] = compare_source(args.source_wav, parsed["after"], args.osc, failures)
        except (OSError, ValueError) as error:
            failures.append(f"source PCM: {error}")
    report["status"] = "FAIL" if failures else "PASS"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"{report['status']} Preset roundtrip: schema 14, {PARAMETER_COUNT} parameters, embedded WAV bytes")
    for failure in failures:
        print(f"FAIL {failure}")
    if "source_pcm_comparison" in report:
        source = report["source_pcm_comparison"]
        print(f"Source PCM: frames={source['frames_compared']}, max error={source['maximum_normalized_error']:.12g}, "
              f"exact conversion mismatches={source['exact_conversion_mismatch_count']}")
    print(f"Report: {args.output.resolve()}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
