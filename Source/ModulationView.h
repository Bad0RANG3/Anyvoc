#pragma once
#include "ModulationRoute.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace anyvoc
{
// Nominal excursion of ONE route. UI clips this to the base control's range.
// This is not voice telemetry: other routes, envelopes, tracking and sample-rate
// limits are intentionally absent. LFOs are bipolar; all other sources unipolar.
inline std::pair<float, float> modulationRange (int destination, float base, int source, float amount)
{
    if (source <= 0 || source >= static_cast<int> (modulationSourceCount)) return { base, base };
    const float a = isNaturalBipolarSource (source) ? -amount : 0.0f;
    const float b = amount;
    const auto apply = [destination, base] (float offset)
    {
        return destination == 1 || destination == 8 ? base * std::exp2 (5.0f * offset) : base + offset;
    };
    const float first = apply (a), second = apply (b);
    return { std::min (first, second), std::max (first, second) };
}

// The product of both source intervals is nominal, not live telemetry. Curves
// are monotonic and retain endpoints, so they do not alter the excursion ring.
inline std::pair<float, float> modulationRange (int destination, float base, const ModRoute& settings)
{
    const auto route = sanitiseModRoute (settings);
    if (! route.enabled || route.source == 0 || destination <= 0 || destination >= static_cast<int> (modulationDestinationCount))
        return { base, base };
    const auto primary = modulationSourceRange (route.source, route.polarity);
    const auto auxiliary = route.auxSource == 0 ? std::pair<float, float> { 1.0f, 1.0f }
                                              : modulationSourceRange (route.auxSource, route.auxPolarity);
    const float products[] { primary.first * auxiliary.first, primary.first * auxiliary.second,
                             primary.second * auxiliary.first, primary.second * auxiliary.second };
    const auto extrema = std::minmax_element (std::begin (products), std::end (products));
    const auto apply = [destination, base] (float offset)
    {
        return destination == 1 || destination == 8 ? base * std::exp2 (5.0f * offset) : base + offset;
    };
    const float first = apply (*extrema.first * route.amount), second = apply (*extrema.second * route.amount);
    return { std::min (first, second), std::max (first, second) };
}

inline std::pair<float, float> modulationRange (float base, const ModRoute& route)
{
    return modulationRange (route.destination, base, route);
}
}
