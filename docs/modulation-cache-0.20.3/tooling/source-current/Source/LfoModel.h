#pragma once

#include <algorithm>
#include <cmath>

namespace anyvoc
{
// One shared musical grid: an eighth-note pair spans one quarter-note beat;
// a sixteenth-note pair spans half a beat. Swing moves the pair's midpoint
// from 50% to 75% without moving either endpoint.
inline double lfoSwingPpq (double ppq, float amount, int division) noexcept
{
    if (! std::isfinite (ppq)) return 0.0;
    amount = std::isfinite (amount) ? std::clamp (amount, 0.0f, 1.0f) : 0.0f;
    if (amount == 0.0f) return ppq;
    const double period = division <= 0 ? 1.0 : 0.5;
    const double grid = ppq / period, whole = std::floor (grid), t = grid - whole;
    const double midpoint = 0.5 + 0.25 * static_cast<double> (amount);
    const double warped = t < midpoint ? 0.5 * t / midpoint
        : 0.5 + 0.5 * (t - midpoint) / (1.0 - midpoint);
    return (whole + warped) * period;
}

// Read offset comes before direction. Zero/integer turns retain the terminal
// clock position 1, including a Custom one-shot's independent final point.
// Random sample-and-hold deliberately does not use this helper.
inline double lfoReadPhase (double clockPhase, double offset, int direction) noexcept
{
    double phase = std::isfinite (clockPhase) ? std::clamp (clockPhase, 0.0, 1.0) : 0.0;
    if (std::isfinite (offset))
    {
        offset -= std::floor (offset);
        if (offset != 0.0)
        {
            phase += offset;
            phase -= std::floor (phase);
        }
    }
    if (direction == 1) return 1.0 - phase;
    if (direction >= 2) return 1.0 - std::abs (2.0 * phase - 1.0);
    return phase;
}
}
