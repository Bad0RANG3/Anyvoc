# Anyvoc 0.13 validation — 2026-11-18 (Asia/Shanghai)

Final Release build, all 121 test groups, pluginval strictness 5 and native standalone checks passed. Completed 0.12 release evidence is preserved in [VALIDATION-0.12.md](VALIDATION-0.12.md). Archive verification is recorded separately in `dist/Anyvoc-0.13.0-PackageVerification.json` after packaging, avoiding archive hash self-reference.

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned to 29396c22c93392d6738e021b83196283d6e4d850. VST3 and standalone use the static MSVC runtime.

## Advanced modulation contract

The matrix has 32 routes. The original eight retain their IDs, positions, ranges, defaults and normalized automation mappings. Routes 9–32 append the same seven fields (Source, Dest, Amount, DestBank, FilterTarget, WarpEnabled, WarpTarget), followed by six additions for each route 1–32: Enabled, Polarity, Curve, AuxSource, AuxPolarity and AuxCurve. These 360 additions use version hint 9. The old parameter-definition source prefix is byte-identical to the retained 0.12 source archive.

Sources remain Off, LFO 1–2, Filter Env, Velocity, Mod Wheel and Macro 1–4. Primary Off is inactive; auxiliary None is the identity multiplier 1. Bypass suppresses the route without deleting settings. The eighteen destination choices, including Off and six Warp amounts, are unchanged.

Polarity is Original, Unipolar or Bipolar. LFO 1–2 are naturally -1 to +1; other active sources are 0 to 1. Polarity conversion precedes shaping. For curve c in [-1,1], exponent p = 2^(4*c). Unipolar shaping is x^p; bipolar shaping is sign(x)*abs(x)^p. Curve zero takes the exact linear path. The contribution is shaped primary times shaped auxiliary times the existing smoothed Amount. Routes sum at their destination. These are original defined transfer functions, not established Serum formulas.

Settings are sanitized before rendering. Nonfinite amounts and curves default to zero. Exponents and active routes are prepared per block. Exact input/transform caching avoids repeated evaluations, preserves signed zero and does not approximate pow. Cache hit/miss, live source/transform changes, reordering and block partition tests passed. No allocations were added to the sample render path.

Enabled, source, destination, polarity, curve and auxiliary edits take effect at the next audio-thread setSettings; no route crossfade is applied. Amount uses the existing 15 ms one-pole (about 99% settled in 69.1 ms), including while bypassed. Re-enabling does not ramp from zero. Position/level/balance/Warp modulation uses the new value next sample; pitch/pan/filter controls follow each voice's 16-sample cadence. Rapid descriptor changes on held notes can click. Prefer Amount or an auxiliary Macro for smooth depth control.

Nominal knob ranges use the product of both source intervals. Monotonic shaping preserves their extrema. These ranges are neither live telemetry nor sums across routes; separate filter envelopes and tracking are excluded. Bypass is greyed. Response plots show transfer functions before Amount, not live signals or arbitrary editable point curves.

## State and interaction

Schema 9 accepts schemas 1–8 with defaults preserving prior matrix behavior and empty added rows. Schema 9 requires all added controls. Existing duplicate/nonfinite-state rejection remains before publication. Older Anyvoc releases cannot open schema 9 presets. All 18 factory programs and their indices remain in place.

Compact matrix view has four pages of eight rows; expanded view scrolls. Each row can be bypassed, cleared, edited or moved by its handle. Dragging writes only on release; Up/Down on a focused handle also moves a row. Moves use stable insertion, shifting intervening rows with all thirteen underlying fields, including inactive destination selectors. Host automation stays attached to row IDs; host automation clips are not rearranged.

Move and reset each create one history transaction. Reusing an empty row resets its advanced settings. A duplicate source/target drop re-enables a bypassed match without replacing its settings; bypassed occupied rows are not free slots. Multi-parameter edits check the project generation and every involved normalized value around host begin/value/end callbacks, aborting remaining stale writes after a related host edit. Already committed writes are not described as atomic host transactions.

Expanded/detail backgrounds consume clicks to prevent edits through gaps to underlying LFO controls. Compact upper space remains transparent. Deferred detail opens check request sequence, history revision and visibility. Shared choice/button/slider guards capture intent before host gesture callbacks and use lifetime checks after callbacks. Tests cover project restore, related automation and editor destruction during callbacks, including continuous drags; stale writes stop and gesture ends remain balanced.

## Verification results

- Final `cmake --build build --config Release --parallel 4` exited 0. Final CTest passed all four executables in 21.07 seconds: Engine 41, Processor 64, Wavetable 10 and Preset Library 6 groups, 121 total. See `ctest-0.13.txt` and `test-details-0.13.txt`.
- Processor coverage includes all 360 appended parameters; schema 1–8 migration and schema 9 rejection cases; all 32 routes; destination mapping; sorting/reset/undo; 56 UI callback and continuous-drag interruption scenarios; and actual rendered audio with the last route.
- Moving a fractional old route Amount to a new row preserves its value. Added Amount/Curve/AuxCurve parameters explicitly use continuous ranges. The earlier audio-oracle failure was corrected by choosing reference values on the unchanged old Warp knob's 0.01 grid and asserting the realized reference value; the original audio comparison tolerance was retained.
- Final VST3 pluginval strictness 5 ended `SUCCESS`; see `pluginval-0.13.txt`. This does not establish FL Studio or other host compatibility.
- The final build regenerated 50 preview PNGs. Final review of MOD and six new matrix views at 1120 and 1008 widths found no overlap or clipping. Other views retain their existing reviewed layout.
- Native Windows standalone 0.13 was opened and inspected. Observed four-page navigation through route 32; route-32 detail Source LFO 1; curve dragging from 0% to +44% with plot feedback; AUX None to Macro 1; bypass greying; row-32-to-31 drag insertion and one-step undo; expanded view and wheel scrolling. All five parameter/move test edits were individually undone, restoring the prior Lead / Neon Skyline patch. Clicking C4 produced a visible output meter response. This is UI/meter verification, not a subjective listening evaluation. No native keyboard-shortcut or host test is claimed.
- Retained baseline executable: `build/validation-baseline/AnyvocProcessorTests-v0.12.exe`. Fresh 15-second legacy demo and final 0.13 demo are byte-identical, SHA-256 `3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920`. This covers the deterministic pad/sample/granular fixture, not every possible old patch.
- New matrix demo: 15 seconds, 48 kHz, 24-bit PCM, two identical channels as expected for this single-unison/no-stereo-effect fixture. Five 3-second segments use AUX Macro 1 at 0%, 25%, 50%, 100%, then 100% with primary curve +50%. Peak 0.055302262 (−25.145 dBFS), overall RMS 0.024207037, no full-scale decoded samples. See `matrix-demo-analysis-0.13.txt`; this is offline measurement, not a listening claim.

Production files both report version 0.13.0:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone Anyvoc.exe | 9194496 | 3680ECC1E865E5902532ED2B4639C95D9252A1D961F15F37D8A4A6101059B2C1 |
| VST3 Contents/x86_64-win/Anyvoc.vst3 | 9732608 | 2D26DC96350777BA8AD8FC2F8263002B31DD1058A9AED5881255D81CB079C85C |

## Matrix performance measurement

On the Ryzen 7 9800X3D, Release engine benchmark at 48 kHz / 256 samples, sixteen staggered sustained voices, one wavetable oscillator at unison 1 and filters/effects off: median wall time for ten audio seconds over three rounds was 0.622229 seconds with no routes; 0.813207 with eight legacy routes; 2.076478 with 32 curved routes plus curved auxiliary sources; 4.824128 with 32 per-voice curved routes and varied note-triggered LFO/envelope/velocity values. The last case achieves 2.073 times real-time throughput. See `matrix-benchmark-0.13.txt`.

These are average offline timings including per-block setSettings, excluding construction/warmup. They are not worst-block deadline measurements, DAW CPU readings or guarantees for complex patches. The earlier uncached fixture used different voice timing; no controlled percentage speedup is claimed.

## Reference and remaining scope

The [official Serum 2 feature overview](https://static.xferrecords.com/Serum%202%20What%27s%20New.pdf) describes bypass, reordering, expanded matrix views and source/auxiliary curves. It is a workflow reference, not proof of DSP or visual equivalence. Anyvoc uses original code/artwork/presets and does not read Serum presets. Full Serum 2 equivalence has not been achieved.

The user chose standalone. FL Studio install/scan/audio/project reopening remain deferred. Other hosts/platforms are unverified; pluginval alone does not establish host compatibility. AU/AAX and MPE remain unavailable. Further gaps include multisample/SFZ, a spectral oscillator, cross modulation of sample/granular targets, filter sources, additional LFOs/envelopes/macros, effects buses, Clip sequencing, wavetable formulas/larger frame sets, library metadata, full accessibility and a Chinese interface.

Existing limits remain: no cross-modulation/saturation oversampling, no complete nonlinear alias suppression or mipmapped sample/granular pitch shifting. Imports remain limited to 20 seconds, 8–192 kHz, mono/stereo. The 0.9 native Ctrl-key delivery issue and older unexplained intermittent arp-sustain RMS failure are not claimed fixed here.
