#pragma once

#include <JuceHeader.h>
#include <array>

namespace anyvoc
{
struct LfoCurveData
{
    std::array<float, 33> points {};
    int interpolation = 0; // 0 = linear, 1 = hold the preceding point
};

/** Offline curve operations and the independent .anycurve asset format.
    No method changes synthesizer parameters, undo history, or the DSP.
*/
class LfoCurveDocument
{
public:
    static constexpr int pointCount = 33;
    static constexpr int maximumFileBytes = 16 * 1024;

    static bool valid (const LfoCurveData&) noexcept;

    // Divisions must be 1, 2, 4, 8, 16, or 32. X spans indices [0,32];
    // Y divides [-1,1] into that many equal intervals. Exact half-grid ties
    // always take the larger coordinate, including negative Y and zero.
    // Invalid input or divisions returns the original argument unchanged.
    static int snapPoint (int point, int divisions) noexcept;
    static float snapValue (float value, int divisions) noexcept;

    // Inclusive selection; everything outside it is unchanged. Invalid data
    // or selection rejects the whole edit without changing the data.
    static bool flipHorizontal (LfoCurveData&, int first = 0, int last = 32) noexcept;
    static bool flipVertical (LfoCurveData&, int first = 0, int last = 32) noexcept;

    // Fixed selection mask, no wrap: read the source at
    // centre + (target - centre - offsetPoints) / width, holding endpoints.
    // Then multiply by height, add offsetValue, and clamp to [-1,1].
    // Horizontal units are points (1/32 cycle); vertical scale pivots at zero.
    // width [0.25,4], height [0,4], offsets [-32,32] / [-2,2].
    // Identity is exact. Aliasing input and output is supported. Failure
    // leaves output unchanged, including for a one-point selection.
    static bool transform (const LfoCurveData& input, int first, int last,
                           float offsetPoints, float offsetValue,
                           float width, float height, LfoCurveData& output) noexcept;

    // UTF-8/ASCII XML subset, at most 16 KiB including whitespace/declaration:
    // ANYVOC_LFO_CURVE schema="1" interpolation="0|1", exactly 33 ordered
    // empty POINT children, each with only value="finite decimal [-1,1]".
    // Attribute order/quote style and XML whitespace may vary. The optional
    // declaration is exactly <?xml version="1.0" encoding="UTF-8"?> at byte 0.
    // DTDs, entities, comments, other PIs, BOMs and non-ASCII are rejected.
    // Invalid data serializes to an empty string. Parse/load are transactional.
    static juce::String serialize (const LfoCurveData&);
    static juce::Result parse (const juce::String&, LfoCurveData& output);
    static juce::Result loadFile (const juce::File&, LfoCurveData& output);
    // Writes a sibling temporary file, flushes/closes it, then replaces the
    // destination. Neither load nor save infers format from the file extension.
    static juce::Result saveFile (const juce::File&, const LfoCurveData&);
};
}
