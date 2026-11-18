# Anyvoc 0.8 validation — 2026-11-18 (Asia/Shanghai)

Windows x64 Release, MSVC 19.44, Windows SDK 10.0.26100.0, CMake 4.4.4, JUCE 8.0.12 pinned to 29396c22c93392d6738e021b83196283d6e4d850. VST3 and standalone built with static MSVC runtime. Earlier details are preserved in [VALIDATION-0.7.md](VALIDATION-0.7.md).

## Results

- **78 groups, zero failures**: 33 engine, 33 processor, 6 wavetable and 6 preset library groups. All four final CTest targets passed. Logs: `ctest-0.8.txt`, `test-details-0.8.txt`. Automated browser tests and previews use isolated temporary libraries.
- Seven new engine groups cover last-note priority, duplicate-note counts, channel-specific sustain/controllers, Mono retrigger versus Legato envelope/LFO continuity, actual glide pitch/duration/interruption, separated notes, all-three-oscillator phase landmarks/random ranges, sample/granular continuity and pitch, source isolation, invalid values, mode changes and reset. Glide frequency is measured from audio zero crossings at 44.1/48/96 kHz with different block partitions.
- Four new processor groups cover the 12 appended parameters/version hints, schema 7 state and legacy migration, invalid-state rejection, Ribbon Glide preset, host controls affecting rendered audio, phase degree conversion, disabled control availability and disabled-toggle updates.
- Existing DSP, arp, sample/granular, LFO, filters, Warp, delay, wavetable and preset tests remain. Windows denied symbolic-link creation in the library fixture; that runtime branch remains skipped, with reparse exclusion reviewed in code.
- **pluginval strictness 5: SUCCESS**, identifying Anyvoc v0.8.0. Default 44.1/48/96 kHz and 64/128/256/512/1024 blocks, state, parameters, buses and editor lifecycle passed. Log: `pluginval-0.8.txt`. The separate Steinberg SDK validator was unavailable.
- 27 real JUCE component previews regenerated. New Voicing normal/minimum-size and Sample-mode images were visually inspected at 1120×760 and 1008×684; no control overlap or clipping observed. `images/Voicing.png`, `images/Voicing-small.png`, `images/Voicing-Sample.png`.
- The retained actual v0.7 processor-test executable and final v0.8 executable rendered the same 15-second pad/sample/granular fixture to **byte-identical WAVs**, SHA256 `3140A2896CD0954762E6406B4B344C22A185FB3F493DC3A96E7B0E9CE8747920`. This covers those programs/events, not all possible patches. All 15 older factory definitions and the pre-existing parameter definitions were compared with the released v0.7 source archive and are unchanged.
- New `Anyvoc-v0.8-Voicing-Demo.wav`: 12 seconds, stereo 24-bit 48 kHz, rendered by the real processor. Four three-second sections use the same overlapping phrase and dry patch: Poly, Mono, Legato, Legato with 180 ms glide. Small section-edge fades prevent hard cuts. Older 15-second and Warp demos remain included.

## Corrections during validation

Main-phase randomization initially consumed an extra shared RNG value, so phase changes on a muted wavetable could alter another oscillator's granular sequence. It now uses a separate stateless per-note/per-oscillator hash. Every new voice still consumes exactly the original 18 shared unison-phase draws. The unchanged isolation test passes and the legacy demo remains identical.

The phase oracle initially measured inside JUCE Chorus's startup wet/dry ramp: its default 50% mix ramps to zero over 50 ms, and the half-cycle phase offset placed the first measured crossing after the wet delay arrived. The fixture now renders 4096 silent samples before the note. The 0.0002-cycle tolerance is unchanged, and production effects initialization was not altered. Startup effects are not claimed phase-neutral before settling. The intermediate failure is retained in `ctest-0.8-diagnostic.txt`.

Native testing found Always Glide could appear unchecked after loading Ribbon Glide from Poly despite its parameter being true. This JUCE version ignores `Button::setToggleState` when disabled. Voicing now reconciles Always and all three Random Unison toggles from APVTS, temporarily enabling a mismatched button for a notification-free display update, then restoring availability. Regression tests cover disabled-to-enabled and disabled-to-disabled changes. No host parameter is changed by this reconciliation.

An older v0.5 run had an intermittent arp-sustain RMS failure; the same binary subsequently passed 131 runs. No reproducible cause was established. Current tests pass, but the historical observation is not claimed fixed by unrelated work.

Package verification also found stale Windows FileVersion metadata from the original build. JUCE's resource-generation command omitted Info.txt as an input dependency. CMake now appends that dependency for the instrument and test resources; rebuilding regenerated version 0.8.0 resources. The packaging script checks executable/module FileVersion against the project version and rejects stale builds. Final CTest and pluginval checks were repeated after this resource-only relink; native functional checks preceded this metadata correction.

## State and behavior

Schema 7 adds voiceMode/glideTime/glideAlways and Phase/PhaseRandom/RandomUnison for each oscillator. All 12 parameters append with version hint 7. States 1–6 receive missing-field defaults: Poly, zero glide, Always off, zero phase/random range, Random Unison on. Invalid/missing current-schema controls reject before mutation. Existing embedded samples/tables and optional preset identity remain. Older releases cannot read schema 7; retain old preset files separately.

Mono and Legato share one global last-note-priority voice across channels. A fixed 16×128 record table tracks count, velocity, age and pedal latches. Mono retriggers envelopes and sources; Legato overlaps retain their position and per-voice LFO phase, with 5 ms velocity smoothing. Glide moves linearly in semitones for 0–2 seconds, retargeting from the current pitch. Existing grains and filter key tracking follow the moving note. Always also glides separated notes; the first note starts directly. Poly ignores glide.

Changing voice mode clears note/pedal ownership and retains a 64-sample per-channel output ramp; CC120 can silence its channel's ramp. Reset/PANIC clears voices and effects. Wavetable phase applies only on initialization/retrigger; Sample/Granular ignore it. Random Unison on gives additional unison voices full-cycle random starts, while the main voice uses the selected range. No callback allocation was added; existing shared-pointer atomics are not a hard real-time guarantee.

## Native and host checks

The initial v0.8 standalone restored v0.7 Glass Garden. Actual mouse input opened Voicing, selected Legato, changed glide to 73 ms, OSC A phase to 90 degrees and random range to 33%; the diagram followed. C4 produced nonzero output metering with the panel open. Browser loading selected Ribbon Glide with its 180 ms glide.

The final executable was launched after the corrections. INIT selected Poly; Voicing showed Always disabled/unchecked. Loading Ribbon Glide through the browser and reopening Voicing correctly checked Always. C4 again produced nonzero metering. The verification instance was closed and the user's older standalone remained open. Physical speaker output was not independently recorded.

FL Studio global installation, scan/load/audio and project-reopen testing remain deferred at the user's request to use standalone. The installed FL version did not discover the earlier user-folder bundle; the global directory needs administrator access. No v0.8 global installation or successful FL integration is claimed. Other DAWs, macOS/AU, AAX and MPE remain untested/unimplemented.

Release SHA256: standalone `F0F65E3DBE161D93C07C2D0DC45B2A4EC5463709012E925C6844E86D44816D96`; VST3 module `759A8C4D45481E6E6B613D2D4B52F609DB1703F3A8C17A328D6E1BA56727E292`.

## Scope

This original instrument is not verified equivalent to Serum 2. Sixteen factory presets are included. pluginval does not prove every DAW workflow. Tables are mipmapped, but saturation is not oversampled and Sync/Mirror/Fold Warp is not fully bandlimited. Pitched samples/grains use interpolation without mipmapped anti-aliasing; imports remain limited to 20 seconds, 8–192 kHz, mono/stereo. Worst-case live CPU has not been benchmarked. Multisample/SFZ, spectral synthesis, cross-source FM/PD, more modulation/effects buses, Clip sequencing, full library metadata/global undo, complete accessibility and a Chinese interface remain unimplemented.
