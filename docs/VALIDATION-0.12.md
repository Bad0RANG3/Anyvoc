# Anyvoc 0.12 validation — 2026-11-18 (Asia/Shanghai)

This document records the final Release build, automated tests, native standalone checks and their limits. Prior release evidence is preserved in [VALIDATION-0.11.md](VALIDATION-0.11.md). Archive integrity is recorded separately in `dist/Anyvoc-0.12.0-PackageVerification.json` after packaging, so the archives do not contain their own hashes.

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned to `29396c22c93392d6738e021b83196283d6e4d850`. VST3 and standalone use the static MSVC runtime.

## Current validation status

The final Release build succeeded. All 108 groups passed: 37 engine, 55 processor/editor, 10 wavetable and 6 preset-library groups. The final [ctest-0.12.txt](ctest-0.12.txt) records 4/4 targets passing in 11.09 seconds; [test-details-0.12.txt](test-details-0.12.txt) preserves the detailed output. An earlier development run failed a strengthened exact-zero assertion for mixed Classic plus AM/RM; that earlier run is superseded by the final passing results.

Numeric diagnosis found mixed AM peaks of approximately `7.38873e-18` and `3.69042e-18` for the two slot orders after 0.5 seconds, while both mixed RM cases were bit-zero. The tiny residual came from the Classic DC state settling after the source's onset fade. The strengthened check requires peak below `1e-12` (-240 dBFS) and prints its numeric measurements; no DSP output floor was added. Pure AM/RM silence checks remain bit-zero.

The 15-second legacy factory-pad/sample/granular demo rendered with the retained 0.11 processor-test executable and the final 0.12 build is byte-identical: SHA-256 `3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920`. The comparison files are `build/validation-baseline/Anyvoc-v0.11-before-v0.12.wav` and `Anyvoc-v0.12-final.wav`. The retained 0.11 test executable has SHA-256 `A4DE811014476E8D9FF1938570C105697AECEB527528526A74AD81F9565FA4C3`. This covers those retained paths, not every possible patch.

All 44 editor preview PNGs were regenerated from the final build. The PM small layout, FM view and Classic Warp small layout received visual inspection. The three added views are `Crossmod-PM.png`, `Crossmod-PM-small.png` and `Crossmod-FM.png`.

The final VST3 passed pluginval strictness level 5, seed `0x6c903f9`: [pluginval-0.12.txt](pluginval-0.12.txt), ending in `SUCCESS`. The separate Steinberg validator was skipped, not passed.

| Final production binary | Bytes | FileVersion | SHA-256 |
| --- | ---: | --- | --- |
| Standalone / Anyvoc.exe | 9,119,232 | 0.12.0 | `13B557A8C8287144D3CBBDD5E9089EF1AEE01D7D9DFC080AA72714A38C9FC1C1` |
| VST3 / Contents/x86_64-win/Anyvoc.vst3 | 9,657,856 | 0.12.0 | `FFF426112D95C3672AF2B0FDF72BD98405346FBF74D19C46C6AB831079B4F4FB` |

Independent comparison against the 0.11 source archive confirmed that the first 16 factory presets and the old parameter-definition prefix remain unchanged. All 4,377 JUCE files remain byte-identical to the retained dependency; vendor HEAD is the pinned revision above.

## Native standalone and demonstration audio

The final Windows standalone was launched and checked at 1122 x 788 window size. In the preset browser, mouse scrolling and double-click loaded `Keys / Phase Alloy`. Its main page showed Osc B at Level 0 and +12 semitones. The Warp view showed PM from Osc B at 16%, with the static-preview omission label visible. Opening the menus showed all eleven modes and all five sources.

Changing PM to FM updated the description and signal-flow text. Selecting Osc A as its own source displayed `SELF FEEDBACK`. Dragging Amount changed the displayed depth from 16% to 33%. Three separate mouse Undo commands restored depth to 16%, source to Osc B and mode to PM in that order. Clicking the on-screen C4 highlighted the key and produced visible output-meter activity. This establishes native event handling and internal output activity, not an external audio-device recording or subjective listening result.

A fourth Undo restored the pre-check modified `Lead / Neon Skyline` state, including Master 67%, Sub 13%, Osc A wave 66% / seven unison voices / 19 ct detune, Osc B level 30% and filter cutoff 6.50 kHz. Undo was then disabled. The verification window was closed. Search text injection did not produce visible text and one attempt was guarded after user input; browser search typing is not counted as a native pass. Mouse browsing was used instead. Redo and the remaining cross modes are covered by the integration suite rather than claimed as new native checks.

`Anyvoc-v0.12-Crossmod-Demo.wav` contains five consecutive three-second sections: dry, FM, PM, AM and RM, with Osc B muted as the modulation source. The final rendered file is stereo, 48 kHz, 24-bit and 15 seconds long. Measured RMS values are 0.0280487, 0.0280428, 0.0280138, 0.0180128 and 0.0165528 respectively; the largest section peak is 0.0413909. All sections contain audio without clipping, and relative level differences are preserved. Numerical audio checks do not imply Serum sound matching.

## Cross-oscillator modulation contract

Each wavetable oscillator has two existing Warp slots. A slot selects either one of the seven Classic modes or FM, PM, AM or RM. The unified editor menu has eleven entries. Cross modes replace the remembered Classic operation in that slot; switching back to Classic preserves its existing host-parameter mapping. The existing slot amount and its modulation destinations continue to control depth.

Sources are Osc A, Osc B, Osc C, Sub and Noise, including self-routing and reciprocal connections. Each voice owns its source taps and FM state. A connection reads the previous sample's tap; all new taps are published after the voice's oscillator, sub and noise outputs have been evaluated. Taps are bounded `[-1,1]` mono signals taken before source level, pan, filters and common amplitude/velocity gain. Wavetable/sample unison taps are averaged; stereo sample taps use the channel mean. Granular taps use the existing grain-window overlap normalization. Noise uses the mean of the same two random draws already used for its stereo output.

A source with Level zero still renders when an audible target needs it. Dependency traversal includes both sides of a topology transition and reachable source chains. A disconnected muted target does not activate an otherwise unused granular source. Taps stay current for all rendered oscillators, and unused mono DC state is cleared, avoiding stale history when an audible source becomes referenced again. Sample/granular oscillators can supply taps but are not cross-modulation targets in this release. Their stored Warp selections do not affect playback.

For effective slot depth `d = clamp(base amount + modulation, 0, 1)` and previous-sample source tap `s`, the operations are:

| Mode | Implemented operation |
| --- | --- |
| FM | Sum `d * s` across FM slots, then use signed increment `clamp(baseIncrement * (1 + 8 * sum), -0.45, 0.45)` turns/sample. Negative increments traverse the table backwards. |
| PM | Add `d * s` phase turns at the selected slot's phase-map position. |
| AM | Multiply by `1 - d + d * (s + 1) / 2`. |
| RM | Multiply by `1 - d + d * s`. |

The two phase maps compose in reverse lookup order, while pointwise amplitude operations retain slot order. FM maintains a separate phase offset from the ordinary dry oscillator phase. Below 1% total FM depth, the readout blends toward the same non-FM dry-phase waveform, preserving other slot processing. At zero FM depth the independent dry phase is used. A missing or finished sample source has a zero tap: full AM then halves the carrier and full RM suppresses it, rather than bypassing the operation.

Mode/source topology changes use the existing approximately 5 ms two-bank transition. FM phase, mip and DC state belong to their respective banks; evaluating the cross-reference waveform does not advance FM a second time. Inactive Classic source selections are canonicalized for DSP topology comparison, so changing an unused source does not trigger a transition. Voice initialization, reset, stealing and channel panic clear per-voice state through the existing lifecycle; mono legato retains the continuing voice.

Pure cross modulation bypasses the Classic delta-DC correction to preserve the AM/RM formulas. Mixed Classic/cross processing uses a cross-modulated reference with Classic phase/fold operations omitted and the same lookup bandwidth. Current and previous banks have separate references. This is not a guarantee that every nonlinear mixed patch has zero DC. Pure cross modulation can also produce DC.

The preview remains a static single cycle showing Classic operations only. It omits live cross modulation and explicitly labels that omission; it does not display an unrelated hidden Classic mode as if it were the cross-modulated output.

## State, automation and editing

Twelve parameters are appended after the previous final parameter, `osc3RandomUnison`: `osc{1..3}Warp{1..2}CrossMode` and `CrossSource`. Each has five choices and version hint 8. The prior seven-choice Warp Mode parameters keep their IDs, order, version hint 6 and normalized automation mapping. Host changes to a remembered Classic mode do not silently disable an active Cross mode.

Saved state is schema 8. Schema 1–7 migration supplies missing Cross parameters with their defaults while continuing to require the parameters belonging to each older schema. Schema 8 requires all new controls. Invalid or duplicate state entries are rejected atomically. Older Anyvoc versions do not accept schema 8; retain older preset files when reopening them in an older release.

The factory library has 18 programs. Existing indices remain in place, with `Keys / Phase Alloy` at index 16 and `Bass / Vector Steel` at index 17. These original presets demonstrate PM and FM respectively, using a muted oscillator as the source.

Unified mode changes form one user-history transaction even when returning to Classic requires two parameter writes. Host begin/end notifications remain balanced. Reentrant project replacement and related-parameter automation stop a stale paired write rather than overwriting newer host state. Source choices create their own single history edit. Host automation updates controls without creating user-history entries. Existing amount knobs, modulation rings and matrix destinations remain available; the release does not expand the eight-row matrix.

## Test coverage and its limits

The four added engine groups exercise independent constant-source AM/RM landmarks, half-turn PM, FM frequency ratios and signed traversal. The signed-FM fixture uses an asymmetric two-harmonic table and a phase-invariant Fourier statistic, so a positive-only frequency estimate cannot incorrectly establish reverse traversal. Additional cases cover muted wavetable/sample/granular/sub/noise sources, source-pan and source-level independence, unison averaging, opposite stereo channels, zero-depth legacy/RNG identity, disconnected muted routes, ignored sample/granular target controls, reciprocal/self routing, oscillator-graph relabeling, block partitioning, rapid topology changes, invalid settings, reset and CC120. Pure constant-source AM/RM silence checks remain bit-zero; mixed Classic cases use the quantified residual bound described above.

The FM return-to-zero test checks convergence to a matched independent dry phase after a settling interval. It does not measure a maximum transient discontinuity. Rapid topology tests establish bounded finite rendering and reset behavior, not inaudibility of every possible transition. Existing lifecycle coverage includes polyphonic and mono/legato behavior, but the new groups do not exhaustively combine every MIDI ownership case with every cross-modulation graph.

Five added processor/editor groups cover appended parameter order and normalized mappings, schema 1–7 migration, schema 8 validation, preset/project round trips, new program indices, static-preview behavior, grouped history and reentrant host boundaries, actual audio changes through host Cross parameters and existing modulation amounts, unified mode/source labels and availability, Undo reconciliation and automation-driven UI refresh. These are direct integration tests, not evidence of FL Studio or another DAW loading the final VST3.

The retained 0.11 modulation-ring and project-restoration regressions remain in the suite. Prior native ring results and their precise limits are recorded in [VALIDATION-0.11.md](VALIDATION-0.11.md); they are not represented as a new 0.12 native pass.

## Reference and remaining scope

The [official Serum 2 product page](https://xferrecords.com/products/serum-2) and [official feature overview](https://static.xferrecords.com/Serum%202%20What%27s%20New.pdf) are workflow and feature references for dual Warp operations and cross-oscillator sound design. They do not document or establish matching DSP formulas, tap ordering or sound. Anyvoc uses original code, artwork and presets; no Xfer assets are included. Serum preset files are not supported, and full Serum 2 sound, interface and feature equivalence has not been achieved.

The user chose standalone. FL Studio installation, scan/load/audio and project reopen remain deferred. Other DAWs and operating systems have not been validated. AU/AAX and MPE are unavailable; pluginval alone would not establish every host workflow.

Remaining gaps include multisample/SFZ, a spectral oscillator, cross modulation of sample/granular targets, filter sources, more modulation/effects buses, Clip sequencing, wavetable formulas and larger frame sets, full library metadata, complete accessibility and a Chinese interface. Saturation and cross modulation are not oversampled. Mip selection reduces source harmonics but does not remove all FM/PM/AM/RM sideband aliasing, feedback aliasing or Classic Sync/Mirror/Fold aliasing. Signed increment clamping bounds phase advance; it is not an anti-aliasing proof. Pitched samples/grains use interpolation without mipmapped anti-aliasing. Sample imports remain limited to 20 seconds, 8–192 kHz, mono/stereo. Worst-case live CPU has not been benchmarked.

Native Windows Ctrl-key automation was inconclusive in 0.9, including JUCE TextEditor's own Ctrl+A/Z. Direct key-handler tests and mouse history commands passed in that release; this is not proof of physical-keyboard delivery. An older v0.5 run had an intermittent arp-sustain RMS failure; the same binary subsequently passed 131 runs without an established cause. Neither is claimed fixed by this release.
