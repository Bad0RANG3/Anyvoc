#pragma once

#include <JuceHeader.h>
#include "ModulationRoute.h"
#include "EnvelopeModel.h"
#include "LfoModel.h"
#include <array>
#include <memory>
#include <vector>

namespace anyvoc
{
struct OscSettings
{
    float position = 0.0f, level = 0.6f, semitone = 0.0f, fine = 0.0f;
    float detune = 12.0f, pan = 0.0f;
    // Wavetable start phase. Randomness adds a forward [0, phaseRandom] turn.
    float phase = 0.0f, phaseRandom = 0.0f;
    bool randomUnison = true; // Other unison voices use a full random turn when enabled.
    int unison = 1;
    int mode = 0; // 0: wavetable, 1: sample, 2: granular
    int rootNote = 60;
    bool sampleLoop = false, sampleReverse = false;
    float grainSize = 0.08f, grainDensity = 16.0f, grainSpray = 0.1f;
    // Wavetable-only slots: Off, Bend+, Bend-, Pulse Width, Sync, Mirror, Fold.
    std::array<int, 2> warpMode {};
    std::array<float, 2> warpAmount {};
    // Cross replaces the classic mode in its slot: Classic, FM, PM, AM, RM.
    // Sources A/B/C/Sub/Noise are per-voice mono taps from the preceding sample,
    // before level, pan and filters. Amount shares the existing Warp control.
    std::array<int, 2> crossMode {}, crossSource {};
};

inline constexpr std::array<float, 33> defaultLfoPoints {
    0.0f, 0.19509032f, 0.38268343f, 0.55557023f, 0.70710678f, 0.83146961f,
    0.92387953f, 0.98078528f, 1.0f, 0.98078528f, 0.92387953f, 0.83146961f,
    0.70710678f, 0.55557023f, 0.38268343f, 0.19509032f, 0.0f, -0.19509032f,
    -0.38268343f, -0.55557023f, -0.70710678f, -0.83146961f, -0.92387953f,
    -0.98078528f, -1.0f, -0.98078528f, -0.92387953f, -0.83146961f,
    -0.70710678f, -0.55557023f, -0.38268343f, -0.19509032f, 0.0f
};

struct SynthSettings
{
    std::array<OscSettings, 3> osc;
    int voiceMode = 0; // Poly, mono retrigger, mono legato; last-note priority in mono modes.
    float glideTime = 0.0f; // Seconds, linear in semitones; ignored in Poly.
    bool glideAlways = false; // Otherwise glide only between overlapping held/sustained notes.
    float sub = 0.15f, noise = 0.0f, cutoff = 12000.0f, resonance = 0.15f;
    float filterEnv = 0.0f, drive = 0.0f, master = 0.65f;
    int filterType = 0;
    bool filterEnabled = true, filter2Enabled = false;
    float filter2Cutoff = 12000.0f, filter2Resonance = 0.15f, filter2Env = 0.0f;
    int filter2Type = 0, filterRouting = 0; // A > B, B > A, parallel
    int filterSlope = 0, filter2Slope = 0; // 12 dB or cascaded 24 dB
    float filterBalance = 0.5f, filterKeyTrack = 0.0f, filter2KeyTrack = 0.0f; // Tracking 0..1, MIDI 60 reference
    // Oscillator A/B/C, sub, noise: Both, A only, B only, bypass.
    std::array<int, 5> filterRoutes {};
    EnvelopeSettings amp, filter;
    std::array<EnvelopeSettings, 2> extraEnvelopes {}; // Env3/Env4; Amp determines voice lifetime.
    std::array<float, lfoCount> lfoRate { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    std::array<int, lfoCount> lfoShape {};
    std::array<bool, lfoCount> lfoSync {};
    std::array<int, lfoCount> lfoDivision { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 };
    std::array<int, lfoCount> lfoMode {}; // Free/global, per-note trigger, per-note one-shot
    std::array<int, lfoCount> lfoInterpolation {}; // Linear or step
    std::array<float, lfoCount> lfoPhaseOffset {};
    std::array<int, lfoCount> lfoDirection {}; // Forward, reverse, ping-pong (one full round trip).
    std::array<bool, lfoCount> lfoFollowSwing {}, lfoAudioRateEnabled {};
    std::array<float, lfoCount> lfoAudioRateHz { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
    float swingAmount = 0.0f;
    int swingDivision = 0; // Eighth-note pairs or sixteenth-note pairs.
    std::array<std::array<float, 33>, lfoCount> lfoPoints = []
    {
        std::array<std::array<float, 33>, lfoCount> points {};
        for (auto& waveform : points) waveform = defaultLfoPoints;
        return points;
    }();
    std::array<float, macroCount> macros {};
    std::array<ModRoute, modulationRouteCount> routes {};
    float chorus = 0.0f, delayMix = 0.0f, delayTime = 0.3f, delayFeedback = 0.3f;
    float reverb = 0.0f, reverbSize = 0.5f;
};

/** Immutable after construction. Frames are periodic single-cycle waveforms;
    each mip level has successively fewer harmonics to reduce oscillator aliasing. */
struct Wavetable
{
    int frameLength = 2048;
    int frameCount = 0;
    int levels = 0;
    std::vector<float> samples;
};

/** Immutable stereo/mono source audio. Construct and publish off the audio thread. */
struct SampleData
{
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> audio;
};

class SynthEngine
{
public:
    SynthEngine();
    ~SynthEngine();
    SynthEngine (const SynthEngine&) = delete;
    SynthEngine& operator= (const SynthEngine&) = delete;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    // Call on the audio thread, before render().
    void setSettings (const SynthSettings& settings);
    // Call each block on the audio thread. PPQ denotes the first sample of that block.
    void setTransport (double bpm, double ppq, bool playing, bool hasPpq);
    // UI-safe actual last read position, including offset/direction/swing and
    // preceding-sample phase modulation. Voice-specific modes show the newest voice.
    float getLfoPhase (int zeroBased) const;
    void render (juce::AudioBuffer<float>& output, juce::MidiBuffer& midi);

    // Construct/import tables off the audio thread. A null table restores the factory waves.
    void setUserWavetable (int oscillator, std::shared_ptr<const Wavetable> table);
    float getWavetableSample (int oscillator, float phase, float position) const;
    float getWarpedWavetableSample (int oscillator, float phase, float position,
                                   const std::array<int, 2>& modes,
                                   const std::array<float, 2>& amounts) const;
    juce::AudioBuffer<float> copyWavetableAudio (int oscillator) const; // Offline copy, 2048 samples per frame
    static std::shared_ptr<const Wavetable> makeWavetable (const juce::AudioBuffer<float>& audio,
                                                        int frameLength = 2048, bool normalise = true);
    void setUserSample (int oscillator, std::shared_ptr<const SampleData> sample);
    float getSamplePreview (int oscillator, float normalizedPosition) const;
    static std::shared_ptr<const SampleData> makeSample (const juce::AudioBuffer<float>& audio,
                                                       double sampleRate);

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
