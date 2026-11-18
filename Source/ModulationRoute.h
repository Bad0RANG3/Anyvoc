#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace anyvoc
{
inline constexpr std::size_t modulationRouteCount = 32;
inline constexpr std::size_t lfoCount = 10;
inline constexpr std::size_t macroCount = 8;
inline constexpr std::size_t modulationSourceCount = 25;
inline constexpr std::size_t modulationDestinationCount = 28;

inline constexpr int lfoPhaseDestinationId (int zeroBased)
{
    return zeroBased < 0 || zeroBased >= static_cast<int> (lfoCount) ? 0 : 18 + zeroBased;
}

inline constexpr int lfoIndexForPhaseDestination (int destination)
{
    return destination >= 18 && destination < static_cast<int> (modulationDestinationCount) ? destination - 18 : -1;
}

// Source IDs 0..9 retain their original host/preset meanings. New LFOs occupy
// 10..17, Amp/Env3/Env4 occupy 18..20, and Macro5..8 occupy 21..24.
inline constexpr int lfoSourceId (int zeroBased)
{
    return zeroBased < 0 || zeroBased >= static_cast<int> (lfoCount) ? 0
         : zeroBased < 2 ? zeroBased + 1 : zeroBased + 8;
}

inline constexpr int macroSourceId (int zeroBased)
{
    return zeroBased < 0 || zeroBased >= static_cast<int> (macroCount) ? 0
         : zeroBased < 4 ? zeroBased + 6 : zeroBased + 17;
}

inline constexpr int lfoIndexForSource (int source)
{
    return source == 1 || source == 2 ? source - 1
         : source >= 10 && source <= 17 ? source - 8 : -1;
}

inline constexpr int macroIndexForSource (int source)
{
    return source >= 6 && source <= 9 ? source - 6
         : source >= 21 && source <= 24 ? source - 17 : -1;
}

inline constexpr bool isNaturalBipolarSource (int source) { return lfoIndexForSource (source) >= 0; }

struct ModRoute
{
    // Keep these first three fields in order for legacy aggregate initialisers.
    // Destinations: off, A cutoff, A/B/C position, pitch, level, pan, B cutoff,
    // A/B resonance, parallel balance, A/B/C warp1/warp2 amounts (12..17),
    // LFO1..10 phase offsets in turns (18..27, one-sample feedback delay).
    int source = 0, destination = 0;
    float amount = 0.0f;
    bool enabled = true;
    int polarity = 0, auxSource = 0, auxPolarity = 0; // Original, unipolar, bipolar.
    float curve = 0.0f, auxCurve = 0.0f;
};

inline float finiteModulationValue (float value, float low, float high, float fallback = 0.0f)
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

inline ModRoute sanitiseModRoute (ModRoute route)
{
    route.source = std::clamp (route.source, 0, static_cast<int> (modulationSourceCount) - 1);
    route.destination = std::clamp (route.destination, 0, static_cast<int> (modulationDestinationCount) - 1);
    route.amount = finiteModulationValue (route.amount, -1.0f, 1.0f);
    route.polarity = std::clamp (route.polarity, 0, 2);
    route.auxSource = std::clamp (route.auxSource, 0, static_cast<int> (modulationSourceCount) - 1);
    route.auxPolarity = std::clamp (route.auxPolarity, 0, 2);
    route.curve = finiteModulationValue (route.curve, -1.0f, 1.0f);
    route.auxCurve = finiteModulationValue (route.auxCurve, -1.0f, 1.0f);
    return route;
}

inline bool modulationSourceIsBipolar (int source, int polarity)
{
    source = std::clamp (source, 0, static_cast<int> (modulationSourceCount) - 1);
    polarity = std::clamp (polarity, 0, 2);
    return source != 0 && (polarity == 2 || (polarity == 0 && isNaturalBipolarSource (source)));
}

// Polarity conversion precedes the curve. Exponents are prepared per block,
// never per voice/sample; the common zero-curve path performs no pow/exp2.
struct ModulationSourceTransform
{
    bool active = false, nativeBipolar = false, bipolar = false, curved = false;
    float exponent = 1.0f;
};

inline ModulationSourceTransform prepareModulationSource (int source, int polarity, float curve)
{
    source = std::clamp (source, 0, static_cast<int> (modulationSourceCount) - 1);
    curve = finiteModulationValue (curve, -1.0f, 1.0f);
    ModulationSourceTransform result;
    result.active = source != 0;
    result.nativeBipolar = isNaturalBipolarSource (source);
    result.bipolar = modulationSourceIsBipolar (source, polarity);
    result.curved = result.active && curve != 0.0f;
    if (result.curved) result.exponent = std::exp2 (4.0f * curve);
    return result;
}

inline float transformModulationSource (float raw, const ModulationSourceTransform& transform)
{
    if (! transform.active) return 0.0f;
    float value = finiteModulationValue (raw, transform.nativeBipolar ? -1.0f : 0.0f, 1.0f);
    if (transform.nativeBipolar != transform.bipolar)
        value = transform.bipolar ? value * 2.0f - 1.0f : (value + 1.0f) * 0.5f;
    if (! transform.curved) return value;
    return transform.bipolar ? std::copysign (std::pow (std::abs (value), transform.exponent), value)
                             : std::pow (value, transform.exponent);
}

// Safe convenience overload for editor curve plots and offline consumers.
inline float transformModulationSource (float raw, int source, int polarity, float curve)
{
    return transformModulationSource (raw, prepareModulationSource (source, polarity, curve));
}

inline std::pair<float, float> modulationSourceRange (int source, int polarity)
{
    if (std::clamp (source, 0, static_cast<int> (modulationSourceCount) - 1) == 0) return { 0.0f, 0.0f };
    return { modulationSourceIsBipolar (source, polarity) ? -1.0f : 0.0f, 1.0f };
}
}
