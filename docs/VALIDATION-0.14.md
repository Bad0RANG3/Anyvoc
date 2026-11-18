# Anyvoc 0.14 validation — 2026-11-18 (Asia/Shanghai)

The final Release build, all 136 test groups, VST3 pluginval strictness 5 and native standalone checks passed. Completed 0.13 evidence is archived in [VALIDATION-0.13.md](VALIDATION-0.13.md). Package verification is recorded separately in dist/Anyvoc-0.14.0-PackageVerification.json after packaging, avoiding archive hash self-reference.

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 at 29396c22c93392d6738e021b83196283d6e4d850; VST3 and standalone use the static MSVC runtime.

## Expanded sources and compatibility

The engine now has ten independent LFOs, four ADSR envelopes (Amp, Filter, Env3, Env4), and eight macros. Effective source IDs 0–9 retain their meanings; 10–17 are LFO3–10, 18 is Amp Env, 19–20 are Env3/4, and 21–24 are Macro5–8. LFOs are naturally bipolar, other active sources unipolar. All are available as matrix primary or auxiliary sources. The previous matrix curve/polarity/amount semantics remain as documented in the 0.13 validation.

The complete 0.13 parameter-definition source prefix is byte-identical to the retained 0.13 source archive. The 396 appended parameters all use version hint 10: eight new LFOs with forty parameters each, two new envelopes with four parameters each, four macros, and two extra source selectors on each of 32 matrix rows. Old source/aux choices keep their ten-choice normalized host mappings. An Extra value of zero selects the legacy control; values 1–15 select effective IDs 10–24. The editor presents a single source menu. Selecting an extended source preserves the inactive legacy value. Switching back sets the legacy value before clearing Extra. Reset, reuse and movement include all fifteen route fields; duplicates are matched by effective source.

Schema 10 saves all controls and accepts schemas 1–9 with default additions preserving old routes. Schema 10 requires all new controls. Twenty factory programs are provided: the prior eighteen retain their indices and content; Motion / Prism Weave and Pad / Four Horizons are appended. Older versions cannot read schema 10.

New LFOs use the existing sine/triangle/saw/square/random/custom shapes, free/trigger/one-shot modes, beat divisions and 33-point linear/step editor. New random seeds are independent; the first two seeds and the audio/grain RNG are retained. Rate range remains 0.05–20 Hz in the UI. Env3/4 follow the existing Filter envelope lifecycle, including sustain, mono retrigger, legato and voice stealing. Amp controls voice lifetime; longer extra-envelope release does not keep an otherwise finished voice alive. Macros are sources only.

The main MOD page shows two selectable LFO cards, a selectable envelope card and two banks of four macros. The Sources panel exposes all 24 active sources while leaving the keyboard available. Changing the displayed LFO, envelope or macro bank is view-only. LFO Lab selects any of the ten curves. Composite source and LFO shape edits have editor-lifetime and host-state guards; already published values are not claimed to be atomic multi-parameter host transactions.

## Verification results

- The full Release build succeeded. All 45 engine groups passed. Two new reset/block-partition and voice-stealing fixtures initially compared different JUCE effect startup ramps; both sides now receive the same silent warmup and reset. Exact comparison assertions were retained; no engine behavior was changed for these failures.
- Final CTest passed all four executables in 44.09 seconds: Engine 45, Processor 75, Wavetable 10 and Preset Library 6 groups, 136 total. See ctest-0.14.txt and test-details-0.14.txt. The complete parameter prefix and all 396 additions, old-schema migration, malformed-state rejection, thirty primary/auxiliary audio comparisons and all twenty factory programs are covered.
- The failed source UI refresh test exposed parameter listener ordering: APVTS raw values can lag a parameter callback. Composite source menus now decode the current parameter values and refresh on the UI timer. The failed interrupted-ring test exposed hidden source-bank changes; rings now guard all four raw source selectors. Matrix moves and popup menus also snapshot all fifteen raw route fields.
- Actual LFO 10 drops exercise 27 begin/value/end host callback cases across project replacement, related automation and editor destruction, including new allocation and duplicate-route re-enabling. Both the knob callback and parent interface check lifetime; assignment stops remaining writes when the originating editor disappears. Matrix handle interruption tests include hidden source/destination selectors. LFO drawing, presets and composite shape controls also exercise callback teardown. LFO-card visibility/timer lifetime guards were statically reviewed; native keyboard-focus loss combined with a host callback destroying the editor was not runtime-tested.
- Fixture corrections kept strict comparisons: bool/choice inputs now use legal normalized values; each navigation segment is compared byte-for-byte before/after, with undo parameters separately checked. Skewed envelope defaults are compared exactly with the expected normalized write/read roundtrip (one-ULP differences for Env3/4 Attack/Decay). Ordinary restore can similarly change serialized LFO1/2 Rate floats by about 1e-7; this is reported in the test log. No production change or relaxed tolerance was used to hide these fixture issues.
- Final VST3 pluginval strictness 5 ended SUCCESS; see pluginval-0.14.txt. This does not establish FL Studio or other host compatibility.
- The final build regenerated fifty-eight preview PNGs. Review of the eight new views and the main Synth/MOD screens found no overlapping or clipped controls at the tested default and minimum sizes. LFO Lab help now directs users through SOURCES.
- Native Windows standalone 0.14 opened the prior Lead / Neon Skyline settings. Loaded Prism Weave, selected LFO 10, opened its Lab, drew a curve and observed one-step undo restoring Triangle. Selected Envelope 4; changed to macros 5–8; dragged Macro 5 from 75% to 53% and undid it. Dragged Env4 from SOURCES to Filter A Cutoff; the palette closed and matrix row 5 showed Env4 / Filter A Cutoff / +25%, then undo removed that route. Clicking C4 produced visible output-meter activity. The preset change was undone, restoring the prior patch; the validation window was closed and the pre-test standalone settings file restored byte-for-byte. This is native UI/meter verification, not subjective listening or DAW validation.
- Retained baseline binary: build/validation-baseline/AnyvocProcessorTests-v0.13.exe, SHA-256 CB14796ED03F884FBBDADDF44C0FA7B3526B00EDA802265542249BD6FB0B5F5E. A fresh 15-second legacy demo generated before rebuilding and the 0.14 output are byte-identical: 4,320,104 bytes, SHA-256 3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920. This covers the deterministic pad/sample/granular fixture, not every possible old patch.
- The new sources demo is 16 seconds, 48 kHz, 24-bit PCM stereo: Prism Weave with Macro 5 at zero then one, followed by Four Horizons with Macro 8 at zero then one, each for four seconds at fixed gain. Peak is 0.140376925 (-17.054 dBFS), RMS 0.022953856 (-32.783 dBFS), with no full-scale decoded samples. Channels differ as expected. See sources-demo-analysis-0.14.txt. These are offline measurements, not a subjective listening evaluation.

Production files both report version 0.14.0:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone Anyvoc.exe | 9247744 | 3CD839E6B1C0F49D10F8E5EF6D65C4250A2CDECCBA75C3D674CE6926E16B62EA |
| VST3 Contents/x86_64-win/Anyvoc.vst3 | 9787392 | 65C11D7472FB31765F490F5393E1F20593E601EA3B5887968CCCDF75D3056CCC |

## Matrix performance measurement

On the Ryzen 7 9800X3D, Release engine at 48 kHz / 256 samples, sixteen staggered sustained voices, one wavetable oscillator at unison 1 and filters/effects off: median wall time for ten audio seconds over three rounds was 0.696016 seconds with no routes; 0.879702 with eight legacy routes; 2.131682 with 32 curved routes plus curved auxiliary sources; 4.872565 with 32 per-voice curved routes. The last case achieves 2.052307 times real-time throughput. See matrix-benchmark-0.14.txt.

The fixture retains the prior source routing; the added eight LFOs run free. It is not an all-ten-LFO per-note worst case. Timings include per-block setSettings and exclude construction/warmup. They are average offline measurements, not worst-block deadlines, DAW CPU readings or guarantees for complex patches. No controlled percentage performance comparison is claimed.

## Reference and remaining scope

The [official Serum 2 overview](https://static.xferrecords.com/Serum%202%20What%27s%20New.pdf) specifies up to ten LFOs, four envelopes and eight macros. Matching these counts is not proof of sonic, visual or complete workflow equivalence. Anyvoc retains original code, graphics and presets and does not read Serum presets.

Remaining modulation gaps include chaos modes, audio-rate LFOs, LFO phase/direction/swing and richer drawing controls, envelope hold/delay/tempo and individual legato options, macros as destinations and more modulation targets. Multisample/SFZ, spectral oscillators, effects buses, Clip sequencing, MPE, further filters, larger wavetable sets/formulas, expanded library metadata, complete accessibility and Chinese UI are also incomplete.

The user chose standalone. FL Studio installation/scan/audio/project reopening and other DAWs remain unverified. AU/AAX/macOS are unavailable here. Existing nonlinear aliasing and sample/granular pitch-shift limits remain; imports are limited to 20 seconds, 8–192 kHz, mono/stereo. The older native Ctrl-key delivery issue and intermittent arp-sustain RMS failure are not claimed fixed by this work.
