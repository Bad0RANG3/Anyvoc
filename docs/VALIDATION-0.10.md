# Anyvoc 0.10 validation — 2026-11-18 (Asia/Shanghai)

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned to 29396c22c93392d6738e021b83196283d6e4d850. VST3 and standalone use the static MSVC runtime. Prior release evidence is preserved in [VALIDATION-0.9.md](VALIDATION-0.9.md).

## Current validation status

The final 0.10.0 Release build succeeded. All four CTest targets passed: 33 engine, 46 processor/editor, 10 wavetable and 6 preset-library groups (95 groups total, 5.87 seconds). See [ctest-0.10.txt](ctest-0.10.txt) and [test-details-0.10.txt](test-details-0.10.txt). VST3 pluginval strictness level 5 finished with SUCCESS; see [pluginval-0.10.txt](pluginval-0.10.txt).

The additional tests use an independent direct sine/cosine DFT oracle, covering phase conventions and wrapping, bins through 1023, phase-only preservation of amplitudes above unity, offline DC/Nyquist preservation, invalid and duplicate batch entries, silent partials, no-op/redo behavior, and atomic overflow rejection. Spectral-morph tests cover opposite phases, shortest-arc interpolation, silent endpoints, unchanged endpoints/outside frames, the previous waveform mode, and rejection of an overflowing interior frame without partial changes.

Processor/editor integration covers actual view/page controls, first/last-page drawing, silent-bin and repeated-reset no-ops, one-step history, stale-editor rejection, both morph modes, project state, preset files, export/import and asset undo/redo. These tests exercise saved waveform data rather than a new phase metadata format.

The 15-second legacy factory-pad/sample/granular demo rendered with the retained 0.9 executable and final 0.10 executable is byte-identical: SHA-256 `3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920`. The retained 0.9 processor-test executable has SHA-256 `63CBE59C45FDACF4C455AE4F64A1CEF6375554B43A777C205464B7B8CC87F97B`. SynthEngine, PluginProcessor and PresetLibrary source/header pairs also match the actual 0.9 source archive byte-for-byte. This checks the retained paths; it is not a proof about every possible patch.

All 37 editor preview PNGs were regenerated. The new phase, final harmonic page and spectral-morph views were visually inspected at their smaller sizes, with readable labels and no overlapping controls. A native Windows run of the final standalone confirmed a phase stroke, one-click TABLE UNDO/REDO, page reset/randomization, navigation to 961–1023, Spectral morph application and output-meter response to the on-screen C4 key. Global undo restored the six test edits to the starting Ribbon Glide sound, including the original 58% oscillator position. The meter observation is not an external audio-device recording. The pre-existing older standalone window was left untouched.

Both production binaries report file version 0.10.0:

| Binary | SHA-256 |
| --- | --- |
| Standalone Anyvoc.exe | `11CDE77487D4DEE4457EFEFCC602BAC0178C628983235455B7AC6111D02D20EC` |
| VST3 module | `DA8A552ABAA514F3B0FD6FABB2EE5153DDFA5AB6926E9D86A8B846DBBCF17B10` |

Package integrity is checked after archive creation against these binaries and the current source/docs; the external package verification report records the final archive hashes.

## Harmonic editor contract

The offline WavetableDocument exposes all 1023 positive-frequency partials of each 2048-point real frame. DC and the real-valued Nyquist bin remain separate. The original first-64 amplitude accessor remains available. Sine has phase zero; cosine has phase +0.25 turns (+90 degrees). Finite phase inputs wrap to [-0.5, 0.5) turns; the interface displays degrees. Magnitude edits clamp the selected partial to [0,1], whereas phase-only edits retain its existing magnitude, including values above one.

A batch validates all indices and values before changing the document. Duplicate entries merge by flagged field, with the last flagged value winning. Effective edits change only the selected complex bins and their conjugates, followed by one inverse FFT. Analysis caches the selected frame's spectrum and invalidates it on other mutations. Other frames are copied unchanged. Untouched partials, DC and Nyquist are retained by the offline model within FFT precision. The existing playback-table builder removes DC/Nyquist and constructs harmonic mip levels; the editor adopts that applied snapshot. This downstream policy predates 0.10 and is not claimed as full raw-sample preservation through playback/export.

Phase analysis and phase-only edits treat amplitudes at or below 1e-6 as silent; a phase-only operation cannot create a tone. No latent phase is stored for an empty partial. No-op field differences at or below 1e-7 amplitude/turns avoid inverse-FFT rounding and extra history. Finite sample peaks above unity are retained; candidates exceeding the existing absolute sample limit of 128 are rejected as a whole rather than clipped.

The interface shows 64 partials per page (63 on the final 961–1023 page), amplitude and phase views, and page-scoped phase commands. A stroke submits a batch and forms one local/global edit. Navigation does not create sound history. All edits remain offline and use the existing immutable wavetable publication, asset revision checks and sound history. No audio-thread FFT, new automation parameter, factory preset change or state-schema change is required; schema 7 remains compatible with 0.8/0.9.

## Spectral morph contract

Waveform morph retains its previous linear sample interpolation. Spectral morph linearly interpolates each partial's magnitude and its shortest wrapped phase arc. A half-turn tie (including FFT rounding within 1e-7 turns) chooses the negative arc. When an endpoint is silent, the audible endpoint supplies phase; silent partials do not acquire arbitrary phase rotations. DC/Nyquist interpolate as real values. Only existing interior frames are replaced; endpoints and frames outside the selected range remain unchanged. The entire result commits atomically as one local edit.

Equal-magnitude opposite-phase endpoint partials retain their magnitude at a spectral midpoint instead of cancelling like a waveform crossfade. This is an offline construction of intermediate wavetable frames, not a new spectral oscillator: live WAVE scanning still linearly mixes adjacent stored frames. Some cancellation can therefore remain between sparse generated frames. The 32-frame limit is unchanged.

## Reference and scope

The [official Serum 2 guide](https://xferrecords.com/manual/serum-2/docs), Wavetable Editor section, describes separate harmonic magnitude/phase editing and restricted-range editing commands. It supports the workflow target, not any claim that Anyvoc shares Xfer's algorithms, phase convention or complete feature set. No Xfer code, presets or graphics are included.

This original instrument is not verified equivalent to Serum 2. Sixteen factory presets remain included. FL Studio installation, scan/load/audio and project reopen remain deferred because the user chose standalone. Other DAWs and operating systems have not been validated. AU/AAX and MPE are unavailable. pluginval does not prove every host workflow.

Remaining gaps include multisample/SFZ, a spectral oscillator, cross-source FM/PD, more modulation and effects buses, Clip sequencing, wavetable formulas and larger frame sets, full library metadata, complete accessibility and a Chinese interface. Saturation is not oversampled; Sync/Mirror/Fold Warp is not fully bandlimited; pitched samples/grains use interpolation without mipmapped anti-aliasing. Sample imports remain limited to 20 seconds, 8–192 kHz, mono/stereo. Worst-case live CPU has not been benchmarked.

Native Windows Ctrl-key automation was inconclusive in 0.9, including JUCE TextEditor's own Ctrl+A/Z. Direct key-handler tests and mouse history commands pass in that release; this is not proof of physical-keyboard delivery. An older v0.5 run had an intermittent arp-sustain RMS failure; the same binary subsequently passed 131 runs without an established cause. Unrelated later work is not claimed to fix either issue.
