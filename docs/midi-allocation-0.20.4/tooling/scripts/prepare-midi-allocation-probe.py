"""Copy a recorded source snapshot, then instrument only its isolated JUCE copy.

No configure, build, device access, original source edits, or cleanup is performed.
The output directory must not already exist. A failed snapshot is kept for audit;
choose a new output directory for a retry. The generated marker is diagnostic,
not a statement that the callback has no other allocation sites.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


JUCE_COMMIT = "29396c22c93392d6738e021b83196283d6e4d850"
MARKER_NAME = "ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC.json"
PATCHES = {
    "modules/juce_core/memory/juce_HeapBlock.h": (
        (
            "        return wrapper (size, [size] { return std::malloc (size); });",
            """        return wrapper (size, [size]
        {
            auto* memory = std::malloc (size);
           #if defined(ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC) && ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC
            ::anyvoc::allocation_probe::record (::anyvoc::allocation_probe::Site::heapMalloc, size, nullptr, memory);
           #endif
            return memory;
        });""",
        ),
        (
            "        return wrapper (num * size, [num, size] { return std::calloc (num, size); });",
            """        return wrapper (num * size, [num, size]
        {
            auto* memory = std::calloc (num, size);
           #if defined(ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC) && ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC
            ::anyvoc::allocation_probe::record (::anyvoc::allocation_probe::Site::heapCalloc, num * size, nullptr, memory);
           #endif
            return memory;
        });""",
        ),
        (
            "        return wrapper (newSize, [ptr, newSize] { return std::realloc (ptr, newSize); });",
            """        return wrapper (newSize, [ptr, newSize]
        {
            auto* memory = std::realloc (ptr, newSize);
           #if defined(ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC) && ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC
            ::anyvoc::allocation_probe::record (::anyvoc::allocation_probe::Site::heapRealloc, newSize, ptr, memory);
           #endif
            return memory;
        });""",
        ),
    ),
    "modules/juce_audio_basics/midi/juce_MidiMessage.cpp": (
        (
            """uint8* MidiMessage::allocateSpace (int bytes)
{
    if (bytes > (int) sizeof (packedData))
    {
        auto d = static_cast<uint8*> (std::malloc ((size_t) bytes));""",
            """uint8* MidiMessage::allocateSpace (int bytes)
{
    if (bytes > (int) sizeof (packedData))
    {
        auto d = static_cast<uint8*> (std::malloc ((size_t) bytes));
       #if defined(ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC) && ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC
        ::anyvoc::allocation_probe::record (::anyvoc::allocation_probe::Site::midiMessageMalloc, (size_t) bytes, nullptr, d);
       #endif""",
        ),
    ),
    "modules/juce_audio_basics/midi/juce_MidiBuffer.cpp": (
        (
            """bool MidiBuffer::addEvent (const void* newData, int maxBytes, int sampleNumber)
{""",
            """bool MidiBuffer::addEvent (const void* newData, int maxBytes, int sampleNumber)
{
   #if defined(ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC) && ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC
    const ::anyvoc::allocation_probe::MidiBufferScope anyvocAllocationBufferScope (this);
   #endif""",
        ),
    ),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git(root, *arguments):
    result = subprocess.run(
        ["git", "-C", str(root), *arguments], check=True, capture_output=True
    )
    return result.stdout.decode("utf-8", errors="strict")


def repository_identity(root, output):
    # Preserve NUL-separated status fields, including both paths for a rename.
    # This records the working tree, rather than falsely attributing dirty bytes
    # to the base commit. Untracked copied tests are intentionally included.
    status_arguments = ["status", "--porcelain=v1", "-z", "--untracked-files=all"]
    excluded_output = None
    if inside(output, root):
        excluded_output = output.relative_to(root).as_posix()
        status_arguments += ["--", ".", f":(top,exclude,literal){excluded_output}"]
    status_output = git(root, *status_arguments)
    return {
        "base_head": git(root, "rev-parse", "HEAD").strip(),
        "status_porcelain_v1_z_entries": status_output.rstrip("\0").split("\0") if status_output else [],
        "status_excluded_generated_output": excluded_output,
    }


def linked(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    )


def inside(path, parent):
    return path == parent or parent in path.parents


def validate_output(root, supplied):
    if not supplied.is_absolute():
        raise ValueError("--output must be an absolute, new directory path")
    # lexists also rejects broken symlinks before resolve could hide them.
    if os.path.lexists(supplied):
        raise FileExistsError(f"Output already exists; choose a new path: {supplied}")
    output = supplied.resolve()
    for forbidden in (root / "Source", root / "Tests", root / "third_party"):
        if inside(output, forbidden.resolve()):
            raise ValueError(f"Output must not be inside source inputs: {forbidden}")
    if inside(root, output):
        raise ValueError("Output must not contain the source root")
    return output


def validate_repository(root):
    if Path(git(root, "rev-parse", "--show-toplevel").strip()).resolve() != root:
        raise ValueError("--root must name the Anyvoc repository root")
    juce = root / "third_party" / "JUCE"
    if linked(juce):
        raise ValueError("JUCE input directory must not be a link or reparse point")
    if git(juce, "rev-parse", "HEAD").strip() != JUCE_COMMIT:
        raise ValueError(f"JUCE must be at the pinned commit {JUCE_COMMIT}")
    if git(juce, "status", "--porcelain=v1", "--untracked-files=no").strip():
        raise ValueError("JUCE tracked files must be clean")
    if not (root / "Tests" / "MidiAllocationRecorder.h").is_file():
        raise ValueError("Tests/MidiAllocationRecorder.h is required")
    if (juce / MARKER_NAME).exists():
        raise ValueError("Original JUCE already contains a diagnostic marker")


def collect_files(root):
    files = []
    excluded = []

    def visit(path):
        relative = path.relative_to(root).as_posix()
        if path.name.casefold() in (".git", "__pycache__"):
            excluded.append({"path": relative, "reason": "excluded metadata/cache"})
        elif linked(path):
            excluded.append({"path": relative, "reason": "link/reparse point"})
        elif path.is_dir():
            for entry in sorted(path.iterdir(), key=lambda p: p.name):
                visit(entry)
        elif stat.S_ISREG(path.stat().st_mode):
            files.append(relative)
        else:
            raise ValueError(f"Non-regular input is not copied: {path}")

    for name in ("CMakeLists.txt", "Source", "Tests", "LICENSE", "third_party/JUCE"):
        path = root / name
        if not path.exists() or linked(path):
            raise ValueError(f"Required input must exist and not be a link: {path}")
        visit(path)
    for path in sorted(root.iterdir(), key=lambda p: p.name):
        if path.name != "LICENSE" and path.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE")):
            visit(path)
    return sorted(files), sorted(excluded, key=lambda entry: entry["path"])


def patched_bytes(original, replacements, label):
    # Preserve the source newline convention and every byte outside exact anchors.
    newline = b"\r\n" if b"\r\n" in original else b"\n"
    result = original
    for before, after in replacements:
        old = before.encode("utf-8").replace(b"\n", newline)
        new = after.encode("utf-8").replace(b"\n", newline)
        if result.count(old) != 1:
            raise ValueError(f"Expected exactly one unmodified patch anchor: {label}")
        result = result.replace(old, new, 1)
    return result


def patch_isolated_juce(source):
    juce = source / "third_party" / "JUCE"
    planned = []
    # Validate all three files before changing any of them.
    for relative, replacements in PATCHES.items():
        path = juce / relative
        original = path.read_bytes()
        patched = patched_bytes(original, replacements, relative)
        planned.append((path, original, patched))
    records = []
    for path, original, patched in planned:
        path.write_bytes(patched)
        if file_hash(path) != sha256(patched):
            raise OSError(f"Patched bytes failed verification: {path}")
        records.append({
            "path": path.relative_to(source).as_posix(),
            "before_sha256": sha256(original),
            "after_sha256": sha256(patched),
        })
    return records


def write_json_new(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def prepare(root_arg, output_arg):
    root = root_arg.resolve(strict=True)
    output = validate_output(root, output_arg)
    validate_repository(root)
    identity = repository_identity(root, output)
    paths, excluded = collect_files(root)
    tool = Path(__file__).resolve()
    tool_hash = file_hash(tool)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()  # Atomic exclusive reservation; no failure handler owns it yet.
    manifest = {
        "schema": 1,
        "complete": False,
        "purpose": "isolated JUCE CRT allocation diagnostics; not a release source tree",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "output": str(output),
        "source_directory": "source",
        "source_identity": identity,
        "juce_commit": JUCE_COMMIT,
        "tool": {"path": str(tool), "sha256": tool_hash},
        "excluded": excluded,
        "files": [],
    }
    try:
        source = output / "source"
        source.mkdir()
        for relative in paths:
            origin, target = root / relative, source / relative
            data = origin.read_bytes()
            digest = sha256(data)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as stream:
                stream.write(data)
            if file_hash(target) != digest or file_hash(origin) != digest:
                raise OSError(f"Source changed during copy or copy verification failed: {relative}")
            manifest["files"].append({
                "path": relative, "bytes": len(data), "source_sha256": digest,
                "copied_sha256": digest, "final_sha256": digest,
            })
        # Detect cross-file edits, new inputs, or root/submodule changes during copy.
        validate_repository(root)
        if repository_identity(root, output) != identity or collect_files(root) != (paths, excluded):
            raise ValueError("Repository identity or input set changed during snapshot")
        for entry in manifest["files"]:
            if file_hash(root / entry["path"]) != entry["source_sha256"]:
                raise ValueError(f"Source changed during snapshot: {entry['path']}")
        if file_hash(tool) != tool_hash:
            raise ValueError("Preparation tool changed during snapshot")
        patches = patch_isolated_juce(source)
        for patch in patches:
            next(entry for entry in manifest["files"] if entry["path"] == patch["path"])["final_sha256"] = patch["after_sha256"]
        marker = {
            "schema": 1, "instrumentation_schema": 1, "diagnostic_only": True, "juce_commit": JUCE_COMMIT,
            "instrumentation_macro": "ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC",
            "source_base_head": identity["base_head"], "tool_sha256": tool_hash,
            "recorder": "Tests/MidiAllocationRecorder.h",
            "recorder_sha256": file_hash(source / "Tests" / "MidiAllocationRecorder.h"),
            "requires_force_include_in_instrumented_targets": True,
            "sites": ["HeapBlock mallocWrapper", "HeapBlock callocWrapper", "HeapBlock reallocWrapper", "MidiMessage::allocateSpace malloc"],
            "owner_scope": "MidiBuffer::addEvent(const void*, int, int)",
            "limitations": ["No free interception", "No global allocator interception", "MidiMessage assignment allocations are not instrumented", "Capture is thread-local and bounded"],
            "patches": patches,
        }
        marker_path = source / "third_party" / "JUCE" / MARKER_NAME
        write_json_new(marker_path, marker)
        manifest["diagnostic_marker"] = {
            "path": marker_path.relative_to(source).as_posix(),
            "sha256": file_hash(marker_path),
        }
        manifest["patches"] = patches
        manifest["complete"] = True
        write_json_new(output / "manifest.json", manifest)
    except Exception as error:
        manifest["complete"] = False
        manifest["error"] = f"{type(error).__name__}: {error}"
        write_json_new(output / "failure.json", manifest)
        raise
    return output / "manifest.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Anyvoc checkout root; dirty root edits are recorded")
    parser.add_argument("--output", type=Path, required=True, help="absolute NEW directory; isolated sources will be in its source child")
    args = parser.parse_args()
    try:
        print(prepare(args.root, args.output))
        return 0
    except Exception as error:
        print(f"Preparation failed (existing evidence is retained): {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
