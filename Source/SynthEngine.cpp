#include "SynthEngine.h"
#include "ChaosLfoModel.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <mutex>

namespace anyvoc
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr int voiceCount = 16;
constexpr int maxUnison = 7;
constexpr int maxGrains = 48;
constexpr size_t modulationDestinations = modulationDestinationCount;
enum VoiceControlGroup : unsigned { pitchControl = 1, panControl = 2, filterAControl = 4,
                                   filterBControl = 8, warpMipControl = 16, allVoiceControls = 31 };
constexpr std::array<double, 12> lfoBeats { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                          1.0 / 3.0, 1.0 / 6.0, 0.75, 0.375 };
constexpr std::array<uint32_t, lfoCount> lfoRandomSeeds {
    0x9e3779b9u, 0x85ebca6bu, 0xc2b2ae35u, 0x27d4eb2fu, 0x165667b1u,
    0xd3a2646cu, 0xfd7046c5u, 0xb55a4f09u, 0x94d049bbu, 0x369dea0fu
};

float finiteClamp (float value, float lo, float hi, float fallback = 0.0f)
{
    return std::isfinite (value) ? juce::jlimit (lo, hi, value) : juce::jlimit (lo, hi, fallback);
}

float lfoRandomValue (uint32_t cycle, uint32_t seed)
{
    uint32_t x = cycle ^ seed;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return static_cast<float> (x & 0x00ffffffu) / 8388607.5f - 1.0f;
}

float readTable (const Wavetable& table, float phase, float position, int mip)
{
    const float frame = juce::jlimit (0.0f, 1.0f, position) * static_cast<float> (table.frameCount - 1);
    const int frameA = static_cast<int> (frame);
    const int frameB = juce::jmin (table.frameCount - 1, frameA + 1);
    const float frameBlend = frame - static_cast<float> (frameA);
    const float sample = phase * static_cast<float> (table.frameLength);
    const int index = juce::jlimit (0, table.frameLength - 1, static_cast<int> (sample));
    const float fraction = sample - static_cast<float> (index);
    const auto stride = static_cast<size_t> (table.frameLength + 1);
    const auto* a = table.samples.data() + (static_cast<size_t> (frameA * table.levels + mip) * stride);
    const auto* b = table.samples.data() + (static_cast<size_t> (frameB * table.levels + mip) * stride);
    const float va = a[index] + fraction * (a[index + 1] - a[index]);
    const float vb = b[index] + fraction * (b[index + 1] - b[index]);
    return va + frameBlend * (vb - va);
}

using WarpModes = std::array<int, 2>;
using WarpAmounts = std::array<float, 2>;

WarpModes effectiveWarpModes (const OscSettings& settings)
{
    auto modes = settings.warpMode;
    for (size_t slot = 0; slot < 2; ++slot)
        if (settings.crossMode[slot] != 0) modes[slot] = 0;
    return modes;
}

WarpModes effectiveCrossSources (const OscSettings& settings)
{
    auto sources = settings.crossSource;
    for (size_t slot = 0; slot < 2; ++slot)
        if (settings.crossMode[slot] == 0) sources[slot] = 0;
    return sources;
}

bool hasCross (const WarpModes& modes, const WarpAmounts& amounts)
{
    return (modes[0] != 0 && amounts[0] > 0.0f) || (modes[1] != 0 && amounts[1] > 0.0f);
}

bool hasFm (const WarpModes& modes) { return modes[0] == 1 || modes[1] == 1; }

float wrapPhase (float phase) { return phase - std::floor (phase); }

float warpedPhase (float phase, int mode, float amount)
{
    if (mode == 0 || mode == 6 || amount == 0.0f) return phase;
    const float bend = 1.0f + 3.0f * amount;
    switch (mode)
    {
        case 1: phase /= bend - (bend - 1.0f) * phase; break;
        case 2: phase = bend * phase / (1.0f + (bend - 1.0f) * phase); break;
        case 3:
        {
            const float width = 0.5f - 0.45f * amount;
            phase = phase < width ? 0.5f * phase / width
                                  : 0.5f + 0.5f * (phase - width) / (1.0f - width);
            break;
        }
        case 4: phase *= 1.0f + 7.0f * amount; break;
        case 5:
        {
            const float mirrored = phase < 0.5f ? 2.0f * phase : 2.0f - 2.0f * phase;
            phase += amount * (mirrored - phase);
            break;
        }
        default: break;
    }
    return phase - std::floor (phase);
}

float foldSample (float sample, float amount)
{
    if (amount == 0.0f) return sample;
    const float driven = sample * (1.0f + 4.0f * amount) + 1.0f;
    const float wrapped = driven - 4.0f * std::floor (driven * 0.25f);
    const float folded = 1.0f - std::abs (wrapped - 2.0f);
    return sample + amount * (folded - sample);
}

float readWarpedTable (const Wavetable& table, float phase, float position, float mip,
                       const WarpModes& modes, const WarpAmounts& amounts)
{
    // Audible operator order is slot 1, then slot 2: W2(W1(f))(p).
    // Phase maps therefore compose as f(g1(g2(p))). Pointwise folds commute
    // with phase remapping, but their own amplitude operations retain slot order.
    for (int slot = 1; slot >= 0; --slot)
        phase = warpedPhase (phase, modes[static_cast<size_t> (slot)], amounts[static_cast<size_t> (slot)]);
    mip = juce::jlimit (0.0f, static_cast<float> (table.levels - 1), mip);
    const int lower = static_cast<int> (mip);
    const float fraction = mip - static_cast<float> (lower);
    float sample = readTable (table, phase, position, lower);
    if (fraction > 0.0f)
        sample += fraction * (readTable (table, phase, position, juce::jmin (table.levels - 1, lower + 1)) - sample);
    for (size_t slot = 0; slot < 2; ++slot)
        if (modes[slot] == 6) sample = foldSample (sample, amounts[slot]);
    return sample;
}

float readCrossWarpedTable (const Wavetable& table, float phase, float position, float mip,
                           const WarpModes& modes, const WarpModes& crossModes,
                           const WarpModes& crossSources, const WarpAmounts& amounts,
                           const std::array<float, 5>& taps)
{
    for (int slot = 1; slot >= 0; --slot)
    {
        const auto index = static_cast<size_t> (slot);
        if (crossModes[index] == 2)
            phase = wrapPhase (phase + amounts[index] * taps[static_cast<size_t> (crossSources[index])]);
        else if (crossModes[index] == 0)
            phase = warpedPhase (phase, modes[index], amounts[index]);
    }
    mip = juce::jlimit (0.0f, static_cast<float> (table.levels - 1), mip);
    const int lower = static_cast<int> (mip);
    const float fraction = mip - static_cast<float> (lower);
    float sample = readTable (table, phase, position, lower);
    if (fraction > 0.0f)
        sample += fraction * (readTable (table, phase, position, juce::jmin (table.levels - 1, lower + 1)) - sample);
    for (size_t slot = 0; slot < 2; ++slot)
    {
        const float source = taps[static_cast<size_t> (crossSources[slot])];
        if (crossModes[slot] == 0 && modes[slot] == 6) sample = foldSample (sample, amounts[slot]);
        else if (crossModes[slot] == 3) sample *= 1.0f - amounts[slot] + amounts[slot] * (source + 1.0f) * 0.5f;
        else if (crossModes[slot] == 4) sample *= 1.0f - amounts[slot] + amounts[slot] * source;
    }
    return sample;
}

bool hasWarp (const WarpModes& modes, const WarpAmounts& amounts)
{
    return (modes[0] != 0 && amounts[0] > 0.0f) || (modes[1] != 0 && amounts[1] > 0.0f);
}

float warpMipOffset (const WarpModes& modes, const WarpAmounts& amounts)
{
    float expansion = 1.0f;
    for (size_t slot = 0; slot < 2; ++slot)
    {
        const float amount = amounts[slot];
        if (amount == 0.0f) continue;
        switch (modes[slot])
        {
            case 1: case 2: expansion *= 1.0f + 3.0f * amount; break;
            case 3: expansion *= 0.5f / (0.5f - 0.45f * amount); break;
            case 4: expansion *= 1.0f + 7.0f * amount; break;
            case 5: expansion *= 1.0f + amount; break;
            case 6: expansion *= 1.0f + 4.0f * amount; break;
            default: break;
        }
    }
    // Conservatively reduce source harmonics for phase expansion. Discontinuous
    // Sync/Mirror and nonlinear Fold still generate additional, non-bandlimited harmonics.
    return expansion == 1.0f ? 0.0f : std::log2 (expansion);
}

struct WarpMipState
{
    float value = 0.0f, target = 0.0f;
    bool valid = false;
    void set (float nextTarget)
    {
        target = nextTarget;
        if (! valid) value = target;
        valid = true;
    }
    float next()
    {
        value += 0.125f * (target - value);
        return value;
    }
};

void readAudio (const SampleData& sample, double position, float& left, float& right)
{
    const int length = sample.audio.getNumSamples();
    if (position < 0.0 || position > static_cast<double> (length - 1))
    {
        left = right = 0.0f;
        return;
    }
    const int a = static_cast<int> (position), b = juce::jmin (a + 1, length - 1);
    const float blend = static_cast<float> (position - a);
    const auto* l = sample.audio.getReadPointer (0);
    const auto* r = sample.audio.getReadPointer (sample.audio.getNumChannels() > 1 ? 1 : 0);
    left = l[a] + blend * (l[b] - l[a]);
    right = r[a] + blend * (r[b] - r[a]);
}

double loopFadeLength (const SampleData& sample, double lower, double upper)
{
    return juce::jmin (sample.sampleRate * 0.005, (upper - lower) * 0.25);
}

void readPlayback (const SampleData& sample, double position, double lower, double upper,
                   bool loop, bool reverse, float& left, float& right)
{
    readAudio (sample, position, left, right);
    if (position < lower || position > upper) { left = right = 0.0f; return; }
    const double width = juce::jmax (1.0, loopFadeLength (sample, lower, upper));
    const double distance = reverse ? position - lower : upper - position;
    if (loop && upper - lower > 4.0 && distance < width)
    {
        float nextL = 0.0f, nextR = 0.0f;
        const double nextPosition = reverse ? upper - (width - distance) : lower + (width - distance);
        readAudio (sample, nextPosition, nextL, nextR);
        const float blend = static_cast<float> (1.0 - distance / width);
        left += blend * (nextL - left);
        right += blend * (nextR - right);
    }
    else if (! loop)
    {
        const float fade = static_cast<float> (juce::jlimit (0.0, 1.0,
            juce::jmin (position - lower, upper - position) / width));
        left *= fade; right *= fade;
    }
}

double advancePlayback (double position, double increment, double lower, double upper,
                        bool loop, double fadeWidth)
{
    position += increment;
    if (loop && upper - lower > 4.0)
    {
        const double loopLength = upper - lower - fadeWidth;
        if (position > upper)
            position = lower + fadeWidth + std::fmod (position - upper, loopLength);
        else if (position < lower)
            position = upper - fadeWidth - std::fmod (lower - position, loopLength);
    }
    return position;
}

std::shared_ptr<const Wavetable> createFactoryTable()
{
    constexpr int n = 2048;
    juce::AudioBuffer<float> source (1, n * 4);
    auto* data = source.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        const float p = static_cast<float> (i) / static_cast<float> (n);
        data[i] = std::sin (twoPi * p);
        data[n + i] = 1.0f - 4.0f * std::abs (p - 0.5f);
        data[2 * n + i] = 2.0f * p - 1.0f;
        data[3 * n + i] = p < 0.5f ? 1.0f : -1.0f;
    }
    return SynthEngine::makeWavetable (source, n);
}

struct EnvelopeRates
{
    std::array<double, 5> seconds {}, increment {}, samples {};
    std::array<float, 3> curve {}, exponent { 1, 1, 1 };
    bool advanced = false;
    void prepare (const EnvelopeSettings& settings, double sampleRate, double bpm)
    {
        for (int i = 0; i < 5; ++i)
        {
            const auto index = static_cast<size_t> (i);
            seconds[index] = envelopeStageSeconds (settings, i, bpm);
            samples[index] = seconds[index] * sampleRate;
            increment[index] = samples[index] > 0.0 ? 1.0 / samples[index] : 1.0;
        }
        curve = { settings.attackCurve, settings.decayCurve, settings.releaseCurve };
        for (size_t i = 0; i < curve.size(); ++i)
            exponent[i] = curve[i] == 0.0f ? 1.0f : std::exp2 (4.0f * curve[i]);
        advanced = settings.tempoSync || settings.delay != 0.0f || settings.hold != 0.0f
            || curve[0] != 0.0f || curve[1] != 0.0f || curve[2] != 0.0f;
    }
};

struct Envelope
{
    enum Stage { idle, attack, decay, sustain, release, delay, hold } stage = idle;
    float value = 0.0f, releaseStep = 0.0f;
    float releaseStart = 0.0f;
    double progress = 0.0, phaseAnchor = 0.0, phaseStep = 0.0;
    uint64_t phaseSamples = 0;
    bool advanced = false;
    void resetProgress (double position = 0.0)
    {
        progress = phaseAnchor = position; phaseStep = 0.0; phaseSamples = 0;
    }
    void start (const EnvelopeRates& rates)
    {
        value = 0.0f; releaseStart = 0.0f;
        advanced = rates.advanced;
        stage = advanced && rates.seconds[0] > 0.0 ? delay : attack;
        resetProgress();
    }
    void stop (float time, double sampleRate, const EnvelopeRates& rates)
    {
        if (stage != idle)
        {
            releaseStart = value;
            releaseStep = value / juce::jmax (1.0f, time * static_cast<float> (sampleRate));
            stage = release;
            advanced = rates.advanced;
            resetProgress();
        }
    }
    void reconcileMode (const EnvelopeSettings& settings, const EnvelopeRates& rates)
    {
        if (advanced == rates.advanced) return;
        if (rates.advanced)
        {
            // Transfer the old linear trajectory without restarting its stage.
            const double phase = stage == attack ? value
                : stage == decay ? (settings.sustain < 1.0f ? (1.0f - value) / (1.0f - settings.sustain) : 1.0)
                : stage == release ? (releaseStart > 0.0f ? 1.0f - value / releaseStart : 1.0) : 0.0;
            resetProgress (juce::jlimit (0.0, 1.0, phase));
        }
        else
        {
            // Resume the original recurrence from the audible level. Removing a
            // wait skips that wait; it never starts a new note or resets oscillators.
            if (stage == delay) { value = 0.0f; stage = attack; }
            else if (stage == hold) { value = 1.0f; stage = decay; }
            else if (stage == release)
                releaseStep = value / static_cast<float> (juce::jmax (1.0, (1.0 - progress) * rates.samples[4]));
        }
        advanced = rates.advanced;
    }
    float nextAdvanced (const EnvelopeSettings& settings, const EnvelopeRates& rates)
    {
        // At most five zero-length stages may be skipped in a sample. Nonzero
        // stages output their final sample before the next stage consumes time.
        for (int skipped = 0; skipped < 6; ++skipped)
        {
            if (stage == idle) return value;
            if (stage == sustain) return value = settings.sustain;
            const int timed = stage == delay ? 0 : stage == attack ? 1 : stage == hold ? 2 : stage == decay ? 3 : 4;
            const auto index = static_cast<size_t> (timed);
            const bool instant = rates.seconds[index] == 0.0;
            if (instant) progress = 1.0;
            else
            {
                const double step = rates.increment[index];
                if (step != phaseStep) { phaseAnchor = progress; phaseSamples = 0; phaseStep = step; }
                progress = juce::jmin (1.0, phaseAnchor + static_cast<double> (++phaseSamples) * step);
                if (progress >= 1.0 - 1.0e-14) progress = 1.0;
            }
            const float t = static_cast<float> (progress);
            if (stage == delay) value = 0.0f;
            else if (stage == hold) value = 1.0f;
            else
            {
                const size_t curveIndex = stage == attack ? 0u : stage == decay ? 1u : 2u;
                const float shaped = rates.curve[curveIndex] == 0.0f ? t : std::pow (t, rates.exponent[curveIndex]);
                value = stage == attack ? shaped
                    : stage == decay ? 1.0f + (settings.sustain - 1.0f) * shaped
                    : releaseStart + (0.0f - releaseStart) * shaped;
            }
            if (progress >= 1.0)
            {
                stage = stage == delay ? attack : stage == attack ? hold : stage == hold ? decay : stage == decay ? sustain : idle;
                resetProgress();
            }
            if (! instant) return value;
        }
        return value;
    }
    float next (const EnvelopeSettings& settings, float attackStep, float decayStep, const EnvelopeRates& rates)
    {
        reconcileMode (settings, rates);
        if (advanced) return nextAdvanced (settings, rates);
        // Preserve the original arithmetic and transition order byte-for-byte
        // when all new timing/curve controls are at their defaults.
        switch (stage)
        {
            case attack:
                value += attackStep;
                if (value >= 1.0f) { value = 1.0f; stage = decay; }
                break;
            case decay:
                value -= decayStep;
                if (value <= settings.sustain) { value = settings.sustain; stage = sustain; }
                break;
            case sustain: value = settings.sustain; break;
            case release:
                value -= releaseStep;
                if (value <= 0.0f) { value = 0.0f; stage = idle; }
                break;
            case idle: break;
            case delay: case hold: break;
        }
        return value;
    }
};

struct StateVariableFilter
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, damping = 1.0f;
    void set (float hz, float resonance, double sampleRate)
    {
        const float g = std::tan (pi * hz / static_cast<float> (sampleRate));
        damping = 2.0f - 1.94f * resonance;
        a1 = 1.0f / (1.0f + g * (g + damping));
        a2 = g * a1;
        a3 = g * a2;
    }
    void copyCoefficients (const StateVariableFilter& other)
    {
        a1 = other.a1; a2 = other.a2; a3 = other.a3; damping = other.damping;
    }
    float process (float x, int type)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return type == 1 ? x - damping * v1 - v2 : (type == 2 ? v1 : v2);
    }
};

struct StereoSignal
{
    float left = 0.0f, right = 0.0f;
    void add (const StereoSignal& other) { left += other.left; right += other.right; }
};

struct WarpDcBlocker
{
    StereoSignal previousInput, previousOutput;
    float wet = 0.0f;

    StereoSignal process (StereoSignal input, StereoSignal reference, bool active, float pole, float fadeStep)
    {
        if (! active && wet == 0.0f)
        {
            *this = WarpDcBlocker {};
            return input; // An untouched Off/zero path stays byte-identical.
        }
        // Remove only the DC introduced by the warp. Filtering the full waveform
        // would change its low-frequency response at arbitrarily small amounts.
        const StereoSignal difference = active
            ? StereoSignal { input.left - reference.left, input.right - reference.right } : StereoSignal {};
        const StereoSignal filtered { difference.left - previousInput.left + pole * previousOutput.left,
                                      difference.right - previousInput.right + pole * previousOutput.right };
        previousInput = difference; previousOutput = filtered;
        wet = juce::jlimit (0.0f, 1.0f, wet + (active ? fadeStep : -fadeStep));
        if (wet == 0.0f) { *this = WarpDcBlocker {}; return input; }
        return { input.left + wet * (filtered.left - difference.left),
                 input.right + wet * (filtered.right - difference.right) };
    }
};

struct FilterTopology
{
    std::array<bool, 2> enabled { true, false };
    std::array<int, 2> type {}, slope {};
    std::array<int, 5> routes {};
    int routing = 0;

    bool operator== (const FilterTopology& other) const
    {
        return enabled == other.enabled && type == other.type && slope == other.slope
            && routes == other.routes && routing == other.routing;
    }
    bool operator!= (const FilterTopology& other) const { return ! (*this == other); }
};

FilterTopology filterTopology (const SynthSettings& settings)
{
    return { { settings.filterEnabled, settings.filter2Enabled },
             { settings.filterType, settings.filter2Type },
             { settings.filterSlope, settings.filter2Slope },
             settings.filterRoutes, settings.filterRouting };
}

struct StereoFilter
{
    std::array<StateVariableFilter, 2> left, right;

    void set (const StateVariableFilter& coefficients)
    {
        for (auto& stage : left) stage.copyCoefficients (coefficients);
        for (auto& stage : right) stage.copyCoefficients (coefficients);
    }
    StereoSignal process (StereoSignal input, bool enabled, int type, int slope)
    {
        if (! enabled) return input;
        input.left = left[0].process (input.left, type);
        input.right = right[0].process (input.right, type);
        if (slope != 0)
        {
            input.left = left[1].process (input.left, type);
            input.right = right[1].process (input.right, type);
        }
        return input;
    }
};

struct FilterNetwork
{
    // The shared (Both) and exclusive inputs need independent integrator states.
    std::array<StereoFilter, 2> both, exclusive;

    void set (const std::array<StateVariableFilter, 2>& coefficients)
    {
        for (size_t filter = 0; filter < 2; ++filter)
        {
            both[filter].set (coefficients[filter]);
            exclusive[filter].set (coefficients[filter]);
        }
    }
    void transition (const FilterTopology& previous, const FilterTopology& next)
    {
        std::array<bool, 4> changedGroups {};
        for (size_t source = 0; source < previous.routes.size(); ++source)
            if (previous.routes[source] != next.routes[source])
            {
                changedGroups[static_cast<size_t> (previous.routes[source])] = true;
                changedGroups[static_cast<size_t> (next.routes[source])] = true;
            }
        // A frozen unused path must never resurrect an earlier source's ringing.
        // Order changes also alter the input history of the shared filter pair.
        if (changedGroups[0] || previous.routing != next.routing) both = {};
        if (changedGroups[1]) exclusive[0] = StereoFilter {};
        if (changedGroups[2]) exclusive[1] = StereoFilter {};
        for (size_t filter = 0; filter < 2; ++filter)
        {
            if (! previous.enabled[filter] && next.enabled[filter])
            {
                both[filter] = StereoFilter {};
                exclusive[filter] = StereoFilter {};
            }
            else if (previous.slope[filter] == 0 && next.slope[filter] != 0)
            {
                both[filter].left[1] = both[filter].right[1] = StateVariableFilter {};
                exclusive[filter].left[1] = exclusive[filter].right[1] = StateVariableFilter {};
            }
        }
    }
    StereoSignal process (const std::array<StereoSignal, 5>& sources,
                          const FilterTopology& topology, float balance)
    {
        std::array<StereoSignal, 4> groups {};
        std::array<bool, 4> used {};
        for (size_t source = 0; source < 3; ++source)
        {
            const auto route = static_cast<size_t> (topology.routes[source]);
            groups[route].add (sources[source]); used[route] = true;
        }
        const auto subRoute = static_cast<size_t> (topology.routes[3]);
        const auto noiseRoute = static_cast<size_t> (topology.routes[4]);
        if (subRoute == noiseRoute)
        {
            // Keep the legacy sum order for the default single-filter patch.
            groups[subRoute].add ({ sources[3].left + sources[4].left, sources[3].right + sources[4].right });
            used[subRoute] = true;
        }
        else
        {
            groups[subRoute].add (sources[3]); used[subRoute] = true;
            groups[noiseRoute].add (sources[4]); used[noiseRoute] = true;
        }
        auto apply = [&] (StereoFilter& filter, StereoSignal input, size_t index)
        {
            return filter.process (input, topology.enabled[index], topology.type[index], topology.slope[index]);
        };
        StereoSignal a {}, b {}, shared {};
        if (used[1]) a = apply (exclusive[0], groups[1], 0);
        if (used[2]) b = apply (exclusive[1], groups[2], 1);
        if (topology.routing == 2)
        {
            if (used[0])
            {
                a.add (apply (both[0], groups[0], 0));
                b.add (apply (both[1], groups[0], 1));
            }
            // A disabled branch is identity, including its parallel balance weight.
            shared = { a.left * (1.0f - balance) + b.left * balance,
                       a.right * (1.0f - balance) + b.right * balance };
        }
        else
        {
            if (used[0])
                shared = topology.routing == 0
                    ? apply (both[1], apply (both[0], groups[0], 0), 1)
                    : apply (both[0], apply (both[1], groups[0], 1), 0);
            shared.add (a); shared.add (b);
        }
        shared.add (groups[3]);
        return shared;
    }
};

struct Grain
{
    bool active = false;
    int age = 0, length = 1, unisonIndex = 0;
    double position = 0.0, increment = 1.0;
    double windowCos = 1.0, windowSin = 0.0, stepCos = 1.0, stepSin = 0.0;
    float leftGain = 0.70710678f, rightGain = 0.70710678f;
};

struct SampleOscillator
{
    int mode = -1;
    bool loop = false, reverse = false;
    double lower = 0.0, upper = 1.0, grainCountdown = 0.0;
    std::array<double, maxUnison> position {}, increment {};
    std::array<int, maxUnison> age {};
    std::array<bool, maxUnison> finished {};
    std::array<Grain, maxGrains> grains;
    int activeGrains = 0;
    uint32_t grainCounter = 0;
    float lastLeft = 0.0f, lastRight = 0.0f, oldLeft = 0.0f, oldRight = 0.0f;
    float lastTap = 0.0f, oldTap = 0.0f;
    int fadeRemaining = 0;
};

struct Voice
{
    bool active = false, keyDown = false, sustained = false;
    int note = 0, channel = 0, controlCounter = 0;
    uint64_t age = 0;
    float velocity = 0.0f, noteHz = 440.0f, subPhase = 0.0f, subIncrement = 0.0f;
    double currentNote = 69.0, targetNote = 69.0, glideStep = 0.0;
    int glideRemaining = 0, velocityRemaining = 0;
    float targetVelocity = 0.0f, velocityStep = 0.0f;
    float lastLeft = 0.0f, lastRight = 0.0f, tailLeft = 0.0f, tailRight = 0.0f;
    int tailRemaining = 0;
    std::array<std::array<float, maxUnison>, 3> phase {}, increment {}, gainLeft {}, gainRight {};
    std::array<std::array<int, maxUnison>, 3> mip {};
    std::array<SampleOscillator, 3> sampleOsc;
    std::array<std::array<WarpDcBlocker, 2>, 3> warpDc;
    std::array<std::array<WarpMipState, 2>, 3> warpMip;
    std::array<std::array<std::array<float, maxUnison>, 2>, 3> fmOffset {};
    std::array<std::array<WarpDcBlocker, 2>, 3> warpTapDc;
    std::array<float, 5> crossTaps {};
    std::array<double, lfoCount> lfoPhase {};
    std::array<float, lfoCount> lfoPhaseMod {}, lfoReadPosition {};
    std::array<uint32_t, lfoCount> lfoCycle {};
    std::array<float, lfoCount> lfoRandom {};
    std::array<ChaosLfoState, lfoCount> chaosLfos;
    std::array<std::array<float, 3>, lfoCount> lfoChaosReadPoint {};
    std::array<int, lfoCount> lfoMode { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    Envelope amp, filter;
    std::array<Envelope, 2> extraEnvelopes;
    std::array<StateVariableFilter, 2> filterCoefficients;
    std::array<FilterNetwork, 2> filterBanks;
};

struct MonoNote
{
    uint32_t count = 0;
    uint64_t order = 0;
    int velocity = 0;
    bool latched = false;
    bool eligible() const { return count != 0 || latched; }
};

EnvelopeSettings sanitiseEnvelope (EnvelopeSettings e)
{
    e.attack = finiteClamp (e.attack, 0.0005f, 20.0f, 0.01f);
    e.decay = finiteClamp (e.decay, 0.001f, 20.0f, 0.2f);
    e.sustain = finiteClamp (e.sustain, 0.0f, 1.0f, 0.7f);
    e.release = finiteClamp (e.release, 0.002f, 30.0f, 0.3f);
    e.delay = finiteClamp (e.delay, 0.0f, 8.0f);
    e.hold = finiteClamp (e.hold, 0.0f, 8.0f);
    e.attackCurve = finiteClamp (e.attackCurve, -1.0f, 1.0f);
    e.decayCurve = finiteClamp (e.decayCurve, -1.0f, 1.0f);
    e.releaseCurve = finiteClamp (e.releaseCurve, -1.0f, 1.0f);
    for (auto& division : e.divisions) division = juce::jlimit (0, 13, division);
    return e;
}

SynthSettings sanitise (SynthSettings s)
{
    for (auto& o : s.osc)
    {
        o.position = finiteClamp (o.position, 0.0f, 1.0f);
        o.level = finiteClamp (o.level, 0.0f, 1.0f);
        o.semitone = finiteClamp (o.semitone, -48.0f, 48.0f);
        o.fine = finiteClamp (o.fine, -100.0f, 100.0f);
        o.detune = finiteClamp (o.detune, 0.0f, 100.0f, 12.0f);
        o.pan = finiteClamp (o.pan, -1.0f, 1.0f);
        o.phase = finiteClamp (o.phase, 0.0f, 1.0f);
        o.phaseRandom = finiteClamp (o.phaseRandom, 0.0f, 1.0f);
        o.unison = juce::jlimit (1, maxUnison, o.unison);
        o.mode = juce::jlimit (0, 2, o.mode);
        o.rootNote = juce::jlimit (0, 127, o.rootNote);
        o.grainSize = finiteClamp (o.grainSize, 0.01f, 0.5f, 0.08f);
        o.grainDensity = finiteClamp (o.grainDensity, 2.0f, 80.0f, 16.0f);
        o.grainSpray = finiteClamp (o.grainSpray, 0.0f, 1.0f, 0.1f);
        for (size_t slot = 0; slot < 2; ++slot)
        {
            o.warpMode[slot] = juce::jlimit (0, 6, o.warpMode[slot]);
            o.warpAmount[slot] = finiteClamp (o.warpAmount[slot], 0.0f, 1.0f);
            o.crossMode[slot] = juce::jlimit (0, 4, o.crossMode[slot]);
            o.crossSource[slot] = juce::jlimit (0, 4, o.crossSource[slot]);
        }
    }
    s.voiceMode = juce::jlimit (0, 2, s.voiceMode);
    s.glideTime = finiteClamp (s.glideTime, 0.0f, 2.0f);
    s.sub = finiteClamp (s.sub, 0.0f, 1.0f);
    s.noise = finiteClamp (s.noise, 0.0f, 1.0f);
    s.cutoff = finiteClamp (s.cutoff, 20.0f, 22000.0f, 12000.0f);
    s.resonance = finiteClamp (s.resonance, 0.0f, 1.0f);
    s.filterEnv = finiteClamp (s.filterEnv, -1.0f, 1.0f);
    s.filter2Cutoff = finiteClamp (s.filter2Cutoff, 20.0f, 22000.0f, 12000.0f);
    s.filter2Resonance = finiteClamp (s.filter2Resonance, 0.0f, 1.0f, 0.15f);
    s.filter2Env = finiteClamp (s.filter2Env, -1.0f, 1.0f);
    s.filterBalance = finiteClamp (s.filterBalance, 0.0f, 1.0f, 0.5f);
    s.filterKeyTrack = finiteClamp (s.filterKeyTrack, 0.0f, 1.0f);
    s.filter2KeyTrack = finiteClamp (s.filter2KeyTrack, 0.0f, 1.0f);
    s.drive = finiteClamp (s.drive, 0.0f, 1.0f);
    s.master = finiteClamp (s.master, 0.0f, 1.0f, 0.65f);
    s.filterType = juce::jlimit (0, 2, s.filterType);
    s.filter2Type = juce::jlimit (0, 2, s.filter2Type);
    s.filterRouting = juce::jlimit (0, 2, s.filterRouting);
    s.filterSlope = juce::jlimit (0, 1, s.filterSlope);
    s.filter2Slope = juce::jlimit (0, 1, s.filter2Slope);
    for (auto& route : s.filterRoutes) route = juce::jlimit (0, 3, route);
    s.amp = sanitiseEnvelope (s.amp);
    s.filter = sanitiseEnvelope (s.filter);
    for (auto& envelope : s.extraEnvelopes) envelope = sanitiseEnvelope (envelope);
    for (int i = 0; i < static_cast<int> (lfoCount); ++i)
    {
        s.lfoRate[static_cast<size_t> (i)] = finiteClamp (s.lfoRate[static_cast<size_t> (i)], 0.01f, 40.0f, 1.0f);
        const auto index = static_cast<size_t> (i);
        s.lfoShape[index] = juce::jlimit (0, 7, s.lfoShape[index]);
        s.lfoDivision[index] = juce::jlimit (0, 11, s.lfoDivision[index]);
        s.lfoMode[index] = juce::jlimit (0, 2, s.lfoMode[index]);
        s.lfoInterpolation[index] = juce::jlimit (0, 1, s.lfoInterpolation[index]);
        s.lfoPhaseOffset[index] = finiteClamp (s.lfoPhaseOffset[index], 0.0f, 1.0f);
        s.lfoDirection[index] = juce::jlimit (0, 2, s.lfoDirection[index]);
        s.lfoAudioRateHz[index] = finiteClamp (s.lfoAudioRateHz[index], 20.0f, 1000.0f, 100.0f);
        for (size_t point = 0; point < 33; ++point)
            s.lfoPoints[index][point] = finiteClamp (s.lfoPoints[index][point], -1.0f, 1.0f, defaultLfoPoints[point]);
    }
    s.swingAmount = finiteClamp (s.swingAmount, 0.0f, 1.0f);
    s.swingDivision = juce::jlimit (0, 1, s.swingDivision);
    for (auto& m : s.macros) m = finiteClamp (m, 0.0f, 1.0f);
    for (auto& r : s.routes) r = sanitiseModRoute (r);
    s.chorus = finiteClamp (s.chorus, 0.0f, 1.0f);
    s.delayMix = finiteClamp (s.delayMix, 0.0f, 1.0f);
    s.delayTime = finiteClamp (s.delayTime, 0.01f, 2.0f, 0.3f);
    s.delayFeedback = finiteClamp (s.delayFeedback, 0.0f, 0.92f, 0.3f);
    s.reverb = finiteClamp (s.reverb, 0.0f, 1.0f);
    s.reverbSize = finiteClamp (s.reverbSize, 0.0f, 1.0f, 0.5f);
    return s;
}
}

class SynthEngine::Impl
{
public:
    Impl() : factory (createFactoryTable())
    {
        activeTables.fill (factory);
        target = sanitise (target);
        smooth = target;
        prepareModulationRoutes();
        for (auto& phase : publishedLfoPhase) phase.store (0.0f, std::memory_order_relaxed);
    }

    void prepare (double newSampleRate, int newMaxBlock)
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1000.0 ? newSampleRate : 44100.0;
        maxBlock = juce::jmax (1, newMaxBlock);
        scratch.setSize (2, maxBlock, false, false, true);
        controlScratch.setSize (4, maxBlock, false, false, true);
        delayLeft.assign (static_cast<size_t> (std::ceil (sampleRate * 2.1)) + 8, 0.0f);
        delayRight.assign (delayLeft.size(), 0.0f);
        chorus.prepare ({ sampleRate, static_cast<juce::uint32> (maxBlock), 2 });
        chorus.setRate (0.32f);
        chorus.setDepth (0.32f);
        chorus.setCentreDelay (12.0f);
        chorus.setFeedback (0.08f);
        reverb.setSampleRate (sampleRate);
        smoothing = 1.0f - std::exp (-1.0f / static_cast<float> (0.015 * sampleRate));
        prepared = true;
        reset();
        firstSettingsAfterPrepare = true;
    }

    void reset()
    {
        for (auto& voice : voices) voice = Voice {};
        clearMonoNotes();
        modeTails.fill (StereoSignal {});
        modeTailRemaining = 0;
        sustained.fill (false);
        pitchbend.fill (0.0f);
        smoothBend.fill (0.0f);
        modwheel.fill (0.0f);
        smoothWheel.fill (0.0f);
        lfoPhase.fill (0.0f);
        sampleLfoPhase.fill (0.0);
        lastLfoReadPosition.fill (0.0f);
        lastLfoChaosPoint.fill ({});
        renderPpq = 0.0;
        lfoCycle.fill (0);
        for (size_t i = 0; i < lfoCount; ++i)
        {
            heldRandom[i] = lfoRandomValue (0, lfoRandomSeeds[i]);
            publishedLfoPhase[i].store (0.0f, std::memory_order_relaxed);
            chaosLfos[i].reset (target.lfoShape[i], static_cast<int> (i));
            publishedLfoChaos[i].publish ({}, target.lfoShape[i], target.lfoMode[i]);
        }
        smooth = target;
        snapFilterTopology();
        snapWarpModes();
        ageCounter = 0;
        delayWrite = 0;
        delayLowLeft = delayLowRight = 0.0f;
        std::fill (delayLeft.begin(), delayLeft.end(), 0.0f);
        std::fill (delayRight.begin(), delayRight.end(), 0.0f);
        chorus.reset();
        reverb.reset();
        rng = 0x51f15e5u;
        updateEnvelopeRates();
    }

    void updateEnvelopeRates()
    {
        const float sr = static_cast<float> (sampleRate);
        ampAttack = 1.0f / juce::jmax (1.0f, target.amp.attack * sr);
        ampDecay = (1.0f - target.amp.sustain) / juce::jmax (1.0f, target.amp.decay * sr);
        filterAttack = 1.0f / juce::jmax (1.0f, target.filter.attack * sr);
        filterDecay = (1.0f - target.filter.sustain) / juce::jmax (1.0f, target.filter.decay * sr);
        for (size_t i = 0; i < target.extraEnvelopes.size(); ++i)
        {
            extraAttack[i] = 1.0f / juce::jmax (1.0f, target.extraEnvelopes[i].attack * sr);
            extraDecay[i] = (1.0f - target.extraEnvelopes[i].sustain)
                           / juce::jmax (1.0f, target.extraEnvelopes[i].decay * sr);
        }
        updateEnvelopeModels();
    }

    void updateEnvelopeModels()
    {
        envelopeRates[0].prepare (target.amp, sampleRate, transportBpm);
        envelopeRates[1].prepare (target.filter, sampleRate, transportBpm);
        for (size_t i = 0; i < target.extraEnvelopes.size(); ++i)
            envelopeRates[i + 2].prepare (target.extraEnvelopes[i], sampleRate, transportBpm);
    }

    void snapFilterTopology()
    {
        filterTopologies.fill (filterTopology (target));
        filterBank = 0;
        filterFadeRemaining = 0;
        filterFadeLength = juce::jmax (1, static_cast<int> (sampleRate * 0.005));
    }

    void updateFilterTopology()
    {
        const auto desired = filterTopology (target);
        if (filterFadeRemaining > 0 || desired == filterTopologies[filterBank]) return;
        const auto previousBank = filterBank;
        filterBank = 1u - filterBank;
        for (auto& voice : voices)
        {
            voice.filterBanks[filterBank] = voice.filterBanks[previousBank];
            voice.filterBanks[filterBank].transition (filterTopologies[previousBank], desired);
            voice.controlCounter = 0;
        }
        filterTopologies[filterBank] = desired;
        filterFadeRemaining = filterFadeLength;
    }

    void snapWarpModes()
    {
        for (size_t oscillator = 0; oscillator < 3; ++oscillator)
        {
            warpModes[oscillator].fill (effectiveWarpModes (target.osc[oscillator]));
            crossModes[oscillator].fill (target.osc[oscillator].crossMode);
            crossSources[oscillator].fill (effectiveCrossSources (target.osc[oscillator]));
        }
        warpBank.fill (0);
        warpFadeRemaining.fill (0);
        warpFadeLength = juce::jmax (1, static_cast<int> (sampleRate * 0.005));
        warpDcPole = std::exp (-twoPi * 10.0f / static_cast<float> (sampleRate));
        warpDcFadeStep = 1.0f / static_cast<float> (warpFadeLength);
    }

    void updateWarpModes()
    {
        for (size_t oscillator = 0; oscillator < 3; ++oscillator)
        {
            const auto& settings = target.osc[oscillator];
            const auto desired = effectiveWarpModes (settings);
            const auto desiredSources = effectiveCrossSources (settings);
            const auto bank = warpBank[oscillator];
            if (warpFadeRemaining[oscillator] > 0 || (desired == warpModes[oscillator][bank]
                && settings.crossMode == crossModes[oscillator][bank]
                && desiredSources == crossSources[oscillator][bank])) continue;
            const auto previous = warpBank[oscillator];
            warpBank[oscillator] = 1u - previous;
            warpModes[oscillator][warpBank[oscillator]] = desired;
            crossModes[oscillator][warpBank[oscillator]] = settings.crossMode;
            crossSources[oscillator][warpBank[oscillator]] = desiredSources;
            for (auto& voice : voices)
            {
                voice.warpDc[oscillator][warpBank[oscillator]] = voice.warpDc[oscillator][previous];
                voice.warpTapDc[oscillator][warpBank[oscillator]] = voice.warpTapDc[oscillator][previous];
                voice.fmOffset[oscillator][warpBank[oscillator]] = hasFm (settings.crossMode) && hasFm (crossModes[oscillator][previous])
                    ? voice.fmOffset[oscillator][previous] : std::array<float, maxUnison> {};
                voice.warpMip[oscillator][warpBank[oscillator]].valid = false;
            }
            warpFadeRemaining[oscillator] = warpFadeLength;
        }
    }

    float randomBipolar()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float> (rng & 0x00ffffffu) / 8388607.5f - 1.0f;
    }

    void releaseVoice (Voice& v)
    {
        v.keyDown = v.sustained = false;
        v.amp.stop (target.amp.release, sampleRate, envelopeRates[0]);
        v.filter.stop (target.filter.release, sampleRate, envelopeRates[1]);
        for (size_t i = 0; i < v.extraEnvelopes.size(); ++i)
            v.extraEnvelopes[i].stop (target.extraEnvelopes[i].release, sampleRate, envelopeRates[i + 2]);
    }

    void clearMonoNotes()
    {
        for (auto& note : monoNotes) note = MonoNote {};
        monoSelected = -1;
        monoOrder = 0;
        hasMonoPitch = false;
        lastMonoPitch = 69.0;
    }

    void changeVoiceMode()
    {
        // Retain only a short output ramp, never old note ownership or envelopes.
        // Note-offs from the previous mode therefore cannot revive a fallback note.
        const float oldTail = static_cast<float> (modeTailRemaining) / 64.0f;
        for (auto& tail : modeTails) { tail.left *= oldTail; tail.right *= oldTail; }
        for (auto& voice : voices)
        {
            if (voice.active)
            {
                auto& tail = modeTails[static_cast<size_t> (voice.channel)];
                tail.left += voice.lastLeft; tail.right += voice.lastRight;
            }
            voice = Voice {};
        }
        modeTailRemaining = std::any_of (modeTails.begin(), modeTails.end(), [] (const auto& tail)
            { return tail.left != 0.0f || tail.right != 0.0f; }) ? 64 : 0;
        clearMonoNotes();
        sustained.fill (false);
    }

    void initialiseVoice (Voice& v, int channel, int note, int velocity)
    {
        const float tailL = v.active ? v.lastLeft : 0.0f;
        const float tailR = v.active ? v.lastRight : 0.0f;
        v = Voice {};
        v.active = v.keyDown = true;
        v.note = note; v.channel = channel; v.age = ++ageCounter;
        v.velocity = v.targetVelocity = static_cast<float> (velocity) / 127.0f;
        v.currentNote = v.targetNote = static_cast<double> (note);
        v.noteHz = 440.0f * std::exp2 ((static_cast<float> (note) - 69.0f) / 12.0f);
        v.tailLeft = tailL; v.tailRight = tailR;
        v.tailRemaining = (tailL != 0.0f || tailR != 0.0f) ? 64 : 0;
        for (size_t o = 0; o < 3; ++o)
        {
            const auto& settings = target.osc[o];
            const bool legacy = settings.mode != 0
                || (settings.phase == 0.0f && settings.phaseRandom == 0.0f && settings.randomUnison);
            for (int u = 0; u < maxUnison; ++u)
            {
                // Consume the original six random draws per oscillator even when
                // fewer unison voices play; sample/granular phase controls do nothing.
                const float random = u == 0 ? 0.0f : (randomBipolar() + 1.0f) * 0.5f;
                float phase = random;
                if (! legacy)
                {
                    // The newly randomized main voice uses an independent hash.
                    // Changing phase controls (even on a muted oscillator) must not
                    // advance the legacy stream used by noise, grains and unison.
                    const float offset = u == 0
                        ? (lfoRandomValue (static_cast<uint32_t> (v.age), 0xa511e9b3u
                              ^ (static_cast<uint32_t> (o) * 0x9e3779b9u)) + 1.0f) * 0.5f * settings.phaseRandom
                        : random * (settings.randomUnison ? 1.0f : settings.phaseRandom);
                    phase = settings.phase + offset;
                    phase -= std::floor (phase);
                }
                v.phase[o][static_cast<size_t> (u)] = phase;
            }
        }
        v.amp.start (envelopeRates[0]); v.filter.start (envelopeRates[1]);
        for (size_t i = 0; i < v.extraEnvelopes.size(); ++i) v.extraEnvelopes[i].start (envelopeRates[i + 2]);
    }

    void selectMonoNote (bool noteOnEvent = false)
    {
        int selected = -1;
        uint64_t newest = 0;
        for (size_t i = 0; i < monoNotes.size(); ++i)
            if (monoNotes[i].eligible() && (selected < 0 || monoNotes[i].order > newest))
            { selected = static_cast<int> (i); newest = monoNotes[i].order; }
        auto& voice = voices[0];
        if (selected < 0)
        {
            if (monoSelected >= 0 && voice.active) releaseVoice (voice);
            monoSelected = -1;
            return;
        }
        const auto& held = monoNotes[static_cast<size_t> (selected)];
        if (selected == monoSelected && ! noteOnEvent)
        {
            voice.keyDown = held.count != 0;
            voice.sustained = held.latched && ! voice.keyDown;
            return;
        }
        const bool overlapping = monoSelected >= 0;
        const bool legato = target.voiceMode == 2 && overlapping && voice.active;
        const double origin = hasMonoPitch ? lastMonoPitch : static_cast<double> (selected % 128);
        if (! legato) initialiseVoice (voice, selected / 128, selected % 128, held.velocity);
        else
        {
            voice.note = selected % 128; voice.channel = selected / 128;
            voice.targetVelocity = static_cast<float> (held.velocity) / 127.0f;
            voice.velocityRemaining = juce::jmax (1, static_cast<int> (sampleRate * 0.005));
            voice.velocityStep = (voice.targetVelocity - voice.velocity) / static_cast<float> (voice.velocityRemaining);
            // Invert Legato is per envelope, and only a real note-on is a
            // trigger. Last-note fallback, pedal changes and note-offs are not.
            if (noteOnEvent)
            {
                if (target.amp.retriggerLegato) voice.amp.start (envelopeRates[0]);
                if (target.filter.retriggerLegato) voice.filter.start (envelopeRates[1]);
                for (size_t i = 0; i < voice.extraEnvelopes.size(); ++i)
                    if (target.extraEnvelopes[i].retriggerLegato) voice.extraEnvelopes[i].start (envelopeRates[i + 2]);
            }
        }
        voice.currentNote = voice.targetNote = static_cast<double> (selected % 128);
        voice.glideRemaining = 0;
        if (hasMonoPitch && target.glideTime > 0.0f && (overlapping || target.glideAlways))
        {
            voice.currentNote = origin;
            voice.glideRemaining = juce::jmax (1, static_cast<int> (std::round (target.glideTime * sampleRate)));
            voice.glideStep = (voice.targetNote - origin) / static_cast<double> (voice.glideRemaining);
        }
        voice.noteHz = 440.0f * std::exp2 ((static_cast<float> (voice.currentNote) - 69.0f) / 12.0f);
        voice.keyDown = held.count != 0;
        voice.sustained = held.latched && ! voice.keyDown;
        voice.controlCounter = 0;
        monoSelected = selected;
        lastMonoPitch = voice.currentNote;
        hasMonoPitch = true;
    }

    void monoNoteOn (int channel, int note, int velocity)
    {
        auto& held = monoNotes[static_cast<size_t> (channel * 128 + note)];
        if (held.count < std::numeric_limits<uint32_t>::max()) ++held.count;
        held.latched = false;
        held.order = ++monoOrder;
        held.velocity = velocity;
        selectMonoNote (true);
    }

    void monoNoteOff (int channel, int note)
    {
        auto& held = monoNotes[static_cast<size_t> (channel * 128 + note)];
        if (held.count == 0) return;
        if (--held.count == 0) held.latched = sustained[static_cast<size_t> (channel)];
        selectMonoNote();
    }

    void monoController (size_t channel, int controller)
    {
        if (controller == 120 || controller == 123)
        {
            for (size_t note = 0; note < 128; ++note) monoNotes[channel * 128 + note] = MonoNote {};
            if (controller == 120 && voices[0].channel == static_cast<int> (channel))
                voices[0] = Voice {};
        }
        else // Pedal-up and Reset All Controllers remove only this channel's latches.
            for (size_t note = 0; note < 128; ++note) monoNotes[channel * 128 + note].latched = false;
        selectMonoNote();
    }

    void noteOn (int channel, int note, int velocity)
    {
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (! v.active) { chosen = &v; break; }
        if (chosen == nullptr)
        {
            // Prefer the quietest released voice; otherwise steal the oldest held note.
            for (auto& v : voices)
                if (! v.keyDown && (chosen == nullptr || v.amp.value < chosen->amp.value)) chosen = &v;
            if (chosen == nullptr)
                chosen = &*std::min_element (voices.begin(), voices.end(),
                    [] (const Voice& a, const Voice& b) { return a.age < b.age; });
        }
        initialiseVoice (*chosen, channel, note, velocity);
    }

    void midi (const juce::uint8* data, int bytes)
    {
        if (bytes < 1) return;
        const int command = data[0] & 0xf0;
        const auto channel = static_cast<size_t> (data[0] & 15);
        const int a = bytes > 1 ? data[1] & 127 : 0;
        const int b = bytes > 2 ? data[2] & 127 : 0;
        if (bytes >= 3 && command == 0x90 && b > 0)
        {
            if (target.voiceMode == 0) noteOn (static_cast<int> (channel), a, b);
            else monoNoteOn (static_cast<int> (channel), a, b);
        }
        else if (bytes >= 3 && (command == 0x80 || (command == 0x90 && b == 0)))
        {
            if (target.voiceMode != 0) { monoNoteOff (static_cast<int> (channel), a); return; }
            for (auto& v : voices)
                if (v.active && v.keyDown && v.channel == static_cast<int> (channel) && v.note == a)
                {
                    v.keyDown = false;
                    if (sustained[channel]) v.sustained = true;
                    else releaseVoice (v);
                }
        }
        else if (bytes >= 3 && command == 0xe0)
            pitchbend[channel] = (static_cast<float> (a + (b << 7)) - 8192.0f) * (2.0f / 8192.0f);
        else if (bytes >= 3 && command == 0xb0)
        {
            if (a == 1) modwheel[channel] = static_cast<float> (b) / 127.0f;
            else if (a == 64)
            {
                sustained[channel] = b >= 64;
                if (! sustained[channel])
                {
                    if (target.voiceMode != 0) { monoController (channel, a); return; }
                    for (auto& v : voices)
                        if (v.active && v.channel == static_cast<int> (channel) && v.sustained) releaseVoice (v);
                }
            }
            else if (a == 120 || a == 123)
            {
                if (a == 120) modeTails[channel] = StereoSignal {};
                if (target.voiceMode != 0) { monoController (channel, a); return; }
                for (auto& v : voices)
                    if (v.channel == static_cast<int> (channel))
                    {
                        if (a == 120) v = Voice {};
                        else releaseVoice (v);
                    }
            }
            else if (a == 121)
            {
                pitchbend[channel] = modwheel[channel] = 0.0f;
                sustained[channel] = false;
                if (target.voiceMode != 0) { monoController (channel, a); return; }
                for (auto& v : voices)
                    if (v.active && v.channel == static_cast<int> (channel) && v.sustained) releaseVoice (v);
            }
        }
    }

    void prepareModulationRoutes()
    {
        activeModulationRoutes = 0;
        for (size_t index = 0; index < modulationRouteCount; ++index)
        {
            const auto& route = target.routes[index];
            if (! route.enabled || route.source == 0 || route.destination == 0) continue;
            auto& preparedRoute = modulationRoutes[activeModulationRoutes++];
            if (preparedRoute.index != index)
            {
                preparedRoute.primary.valid = false;
                preparedRoute.auxiliary.valid = false;
            }
            preparedRoute.index = index;
            preparedRoute.primary.prepare (route.source, route.polarity, route.curve);
            preparedRoute.auxiliary.prepare (route.auxSource, route.auxPolarity, route.auxCurve);
        }
        prepareLfoDependencies();
    }

    void prepareLfoDependencies()
    {
        std::array<bool, lfoCount> fast {};
        phaseModulatedLfos.fill (false);
        for (size_t i = 0; i < lfoCount; ++i)
            fast[i] = target.lfoAudioRateEnabled[i] && ! target.lfoSync[i];
        const auto hasAmount = [this] (size_t index)
        { return target.routes[index].amount != 0.0f || smooth.routes[index].amount != 0.0f; };
        for (size_t r = 0; r < activeModulationRoutes; ++r)
        {
            const auto index = modulationRoutes[r].index;
            const int destination = lfoIndexForPhaseDestination (target.routes[index].destination);
            if (destination >= 0 && hasAmount (index) && target.lfoShape[static_cast<size_t> (destination)] != 4
                && ! isChaosLfoShape (target.lfoShape[static_cast<size_t> (destination)]))
                phaseModulatedLfos[static_cast<size_t> (destination)] = true;
        }
        // Phase feedback can generate audio bandwidth even from a slow clock.
        // Close the primary AND auxiliary dependency graph with bounded work.
        for (size_t i = 0; i < lfoCount; ++i) fast[i] = fast[i] || phaseModulatedLfos[i];
        const auto fastSource = [&fast] (int source)
        { const int index = lfoIndexForSource (source); return index >= 0 && fast[static_cast<size_t> (index)]; };
        for (size_t pass = 0; pass < lfoCount; ++pass)
            for (size_t r = 0; r < activeModulationRoutes; ++r)
            {
                const auto index = modulationRoutes[r].index;
                const auto& route = target.routes[index];
                const int destination = lfoIndexForPhaseDestination (route.destination);
                if (destination >= 0 && hasAmount (index) && target.lfoShape[static_cast<size_t> (destination)] != 4
                    && ! isChaosLfoShape (target.lfoShape[static_cast<size_t> (destination)])
                    && (fastSource (route.source) || fastSource (route.auxSource)))
                    fast[static_cast<size_t> (destination)] = true;
            }
        fastControlMask = 0;
        for (size_t r = 0; r < activeModulationRoutes; ++r)
        {
            const auto index = modulationRoutes[r].index;
            const auto& route = target.routes[index];
            if (! hasAmount (index) || (! fastSource (route.source) && ! fastSource (route.auxSource))) continue;
            switch (route.destination)
            {
                case 5: fastControlMask |= pitchControl; break;
                case 7: fastControlMask |= panControl; break;
                case 1: case 9: fastControlMask |= filterAControl; break;
                case 8: case 10: fastControlMask |= filterBControl; break;
                default: if (route.destination >= 12 && route.destination <= 17) fastControlMask |= warpMipControl; break;
            }
        }
    }

    void smoothParameters()
    {
        auto approach = [this] (float& a, float b) { a += smoothing * (b - a); };
        for (size_t i = 0; i < 3; ++i)
        {
            auto& a = smooth.osc[i]; const auto& b = target.osc[i];
            approach (a.position, b.position); approach (a.level, b.level);
            approach (a.semitone, b.semitone); approach (a.fine, b.fine);
            approach (a.detune, b.detune); approach (a.pan, b.pan);
            a.unison = b.unison;
            a.mode = b.mode; a.rootNote = b.rootNote;
            a.sampleLoop = b.sampleLoop; a.sampleReverse = b.sampleReverse;
            approach (a.grainSize, b.grainSize); approach (a.grainDensity, b.grainDensity);
            approach (a.grainSpray, b.grainSpray);
            for (size_t slot = 0; slot < 2; ++slot) approach (a.warpAmount[slot], b.warpAmount[slot]);
        }
        approach (smooth.sub, target.sub); approach (smooth.noise, target.noise);
        approach (smooth.cutoff, target.cutoff); approach (smooth.resonance, target.resonance);
        approach (smooth.filterEnv, target.filterEnv); approach (smooth.master, target.master);
        approach (smooth.filter2Cutoff, target.filter2Cutoff); approach (smooth.filter2Resonance, target.filter2Resonance);
        approach (smooth.filter2Env, target.filter2Env); approach (smooth.filterBalance, target.filterBalance);
        approach (smooth.filterKeyTrack, target.filterKeyTrack); approach (smooth.filter2KeyTrack, target.filter2KeyTrack);
        approach (smooth.drive, target.drive); approach (smooth.delayMix, target.delayMix);
        approach (smooth.delayTime, target.delayTime); approach (smooth.delayFeedback, target.delayFeedback);
        approach (smooth.amp.sustain, target.amp.sustain); approach (smooth.filter.sustain, target.filter.sustain);
        for (size_t i = 0; i < smooth.extraEnvelopes.size(); ++i)
            approach (smooth.extraEnvelopes[i].sustain, target.extraEnvelopes[i].sustain);
        for (size_t i = 0; i < lfoCount; ++i) approach (smooth.lfoRate[i], target.lfoRate[i]);
        for (size_t i = 0; i < lfoCount; ++i) approach (smooth.lfoAudioRateHz[i], target.lfoAudioRateHz[i]);
        for (size_t i = 0; i < macroCount; ++i) approach (smooth.macros[i], target.macros[i]);
        for (size_t i = 0; i < modulationRouteCount; ++i) approach (smooth.routes[i].amount, target.routes[i].amount);
        for (size_t i = 0; i < 16; ++i)
        {
            approach (smoothBend[i], pitchbend[i]);
            approach (smoothWheel[i], modwheel[i]);
        }
    }

    double lfoIncrement (size_t i) const
    {
        if (target.lfoSync[i] && target.lfoFollowSwing[i] && target.swingAmount != 0.0f)
        {
            const double delta = transportBpm / (60.0 * sampleRate);
            return (lfoSwingPpq (renderPpq + delta, target.swingAmount, target.swingDivision)
                  - lfoSwingPpq (renderPpq, target.swingAmount, target.swingDivision))
                / lfoBeats[static_cast<size_t> (target.lfoDivision[i])];
        }
        const double rate = target.lfoSync[i]
            ? transportBpm / (60.0 * lfoBeats[static_cast<size_t> (target.lfoDivision[i])])
            : static_cast<double> (target.lfoAudioRateEnabled[i] ? smooth.lfoAudioRateHz[i] : smooth.lfoRate[i]);
        return rate / sampleRate;
    }

    double readLfoPhase (size_t i, double clock, float modulation = 0.0f) const
    {
        if (target.lfoShape[i] == 4 || isChaosLfoShape (target.lfoShape[i]) || (target.lfoPhaseOffset[i] == 0.0f
            && modulation == 0.0f && target.lfoDirection[i] == 0)) return clock;
        return lfoReadPhase (clock, static_cast<double> (target.lfoPhaseOffset[i]) + modulation, target.lfoDirection[i]);
    }

    float evaluateLfo (size_t i, double phase, float random) const
    {
        const float p = static_cast<float> (phase);
        switch (target.lfoShape[i])
        {
            case 1: return 1.0f - 4.0f * std::abs (p - 0.5f);
            case 2: return 2.0f * p - 1.0f;
            case 3: return p < 0.5f ? 1.0f : -1.0f;
            case 4: return random;
            case 5:
            {
                const float position = juce::jlimit (0.0f, 1.0f, p) * 32.0f;
                const auto a = static_cast<size_t> (position);
                const auto b = juce::jmin (static_cast<size_t> (32), a + 1);
                const auto& points = target.lfoPoints[i];
                return target.lfoInterpolation[i] == 1 ? points[a]
                    : points[a] + (position - static_cast<float> (a)) * (points[b] - points[a]);
            }
            default: return std::sin (twoPi * p);
        }
    }

    void alignLfosToTransport()
    {
        if (! transportPlaying || ! transportHasPpq) return;
        for (size_t i = 0; i < lfoCount; ++i)
        {
            if (! target.lfoSync[i] || target.lfoMode[i] != 0 || isChaosLfoShape (target.lfoShape[i])) continue;
            const double ppq = target.lfoFollowSwing[i] && target.swingAmount != 0.0f
                ? lfoSwingPpq (transportPpq, target.swingAmount, target.swingDivision) : transportPpq;
            const double cycles = ppq / lfoBeats[static_cast<size_t> (target.lfoDivision[i])];
            const double whole = std::floor (cycles);
            double phase = cycles - whole;
            double wrappedCycle = std::fmod (whole, 4294967296.0);
            if (wrappedCycle < 0.0) wrappedCycle += 4294967296.0;
            lfoCycle[i] = static_cast<uint32_t> (wrappedCycle);
            if (phase >= 1.0 - 1.0e-12) { phase = 0.0; ++lfoCycle[i]; }
            if (phase < 1.0e-12) phase = 0.0;
            lfoPhase[i] = phase;
            heldRandom[i] = lfoRandomValue (lfoCycle[i], lfoRandomSeeds[i]);
        }
    }

    static void advanceLfo (double& phase, uint32_t& cycle, float& random, double increment,
                            bool oneShot, uint32_t seed)
    {
        phase += increment;
        if (phase >= 1.0 - 1.0e-12)
        {
            if (oneShot) phase = 1.0;
            else
            {
                phase = juce::jmax (0.0, phase - 1.0);
                ++cycle;
                random = lfoRandomValue (cycle, seed);
            }
        }
    }

    std::array<float, lfoCount> nextLfos()
    {
        std::array<float, lfoCount> values {};
        for (size_t i = 0; i < lfoCount; ++i)
        {
            sampleLfoPhase[i] = lfoPhase[i];
            sampleLfoIncrement[i] = lfoIncrement (i);
            if (isChaosLfoShape (target.lfoShape[i]))
            {
                lastLfoReadPosition[i] = 0.0f; // A chaotic path has no curve position.
                if (target.lfoMode[i] == 0)
                {
                    lastLfoChaosPoint[i] = chaosLfos[i].point();
                    values[i] = lastLfoChaosPoint[i][0];
                    chaosLfos[i].advance (sampleLfoIncrement[i]);
                }
                // Retain the underlying legacy clock; no chaos integration for
                // this unused shared state in per-voice modes.
                advanceLfo (lfoPhase[i], lfoCycle[i], heldRandom[i], sampleLfoIncrement[i], false, lfoRandomSeeds[i]);
                continue;
            }
            const double read = readLfoPhase (i, lfoPhase[i]);
            lastLfoReadPosition[i] = static_cast<float> (read);
            values[i] = evaluateLfo (i, read, heldRandom[i]);
            advanceLfo (lfoPhase[i], lfoCycle[i], heldRandom[i], sampleLfoIncrement[i], false, lfoRandomSeeds[i]);
        }
        return values;
    }

    std::array<float, lfoCount> voiceLfos (Voice& voice, const std::array<float, lfoCount>& global)
    {
        auto values = global;
        for (size_t i = 0; i < lfoCount; ++i)
        {
            const auto seed = lfoRandomSeeds[i] ^ (static_cast<uint32_t> (voice.age) * 0x27d4eb2du);
            if (voice.lfoMode[i] != target.lfoMode[i])
            {
                voice.lfoMode[i] = target.lfoMode[i];
                voice.lfoPhase[i] = 0.0;
                voice.lfoCycle[i] = 0;
                voice.lfoRandom[i] = lfoRandomValue (0, seed);
                voice.lfoPhaseMod[i] = 0.0f;
                if (isChaosLfoShape (target.lfoShape[i]))
                    voice.chaosLfos[i].reset (target.lfoShape[i], static_cast<int> (i));
            }
            if (isChaosLfoShape (target.lfoShape[i]))
            {
                voice.lfoReadPosition[i] = 0.0f;
                if (target.lfoMode[i] == 0)
                    voice.lfoChaosReadPoint[i] = lastLfoChaosPoint[i];
                else
                {
                    auto& chaos = voice.chaosLfos[i];
                    if (chaos.getShape() != target.lfoShape[i]) chaos.reset (target.lfoShape[i], static_cast<int> (i));
                    voice.lfoChaosReadPoint[i] = chaos.point();
                    values[i] = voice.lfoChaosReadPoint[i][0];
                    chaos.advance (sampleLfoIncrement[i], target.lfoMode[i] == 2);
                    // The remembered periodic/random mode continues behind
                    // Chaos, so returning to it preserves its trigger clock,
                    // random sequence and one-shot endpoint.
                    advanceLfo (voice.lfoPhase[i], voice.lfoCycle[i], voice.lfoRandom[i], sampleLfoIncrement[i],
                                target.lfoMode[i] == 2, seed);
                }
                continue;
            }
            if (target.lfoMode[i] == 0)
            {
                if (voice.lfoPhaseMod[i] == 0.0f || target.lfoShape[i] == 4)
                {
                    voice.lfoReadPosition[i] = lastLfoReadPosition[i];
                    continue;
                }
                const double read = readLfoPhase (i, sampleLfoPhase[i], voice.lfoPhaseMod[i]);
                voice.lfoReadPosition[i] = static_cast<float> (read);
                values[i] = evaluateLfo (i, read, heldRandom[i]);
                continue;
            }
            const double read = readLfoPhase (i, voice.lfoPhase[i], voice.lfoPhaseMod[i]);
            voice.lfoReadPosition[i] = static_cast<float> (read);
            values[i] = evaluateLfo (i, read, voice.lfoRandom[i]);
            advanceLfo (voice.lfoPhase[i], voice.lfoCycle[i], voice.lfoRandom[i], sampleLfoIncrement[i],
                        target.lfoMode[i] == 2, seed);
        }
        return values;
    }

    void publishLfoPhases()
    {
        const Voice* latest = nullptr;
        for (const auto& voice : voices)
            if (voice.active && (latest == nullptr || voice.age > latest->age)) latest = &voice;
        for (size_t i = 0; i < lfoCount; ++i)
        {
            if (isChaosLfoShape (target.lfoShape[i]))
            {
                if (target.lfoMode[i] == 0)
                    publishedLfoChaos[i].publish (lastLfoChaosPoint[i], target.lfoShape[i], target.lfoMode[i]);
                else if (latest != nullptr)
                    publishedLfoChaos[i].publish (latest->lfoChaosReadPoint[i], target.lfoShape[i], target.lfoMode[i]);
            }
            else publishedLfoChaos[i].publish ({}, target.lfoShape[i], target.lfoMode[i]);
            // A disabled phase route still has one pending sample. Its actual
            // read position must remain visible even after the graph is clear.
            if (target.lfoMode[i] > 0 || phaseModulatedLfos[i]
                || (latest != nullptr && latest->lfoReadPosition[i] != lastLfoReadPosition[i]))
            {
                if (latest != nullptr)
                    publishedLfoPhase[i].store (latest->lfoReadPosition[i], std::memory_order_relaxed);
                else if (target.lfoMode[i] == 0)
                    publishedLfoPhase[i].store (lastLfoReadPosition[i], std::memory_order_relaxed);
            }
            else
                publishedLfoPhase[i].store (lastLfoReadPosition[i], std::memory_order_relaxed);
        }
    }

    void updateVoiceControl (Voice& v, const std::array<float, modulationDestinations>& mod, float filterEnvelope,
                             unsigned groups = allVoiceControls)
    {
        const bool pitch = (groups & pitchControl) != 0, pan = (groups & panControl) != 0;
        const float pitchScale = pitch ? std::exp2 ((smoothBend[static_cast<size_t> (v.channel)] + mod[5] * 12.0f) / 12.0f) : 1.0f;
        const float sr = static_cast<float> (sampleRate);
        for (size_t o = 0; o < 3; ++o)
        {
            const auto& settings = smooth.osc[o];
            if ((groups & warpMipControl) != 0 && settings.mode == 0)
            {
                const WarpAmounts amounts {
                    juce::jlimit (0.0f, 1.0f, settings.warpAmount[0] + mod[12 + o * 2]),
                    juce::jlimit (0.0f, 1.0f, settings.warpAmount[1] + mod[13 + o * 2]) };
                for (size_t bank = 0; bank < 2; ++bank)
                    v.warpMip[o][bank].set (crossMipOffset (o, bank, amounts));
            }
            if (! pitch && ! pan) continue;
            const auto& table = *activeTables[o];
            const float fundamental = pitch ? v.noteHz * pitchScale * std::exp2 ((settings.semitone + settings.fine * 0.01f) / 12.0f) : 0.0f;
            const float normalisation = pan ? 1.0f / std::sqrt (static_cast<float> (settings.unison)) : 1.0f;
            const double sourceRate = activeSamples[o] != nullptr ? activeSamples[o]->sampleRate : sampleRate;
            const double sourceNote = target.voiceMode == 0 ? static_cast<double> (v.note) : v.currentNote;
            const double sourceRatio = pitch ? sourceRate / sampleRate * std::exp2 (
                (sourceNote - static_cast<double> (settings.rootNote) + settings.semitone + settings.fine * 0.01) / 12.0) * pitchScale : 1.0;
            for (int u = 0; u < settings.unison; ++u)
            {
                const auto index = static_cast<size_t> (u);
                const float spread = settings.unison == 1 ? 0.0f
                    : 2.0f * static_cast<float> (u) / static_cast<float> (settings.unison - 1) - 1.0f;
                if (pitch)
                {
                    const float frequency = juce::jlimit (0.1f, sr * 0.45f,
                        fundamental * std::exp2 (spread * settings.detune / 1200.0f));
                    v.increment[o][index] = frequency / sr;
                    v.sampleOsc[o].increment[index] = juce::jlimit (0.000001, 65536.0,
                        sourceRatio * std::exp2 (static_cast<double> (spread * settings.detune) / 1200.0))
                        * (settings.sampleReverse ? -1.0 : 1.0);
                    const float lod = std::log2 (static_cast<float> (table.frameLength / 2) * frequency / (sr * 0.45f));
                    v.mip[o][index] = juce::jlimit (0, table.levels - 1, static_cast<int> (std::ceil (lod)));
                }
                if (pan)
                {
                    const float position = juce::jlimit (-1.0f, 1.0f, settings.pan + spread * 0.75f + mod[7]);
                    v.gainLeft[o][index] = std::cos ((position + 1.0f) * pi * 0.25f) * normalisation;
                    v.gainRight[o][index] = std::sin ((position + 1.0f) * pi * 0.25f) * normalisation;
                }
            }
        }
        if (pitch) v.subIncrement = juce::jmin (sr * 0.45f, v.noteHz * 0.5f * pitchScale) / sr;
        if ((groups & (filterAControl | filterBControl)) == 0) return;
        const float keyOctaves = target.voiceMode == 0 ? static_cast<float> (v.note - 60) / 12.0f
            : (static_cast<float> (v.currentNote) - 60.0f) / 12.0f;
        if ((groups & filterAControl) != 0)
        {
            const float cutoffA = juce::jlimit (20.0f, juce::jmin (22000.0f, sr * 0.45f),
                smooth.cutoff * std::exp2 (smooth.filterEnv * filterEnvelope * 6.0f + mod[1] * 5.0f
                                          + keyOctaves * smooth.filterKeyTrack));
            v.filterCoefficients[0].set (cutoffA, juce::jlimit (0.0f, 1.0f, smooth.resonance + mod[9]), sampleRate);
        }
        if ((groups & filterBControl) != 0)
        {
            const float cutoffB = juce::jlimit (20.0f, juce::jmin (22000.0f, sr * 0.45f),
                smooth.filter2Cutoff * std::exp2 (smooth.filter2Env * filterEnvelope * 6.0f + mod[8] * 5.0f
                                                + keyOctaves * smooth.filter2KeyTrack));
            v.filterCoefficients[1].set (cutoffB, juce::jlimit (0.0f, 1.0f, smooth.filter2Resonance + mod[10]), sampleRate);
        }
        for (auto& bank : v.filterBanks) bank.set (v.filterCoefficients);
    }

    void initialiseSampleOscillator (Voice& voice, size_t oscillator, float position)
    {
        auto& state = voice.sampleOsc[oscillator];
        voice.warpDc[oscillator] = {};
        voice.warpTapDc[oscillator] = {};
        voice.fmOffset[oscillator] = {};
        voice.warpMip[oscillator] = {};
        const float oldLeft = state.lastLeft, oldRight = state.lastRight, oldTap = state.lastTap;
        state = SampleOscillator {};
        const auto& settings = smooth.osc[oscillator];
        state.mode = settings.mode; state.loop = settings.sampleLoop; state.reverse = settings.sampleReverse;
        state.oldLeft = oldLeft; state.oldRight = oldRight; state.fadeRemaining = 64;
        state.oldTap = oldTap;
        if (const auto* source = activeSamples[oscillator].get())
        {
            const double last = static_cast<double> (source->audio.getNumSamples() - 1);
            const double start = (settings.sampleReverse ? 1.0 - position : position) * last;
            state.lower = settings.sampleReverse ? 0.0 : start;
            state.upper = settings.sampleReverse ? start : last;
            state.position.fill (start);
        }
        voice.controlCounter = 0;
    }

    void sampleOscillator (Voice& voice, size_t oscillator, bool audible, float& left, float& right, float* tap)
    {
        const auto* source = activeSamples[oscillator].get();
        if (source == nullptr) return;
        auto& state = voice.sampleOsc[oscillator];
        const auto& settings = smooth.osc[oscillator];
        if (state.upper - state.lower < 1.0) return;
        const double fadeWidth = loopFadeLength (*source, state.lower, state.upper);
        for (int u = 0; u < settings.unison; ++u)
        {
            const auto index = static_cast<size_t> (u);
            if (state.finished[index]) continue;
            float l = 0.0f, r = 0.0f;
            if (audible)
                readPlayback (*source, state.position[index], state.lower, state.upper,
                              settings.sampleLoop, settings.sampleReverse, l, r);
            const float attack = juce::jmin (1.0f, static_cast<float> (state.age[index]) / 64.0f);
            if (state.age[index] < 64) ++state.age[index];
            left += l * voice.gainLeft[oscillator][index] * attack;
            right += r * voice.gainRight[oscillator][index] * attack;
            if (tap != nullptr) *tap += (l + r) * 0.5f * attack / static_cast<float> (settings.unison);
            state.position[index] = advancePlayback (state.position[index], state.increment[index],
                state.lower, state.upper, settings.sampleLoop, fadeWidth);
            if (state.position[index] < state.lower || state.position[index] > state.upper)
                state.finished[index] = true;
        }
    }

    void spawnGrain (Voice& voice, size_t oscillator, float position)
    {
        auto& state = voice.sampleOsc[oscillator];
        if (state.activeGrains >= maxGrains) return; // Fixed pool; never allocate or steal a live window.
        const auto& settings = smooth.osc[oscillator];
        const auto& source = *activeSamples[oscillator];
        const auto unisonIndex = static_cast<size_t> (state.grainCounter++ % static_cast<uint32_t> (settings.unison));
        auto& grain = state.grains[static_cast<size_t> (state.activeGrains++)];
        grain = Grain {};
        grain.active = true;
        grain.length = juce::jmax (2, static_cast<int> (settings.grainSize * static_cast<float> (sampleRate)));
        grain.increment = state.increment[unisonIndex];
        grain.unisonIndex = static_cast<int> (unisonIndex);
        const double last = static_cast<double> (source.audio.getNumSamples() - 1);
        const double centre = juce::jlimit (0.0, last,
            (static_cast<double> (position) + randomBipolar() * settings.grainSpray * 0.5) * last);
        grain.position = centre - grain.increment * static_cast<double> (grain.length - 1) * 0.5;
        if (settings.sampleLoop && last > 4.0)
        {
            grain.position = std::fmod (grain.position, last);
            if (grain.position < 0.0) grain.position += last;
        }
        const double angle = juce::MathConstants<double>::twoPi / static_cast<double> (grain.length - 1);
        grain.stepCos = std::cos (angle); grain.stepSin = std::sin (angle);
        // Unison distributes grains across detune/stereo positions; it does not multiply density.
        const float unisonCompensation = std::sqrt (static_cast<float> (settings.unison));
        grain.leftGain = voice.gainLeft[oscillator][unisonIndex] * unisonCompensation;
        grain.rightGain = voice.gainRight[oscillator][unisonIndex] * unisonCompensation;
    }

    void granularOscillator (Voice& voice, size_t oscillator, float position, float& left, float& right, float* tap)
    {
        const auto* source = activeSamples[oscillator].get();
        if (source == nullptr) return;
        auto& state = voice.sampleOsc[oscillator];
        const auto& settings = smooth.osc[oscillator];
        if (state.grainCountdown <= 0.0)
        {
            spawnGrain (voice, oscillator, position);
            state.grainCountdown += sampleRate / settings.grainDensity;
        }
        --state.grainCountdown;
        const double last = static_cast<double> (source->audio.getNumSamples() - 1);
        const double fadeWidth = loopFadeLength (*source, 0.0, last);
        for (int i = 0; i < state.activeGrains;)
        {
            auto& grain = state.grains[static_cast<size_t> (i)];
            float l = 0.0f, r = 0.0f;
            readPlayback (*source, grain.position, 0.0, last, settings.sampleLoop, settings.sampleReverse, l, r);
            const float window = static_cast<float> (0.5 - 0.5 * juce::jlimit (-1.0, 1.0, grain.windowCos));
            left += l * window * grain.leftGain;
            right += r * window * grain.rightGain;
            if (tap != nullptr) *tap += (l + r) * 0.5f * window;
            const double nextCos = grain.windowCos * grain.stepCos - grain.windowSin * grain.stepSin;
            grain.windowSin = grain.windowSin * grain.stepCos + grain.windowCos * grain.stepSin;
            grain.windowCos = nextCos;
            // Legato retunes existing windows instead of replacing them. The Poly
            // path deliberately retains its original pitch-at-spawn behaviour.
            const double increment = target.voiceMode == 0 ? grain.increment
                : state.increment[static_cast<size_t> (juce::jmin (settings.unison - 1, grain.unisonIndex))];
            grain.position = advancePlayback (grain.position, increment, 0.0, last, settings.sampleLoop, fadeWidth);
            if (++grain.age >= grain.length)
                grain = state.grains[static_cast<size_t> (--state.activeGrains)];
            else
                ++i;
        }
        const float normalisation = 1.0f / juce::jmax (1.0f, settings.grainDensity * settings.grainSize * 0.5f);
        left *= normalisation; right *= normalisation;
        if (tap != nullptr) *tap *= normalisation;
    }

    float crossMipOffset (size_t oscillator, size_t bank, const WarpAmounts& amounts) const
    {
        float expansion = 1.0f;
        for (size_t slot = 0; slot < 2; ++slot)
            if (crossModes[oscillator][bank][slot] == 1 || crossModes[oscillator][bank][slot] == 2)
                expansion += 8.0f * amounts[slot];
        // Conservative source harmonic reduction, not a claim of alias-free FM/PM.
        return warpMipOffset (warpModes[oscillator][bank], amounts)
            + (expansion == 1.0f ? 0.0f : std::log2 (expansion));
    }

    void wavetableOscillator (Voice& voice, size_t oscillator, float position, bool audible,
                              const std::array<float, modulationDestinations>& mod, float& left, float& right, float* tap)
    {
        const auto& settings = smooth.osc[oscillator];
        const auto& table = *activeTables[oscillator];
        const auto bank = warpBank[oscillator], previousBank = 1u - bank;
        const auto& currentModes = warpModes[oscillator][bank];
        const auto& previousModes = warpModes[oscillator][previousBank];
        const WarpAmounts amounts {
            juce::jlimit (0.0f, 1.0f, settings.warpAmount[0] + mod[12 + oscillator * 2]),
            juce::jlimit (0.0f, 1.0f, settings.warpAmount[1] + mod[13 + oscillator * 2]) };
        const bool currentCross = hasCross (crossModes[oscillator][bank], amounts);
        const bool previousCross = hasCross (crossModes[oscillator][previousBank], amounts);
        const bool currentActive = hasWarp (currentModes, amounts) || currentCross;
        const bool previousActive = hasWarp (previousModes, amounts) || previousCross;
        const bool fading = warpFadeRemaining[oscillator] > 0
            && (currentActive || previousActive || voice.warpDc[oscillator][bank].wet > 0.0f
                || voice.warpDc[oscillator][previousBank].wet > 0.0f);
        if (! currentActive && ! fading && voice.warpDc[oscillator][bank].wet == 0.0f)
        {
            voice.warpMip[oscillator][bank].value = voice.warpMip[oscillator][bank].target = 0.0f;
            // Keep the original no-warp loop, including its sum and RNG-independent order.
            for (int unison = 0; unison < settings.unison; ++unison)
            {
                const auto index = static_cast<size_t> (unison);
                float phase = voice.phase[oscillator][index] + voice.increment[oscillator][index];
                if (phase >= 1.0f) phase -= 1.0f;
                voice.phase[oscillator][index] = phase;
                if (! audible) continue;
                const float sample = readTable (table, phase, position, voice.mip[oscillator][index]);
                left += sample * voice.gainLeft[oscillator][index];
                right += sample * voice.gainRight[oscillator][index];
                if (tap != nullptr) *tap += sample / static_cast<float> (settings.unison);
            }
            return;
        }
        auto mipOffset = [&] (size_t index, bool active)
        {
            auto& state = voice.warpMip[oscillator][index];
            if (! active) { state.value = state.target = 0.0f; return 0.0f; }
            if (! state.valid) state.set (crossMipOffset (oscillator, index, amounts));
            return state.next();
        };
        const float currentOffset = mipOffset (bank, currentActive);
        const float previousOffset = fading ? mipOffset (previousBank, previousActive) : 0.0f;
        StereoSignal current {}, previous {}, reference {}, previousReference {};
        float currentTap = 0.0f, previousTap = 0.0f, referenceTap = 0.0f, previousReferenceTap = 0.0f;
        auto readBank = [&] (size_t bankIndex, size_t unisonIndex, float phase, int baseMip,
                            float offset, bool crossActive, float raw) -> std::pair<float, float>
        {
            if (! crossActive)
                return { readWarpedTable (table, phase, position, static_cast<float> (baseMip) + offset,
                                         warpModes[oscillator][bankIndex], amounts), raw };
            const auto& modes = crossModes[oscillator][bankIndex];
            const auto& sources = crossSources[oscillator][bankIndex];
            float fmSum = 0.0f, fmDepth = 0.0f;
            for (size_t slot = 0; slot < 2; ++slot)
                if (modes[slot] == 1)
                {
                    fmSum += amounts[slot] * voice.crossTaps[static_cast<size_t> (sources[slot])];
                    fmDepth += amounts[slot];
                }
            const float dryPhase = phase;
            if (fmDepth > 0.0f)
            {
                const float increment = voice.increment[oscillator][unisonIndex];
                auto& phaseOffset = voice.fmOffset[oscillator][bankIndex][unisonIndex];
                const float modulatedIncrement = juce::jlimit (-0.45f, 0.45f, increment * (1.0f + 8.0f * fmSum));
                phaseOffset = wrapPhase (phaseOffset + (modulatedIncrement - increment));
                phase = wrapPhase (phase + phaseOffset);
            }
            const float sample = readCrossWarpedTable (table, phase, position, static_cast<float> (baseMip) + offset,
                warpModes[oscillator][bankIndex], modes, sources, amounts, voice.crossTaps);
            const bool classicActive = hasWarp (warpModes[oscillator][bankIndex], amounts);
            // Correct only DC introduced by classic operations. Using a plain
            // dry reference here would reintroduce the carrier into silent AM/RM.
            const float crossReference = classicActive
                ? readCrossWarpedTable (table, phase, position, static_cast<float> (baseMip) + offset,
                                       {}, modes, sources, amounts, voice.crossTaps) : sample;
            // FM remembers an integrated phase offset. Over the final 1% depth,
            // fade it back to the dry phase so returning to zero never snaps phase.
            const float fmWet = juce::jmin (1.0f, fmDepth * 100.0f);
            if (fmDepth > 0.0f && fmWet < 1.0f)
            {
                const float dry = readCrossWarpedTable (table, dryPhase, position, static_cast<float> (baseMip) + offset,
                    warpModes[oscillator][bankIndex], modes, sources, amounts, voice.crossTaps);
                const float dryReference = classicActive
                    ? readCrossWarpedTable (table, dryPhase, position, static_cast<float> (baseMip) + offset,
                                           {}, modes, sources, amounts, voice.crossTaps) : dry;
                return { dry + fmWet * (sample - dry), dryReference + fmWet * (crossReference - dryReference) };
            }
            return { sample, crossReference };
        };
        for (int unison = 0; unison < settings.unison; ++unison)
        {
            const auto index = static_cast<size_t> (unison);
            float phase = voice.phase[oscillator][index] + voice.increment[oscillator][index];
            if (phase >= 1.0f) phase -= 1.0f;
            voice.phase[oscillator][index] = phase;
            if (! audible) continue;
            const int baseMip = voice.mip[oscillator][index];
            const float raw = readTable (table, phase, position, baseMip);
            const auto value = currentActive
                ? readBank (bank, index, phase, baseMip, currentOffset, currentCross, raw)
                : std::pair<float, float> { raw, raw };
            const float sample = value.first;
            reference.left += value.second * voice.gainLeft[oscillator][index];
            reference.right += value.second * voice.gainRight[oscillator][index];
            if (tap != nullptr) referenceTap += value.second / static_cast<float> (settings.unison);
            current.left += sample * voice.gainLeft[oscillator][index];
            current.right += sample * voice.gainRight[oscillator][index];
            if (tap != nullptr) currentTap += sample / static_cast<float> (settings.unison);
            if (fading)
            {
                const auto oldValue = previousActive
                    ? readBank (previousBank, index, phase, baseMip, previousOffset, previousCross, raw)
                    : std::pair<float, float> { raw, raw };
                const float old = oldValue.first;
                previous.left += old * voice.gainLeft[oscillator][index];
                previous.right += old * voice.gainRight[oscillator][index];
                previousReference.left += oldValue.second * voice.gainLeft[oscillator][index];
                previousReference.right += oldValue.second * voice.gainRight[oscillator][index];
                if (tap != nullptr) previousTap += old / static_cast<float> (settings.unison);
                if (tap != nullptr) previousReferenceTap += oldValue.second / static_cast<float> (settings.unison);
            }
        }
        const bool currentDc = hasWarp (currentModes, amounts), previousDc = hasWarp (previousModes, amounts);
        current = voice.warpDc[oscillator][bank].process (current, reference, currentDc, warpDcPole, warpDcFadeStep);
        if (tap != nullptr)
            currentTap = voice.warpTapDc[oscillator][bank].process ({ currentTap, 0.0f }, { referenceTap, 0.0f },
                currentDc, warpDcPole, warpDcFadeStep).left;
        if (fading)
        {
            previous = voice.warpDc[oscillator][previousBank].process (previous, previousReference, previousDc, warpDcPole, warpDcFadeStep);
            const float mix = static_cast<float> (warpFadeRemaining[oscillator]) / static_cast<float> (warpFadeLength);
            current.left += (previous.left - current.left) * mix;
            current.right += (previous.right - current.right) * mix;
            if (tap != nullptr)
            {
                previousTap = voice.warpTapDc[oscillator][previousBank].process ({ previousTap, 0.0f }, { previousReferenceTap, 0.0f },
                    previousDc, warpDcPole, warpDcFadeStep).left;
                currentTap += (previousTap - currentTap) * mix;
            }
        }
        left = current.left; right = current.right;
        if (tap != nullptr) *tap = currentTap;
    }

    void synthSample (float& left, float& right)
    {
        updateFilterTopology();
        updateWarpModes();
        smoothParameters();
        const auto globalLfo = nextLfos();
        left = right = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (target.voiceMode != 0)
            {
                if (v.glideRemaining > 0)
                {
                    v.currentNote += v.glideStep;
                    if (--v.glideRemaining == 0) v.currentNote = v.targetNote;
                    v.noteHz = 440.0f * std::exp2 ((static_cast<float> (v.currentNote) - 69.0f) / 12.0f);
                    // Only one mono voice exists. During glide update all source
                    // increments and keytracking each sample, keeping constant time
                    // in semitones even when a new target interrupts the movement.
                    v.controlCounter = 0;
                }
                if (v.velocityRemaining > 0)
                {
                    v.velocity += v.velocityStep;
                    if (--v.velocityRemaining == 0) v.velocity = v.targetVelocity;
                }
                lastMonoPitch = v.currentNote;
            }
            const float amplitude = v.amp.next (smooth.amp, ampAttack, ampDecay, envelopeRates[0]);
            const float filterEnvelope = v.filter.next (smooth.filter, filterAttack, filterDecay, envelopeRates[1]);
            if (v.amp.stage == Envelope::idle) { v.active = false; continue; }
            const auto lfo = voiceLfos (v, globalLfo);
            std::array<float, modulationSourceCount> sources { 0.0f, lfo[0], lfo[1], filterEnvelope, v.velocity,
                smoothWheel[static_cast<size_t> (v.channel)], smooth.macros[0], smooth.macros[1], smooth.macros[2], smooth.macros[3] };
            for (size_t i = 2; i < lfoCount; ++i) sources[static_cast<size_t> (lfoSourceId (static_cast<int> (i)))] = lfo[i];
            sources[18] = amplitude;
            for (size_t i = 0; i < v.extraEnvelopes.size(); ++i)
                sources[19 + i] = v.extraEnvelopes[i].next (smooth.extraEnvelopes[i], extraAttack[i], extraDecay[i], envelopeRates[i + 2]);
            for (size_t i = 4; i < macroCount; ++i)
                sources[static_cast<size_t> (macroSourceId (static_cast<int> (i)))] = smooth.macros[i];
            std::array<float, modulationDestinations> mod {};
            for (size_t r = 0; r < activeModulationRoutes; ++r)
            {
                auto& preparedRoute = modulationRoutes[r];
                const auto& route = target.routes[preparedRoute.index];
                const float amount = smooth.routes[preparedRoute.index].amount;
                if (amount == 0.0f) continue;
                float value = preparedRoute.primary.value (sources[static_cast<size_t> (route.source)]);
                if (route.auxSource != 0)
                    value *= preparedRoute.auxiliary.value (sources[static_cast<size_t> (route.auxSource)]);
                mod[static_cast<size_t> (route.destination)] += value * amount;
            }
            // All sources above read the preceding sample's phase offsets.
            // Publish this sample's sums only after the entire matrix has run.
            for (size_t i = 0; i < lfoCount; ++i)
                v.lfoPhaseMod[i] = target.lfoShape[i] == 4 || isChaosLfoShape (target.lfoShape[i]) ? 0.0f : mod[18 + i];
            for (size_t o = 0; o < 3; ++o)
            {
                const auto& state = v.sampleOsc[o];
                const auto& settings = smooth.osc[o];
                if (state.mode != settings.mode || state.loop != settings.sampleLoop || state.reverse != settings.sampleReverse)
                    initialiseSampleOscillator (v, o, juce::jlimit (0.0f, 1.0f,
                        (settings.mode == 1 ? target.osc[o].position : settings.position) + mod[o + 2]));
            }
            if (v.controlCounter-- <= 0)
            {
                updateVoiceControl (v, mod, filterEnvelope);
                v.controlCounter = 15;
            }
            else if (fastControlMask != 0)
                updateVoiceControl (v, mod, filterEnvelope, fastControlMask);
            // Start from audible oscillators, then close over current AND fading
            // connections. A disconnected muted route must not start grains/RNG.
            std::array<bool, 5> required {};
            for (size_t o = 0; o < 3; ++o) required[o] = smooth.osc[o].level >= 0.00001f;
            for (int pass = 0; pass < 3; ++pass)
                for (size_t o = 0; o < 3; ++o)
                {
                    if (! required[o] || smooth.osc[o].mode != 0) continue;
                    for (size_t bank = 0; bank < 2; ++bank)
                    {
                        if (bank != warpBank[o] && warpFadeRemaining[o] == 0) continue;
                        for (size_t slot = 0; slot < 2; ++slot)
                            if (crossModes[o][bank][slot] != 0
                                && smooth.osc[o].warpAmount[slot] + mod[12 + o * 2 + slot] > 0.0f)
                            {
                                const auto source = static_cast<size_t> (crossSources[o][bank][slot]);
                                required[source] = true;
                            }
                    }
                }
            std::array<float, 5> nextTaps {};
            std::array<StereoSignal, 5> oscillatorSignals {};
            for (size_t o = 0; o < 3; ++o)
            {
                const auto& settings = smooth.osc[o];
                const float position = juce::jlimit (0.0f, 1.0f, settings.position + mod[o + 2]);
                const bool audible = required[o];
                float oscLeft = 0.0f, oscRight = 0.0f;
                float* tap = required[o] ? &nextTaps[o] : nullptr;
                if (! required[o]) v.warpTapDc[o] = {};
                if (settings.mode == 1)
                    sampleOscillator (v, o, audible, oscLeft, oscRight, tap);
                else if (settings.mode == 2)
                {
                    if (audible)
                        granularOscillator (v, o, position, oscLeft, oscRight, tap);
                    else
                    {
                        v.sampleOsc[o].activeGrains = 0;
                        v.sampleOsc[o].grainCountdown = 0.0;
                    }
                }
                else wavetableOscillator (v, o, position, audible, mod, oscLeft, oscRight, tap);
                auto& oscillator = v.sampleOsc[o];
                if (oscillator.fadeRemaining > 0)
                {
                    const float previous = static_cast<float> (oscillator.fadeRemaining--) / 64.0f;
                    oscLeft = oscLeft * (1.0f - previous) + oscillator.oldLeft * previous;
                    oscRight = oscRight * (1.0f - previous) + oscillator.oldRight * previous;
                    if (tap != nullptr) *tap = *tap * (1.0f - previous) + oscillator.oldTap * previous;
                }
                oscillator.lastLeft = oscLeft; oscillator.lastRight = oscRight;
                oscillator.lastTap = nextTaps[o];
                oscillatorSignals[o] = { oscLeft * settings.level, oscRight * settings.level };
            }
            v.subPhase += v.subIncrement;
            if (v.subPhase >= 1.0f) v.subPhase -= 1.0f;
            const float subRaw = std::sin (twoPi * v.subPhase);
            const float sub = subRaw * smooth.sub * 0.70710678f;
            oscillatorSignals[3] = { sub, sub };
            const float noiseLeft = randomBipolar(), noiseRight = randomBipolar();
            oscillatorSignals[4] = { noiseLeft * smooth.noise * 0.35f,
                                     noiseRight * smooth.noise * 0.35f };
            nextTaps[3] = subRaw;
            nextTaps[4] = (noiseLeft + noiseRight) * 0.5f;
            for (size_t source = 0; source < 5; ++source)
                v.crossTaps[source] = finiteClamp (nextTaps[source], -1.0f, 1.0f);
            const float gain = amplitude * (0.25f + 0.75f * v.velocity) * juce::jlimit (0.0f, 2.0f, 1.0f + mod[6]) * 0.23f;
            const float balance = juce::jlimit (0.0f, 1.0f, smooth.filterBalance + mod[11]);
            auto filtered = v.filterBanks[filterBank].process (oscillatorSignals, filterTopologies[filterBank], balance);
            if (filterFadeRemaining > 0)
            {
                const auto previousBank = 1u - filterBank;
                const auto previous = v.filterBanks[previousBank].process (oscillatorSignals, filterTopologies[previousBank], balance);
                const float mix = static_cast<float> (filterFadeRemaining) / static_cast<float> (filterFadeLength);
                filtered.left += (previous.left - filtered.left) * mix;
                filtered.right += (previous.right - filtered.right) * mix;
            }
            float vl = filtered.left * gain, vr = filtered.right * gain;
            if (v.tailRemaining > 0)
            {
                const float fade = static_cast<float> (v.tailRemaining--) / 64.0f;
                vl += v.tailLeft * fade; vr += v.tailRight * fade;
            }
            v.lastLeft = vl; v.lastRight = vr;
            left += vl; right += vr;
        }
        if (filterFadeRemaining > 0) --filterFadeRemaining;
        for (auto& remaining : warpFadeRemaining) if (remaining > 0) --remaining;
        if (modeTailRemaining > 0)
        {
            const float fade = static_cast<float> (modeTailRemaining--) / 64.0f;
            for (const auto& tail : modeTails) { left += tail.left * fade; right += tail.right * fade; }
        }
        const float driveGain = 1.0f + smooth.drive * 9.0f;
        const float driveCompensation = 1.0f / (1.0f + smooth.drive * 2.0f);
        left = std::tanh (left * driveGain) * driveCompensation;
        right = std::tanh (right * driveGain) * driveCompensation;
        renderPpq += transportBpm / (60.0 * sampleRate);
        if (renderPpq >= 1.0) renderPpq -= std::floor (renderPpq);
    }

    void renderRange (juce::AudioBuffer<float>& output, int start, int length)
    {
        while (length > 0)
        {
            const int count = juce::jmin (maxBlock, length);
            auto* left = scratch.getWritePointer (0);
            auto* right = scratch.getWritePointer (1);
            auto* masterGain = controlScratch.getWritePointer (0);
            auto* delayTime = controlScratch.getWritePointer (1);
            auto* delayMix = controlScratch.getWritePointer (2);
            auto* delayFeedback = controlScratch.getWritePointer (3);
            for (int i = 0; i < count; ++i)
            {
                synthSample (left[i], right[i]);
                masterGain[i] = smooth.master;
                delayTime[i] = smooth.delayTime;
                delayMix[i] = smooth.delayMix;
                delayFeedback[i] = smooth.delayFeedback;
            }
            auto block = juce::dsp::AudioBlock<float> (scratch).getSubBlock (0, static_cast<size_t> (count));
            juce::dsp::ProcessContextReplacing<float> context (block);
            chorus.process (context);
            const auto delaySize = static_cast<int> (delayLeft.size());
            for (int i = 0; i < count; ++i)
            {
                // Single precision can round (-0.0009765625 + 100808) to
                // exactly 100808 at the 0.3 s / 48 kHz wrap, indexing one past
                // the ring. Keep position arithmetic in double and enforce [0, size).
                const double ringLength = static_cast<double> (delaySize);
                double read = static_cast<double> (delayWrite) - static_cast<double> (delayTime[i]) * sampleRate;
                // The ring is longer than the maximum 2 s delay, so one wrap suffices.
                if (read < 0.0) read += ringLength;
                if (read >= ringLength) read = 0.0;
                const int a = juce::jlimit (0, delaySize - 1, static_cast<int> (read));
                const int b = a + 1 < delaySize ? a + 1 : 0;
                const float mix = static_cast<float> (juce::jlimit (0.0, 1.0, read - static_cast<double> (a)));
                const float dl = delayLeft[static_cast<size_t> (a)] + mix * (delayLeft[static_cast<size_t> (b)] - delayLeft[static_cast<size_t> (a)]);
                const float dr = delayRight[static_cast<size_t> (a)] + mix * (delayRight[static_cast<size_t> (b)] - delayRight[static_cast<size_t> (a)]);
                delayLowLeft += 0.35f * (dl - delayLowLeft);
                delayLowRight += 0.35f * (dr - delayLowRight);
                delayLeft[static_cast<size_t> (delayWrite)] = std::tanh (left[i] + delayLowRight * delayFeedback[i]);
                delayRight[static_cast<size_t> (delayWrite)] = std::tanh (right[i] + delayLowLeft * delayFeedback[i]);
                left[i] += dl * delayMix[i];
                right[i] += dr * delayMix[i];
                if (++delayWrite == delaySize) delayWrite = 0;
            }
            reverb.processStereo (left, right, count);
            auto* outLeft = output.getWritePointer (0, start);
            auto* outRight = output.getNumChannels() > 1 ? output.getWritePointer (1, start) : nullptr;
            for (int i = 0; i < count; ++i)
            {
                // A soft ceiling bounds extreme modulation, stacked voices, and feedback.
                const float l = std::isfinite (left[i]) ? std::tanh (left[i] * masterGain[i]) : 0.0f;
                const float r = std::isfinite (right[i]) ? std::tanh (right[i] * masterGain[i]) : 0.0f;
                outLeft[i] = outRight != nullptr ? l : 0.5f * (l + r);
                if (outRight != nullptr) outRight[i] = r;
            }
            start += count;
            length -= count;
        }
    }

    double sampleRate = 44100.0;
    int maxBlock = 512;
    bool prepared = false;
    bool firstSettingsAfterPrepare = true;
    SynthSettings target, smooth;
    struct CachedModulationSource
    {
        ModulationSourceTransform transform;
        int source = 0, polarity = 0;
        float curve = 0, previousInput = 0, previousOutput = 0;
        bool valid = false;

        void prepare (int nextSource, int nextPolarity, float nextCurve)
        {
            if (source == nextSource && polarity == nextPolarity && curve == nextCurve) return;
            source = nextSource; polarity = nextPolarity; curve = nextCurve;
            transform = prepareModulationSource (source, polarity, curve);
            valid = false;
        }

        float value (float raw)
        {
            // No approximation: equal inputs to the same descriptor have exactly
            // equal outputs. Retain the sign of zero for signed curve evaluation.
            if (! transform.curved) return transformModulationSource (raw, transform);
            if (valid && raw == previousInput
                && (raw != 0.0f || std::signbit (raw) == std::signbit (previousInput))) return previousOutput;
            previousInput = raw;
            previousOutput = transformModulationSource (raw, transform);
            valid = true;
            return previousOutput;
        }
    };
    struct PreparedModulationRoute
    {
        size_t index = 0;
        CachedModulationSource primary, auxiliary;
    };
    std::array<PreparedModulationRoute, modulationRouteCount> modulationRoutes {};
    size_t activeModulationRoutes = 0;
    std::array<bool, lfoCount> phaseModulatedLfos {};
    unsigned fastControlMask = 0;
    std::array<FilterTopology, 2> filterTopologies;
    size_t filterBank = 0;
    int filterFadeRemaining = 0, filterFadeLength = 220;
    std::array<std::array<WarpModes, 2>, 3> warpModes {};
    std::array<std::array<WarpModes, 2>, 3> crossModes {}, crossSources {};
    std::array<size_t, 3> warpBank {};
    std::array<int, 3> warpFadeRemaining {};
    int warpFadeLength = 220;
    float warpDcPole = 0.9985763f, warpDcFadeStep = 1.0f / 220.0f;
    float smoothing = 0.001f, ampAttack = 0.01f, ampDecay = 0.001f, filterAttack = 0.01f, filterDecay = 0.001f;
    std::array<float, 2> extraAttack {}, extraDecay {};
    std::array<EnvelopeRates, 4> envelopeRates;
    std::array<Voice, voiceCount> voices;
    std::array<MonoNote, 16 * 128> monoNotes;
    int monoSelected = -1;
    uint64_t monoOrder = 0;
    double lastMonoPitch = 69.0;
    bool hasMonoPitch = false;
    std::array<StereoSignal, 16> modeTails {};
    int modeTailRemaining = 0;
    std::array<bool, 16> sustained {};
    std::array<float, 16> pitchbend {}, smoothBend {}, modwheel {}, smoothWheel {};
    std::array<double, lfoCount> lfoPhase {};
    std::array<double, lfoCount> sampleLfoPhase {}, sampleLfoIncrement {};
    std::array<float, lfoCount> lastLfoReadPosition {};
    std::array<uint32_t, lfoCount> lfoCycle {};
    std::array<float, lfoCount> heldRandom {};
    std::array<std::atomic<float>, lfoCount> publishedLfoPhase {};
    std::array<ChaosLfoState, lfoCount> chaosLfos;
    std::array<std::array<float, 3>, lfoCount> lastLfoChaosPoint {};
    struct PublishedChaosPoint
    {
        // All accesses are sequentially consistent, so equal even generations
        // bracket exactly one complete point in the global atomic order.
        // Only the UI reader retries; the audio writer has fixed bounded work.
        std::atomic<uint32_t> generation { 0 };
        std::array<std::atomic<float>, 3> coordinates {};
        std::atomic<int> shape { 0 }, mode { 0 };
        PublishedChaosPoint() noexcept
        { for (auto& coordinate : coordinates) coordinate.store (0.0f, std::memory_order_relaxed); }
        void publish (const std::array<float, 3>& point, int effectiveShape, int playMode) noexcept
        {
            generation.fetch_add (1);
            for (size_t axis = 0; axis < 3; ++axis) coordinates[axis].store (point[axis]);
            shape.store (effectiveShape);
            mode.store (playMode);
            generation.fetch_add (1);
        }
        std::array<float, 3> read (int expectedShape, int expectedMode) const noexcept
        {
            for (;;)
            {
                const auto before = generation.load();
                if ((before & 1u) != 0) continue;
                const std::array<float, 3> point { coordinates[0].load(), coordinates[1].load(), coordinates[2].load() };
                const int actualShape = shape.load(), actualMode = mode.load();
                if (generation.load() == before)
                    return (expectedShape >= 0 && expectedShape != actualShape)
                        || (expectedMode >= 0 && expectedMode != actualMode) ? std::array<float, 3> {} : point;
            }
        }
    };
    std::array<PublishedChaosPoint, lfoCount> publishedLfoChaos;
    double transportBpm = 120.0, transportPpq = 0.0;
    double renderPpq = 0.0; // Fractional beat avoids loss of tiny sample deltas at huge PPQ.
    bool transportPlaying = false, transportHasPpq = false;
    uint64_t ageCounter = 0;
    uint32_t rng = 0x51f15e5u;
    std::shared_ptr<const Wavetable> factory;
    std::array<std::shared_ptr<const Wavetable>, 3> userTables, activeTables;
    std::array<std::shared_ptr<const SampleData>, 3> userSamples, activeSamples;
    std::mutex tableWriterMutex;
    // A writer retains replaced tables while the audio thread still reads them.
    // Their potentially large allocations are reclaimed by a later writer, not render().
    std::vector<std::shared_ptr<const Wavetable>> retiredTables;
    std::vector<std::shared_ptr<const SampleData>> retiredSamples;
    juce::AudioBuffer<float> scratch, controlScratch;
    juce::dsp::Chorus<float> chorus;
    juce::Reverb reverb;
    std::vector<float> delayLeft, delayRight;
    int delayWrite = 0;
    float delayLowLeft = 0.0f, delayLowRight = 0.0f;
};

SynthEngine::SynthEngine() : impl (std::make_unique<Impl>()) {}
SynthEngine::~SynthEngine() = default;
void SynthEngine::prepare (double sampleRate, int maxBlock) { impl->prepare (sampleRate, maxBlock); }
void SynthEngine::reset() { impl->reset(); }

void SynthEngine::setSettings (const SynthSettings& settings)
{
    const int previousVoiceMode = impl->target.voiceMode;
    const auto previousShapes = impl->target.lfoShape, previousModes = impl->target.lfoMode;
    impl->target = sanitise (settings);
    for (size_t i = 0; i < lfoCount; ++i)
        if ((previousShapes[i] != impl->target.lfoShape[i] || previousModes[i] != impl->target.lfoMode[i])
            && (isChaosLfoShape (previousShapes[i]) || isChaosLfoShape (impl->target.lfoShape[i])))
        {
            impl->chaosLfos[i].reset (impl->target.lfoShape[i], static_cast<int> (i));
            // No actual read exists in this new algorithm/mode epoch yet.
            // In particular, an idle Trigger must not publish the old model.
            impl->lastLfoChaosPoint[i] = {};
            impl->publishedLfoChaos[i].publish ({}, impl->target.lfoShape[i], impl->target.lfoMode[i]);
            for (auto& voice : impl->voices)
            {
                voice.chaosLfos[i].reset (impl->target.lfoShape[i], static_cast<int> (i));
                voice.lfoChaosReadPoint[i] = {};
            }
        }
    impl->prepareModulationRoutes();
    if (previousVoiceMode != impl->target.voiceMode) impl->changeVoiceMode();
    if (impl->firstSettingsAfterPrepare)
    {
        impl->smooth = impl->target;
        impl->snapFilterTopology();
        impl->snapWarpModes();
        impl->firstSettingsAfterPrepare = false;
    }
    impl->updateEnvelopeRates();
}

void SynthEngine::setTransport (double bpm, double ppq, bool playing, bool hasPpq)
{
    impl->transportBpm = std::isfinite (bpm) ? juce::jlimit (1.0, 960.0, bpm) : 120.0;
    // Processor supplies transport after settings. Resolve this block's tempo
    // here; PPQ seeks and play/stop never retrigger a note envelope.
    impl->updateEnvelopeModels();
    impl->transportHasPpq = hasPpq && std::isfinite (ppq) && std::abs (ppq) <= 1.0e12;
    impl->transportPpq = impl->transportHasPpq ? ppq : 0.0;
    impl->transportPlaying = playing;
}

float SynthEngine::getLfoPhase (int zeroBased) const
{
    if (zeroBased < 0 || zeroBased >= static_cast<int> (lfoCount)) return 0.0f;
    return impl->publishedLfoPhase[static_cast<size_t> (zeroBased)].load (std::memory_order_relaxed);
}

std::array<float, 3> SynthEngine::getLfoChaosPoint (int zeroBased, int expectedShape, int expectedMode) const noexcept
{
    if (zeroBased < 0 || zeroBased >= static_cast<int> (lfoCount)) return {};
    return impl->publishedLfoChaos[static_cast<size_t> (zeroBased)].read (expectedShape, expectedMode);
}

void SynthEngine::render (juce::AudioBuffer<float>& output, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    output.clear();
    if (! impl->prepared || output.getNumChannels() == 0) return;
    if (impl->transportPlaying && impl->transportHasPpq)
        impl->renderPpq = impl->transportPpq - std::floor (impl->transportPpq);
    impl->prepareLfoDependencies();
    impl->alignLfosToTransport();
    for (size_t o = 0; o < 3; ++o)
    {
        auto table = std::atomic_load (&impl->userTables[o]);
        auto next = table != nullptr ? std::move (table) : impl->factory;
        if (impl->activeTables[o] != next)
        {
            impl->activeTables[o] = std::move (next);
            for (auto& v : impl->voices) v.controlCounter = 0;
        }
        auto sample = std::atomic_load (&impl->userSamples[o]);
        if (impl->activeSamples[o] != sample)
        {
            impl->activeSamples[o] = std::move (sample);
            for (auto& v : impl->voices)
            {
                v.sampleOsc[o].mode = -1;
                v.controlCounter = 0;
            }
        }
    }
    impl->chorus.setMix (impl->target.chorus * 0.65f);
    juce::Reverb::Parameters rp;
    rp.roomSize = impl->target.reverbSize;
    rp.damping = 0.5f;
    rp.width = 1.0f;
    rp.wetLevel = impl->target.reverb * 0.38f;
    // JUCE's classic reverb scales its dry parameter by two internally.
    rp.dryLevel = 0.5f - impl->target.reverb * 0.09f;
    rp.freezeMode = 0.0f;
    impl->reverb.setParameters (rp);
    int cursor = 0;
    for (const auto metadata : midi)
    {
        const int position = juce::jlimit (cursor, output.getNumSamples(), metadata.samplePosition);
        impl->renderRange (output, cursor, position - cursor);
        impl->midi (metadata.data, metadata.numBytes);
        cursor = position;
    }
    impl->renderRange (output, cursor, output.getNumSamples() - cursor);
    impl->publishLfoPhases();
}

void SynthEngine::setUserWavetable (int oscillator, std::shared_ptr<const Wavetable> table)
{
    if (oscillator < 0 || oscillator >= 3) return;
    if (table != nullptr)
    {
        if (table->frameLength < 32 || table->frameLength > 8192 || table->frameCount < 1
            || table->frameCount > 128 || table->levels < 1 || table->levels > 14)
            return;
        const auto expected = static_cast<size_t> (table->frameLength + 1)
                            * static_cast<size_t> (table->frameCount) * static_cast<size_t> (table->levels);
        if (table->samples.size() < expected) return;
    }
    const std::lock_guard<std::mutex> lock (impl->tableWriterMutex);
    impl->retiredTables.erase (std::remove_if (impl->retiredTables.begin(), impl->retiredTables.end(),
        [] (const auto& previous) { return previous.use_count() == 1; }), impl->retiredTables.end());
    auto previous = std::atomic_exchange (&impl->userTables[static_cast<size_t> (oscillator)], std::move (table));
    if (previous != nullptr) impl->retiredTables.push_back (std::move (previous));
}

float SynthEngine::getWavetableSample (int oscillator, float phase, float position) const
{
    if (oscillator < 0 || oscillator >= 3 || ! std::isfinite (phase)) return 0.0f;
    auto table = std::atomic_load (&impl->userTables[static_cast<size_t> (oscillator)]);
    if (table == nullptr) table = impl->factory;
    phase -= std::floor (phase);
    return readTable (*table, phase, finiteClamp (position, 0.0f, 1.0f), 0);
}

float SynthEngine::getWarpedWavetableSample (int oscillator, float phase, float position,
                                            const std::array<int, 2>& modes,
                                            const std::array<float, 2>& amounts) const
{
    if (oscillator < 0 || oscillator >= 3 || ! std::isfinite (phase)) return 0.0f;
    auto table = std::atomic_load (&impl->userTables[static_cast<size_t> (oscillator)]);
    if (table == nullptr) table = impl->factory;
    WarpModes safeModes;
    WarpAmounts safeAmounts;
    for (size_t slot = 0; slot < 2; ++slot)
    {
        safeModes[slot] = juce::jlimit (0, 6, modes[slot]);
        safeAmounts[slot] = finiteClamp (amounts[slot], 0.0f, 1.0f);
    }
    phase -= std::floor (phase);
    return readWarpedTable (*table, phase, finiteClamp (position, 0.0f, 1.0f), 0.0f, safeModes, safeAmounts);
}

void SynthEngine::setUserSample (int oscillator, std::shared_ptr<const SampleData> sample)
{
    if (oscillator < 0 || oscillator >= 3) return;
    if (sample != nullptr && (! std::isfinite (sample->sampleRate) || sample->sampleRate < 1000.0
        || sample->sampleRate > 384000.0 || sample->audio.getNumChannels() < 1
        || sample->audio.getNumChannels() > 2 || sample->audio.getNumSamples() < 2)) return;
    const std::lock_guard<std::mutex> lock (impl->tableWriterMutex);
    impl->retiredSamples.erase (std::remove_if (impl->retiredSamples.begin(), impl->retiredSamples.end(),
        [] (const auto& previous) { return previous.use_count() == 1; }), impl->retiredSamples.end());
    auto previous = std::atomic_exchange (&impl->userSamples[static_cast<size_t> (oscillator)], std::move (sample));
    if (previous != nullptr) impl->retiredSamples.push_back (std::move (previous));
}

float SynthEngine::getSamplePreview (int oscillator, float normalizedPosition) const
{
    if (oscillator < 0 || oscillator >= 3) return 0.0f;
    const auto sample = std::atomic_load (&impl->userSamples[static_cast<size_t> (oscillator)]);
    if (sample == nullptr) return 0.0f;
    float left = 0.0f, right = 0.0f;
    readAudio (*sample, static_cast<double> (finiteClamp (normalizedPosition, 0.0f, 1.0f))
                       * static_cast<double> (sample->audio.getNumSamples() - 1), left, right);
    return left;
}

std::shared_ptr<const SampleData> SynthEngine::makeSample (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    if (! std::isfinite (sampleRate) || sampleRate < 1000.0 || sampleRate > 384000.0
        || audio.getNumSamples() < 2 || audio.getNumChannels() < 1
        || static_cast<double> (audio.getNumSamples()) > sampleRate * 20.0) return {};
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = sampleRate;
    sample->audio.setSize (juce::jmin (2, audio.getNumChannels()), audio.getNumSamples());
    for (int channel = 0; channel < sample->audio.getNumChannels(); ++channel)
    {
        const auto* source = audio.getReadPointer (channel);
        auto* destination = sample->audio.getWritePointer (channel);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            destination[i] = finiteClamp (source[i], -8.0f, 8.0f);
    }
    return sample;
}

juce::AudioBuffer<float> SynthEngine::copyWavetableAudio (int oscillator) const
{
    if (oscillator < 0 || oscillator >= 3) return {};
    auto table = std::atomic_load (&impl->userTables[static_cast<size_t> (oscillator)]);
    if (table == nullptr) table = impl->factory;
    constexpr int canonicalLength = 2048;
    juce::AudioBuffer<float> result (1, table->frameCount * canonicalLength);
    auto* destination = result.getWritePointer (0);
    for (int frame = 0; frame < table->frameCount; ++frame)
    {
        const auto* source = table->samples.data() + static_cast<size_t> (frame * table->levels)
                                                   * static_cast<size_t> (table->frameLength + 1);
        for (int i = 0; i < canonicalLength; ++i)
        {
            const double position = static_cast<double> (i) * table->frameLength / canonicalLength;
            const int a = static_cast<int> (position);
            const float fraction = static_cast<float> (position - a);
            destination[frame * canonicalLength + i] = source[a] + fraction * (source[a + 1] - source[a]);
        }
    }
    return result;
}

std::shared_ptr<const Wavetable> SynthEngine::makeWavetable (const juce::AudioBuffer<float>& audio, int frameLength, bool normalise)
{
    if (audio.getNumSamples() < 2 || audio.getNumChannels() < 1) return {};
    frameLength = juce::jlimit (32, 8192, juce::nextPowerOfTwo (juce::jlimit (32, 8192, frameLength)));
    const int frames = juce::jlimit (1, 128, audio.getNumSamples() / frameLength);
    const int order = static_cast<int> (std::log2 (frameLength));
    auto table = std::make_shared<Wavetable>();
    table->frameLength = frameLength;
    table->frameCount = frames;
    table->levels = order;
    table->samples.resize (static_cast<size_t> (frames) * static_cast<size_t> (order) * static_cast<size_t> (frameLength + 1));
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> input (static_cast<size_t> (frameLength));
    std::vector<std::complex<float>> spectrum (input.size()), filtered (input.size()), result (input.size());
    for (int frame = 0; frame < frames; ++frame)
    {
        float mean = 0.0f, peak = 0.0f;
        for (int i = 0; i < frameLength; ++i)
        {
            const float source = audio.getNumSamples() < frameLength
                ? static_cast<float> (i) * static_cast<float> (audio.getNumSamples()) / static_cast<float> (frameLength)
                : static_cast<float> (frame * frameLength + i);
            const int a = juce::jmin (audio.getNumSamples() - 1, static_cast<int> (source));
            const int b = juce::jmin (audio.getNumSamples() - 1, a + 1);
            float sample = 0.0f;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                const auto* data = audio.getReadPointer (channel);
                const float maximum = normalise ? 32.0f : 128.0f;
                const float va = finiteClamp (data[a], -maximum, maximum);
                const float vb = finiteClamp (data[b], -maximum, maximum);
                sample += va + (source - static_cast<float> (a)) * (vb - va);
            }
            sample /= static_cast<float> (audio.getNumChannels());
            input[static_cast<size_t> (i)] = { sample, 0.0f };
            mean += sample;
        }
        mean /= static_cast<float> (frameLength);
        for (auto& sample : input)
        {
            sample.real (sample.real() - mean);
            peak = juce::jmax (peak, std::abs (sample.real()));
        }
        if (! normalise && peak > 128.0f) return {};
        const float normalisation = normalise ? (peak > 0.00001f ? 0.95f / peak : 0.0f) : 1.0f;
        for (auto& sample : input) sample *= normalisation;
        fft.perform (input.data(), spectrum.data(), false);
        for (int level = 0; level < order; ++level)
        {
            const int harmonics = juce::jmax (1, (frameLength / 2 - 1) >> level);
            filtered = spectrum;
            filtered[0] = {};
            for (int bin = harmonics + 1; bin < frameLength - harmonics; ++bin)
                filtered[static_cast<size_t> (bin)] = {};
            fft.perform (filtered.data(), result.data(), true);
            auto* destination = table->samples.data()
                + static_cast<size_t> (frame * order + level) * static_cast<size_t> (frameLength + 1);
            for (int i = 0; i < frameLength; ++i)
            {
                const float value = result[static_cast<size_t> (i)].real();
                if (! normalise && level == 0 && (! std::isfinite (value) || std::abs (value) > 128.0f)) return {};
                destination[i] = value;
            }
            destination[frameLength] = destination[0];
        }
    }
    return table;
}
}
