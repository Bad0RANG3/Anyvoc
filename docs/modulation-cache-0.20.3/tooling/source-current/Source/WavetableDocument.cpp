#include "WavetableDocument.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdint>
#include <vector>

namespace anyvoc
{
namespace
{
constexpr int n = WavetableDocument::frameLength;
constexpr int historyLimit = 20;
constexpr float silentAmplitude = 1.0e-6f;
using Spectrum = std::array<std::complex<float>, static_cast<size_t> (n)>;

float wrapPhase (double turns)
{
    const float result = static_cast<float> (turns - std::floor (turns + 0.5));
    return result >= 0.5f ? -0.5f : result;
}

float phaseOf (const std::complex<float>& value)
{
    return wrapPhase (std::atan2 (static_cast<double> (value.real()), -static_cast<double> (value.imag()))
                      / juce::MathConstants<double>::twoPi);
}

std::complex<float> sinePartial (float magnitude, float phase)
{
    const double angle = static_cast<double> (phase) * juce::MathConstants<double>::twoPi;
    return { static_cast<float> (magnitude * std::sin (angle)),
             static_cast<float> (-magnitude * std::cos (angle)) };
}

bool equalAudio (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        if (std::memcmp (a.getReadPointer (channel), b.getReadPointer (channel),
                         static_cast<size_t> (a.getNumSamples()) * sizeof (float)) != 0) return false;
    return true;
}

bool finiteAudio (const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            if (! std::isfinite (audio.getSample (channel, sample))
                || std::abs (audio.getSample (channel, sample)) > 128.0f) return false;
    return true;
}

void pushHistory (std::vector<juce::AudioBuffer<float>>& history, juce::AudioBuffer<float>&& audio)
{
    if (history.size() >= static_cast<size_t> (historyLimit)) history.erase (history.begin());
    history.push_back (std::move (audio));
}
}

class WavetableDocument::Impl
{
public:
    Impl() : audio (1, n), fft (11)
    {
        audio.clear();
        undoHistory.reserve (historyLimit);
        redoHistory.reserve (historyLimit);
    }

    bool validFrame (int frame) const { return frame >= 0 && frame < audio.getNumSamples() / n; }

    bool commit (juce::AudioBuffer<float>&& candidate)
    {
        if (candidate.getNumChannels() != 1 || candidate.getNumSamples() < n
            || candidate.getNumSamples() > n * maximumFrames || candidate.getNumSamples() % n != 0
            || ! finiteAudio (candidate)) return false;
        if (equalAudio (audio, candidate)) return true;
        if (editDepth > 0)
        {
            if (editStart == nullptr) editStart = std::make_unique<juce::AudioBuffer<float>> (audio);
        }
        else
        {
            pushHistory (undoHistory, juce::AudioBuffer<float> (audio));
            redoHistory.clear();
        }
        audio = std::move (candidate);
        cachedFrame = -1;
        return true;
    }

    void finishEdit()
    {
        editDepth = 0;
        if (editStart != nullptr && ! equalAudio (*editStart, audio))
        {
            pushHistory (undoHistory, std::move (*editStart));
            redoHistory.clear();
        }
        editStart.reset();
    }

    void spectrum (int frame, Spectrum& output) const
    {
        if (cachedFrame == frame) { output = cachedSpectrum; return; }
        Spectrum input {};
        const auto* samples = audio.getReadPointer (0, frame * n);
        for (size_t i = 0; i < input.size(); ++i) input[i] = { samples[i], 0.0f };
        fft.perform (input.data(), output.data(), false);
        cachedFrame = frame; cachedSpectrum = output;
    }

    juce::AudioBuffer<float> audio;
    juce::dsp::FFT fft;
    std::vector<juce::AudioBuffer<float>> undoHistory, redoHistory;
    std::unique_ptr<juce::AudioBuffer<float>> editStart;
    int editDepth = 0;
    uint32_t noiseSeed = 0x41564f43u;
    mutable int cachedFrame = -1;
    mutable Spectrum cachedSpectrum {};
};

WavetableDocument::WavetableDocument() : impl (std::make_unique<Impl>()) {}
WavetableDocument::~WavetableDocument() = default;

bool WavetableDocument::load (const juce::AudioBuffer<float>& audio, int sourceFrameLength)
{
    if (audio.getNumChannels() < 1 || sourceFrameLength < 2 || sourceFrameLength > audio.getNumSamples()) return false;
    const int frames = audio.getNumSamples() / sourceFrameLength;
    if (frames < 1 || frames > maximumFrames || ! finiteAudio (audio)) return false;
    juce::AudioBuffer<float> candidate (1, frames * n);
    auto* destination = candidate.getWritePointer (0);
    for (int frame = 0; frame < frames; ++frame)
        for (int sample = 0; sample < n; ++sample)
        {
            const double position = static_cast<double> (sample) * sourceFrameLength / n;
            const int a = static_cast<int> (position), b = (a + 1) % sourceFrameLength;
            const double fraction = position - a;
            double value = 0.0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                const auto* source = audio.getReadPointer (channel, frame * sourceFrameLength);
                value += static_cast<double> (source[a])
                       + fraction * (static_cast<double> (source[b]) - source[a]);
            }
            destination[frame * n + sample] = static_cast<float> (value / audio.getNumChannels());
        }
    if (! finiteAudio (candidate)) return false;
    impl->audio = std::move (candidate);
    impl->cachedFrame = -1;
    impl->undoHistory.clear(); impl->redoHistory.clear(); impl->editStart.reset(); impl->editDepth = 0;
    return true;
}

bool WavetableDocument::replaceCurrentData (const juce::AudioBuffer<float>& audio)
{
    if (impl->editDepth > 0 || audio.getNumChannels() != 1
        || audio.getNumSamples() != impl->audio.getNumSamples() || ! finiteAudio (audio)) return false;
    juce::AudioBuffer<float> replacement (audio);
    impl->audio = std::move (replacement);
    impl->cachedFrame = -1;
    return true;
}

const juce::AudioBuffer<float>& WavetableDocument::data() const { return impl->audio; }
int WavetableDocument::frameCount() const { return impl->audio.getNumSamples() / n; }
const float* WavetableDocument::frameData (int frame) const
{
    return impl->validFrame (frame) ? impl->audio.getReadPointer (0, frame * n) : nullptr;
}

bool WavetableDocument::draw (int frame, int from, float start, int to, float end)
{
    if (! impl->validFrame (frame) || from < 0 || from >= n || to < 0 || to >= n
        || ! std::isfinite (start) || ! std::isfinite (end)) return false;
    start = juce::jlimit (-1.0f, 1.0f, start); end = juce::jlimit (-1.0f, 1.0f, end);
    if (from > to) { std::swap (from, to); std::swap (start, end); }
    juce::AudioBuffer<float> candidate (impl->audio);
    auto* destination = candidate.getWritePointer (0, frame * n);
    if (from == to) destination[from] = end;
    else for (int i = from; i <= to; ++i)
        destination[i] = start + (end - start) * static_cast<float> (i - from) / static_cast<float> (to - from);
    return impl->commit (std::move (candidate));
}

std::array<float, 64> WavetableDocument::harmonics (int frame) const
{
    std::array<float, 64> amplitudes {};
    const auto all = harmonicSpectrum (frame);
    std::copy_n (all.amplitudes.begin(), amplitudes.size(), amplitudes.begin());
    return amplitudes;
}

WavetableDocument::HarmonicSpectrum WavetableDocument::harmonicSpectrum (int frame) const
{
    HarmonicSpectrum result;
    if (! impl->validFrame (frame)) return result;
    Spectrum frequency {};
    impl->spectrum (frame, frequency);
    for (size_t i = 0; i < result.amplitudes.size(); ++i)
    {
        result.amplitudes[i] = std::abs (frequency[i + 1]) * (2.0f / n);
        result.phases[i] = result.amplitudes[i] > silentAmplitude ? phaseOf (frequency[i + 1]) : 0.0f;
    }
    return result;
}

bool WavetableDocument::setHarmonic (int frame, int harmonic1to1023, float amplitude0to1)
{
    return setHarmonics (frame, { { harmonic1to1023, amplitude0to1, 0.0f, true, false } });
}

bool WavetableDocument::setHarmonics (int frame, const std::vector<HarmonicEdit>& edits)
{
    if (! impl->validFrame (frame)) return false;
    std::array<HarmonicEdit, maximumHarmonics> targets {};
    bool requestedChange = false;
    // Resolve the complete batch before touching data or undo history. Duplicate
    // entries merge by flagged field, so intermediate edits cause no extra FFT.
    for (const auto& edit : edits)
    {
        if (edit.harmonic < 1 || edit.harmonic > maximumHarmonics
            || ! std::isfinite (edit.amplitude) || ! std::isfinite (edit.phase)) return false;
        auto& target = targets[static_cast<size_t> (edit.harmonic - 1)];
        if (edit.changeAmplitude) { target.amplitude = juce::jlimit (0.0f, 1.0f, edit.amplitude); target.changeAmplitude = true; }
        if (edit.changePhase) { target.phase = wrapPhase (edit.phase); target.changePhase = true; }
        requestedChange = requestedChange || edit.changeAmplitude || edit.changePhase;
    }
    if (! requestedChange) return true;
    Spectrum frequency {}, result {};
    impl->spectrum (frame, frequency);
    bool changed = false;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const auto& target = targets[i];
        if (! target.changeAmplitude && ! target.changePhase) continue;
        const size_t harmonic = i + 1;
        const float magnitude = std::abs (frequency[harmonic]);
        const float amplitude = magnitude * (2.0f / n);
        const float phase = amplitude > silentAmplitude ? phaseOf (frequency[harmonic]) : 0.0f;
        const float nextAmplitude = target.changeAmplitude ? target.amplitude : amplitude;
        // A silent bin has no observable phase. Never create a tone (or FFT
        // roundoff history) just because its phase bar was dragged or reset.
        const bool phaseChanges = target.changePhase && nextAmplitude > silentAmplitude
            && std::abs (wrapPhase (static_cast<double> (target.phase) - phase)) > 1.0e-7f;
        const bool amplitudeChanges = target.changeAmplitude && std::abs (nextAmplitude - amplitude) > 1.0e-7f;
        if (! phaseChanges && ! amplitudeChanges) continue;
        const float nextMagnitude = nextAmplitude * (n * 0.5f);
        if (target.changePhase)
            frequency[harmonic] = sinePartial (nextMagnitude, target.phase);
        else if (amplitude > silentAmplitude)
            frequency[harmonic] *= nextMagnitude / magnitude;
        else
            frequency[harmonic] = { 0.0f, -nextMagnitude };
        frequency[static_cast<size_t> (n) - harmonic] = std::conj (frequency[harmonic]);
        changed = true;
    }
    if (! changed) return true;
    impl->fft.perform (frequency.data(), result.data(), true);
    juce::AudioBuffer<float> candidate (impl->audio);
    auto* destination = candidate.getWritePointer (0, frame * n);
    for (size_t i = 0; i < result.size(); ++i) destination[i] = result[i].real();
    // Do not clip additive peaks: clipping would modify harmonics the user did not edit.
    const bool committed = impl->commit (std::move (candidate));
    if (committed) { impl->cachedFrame = frame; impl->cachedSpectrum = frequency; }
    return committed;
}

bool WavetableDocument::generate (int frame, int shape)
{
    if (! impl->validFrame (frame) || shape < 0 || shape > 5) return false;
    juce::AudioBuffer<float> candidate (impl->audio);
    auto* destination = candidate.getWritePointer (0, frame * n);
    uint32_t seed = impl->noiseSeed;
    for (int i = 0; i < n; ++i)
    {
        const float phase = static_cast<float> (i) / n;
        switch (shape)
        {
            case 0: destination[i] = std::sin (juce::MathConstants<float>::twoPi * phase); break;
            case 1: destination[i] = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
            case 2: destination[i] = 2.0f * phase - 1.0f; break;
            case 3: destination[i] = phase < 0.5f ? 1.0f : -1.0f; break;
            case 4:
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                destination[i] = static_cast<float> (seed & 0x00ffffffu) / 8388607.5f - 1.0f;
                break;
            default: destination[i] = 0.0f; break;
        }
    }
    const bool success = impl->commit (std::move (candidate));
    if (success && shape == 4) impl->noiseSeed = seed;
    return success;
}

bool WavetableDocument::duplicateFrame (int frame)
{
    if (! impl->validFrame (frame) || frameCount() >= maximumFrames) return false;
    juce::AudioBuffer<float> candidate (1, (frameCount() + 1) * n);
    const int prefix = (frame + 1) * n, suffix = impl->audio.getNumSamples() - prefix;
    candidate.copyFrom (0, 0, impl->audio, 0, 0, prefix);
    candidate.copyFrom (0, prefix, impl->audio, 0, frame * n, n);
    if (suffix > 0) candidate.copyFrom (0, prefix + n, impl->audio, 0, prefix, suffix);
    return impl->commit (std::move (candidate));
}

bool WavetableDocument::deleteFrame (int frame)
{
    if (! impl->validFrame (frame) || frameCount() <= 1) return false;
    juce::AudioBuffer<float> candidate (1, (frameCount() - 1) * n);
    const int prefix = frame * n, suffix = impl->audio.getNumSamples() - prefix - n;
    if (prefix > 0) candidate.copyFrom (0, 0, impl->audio, 0, 0, prefix);
    if (suffix > 0) candidate.copyFrom (0, prefix, impl->audio, 0, prefix + n, suffix);
    return impl->commit (std::move (candidate));
}

bool WavetableDocument::morph (int first, int last, int mode)
{
    if (! impl->validFrame (first) || ! impl->validFrame (last) || first >= last || mode < 0 || mode > 1) return false;
    if (last == first + 1) return true;
    juce::AudioBuffer<float> candidate (impl->audio);
    const auto* a = impl->audio.getReadPointer (0, first * n);
    const auto* b = impl->audio.getReadPointer (0, last * n);
    if (mode == 1)
    {
        Spectrum left {}, right {}, frequency {}, result {};
        impl->spectrum (first, left); impl->spectrum (last, right);
        for (int frame = first + 1; frame < last; ++frame)
        {
            const float mix = static_cast<float> (frame - first) / (last - first);
            // DC and Nyquist are real-valued bins, not rotatable partials.
            frequency[0] = { left[0].real() + mix * (right[0].real() - left[0].real()), 0.0f };
            frequency[n / 2] = { left[n / 2].real() + mix * (right[n / 2].real() - left[n / 2].real()), 0.0f };
            for (size_t harmonic = 1; harmonic <= maximumHarmonics; ++harmonic)
            {
                const float magnitudeA = std::abs (left[harmonic]), magnitudeB = std::abs (right[harmonic]);
                float phaseA = phaseOf (left[harmonic]), phaseB = phaseOf (right[harmonic]);
                if (magnitudeA <= silentAmplitude * (n * 0.5f)) phaseA = phaseB;
                if (magnitudeB <= silentAmplitude * (n * 0.5f)) phaseB = phaseA;
                float distance = wrapPhase (static_cast<double> (phaseB) - phaseA);
                // Opposite bins are mathematically tied. atan2 and float phase
                // storage can place that tie on either side by a few ulps.
                if (std::abs (std::abs (distance) - 0.5f) <= 1.0e-7f) distance = -0.5f;
                frequency[harmonic] = sinePartial (magnitudeA + mix * (magnitudeB - magnitudeA),
                                                   wrapPhase (static_cast<double> (phaseA) + mix * distance));
                frequency[static_cast<size_t> (n) - harmonic] = std::conj (frequency[harmonic]);
            }
            impl->fft.perform (frequency.data(), result.data(), true);
            auto* destination = candidate.getWritePointer (0, frame * n);
            for (size_t i = 0; i < result.size(); ++i) destination[i] = result[i].real();
        }
        return impl->commit (std::move (candidate));
    }
    for (int frame = first + 1; frame < last; ++frame)
    {
        auto* destination = candidate.getWritePointer (0, frame * n);
        const double mix = static_cast<double> (frame - first) / (last - first);
        for (int i = 0; i < n; ++i)
            destination[i] = static_cast<float> (static_cast<double> (a[i]) + mix * (static_cast<double> (b[i]) - a[i]));
    }
    return impl->commit (std::move (candidate));
}

bool WavetableDocument::processFrame (int frame, int operation)
{
    if (! impl->validFrame (frame) || operation < 0 || operation > 3) return false;
    juce::AudioBuffer<float> candidate (impl->audio);
    auto* destination = candidate.getWritePointer (0, frame * n);
    const auto* source = impl->audio.getReadPointer (0, frame * n);
    if (operation == 0)
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (source[i]));
        if (peak > 0.0f) for (int i = 0; i < n; ++i) destination[i] = source[i] / peak;
    }
    else if (operation == 1)
    {
        double mean = 0.0;
        for (int i = 0; i < n; ++i) mean += source[i];
        mean /= n;
        for (int i = 0; i < n; ++i) destination[i] = static_cast<float> (source[i] - mean);
    }
    else if (operation == 2)
        for (int i = 0; i < n; ++i) destination[i] = -source[i];
    else
        for (int i = 0; i < n; ++i)
            destination[i] = static_cast<float> ((static_cast<double> (source[(i + n - 1) % n])
                                                  + 2.0 * source[i] + source[(i + 1) % n]) * 0.25);
    return impl->commit (std::move (candidate));
}

void WavetableDocument::beginEdit() { ++impl->editDepth; }
void WavetableDocument::endEdit()
{
    if (impl->editDepth > 0 && --impl->editDepth == 0) impl->finishEdit();
}
bool WavetableDocument::canUndo() const
{
    return ! impl->undoHistory.empty() || (impl->editStart != nullptr && ! equalAudio (*impl->editStart, impl->audio));
}
bool WavetableDocument::canRedo() const
{
    return ! impl->redoHistory.empty() && (impl->editStart == nullptr || equalAudio (*impl->editStart, impl->audio));
}
bool WavetableDocument::undo()
{
    impl->finishEdit();
    if (impl->undoHistory.empty()) return false;
    pushHistory (impl->redoHistory, std::move (impl->audio));
    impl->audio = std::move (impl->undoHistory.back());
    impl->cachedFrame = -1;
    impl->undoHistory.pop_back();
    return true;
}
bool WavetableDocument::redo()
{
    impl->finishEdit();
    if (impl->redoHistory.empty()) return false;
    pushHistory (impl->undoHistory, std::move (impl->audio));
    impl->audio = std::move (impl->redoHistory.back());
    impl->cachedFrame = -1;
    impl->redoHistory.pop_back();
    return true;
}
}
