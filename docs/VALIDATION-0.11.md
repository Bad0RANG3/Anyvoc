# Anyvoc 0.11 validation — 2026-11-18 (Asia/Shanghai)

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned to 29396c22c93392d6738e021b83196283d6e4d850. VST3 and standalone use the static MSVC runtime. Prior release evidence is preserved in [VALIDATION-0.10.md](VALIDATION-0.10.md).

## Current validation status

The final 0.11.0 Release build succeeded. All four CTest targets passed: 33 engine, 50 processor/editor, 10 wavetable and 6 preset-library groups (99 groups total, 8.45 seconds). See [ctest-0.11.txt](ctest-0.11.txt) and [test-details-0.11.txt](test-details-0.11.txt). VST3 pluginval strictness level 5 finished with SUCCESS; see [pluginval-0.11.txt](pluginval-0.11.txt). The separate Steinberg validator was skipped by pluginval and is not claimed as passed.

Four new integration groups cover independent expected modulation endpoints, source selection with byte-identical active audio, annular hit testing and its lower gap, signed depth, Shift precision, clamping, duplicate routes, zero-depth retention, one-step undo/redo and balanced host gestures. They also cover project restoration at gesture start/during writes/between moves, source/destination/amount changes, and release, Escape, focus loss, hiding, source changes and editor destruction. The existing restoration group gained an independent direct-parameter regression for restoring the initial cutoff inside its outgoing callback. Tests do not directly delete the editor from inside a parameter callback; those lifetime guards received code review.

The 15-second legacy factory-pad/sample/granular demo rendered with the retained 0.10 executable and final 0.11 executable is byte-identical: SHA-256 `3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920`. The retained 0.10 processor-test executable has SHA-256 `9728C44A59EE3B38F86044AEDE1724094D077D8C34A5060E773A45676A3DB655`. SynthEngine, WavetableDocument and PresetLibrary source/header pairs, PluginProcessor.h and all 4,377 JUCE files match the actual 0.10 source archive byte-for-byte. PluginProcessor.cpp changes are confined to the restore reconciliation described below. This checks retained paths; it is not proof about every possible patch.

All 41 editor preview PNGs were regenerated. The new main-page rings, smaller layout, Macro/Filter and Warp views were visually inspected; knob captions and outer rings remain separated. A native Windows run of the final standalone confirmed source drag assignment, outer-ring depth +25% to +50%, one-click undo to +25%, independent center adjustment from 3.60 kHz to 1.71 kHz, double-click depth zero while retaining the row, and row-specific selection through the right-click menu. A downward ring drag set row 1 LFO 1 to -25% while row 2 Macro 1 remained +25% and base cutoff stayed 1.71 kHz. The MOD page confirmed both values. Mouse undo restored all five remaining test edits to Ribbon Glide: empty matrix, cutoff 3.60 kHz and oscillator A position 58%. The on-screen C4 key highlighted when clicked; this run did not capture an output-meter response or an external audio-device recording. Native Shift/Escape combinations during a held drag were not exercised; their behavior is covered by direct integration tests. Only the new verification window was closed; the pre-existing older standalone window was left untouched.

Both production binaries report file version 0.11.0:

| Binary | SHA-256 |
| --- | --- |
| Standalone Anyvoc.exe | `77621E00D968B7DB8EB6B3F70DE18447E7704CB8A7CA140E07B464E271CE06AD` |
| VST3 module | `75EFE3E926196917D0E435BB2EE96079BAFC394156436F478311989B15C2EE2D` |

Package integrity is checked after archive creation against these binaries and current source/docs. The external `dist/Anyvoc-0.11.0-PackageVerification.json` records final archive hashes, CRC checks and exact file comparisons.

## Selected-source modulation rings

Clicking a source chip selects the displayed source without writing sound parameters. Dragging a source still assigns an existing matrix route. The default selected source is LFO 1. A visible ring edits exactly one captured matrix row's amount, not the destination base parameter. Duplicate source/destination pairs initially select the first row; the right-click menu identifies row numbers and can choose another row. A zero-depth row remains occupied and editable. Velocity and Mod Wheel are available through the row menu despite having no top-row chips.

The colored arc shows a nominal range for that single row. LFO 1/2 are bipolar [-1,1]; Filter Env, Velocity, Mod Wheel and Macro 1–4 are unipolar [0,1]. Cutoff uses base * 2^(5 * source * amount); position, resonance, balance and warp use additive offsets. Displayed endpoints are clipped to the base knob's range, which can be narrower than the DSP limits. Other rows, filter envelopes, key tracking, sample-rate limits and actual voice activity are excluded. The dot marks the source=1 endpoint, preserving direction even when a bipolar arc is symmetric. This is not live modulation telemetry.

Only the painted annular area takes depth input; the center and text field retain their original base-value behavior. Up/down drag adjusts amount, Shift is finer, and double-click sets zero without removing the route. Each gesture uses one sound-history transaction and balanced host begin/end notifications. Release, Escape, focus loss, hiding, source change and destruction finish accepted edits rather than rolling them back; Undo restores the original amount. Source/destination/amount changes and project-boundary revisions terminate stale gestures. Callback lifetime guards check whether the editor survived synchronous host callbacks before continuing.

The feature uses existing parameters and schema 7. No new parameter IDs, automation ordering, factory presets or audio synthesis algorithms are introduced. Global Pitch/Level/Pan remain matrix-only because they are not equivalent to individual oscillator knobs. Eight matrix rows and the existing set of modulation targets remain the limit.

## Reentrant project restoration fix

A new integration test exposed an APVTS cache boundary: when a host restores a project from inside an outgoing parameter callback, the adapter may still cache the pre-drag value even though the underlying parameter has moved. If the restored value equals that old cache, replaceState can skip the actual parameter. The non-user restore path now reconciles actual normalized parameter values against the validated incoming state after replaceState. Generation checks stop the outer restoration if a nested callback establishes a newer project boundary. This also protects callers other than the new ring. The original JUCE files are unmodified.

## Reference and remaining scope

The [official Serum 2 control guide](https://xferrecords.com/web-manual/serum-2/using-knobs-and-sliders) is a workflow reference for direct adjustment, fine control and per-target modulation menus. Anyvoc uses original code, artwork and presets; no Xfer assets are included. This is not evidence of full Serum 2 sound, UI or feature equivalence, and Serum preset files are not supported.

The user chose standalone, so FL Studio installation, scan/load/audio and project reopen remain deferred. Other DAWs and operating systems have not been validated. AU/AAX and MPE are unavailable; pluginval alone cannot prove every host workflow.

Remaining gaps include multisample/SFZ, a spectral oscillator, cross-source FM/PD, more modulation/effects buses, Clip sequencing, wavetable formulas and larger frame sets, full library metadata, complete accessibility and a Chinese interface. Saturation is not oversampled; Sync/Mirror/Fold Warp is not fully bandlimited; pitched samples/grains use interpolation without mipmapped anti-aliasing. Sample imports remain limited to 20 seconds, 8–192 kHz, mono/stereo. Worst-case live CPU has not been benchmarked.

Native Windows Ctrl-key automation was inconclusive in 0.9, including JUCE TextEditor's own Ctrl+A/Z. Direct key-handler tests and mouse history commands pass in that release; this is not proof of physical-keyboard delivery. An older v0.5 run had an intermittent arp-sustain RMS failure; the same binary subsequently passed 131 runs without an established cause. Neither is claimed fixed by this release.
