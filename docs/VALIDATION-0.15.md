# Anyvoc 0.15 validation — 2026-11-18 (Asia/Shanghai)

Release build, all 148 test groups, pluginval strictness 5, audio comparisons and standalone UI inspection passed. The previous release remains available; its evidence is archived in [VALIDATION-0.14.md](VALIDATION-0.14.md). The external package report records the final archive checks without creating a self-referential archive hash.

## Envelope contract

All four envelopes gain Delay and Hold stages, Attack/Decay/Release curvature, a per-envelope tempo-sync switch with independent stage divisions, and a per-envelope Retrigger in Legato option. The existing attack/decay/sustain/release parameter IDs, order, ranges and defaults remain unchanged. The 48 additions use parameter version hint 11 and follow the complete 1,018-parameter 0.14 prefix.

For each prefix amp, env, env3, env4, additions are ordered Delay, Hold, AttackCurve, DecayCurve, ReleaseCurve, TempoSync, DelayDivision, AttackDivision, HoldDivision, DecayDivision, ReleaseDivision, RetriggerLegato. Delay/Hold are continuous 0–8 seconds (default zero, skew 0.3); curves are continuous -1 to +1 (default zero). Sync and legato retrigger default off. Divisions are Instant, 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars, 1/8 T, 1/16 T, 1/8 D, 1/16 D, with defaults 0/3/0/5/5 for Delay/Attack/Hold/Decay/Release. Quarter-note beat lengths are 0, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8, 16, 1/3, 1/6, 3/4, 3/8. Sustain is always a level.

The curve is F(t)=t^(2^(4*c)); a segment is start+(end-start)*F(t). Zero curve takes a linear path. This is Anyvoc's defined transfer function, not a claim about Serum's internal curve algorithm. Free seconds and sync divisions are stored independently. Sync durations use the current transport tempo without a one-block lag. Legato retrigger affects the selected envelope on overlapping note-on events, not on note-off fallback to a previous held note; oscillator phase and glide behavior remain independent.

Advanced stages retain normalized progress during live duration/BPM changes; the remaining progression speed changes. Curvature updates immediately at the current progress and may change the current level. Delay is entered at note start and Hold after Attack; Instant skips the stage within the current sample. Engine transport seeks do not retrigger envelopes. The processor retains its existing reset on host transport stop. Turning advanced controls off resumes the original linear recurrence from the current level. With all additions at defaults, the legacy ADSR arithmetic and update order remain unchanged, including release timing latched at note-off. Amp still determines voice lifetime.

Schema 11 saves all controls and migrates schemas 1–10 with default additions. Missing/duplicate/nonfinite controls in schema 11 are rejected before changing sound. The original twenty factory program indices remain; Motion / Clockwork Bloom and Lead / Second Breath are appended. The 1,018-parameter source prefix was compared against the retained 0.14 source archive and is unchanged.

Envelope Lab exposes a graph and stage controls for each envelope, with direct node/curve manipulation, exact feedback and one undo entry per drag. The graph guards the exact 16-parameter snapshot, history revision and editor lifetime, including one-ULP related host automation. Sustain's graph plateau represents a held key, not a programmable time. The main ADSR cards display divisions and disable second-based time editing when synchronized; the Lab edits the divisions. Stage buttons bring overlapping zero-length handles to the front.

## Build and automated verification

Windows x64 Release; MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned at 29396c22c93392d6738e021b83196283d6e4d850. Both executable and VST3 module have FileVersion 0.15.0. Existing conversion/deprecation warnings remain; build exit code is zero.

- CTest: 4/4 suites, 65.60 seconds; [summary](ctest-0.15.txt), [individual results](test-details-0.15.txt).
- Engine: 49 groups. New tests measure actual rendered envelope modulation against an independent piecewise mathematical reference, including sample boundaries, curves, Instant, live timing, legato isolation, panic/sustain, block partition and legacy identity.
- Processor: 83 groups. Covers 48 appended parameters, previous mappings, schema 1–10 migration, schema 11 roundtrips and atomic rejection, all four audio paths, independent BPM/seconds references, host/local clock priority, tail reporting before the next audio block, all 22 audible factory programs, graph/direct-control host callbacks, exact one-ULP automation cancellation, editor destruction and balanced gestures.
- Wavetable: 10 groups. Preset library: 6 groups.
- [pluginval](pluginval-0.15.txt): strictness 5, SUCCESS, version 0.15.0. This is not a substitute for FL Studio or other DAW integration tests.

The initial run exposed two test synchronization defects: a fixed 2 ms dispatcher could leave an asynchronous matrix navigation pending; an envelope gesture fixture used a stale handle position after direct undo. The fixture now waits for its FIFO dispatch marker with a bounded timeout and refreshes graph layout before deriving coordinates. All original state/navigation/audio assertions remain. Final complete rerun passed; production UI logic did not need relaxation.

## Audio and native UI verification

- Retained 0.14 processor-test binary: build/validation-baseline/AnyvocProcessorTests-v0.14.exe, SHA-256 2D2F387964F2DFA272AA215C84FF99890E3D947516BF22AA1ED3A810C81368EB.
- Fresh 0.14 legacy demo: build/validation-baseline/Anyvoc-v0.14-before-v0.15.wav. The final 0.15 render is byte-identical, SHA-256 3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920 (4,320,104 bytes).
- [Independent audio report](audio-analysis-0.15.json): Anyvoc-v0.15-Envelopes-Demo.wav is exactly 20 seconds, 48 kHz, 24-bit stereo, 960,000 frames. SHA-256 b0cddb1fd9dfeceac0f4c2494f5509de7b904f9fd142e8dea8fd7443605558dc. Peak is 0.161003470 (-15.863 dBFS), overall RMS 0.024328926, no full-scale PCM samples. Integer PCM decoding cannot independently certify pre-encoding float finiteness; the rendering tests separately check finite output.
- The five 4-second segments use a fixed gain: Clockwork Bloom linear curves/100 BPM, factory curves/100 BPM, factory curves/150 BPM, Second Breath legato retrigger off, and retrigger on. Difference RMS is 0.008259881 for curvature, 0.026610484 for BPM, and 0.013683922 for legato. These are numerical comparisons, not a claim of subjective listening or Serum equivalence.
- All 66 previews regenerated. The eight new Envelope Lab standard/minimum-size previews were visually inspected: no clipped labels, overlapping controls or obscured keyboard; division/millisecond and key-held/note-off displays agree.
- Native Windows standalone launched with the user's prior Neon Skyline state. Loaded Clockwork Bloom from the browser; opened Amp Lab; dragged Decay Curve +45% to 0% and undid it; dragged Sustain 22% to 55% and undid it; switched BPM off to show the saved 10/200/300 ms ADSR values, then undid; selected Envelope 4; enabled its Retrigger in Legato switch and undid; clicked C4 with Lab open and observed the output meter. Closed the Lab and undid the preset load, restoring the prior visible sound.
- Search-text injection through the automation tool did not insert text on this run; browsing the list worked. No native typing/shortcut fix is claimed. Actual overlapping-note legato semantics were verified by deterministic MIDI tests and the demo, not by a single mouse key click.
- Closed only the QA window and restored the original settings file byte-for-byte; SHA-256 5000F1074D6B885621AFABB357A32E2BA2050F8661AD38C3FB92BC520674AB7E.

## Release contents

- Standalone: Anyvoc.exe, 9,329,664 bytes, SHA-256 E905A06688160A341FC1DB9BB3A334E163245DF3F0C0BBFF09B0F5390B476773.
- VST3 module: 9,869,824 bytes, SHA-256 C51858D0E7B1F5059224F9A424CB40E461B9F1B9D318782744E6A220E2C4CFF2.
- dist/Anyvoc-0.15.0-Windows, Windows-x64.zip, and corresponding Source.zip include current documentation; source includes pinned JUCE and EnvelopeModel.h. Older release folders remain available.
- External report: dist/Anyvoc-0.15.0-PackageVerification.json. Verification checks every ZIP CRC, unique safe paths, exact file sets, every packaged file against current input bytes, both module versions/hashes, and the mandatory 20-second envelope demo. Archive checks are performed after both packaging commands finish.

## Reference and remaining scope

The [official Serum 2 feature overview](https://static.xferrecords.com/Serum%202%20What%27s%20New.pdf), page 14, describes tempo-synced envelopes and an option to trigger them at note-on even in legato mode. Anyvoc uses original code, graphics and presets; feature work is not proof of sonic or complete workflow equivalence, and Serum presets are not supported.

Still incomplete: chaos/audio-rate LFOs, LFO phase/direction/swing and further drawing tools, macros as destinations, more modulation targets, multisample/SFZ, spectral oscillators, effects buses, Clip sequencing, MPE, further filter types, expanded wavetable/formula capabilities, library metadata, full accessibility and Chinese UI. Full Serum 2 equivalence has not been achieved.

The user chose standalone. FL Studio and other DAWs remain unverified. AU/AAX/macOS are not provided. Existing nonlinear aliasing and sample/granular pitch-shift limitations remain; imports are limited to 20 seconds, 8–192 kHz, mono/stereo. Earlier native Ctrl-key delivery and intermittent arp-sustain RMS issues are not claimed fixed by this work.
