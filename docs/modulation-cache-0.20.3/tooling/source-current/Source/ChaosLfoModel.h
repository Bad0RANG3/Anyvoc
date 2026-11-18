#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace anyvoc
{
inline bool isChaosLfoShape (int shape) noexcept { return shape == 6 || shape == 7; }

// A continuous ODE trajectory, never a repeated wavetable. The numerical grid
// is 128 RK4 steps per nominal LFO period, independent of audio sample rate.
// Cubic Hermite interpolation uses the ODE derivatives at both grid endpoints.
class ChaosLfoState
{
public:
    using Point = std::array<double, 3>;
    static constexpr int stepsPerPeriod = 128;

    void reset (int effectiveShape, int zeroBasedLfo) noexcept
    {
        shape = isChaosLfoShape (effectiveShape) ? effectiveShape : 0;
        index = std::clamp (zeroBasedLfo, 0, 9);
        fraction = 0.0;
        firstPeriodSteps = 0;
        recoveries = 0;
        if (shape == 0) { lower = upper = lowerSlope = upperSlope = {}; return; }
        initialiseSegment();
    }

    int getShape() const noexcept { return shape; }
    uint32_t getRecoveryCount() const noexcept { return recoveries; }
    double getNominalProgress() const noexcept
    { return std::min (1.0, (static_cast<double> (firstPeriodSteps) + fraction) / stepsPerPeriod); }

    Point rawPoint() const noexcept
    {
        if (fraction == 0.0) return lower;
        const double t = fraction, t2 = t * t, t3 = t2 * t;
        const double a = 2 * t3 - 3 * t2 + 1, b = t3 - 2 * t2 + t;
        const double c = -2 * t3 + 3 * t2, d = t3 - t2;
        Point result {};
        for (size_t axis = 0; axis < 3; ++axis)
            result[axis] = a * lower[axis] + b * stepSize() * lowerSlope[axis]
                         + c * upper[axis] + d * stepSize() * upperSlope[axis];
        return result;
    }

    std::array<float, 3> point() const noexcept
    {
        if (shape == 0) return {};
        auto p = rawPoint();
        const double xy = shape == 6 ? 10.0 : 6.0;
        const double z = shape == 6 ? 25.0 : 10.0;
        p[2] -= z;
        return { softsign (p[0], xy), softsign (p[1], xy), softsign (p[2], z) };
    }

    void advance (double nominalCycles, bool oneShot = false) noexcept
    {
        if (shape == 0 || ! std::isfinite (nominalCycles) || nominalCycles <= 0.0) return;
        // All engine rates have delta < 1 (sample rate > 1000 Hz, rate <= 1000).
        // Cap malformed direct callers too: at most 128 numerical steps/call.
        double steps = std::min (1.0, nominalCycles) * stepsPerPeriod;
        if (oneShot)
        {
            const double remaining = stepsPerPeriod - static_cast<double> (firstPeriodSteps) - fraction;
            if (remaining <= 0.0) return;
            if (steps >= remaining - 1.0e-11)
            {
                while (firstPeriodSteps < stepsPerPeriod) advanceSegment();
                fraction = 0.0;
                return;
            }
        }
        fraction += steps;
        while (fraction >= 1.0)
        {
            fraction -= 1.0;
            advanceSegment();
        }
    }

private:
    double stepSize() const noexcept { return (shape == 6 ? 1.0 : 6.0) / stepsPerPeriod; }
    Point derivative (const Point& p) const noexcept
    {
        if (shape == 6)
            return { 10.0 * (p[1] - p[0]), p[0] * (28.0 - p[2]) - p[1],
                     p[0] * p[1] - (8.0 / 3.0) * p[2] };
        return { -p[1] - p[2], p[0] + .2 * p[1], .2 + p[2] * (p[0] - 5.7) };
    }
    Point rk4 (const Point& p, const Point& k1) const noexcept
    {
        const double h = stepSize();
        Point temporary {};
        for (size_t axis = 0; axis < 3; ++axis) temporary[axis] = p[axis] + .5 * h * k1[axis];
        const auto k2 = derivative (temporary);
        for (size_t axis = 0; axis < 3; ++axis) temporary[axis] = p[axis] + .5 * h * k2[axis];
        const auto k3 = derivative (temporary);
        for (size_t axis = 0; axis < 3; ++axis) temporary[axis] = p[axis] + h * k3[axis];
        const auto k4 = derivative (temporary);
        for (size_t axis = 0; axis < 3; ++axis)
            temporary[axis] = p[axis] + (h / 6.0) * (k1[axis] + 2 * k2[axis] + 2 * k3[axis] + k4[axis]);
        return temporary;
    }
    void initialiseSegment() noexcept
    {
        lower = { 1.0 + .01 * index, shape == 6 ? 1.0 : 0.0, shape == 6 ? 1.0 : 0.0 };
        lowerSlope = derivative (lower);
        upper = rk4 (lower, lowerSlope);
        upperSlope = derivative (upper);
    }
    void advanceSegment() noexcept
    {
        lower = upper;
        lowerSlope = upperSlope;
        upper = rk4 (lower, lowerSlope);
        // Far outside either attractor: protect future DSP/telemetry from a
        // corrupted or divergent numerical state. Normal trajectories never
        // hit this guard; tests explicitly count recoveries over long runs.
        const bool valid = std::all_of (upper.begin(), upper.end(), [] (double value)
            { return std::isfinite (value) && std::abs (value) <= 1.0e6; });
        if (! valid)
        {
            if (recoveries != UINT32_MAX) ++recoveries;
            initialiseSegment();
        }
        else upperSlope = derivative (upper);
        firstPeriodSteps = std::min (stepsPerPeriod, firstPeriodSteps + 1);
    }
    static float softsign (double value, double scale) noexcept
    { return std::isfinite (value) ? static_cast<float> (value / (scale + std::abs (value))) : 0.0f; }

    int shape = 0, index = 0, firstPeriodSteps = 0;
    uint32_t recoveries = 0;
    double fraction = 0.0;
    Point lower {}, upper {}, lowerSlope {}, upperSlope {};
};
}
