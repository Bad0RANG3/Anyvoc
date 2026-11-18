#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

namespace anyvoc
{
/** Offline wavetable editor model. All methods belong on a UI/worker thread. */
class WavetableDocument
{
public:
    static constexpr int frameLength = 2048;
    static constexpr int maximumFrames = 32;
    static constexpr int maximumHarmonics = frameLength / 2 - 1;
    static constexpr int maxHarmonic = maximumHarmonics;
    struct HarmonicSpectrum
    {
        // Index zero is harmonic 1. Phase is sine-relative turns [-0.5, 0.5).
        std::array<float, maximumHarmonics> amplitudes {}, phases {};
    };
    struct HarmonicEdit
    {
        int harmonic = 1;
        float amplitude = 0, phase = 0;
        bool changeAmplitude = false, changePhase = false;
    };

    WavetableDocument();
    ~WavetableDocument();
    WavetableDocument (const WavetableDocument&) = delete;
    WavetableDocument& operator= (const WavetableDocument&) = delete;

    bool load (const juce::AudioBuffer<float>& audio, int sourceFrameLength = frameLength);
    // Synchronize an applied DSP snapshot without changing undo/redo history.
    // The canonical dimensions must match, and no grouped edit may be active.
    bool replaceCurrentData (const juce::AudioBuffer<float>& audio);
    const juce::AudioBuffer<float>& data() const;
    int frameCount() const;
    const float* frameData (int frame) const;
    bool draw (int frame, int from, float start, int to, float end);
    std::array<float, 64> harmonics (int frame) const;
    HarmonicSpectrum harmonicSpectrum (int frame) const;
    bool setHarmonic (int frame, int harmonic1to1023, float amplitude0to1);
    // Finite amplitudes clamp to [0,1], finite phases wrap in turns. Invalid
    // entries reject the entire batch. Duplicate flagged fields use the last edit.
    // Phase-only edits never give a silent partial a nonzero amplitude.
    bool setHarmonics (int frame, const std::vector<HarmonicEdit>& edits);
    bool generate (int frame, int shape); // Sine, triangle, saw, square, noise, clear
    bool duplicateFrame (int frame);
    bool deleteFrame (int frame);
    // Spectral mode follows the shortest phase arc; half-turn ties within
    // 1e-7 turns of float roundoff take the negative arc. Silent anchors borrow
    // the audible endpoint's phase (amplitude <=1e-6 is phase-silent).
    bool morph (int first, int last, int mode = 0); // 0 waveform, 1 spectral
    bool processFrame (int frame, int operation); // Normalize, remove DC, invert, periodic smooth

    void beginEdit();
    void endEdit();
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
