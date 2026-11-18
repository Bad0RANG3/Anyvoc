# Anyvoc 0.9 validation — 2026-11-18 (Asia/Shanghai)

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned to 29396c22c93392d6738e021b83196283d6e4d850. VST3 and standalone use the static MSVC runtime. Previous release evidence is preserved in [VALIDATION-0.8.md](VALIDATION-0.8.md).

## Release checks

- Release VST3 and standalone builds passed. Both binaries report FileVersion/ProductVersion `0.9.0`.
- CTest passed all four targets: 33 engine, 43 processor, 6 wavetable and 6 library groups (88 total). See `ctest-0.9.txt` and `test-details-0.9.txt`.
- pluginval strictness level 5 finished with `SUCCESS` for the final VST3 binary. See `pluginval-0.9.txt`. The launcher exit code alone was not used as the result.
- Generated 31 interface previews, including history-ready and history-redo states at normal and reduced sizes. Inspected the reduced header and widened local table-history controls for overlap and readability.
- A retained actual 0.8 test executable and the final 0.9 executable rendered the same 15-second pad/sample/granular demo byte-for-byte. Both WAV SHA-256 values are `3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920`. SynthEngine, WavetableDocument and PresetLibrary source files, the parameter layout and all 16 preset definitions are unchanged from the released 0.8 source archive.

The new processor coverage includes transaction grouping/no-op/redo, compound LFO and modulation edits, host ownership conflicts, reentrant project/automation callbacks, a deterministic blocked background project publication, replay after source assets are deleted, Save As identity, retention limits, live voice/reference audio continuity, GUI control adapters and stale wavetable sessions.

Initial validation exposed test-harness issues, subsequently corrected: serialized LFO-rate values acquire one normalized float rounding step through normal project restoration, so exact state comparisons now use an independently restored reference; and a pending JUCE timer callback must be serviced before waiting for the offscreen header refresh. The diagnostic logs are retained. These corrections did not change DSP or widen numerical test tolerances.

## Native standalone checks

The final standalone was launched in a separate verification window. Observed mouse-driven cutoff undo/redo (12.00 kHz to 2.11 kHz and back), a whole LFO stroke returning to its sine shape in one undo, scalar undo retaining an open Wavetable Lab, and a wavetable stroke undo restoring the old table and dismissing the stale Lab. Undoing INIT restored the original Ribbon Glide preset and its visible settings. Clicking C4 lit the key and output meter; this is an audio-output indication, not a claim of a separate listening or device-loopback measurement. The verification window was closed after returning to the starting preset; the user's older standalone and FL Studio session were left open.

Native shortcut validation remains inconclusive: the UI automation tool delivered a plain `a` to the focused search field, but Ctrl+A and Ctrl+Z did not operate even that JUCE TextEditor's existing native commands; Ctrl+Z/Y also did not trigger sound history. Source review found no missing focus flag or standalone interception. No speculative production workaround was added for this unconfirmed input-delivery issue. Mouse UNDO/REDO were verified independently. The processor UI group passes direct Editor::keyPressed checks for Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z, exact history counts/parameter restoration, unrelated host automation preservation, rejection of unrelated/Alt-modified keys, and refusal to replay an active gesture. This offscreen suite has no native peer and cannot establish TextEditor focus, native text undo, physical-keyboard delivery through Windows or every DAW.

## Binary identity

- Standalone SHA-256: `E1B2A827D259E7EBA1B7D159BC93404CB7BB7CAED71454A82780E26EEFD5C246`.
- VST3 module SHA-256: `06628E2264514EEAA11742C7AF5314FDF67401010CA457649DCD97EB87C00F34`.

## Sound history design

History belongs to the processor and survives closing/reopening its editor within the same instrument instance. Explicit message-thread user setters distinguish edits from parameter notifications. Parameter notifications from other origins only update atomic ownership epochs; history is reclaimed on the editor thread. External host project/program publications establish history boundaries. History is not serialized; schema 7 and all existing parameters/presets remain unchanged.

Each committed entry retains changed parameter deltas, changed immutable encoded/prepared assets, and preset identity when that action changed it. It does not serialize a full base64 state for every knob movement. Scalar undo/redo writes only affected parameters, preserves unrelated host automation, does not republish tables, and does not reset active voices. Preset/init/load entries request the normal sound reset. Assets are retained in memory, so replay does not reopen source files. Wavetable replay advances only affected table revisions; obsolete drawing sessions cannot overwrite the restored table.

Default retention is 64 complete entries and 128 MiB of counted history storage, including unique encoded/prepared payloads. Oldest complete entries are evicted. An oversized action still succeeds, while no unusable partial history remains. This bounds committed history rather than all process memory or peak import working memory. No-op and failed actions preserve redo; a newly committed edit replaces redo. Save/export do not undo files or create sound edits. A newer successful Save As identity is preserved through replay of earlier sound changes.

Host changes to an affected parameter invalidate that entry, even when automation returns it to the same value. A pre-existing conflict rejects before any replayed changes. A host callback that intervenes during replay aborts remaining work; values already emitted cannot be atomically rolled back without potentially overwriting newer host state. An in-flight physical GUI gesture must still close after a project boundary; its stale pending snapshot is discarded.

## Interface

Custom slider/choice/button adapters preserve parameter ranges, snapping, units and default resets while tracking explicit user actions. Host readback and disabled-toggle reconciliation do not create user history. A drag, two-parameter filter gesture, LFO stroke, compound modulation assignment and applied table operation each form one entry. Header UNDO/REDO show the next action in tooltips. Ctrl+Z and Ctrl+Y/Ctrl+Shift+Z operate sound history; focused text editors keep native text undo. History commands remain unavailable during an open drag/stroke. Local TABLE UNDO/TABLE REDO retain their separate 20-step document history and each applied result enters global history.

## Scope and remaining validation

This original instrument is not verified equivalent to Serum 2. Sixteen factory presets remain included. FL Studio installation, scan/load/audio and project reopen are deferred because the user chose standalone. Other DAWs and operating systems have not been validated. AU/AAX and MPE are unavailable. pluginval, when run, does not prove every host workflow.

The prior DSP limitations remain: saturation is not oversampled; Sync/Mirror/Fold Warp is not fully bandlimited; pitched samples/grains use interpolation without mipmapped anti-aliasing. Imports remain limited to 20 seconds, 8–192 kHz, mono/stereo. Worst-case live CPU has not been benchmarked. Multisample/SFZ, spectral synthesis, cross-source FM/PD, more modulation/effects buses, Clip sequencing, full library metadata, complete accessibility and a Chinese interface remain unimplemented.

An older v0.5 run had an intermittent arp-sustain RMS failure; the same binary subsequently passed 131 runs. No reproducible cause was established, and unrelated later changes are not claimed to fix it. Earlier phase-startup and disabled-toggle corrections are documented in the 0.8 report.
