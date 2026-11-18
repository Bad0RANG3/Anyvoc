# Anyvoc 0.16 validation — 2026-11-18 (Asia/Shanghai)

Release build, all 162 test groups, pluginval strictness 5, legacy audio identity and new audio comparisons passed. Previous release evidence is archived in [VALIDATION-0.15.md](VALIDATION-0.15.md). Package integrity is recorded in the external release report.

## LFO motion contract

Ten LFOs gain a phase offset, Forward/Reverse/Ping Pong direction, Follow Swing, and an independently saved 20–1000 Hz audio-rate mode. Rate mode has three UI options: Free Hz, Tempo and Audio Rate. Legacy Sync takes precedence over AudioRateEnabled. The original .05–20 Hz parameters are unchanged. Random sample-and-hold ignores Phase/Direction, retaining their saved values and its original random sequence.

Phase is a read offset, not a clock reset. Offset is added and wrapped before direction is applied. With u in [0,1], Forward reads u, Reverse reads 1-u, and Ping Pong reads 1-|2u-1|. One Ping Pong period is a complete outbound and return trip. Integer offsets preserve a one-shot's terminal coordinate 1, so an independent final Custom point remains reachable. At phase zero, one-shot Forward holds its right endpoint and Reverse/Ping Pong hold the left endpoint.

Swing warps one shared musical grid. The pair period P is one quarter-note beat for the 1/8 grid and half a beat for 1/16. With x=q/P, t=x-floor(x), a=.5+.25*s, the warped beat is P*(floor(x)+(t<a ? .5*t/a : .5+.5*(t-a)/(1-a))). Amount s=0 is a direct identity path; s=1 gives 75:25. Only Sync plus FollowSwing applies this mapping. Free/synced LFOs align to warped host PPQ; triggered and one-shot voices integrate warped per-sample deltas without retriggering on a seek. Without a playing host PPQ, a shared local beat clock continues.

Phase destinations 18–27 correspond to LFO 1–10, with amount in turns. All sources read the preceding sample's per-voice offsets; the full matrix then writes the next offsets. Self-feedback and cyclic graphs therefore have one sample of delay and no recursive row-order dependency. Random targets ignore phase routes. Audio-rate sources, including auxiliaries and phase dependencies, update the affected pitch/pan/filter controls each sample; unrelated legacy controls retain their original 16-sample cadence. This adds bandwidth, not full anti-aliasing.

The complete 1,066-parameter 0.15 prefix is unchanged byte-for-byte in its parameter-definition source (SHA-256 f6f0d656093b03083d41f2692fe9cbce94b5c45187f1ac472a3a32c4523ba4c8). The 116 additions use version hint 12, total 1,182. Order: five controls per LFO (Phase, Direction, FollowSwing, AudioRateEnabled, AudioRateHz), SwingAmount/SwingDivision, then LfoPhaseEnabled/LfoPhaseTarget for each of 32 routes. Phase is continuous 0–1/default 0; direction defaults Forward; both flags default off; audio rate is continuous 20–1000/default100/skew .5; global swing is 0–1/default0 and grid defaults 1/8. Phase destination selectors default disabled/LFO1.

Each raw matrix row now has 17 fields. Effective destination priority is Phase, Warp, Filter, Original. Composite destination changes preserve inactive selectors and guard all seven selectors against host reentry; row copy/move/reset/reuse and UI snapshots cover all 17 fields. Rate-mode changes guard both switches and preserve all remembered rate/division values. Schema12 migrates schemas1–11 with defaults; malformed current states are rejected before sound publication. Factory indices0–21 remain unchanged; Swing Carousel and Phase Lantern append indices22/23.

The full-width LFO Lab provides curve editing, both rates, division, play mode, direction, phase, swing and interpolation. The curve edit guard covers45 related parameters plus history/editor lifetime. START shows the configured read start and LIVE uses actual engine read position, including phase/direction/swing/routing. Phase knobs accept source drops and route-depth rings. The source palette preserves an open LFO Lab so this drop target stays reachable.

## Build and automated verification

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned at 29396c22c93392d6738e021b83196283d6e4d850. Build exit code zero; both binary FileVersions are 0.16.0. Existing conversion/deprecation warnings remain.

Engine: 54 groups passed. Independent references cover actual phase/direction/one-shot output, musical swing grids and seeks, per-voice unit-delay phase feedback, high-rate primary/auxiliary/phase chains, pitch phase integration, pan gain, and both filters' cutoff/resonance. Random sequence, inactive-control identity, block partition, reset and invalid-value checks pass. Wavetable: 10 groups passed. Preset library: 6 groups passed.

Processor: 92 groups passed in the [targeted final rerun](ctest-processor-0.16.txt), 108.02 seconds; [full Processor details](test-details-0.16.txt). Coverage includes all 116 parameter definitions, schema 1–11 migration and schema 12 rejection/roundtrips, all 24 factory programs, 50 LFO control audio paths, all 10 phase targets, composite mode/destination host callbacks, exact one-ULP curve conflicts, phase drag/drop, hidden-control gating, and lifetime/gesture balance. The [initial full run](ctest-0.16.txt) records passing Engine/Wavetable/Preset suites and four Processor failures subsequently resolved in this targeted rerun. No unresolved test failure remains.

[pluginval](pluginval-0.16.txt) strictness 5 passed on the final rebuilt module (version 0.16.0). This does not substitute for tests in individual DAWs.

The initial engine pitch reference omitted the existing 64-sample onset fade. Adding that known fade to the independent reference resolved the mismatch without widening its 2e-5 tolerance. A real telemetry edge was also fixed: the pending one-sample phase offset remains visible immediately after disabling its route. Processor regression exposed JUCE's convenience float constructor defaulting to a .01 interval; Phase and Swing now explicitly use continuous ranges. Two new UI fixtures addressed the hidden compact destination instead of the visible Detail control; the fixtures now navigate to the correct page and control.

## Audio and native UI verification

- A fresh 0.15 legacy demo was rendered with the retained processor-test binary before changing source. Final 0.16 output is byte-identical: 4,320,104 bytes, SHA-256 3140a2896cd0954762e6406b4b344c22a185fb3f493dc3a96e7b0e9ce8747920.
- [Independent audio report](audio-analysis-0.16.json): 20 seconds, 48 kHz, 24-bit stereo, 960,000 frames; 5,760,104 bytes; SHA-256 29a826a0882498a1935b90c00232e4db76ab11d77dc04b7c4aa3caae9aa3d7b6. Peak 0.155530810 (-16.164 dBFS), RMS 0.022782152, no PCM full-scale samples. Integer PCM does not independently certify pre-encoding float finiteness; rendering tests check finite output separately.
- Five fixed-gain 4-second segments: Swing 0%, Swing 60%, Swing 60% with Reverse, Phase Lantern 110 Hz, and 220 Hz. Controlled waveform difference RMS: 0.009958185 for swing, 0.016287551 for direction, and 0.000493519 for audio rate. These numerical comparisons do not imply subjective listening or Serum equivalence.
- Native standalone QA loaded Phase Lantern, opened LFO Lab, dragged Phase from 0 to 94 degrees and undid it, dragged Macro 5 from SOURCES onto Phase and undid the route, changed Audio Rate to Tempo and undid back to the saved 110 Hz, then played C4 and observed the output meter and LIVE cursor. Undo restored the user's previous Neon Skyline preset. This UI pass preceded the final continuous-range correction; automated final tests cover the corrected precise values.
- Only the QA window was closed. The user's original settings were restored byte-for-byte: SHA-256 5000F1074D6B885621AFABB357A32E2BA2050F8661AD38C3FB92BC520674AB7E. No third-party app state was changed.
- All 74 previews regenerated. The eight new standard/minimum-size Swing, Motion, Audio Rate and Phase Routing previews were individually inspected: no clipped labels, overlapping controls or obscured keyboard. Swing 60% shows 65:35; Ping Pong transforms 45 degrees to a 25% start reading and 135 degrees to 75%. Audio Rate shows 110 Hz and disables Follow Swing; Sine disables Custom Curve interpolation. Route Detail clearly shows LFO 2 to LFO 1 Phase at +22%. Random gating is verified by tests, not these eight images.

## Release contents

- Standalone: 9,379,840 bytes, SHA-256 2C8B987BDFEDD833BD4B01CA6C5FEC4F6D1F2285AF4C5179714016DAE9611046.
- VST3 module: 9,918,976 bytes, SHA-256 6CBFEB6340F40F59E6BEB11EB9A0B45D3B251C705755E8C3E86619EC81F9CBFF.
- Release folders: dist/Anyvoc-0.16.0-Windows, Anyvoc-0.16.0-Windows-x64.zip and Anyvoc-0.16.0-Source.zip. Source includes pinned JUCE and LfoModel.h. Older releases remain available.
- External report: dist/Anyvoc-0.16.0-PackageVerification.json. The verifier checks ZIP CRCs, unique safe paths, exact file sets, every file against current source bytes, binary versions/hashes, and the mandatory 20-second LFO motion demo. It runs after both packaging commands finish and remains outside both archives to avoid self-referential hashes.

## Reference and remaining scope

The [official Serum2 feature overview](https://static.xferrecords.com/Serum%202%20What%27s%20New.pdf), page14, describes phase and direction controls, swing following and LFO rates up to1000Hz. Anyvoc implements its own specified timing and transfer functions; the document is not a disclosure of Serum's DSP algorithms.

Still incomplete: chaos LFOs and additional drawing tools, macros as destinations, more modulation targets, multisample/SFZ, spectral oscillators, effects buses, Clip sequencing, MPE, further filter types, expanded wavetable/formula capabilities, library metadata, full accessibility and Chinese UI. Full Serum2 equivalence has not been achieved; its presets are unsupported.

The user chose standalone. FL Studio and other DAWs remain unverified. AU/AAX/macOS are not provided. Nonlinear/high-rate aliasing and sample/granular pitch-shift limitations remain; imports are limited to20seconds,8–192kHz,mono/stereo. Earlier native Ctrl-key delivery and intermittent arp-sustain RMS issues are not claimed fixed by this work.
