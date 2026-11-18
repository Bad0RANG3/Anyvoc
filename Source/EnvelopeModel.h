#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace anyvoc
{
struct EnvelopeSettings
{
    // Keep the original aggregate fields and defaults for old presets/callers.
    float attack = 0.01f, decay = 0.2f, sustain = 0.7f, release = 0.3f;
    float delay = 0.0f, hold = 0.0f;
    float attackCurve = 0.0f, decayCurve = 0.0f, releaseCurve = 0.0f;
    bool tempoSync = false;
    std::array<int, 5> divisions { 0, 3, 0, 5, 5 };
    bool retriggerLegato = false;
};

inline constexpr std::array<double, 14> envelopeDivisionBeats {
    0.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0,
    8.0, 16.0, 1.0 / 3.0, 1.0 / 6.0, 0.75, 0.375
};

// Stage indices: Delay, Attack, Hold, Decay, Release. Sustain is a key-held level.
inline double envelopeStageSeconds (const EnvelopeSettings& settings, int stage, double bpm) noexcept
{
    if (stage < 0 || stage >= 5) return 0.0;
    if (settings.tempoSync)
    {
        const int division = std::clamp (settings.divisions[static_cast<std::size_t> (stage)], 0, 13);
        const double tempo = std::isfinite (bpm) ? std::clamp (bpm, 1.0, 960.0) : 120.0;
        return envelopeDivisionBeats[static_cast<std::size_t> (division)] * 60.0 / tempo;
    }
    const std::array<float, 5> values { settings.delay, settings.attack, settings.hold, settings.decay, settings.release };
    const std::array<float, 5> minima { 0.0f, 0.0005f, 0.0f, 0.001f, 0.002f };
    const std::array<float, 5> maxima { 8.0f, 20.0f, 8.0f, 20.0f, 30.0f };
    const std::array<float, 5> defaults { 0.0f, 0.01f, 0.0f, 0.2f, 0.3f };
    const auto i = static_cast<std::size_t> (stage);
    return std::isfinite (values[i]) ? std::clamp (values[i], minima[i], maxima[i]) : defaults[i];
}

inline float envelopeCurveProgress (float progress, float curve) noexcept
{
    const float t = std::isfinite (progress) ? std::clamp (progress, 0.0f, 1.0f) : 0.0f;
    const float c = std::isfinite (curve) ? std::clamp (curve, -1.0f, 1.0f) : 0.0f;
    return c == 0.0f ? t : std::pow (t, std::exp2 (4.0f * c));
}

inline float envelopeSegmentValue (float start, float end, float progress, float curve) noexcept
{
    const float a = std::isfinite (start) ? std::clamp (start, 0.0f, 1.0f) : 0.0f;
    const float b = std::isfinite (end) ? std::clamp (end, 0.0f, 1.0f) : 0.0f;
    return a + (b - a) * envelopeCurveProgress (progress, curve);
}
}
