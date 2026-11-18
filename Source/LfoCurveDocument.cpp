#include "LfoCurveDocument.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace anyvoc
{
namespace
{
bool validDivisions (int n) noexcept
{
    return n == 1 || n == 2 || n == 4 || n == 8 || n == 16 || n == 32;
}

bool validSelection (int first, int last) noexcept
{
    return first >= 0 && first <= last && last < LfoCurveDocument::pointCount;
}

bool inRange (float n, float low, float high) noexcept
{
    return std::isfinite (n) && n >= low && n <= high;
}

bool xmlSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool nameCharacter (char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// A deliberately small, non-expanding XML reader. Restricting the grammar
// before interpreting numbers avoids permissive XML recovery, duplicate
// attributes, external resources, and partial consumption of trailing input.
class CurveXmlReader
{
public:
    explicit CurveXmlReader (std::string_view input) : source (input) {}

    bool read (LfoCurveData& result)
    {
        for (const unsigned char c : source)
            if ((c < 32 && ! xmlSpace (static_cast<char> (c))) || c > 126)
                return false;

        if (startsWith ("<?"))
        {
            if (! consume ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>")) return false;
        }
        skipSpace();

        Tag root;
        if (! readOpen (root) || root.name != "ANYVOC_LFO_CURVE" || root.empty
            || root.count != 2) return false;
        const auto schema = attribute (root, "schema");
        const auto interpolation = attribute (root, "interpolation");
        if (schema != "1" || (interpolation != "0" && interpolation != "1")) return false;
        result.interpolation = interpolation == "1" ? 1 : 0;

        for (auto& value : result.points)
        {
            skipSpace();
            Tag point;
            if (! readOpen (point) || point.name != "POINT" || point.count != 1
                || point.attributes[0].first != "value"
                || ! readNumber (point.attributes[0].second, value)) return false;
            if (! point.empty)
            {
                skipSpace();
                if (! readClose ("POINT")) return false;
            }
        }
        skipSpace();
        if (! readClose ("ANYVOC_LFO_CURVE")) return false;
        skipSpace();
        return cursor == source.size();
    }

private:
    struct Tag
    {
        std::string_view name;
        std::array<std::pair<std::string_view, std::string_view>, 2> attributes;
        size_t count = 0;
        bool empty = false;
    };

    static bool readNumber (std::string_view text, float& value)
    {
        // Exact decimal grammar: [+-]?(digits[.digits]?|.digits)([eE][+-]?digits)?
        size_t p = 0;
        if (p < text.size() && (text[p] == '+' || text[p] == '-')) ++p;
        const auto digits = [&] ()
        {
            const auto start = p;
            while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
            return p != start;
        };
        bool hasDigits = digits();
        if (p < text.size() && text[p] == '.')
        {
            ++p;
            hasDigits = digits() || hasDigits;
        }
        if (! hasDigits) return false;
        if (p < text.size() && (text[p] == 'e' || text[p] == 'E'))
        {
            ++p;
            if (p < text.size() && (text[p] == '+' || text[p] == '-')) ++p;
            if (! digits()) return false;
        }
        if (p != text.size()) return false;
        if (text.front() == '+') text.remove_prefix (1);
        double number = 0.0;
        const auto parsed = std::from_chars (text.data(), text.data() + text.size(), number,
                                             std::chars_format::general);
        if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()
            || ! std::isfinite (number) || number < -1.0 || number > 1.0) return false;
        value = static_cast<float> (number);
        // A finite, representable float is the format's stored numeric value.
        // Reject decimal underflow rather than silently accepting a nonzero as zero.
        return number == 0.0 || value != 0.0f;
    }

    std::string_view name()
    {
        const auto start = cursor;
        while (cursor < source.size() && nameCharacter (source[cursor])) ++cursor;
        return source.substr (start, cursor - start);
    }

    bool readOpen (Tag& tag)
    {
        if (! consume ("<")) return false;
        tag.name = name();
        if (tag.name.empty()) return false;
        for (;;)
        {
            const auto beforeSpace = cursor;
            skipSpace();
            if (consume ("/>")) { tag.empty = true; return true; }
            if (consume (">")) return true;
            if (cursor == beforeSpace || tag.count == tag.attributes.size()) return false;
            const auto key = name();
            if (key.empty()) return false;
            for (size_t i = 0; i < tag.count; ++i)
                if (tag.attributes[i].first == key) return false;
            skipSpace();
            if (! consume ("=")) return false;
            skipSpace();
            if (cursor == source.size() || (source[cursor] != '\'' && source[cursor] != '"')) return false;
            const char quote = source[cursor++];
            const auto start = cursor;
            while (cursor < source.size() && source[cursor] != quote)
            {
                const char c = source[cursor++];
                if (c == '<' || c == '&' || static_cast<unsigned char> (c) < 32) return false;
            }
            if (cursor == source.size()) return false;
            tag.attributes[tag.count++] = { key, source.substr (start, cursor - start) };
            ++cursor;
        }
    }

    bool readClose (std::string_view expected)
    {
        if (! consume ("</") || name() != expected) return false;
        skipSpace();
        return consume (">");
    }

    static std::string_view attribute (const Tag& tag, std::string_view key)
    {
        for (size_t i = 0; i < tag.count; ++i)
            if (tag.attributes[i].first == key) return tag.attributes[i].second;
        return {};
    }

    void skipSpace() { while (cursor < source.size() && xmlSpace (source[cursor])) ++cursor; }
    bool startsWith (std::string_view token) const { return source.substr (cursor, token.size()) == token; }
    bool consume (std::string_view token)
    {
        if (! startsWith (token)) return false;
        cursor += token.size();
        return true;
    }

    std::string_view source;
    size_t cursor = 0;
};

juce::Result parseBytes (std::string_view bytes, LfoCurveData& output)
{
    if (bytes.empty() || bytes.size() > LfoCurveDocument::maximumFileBytes)
        return juce::Result::fail ("Curve XML must contain 1 to 16384 bytes.");
    LfoCurveData candidate;
    if (! CurveXmlReader (bytes).read (candidate))
        return juce::Result::fail ("Invalid Anyvoc curve: expected schema 1, interpolation 0/1 and exactly 33 finite points in [-1,1].");
    output = candidate;
    return juce::Result::ok();
}
}

bool LfoCurveDocument::valid (const LfoCurveData& data) noexcept
{
    return (data.interpolation == 0 || data.interpolation == 1)
        && std::all_of (data.points.begin(), data.points.end(), [] (float n) { return inRange (n, -1.0f, 1.0f); });
}

int LfoCurveDocument::snapPoint (int point, int divisions) noexcept
{
    if (point < 0 || point >= pointCount || ! validDivisions (divisions)) return point;
    const int step = (pointCount - 1) / divisions;
    return ((point + step / 2) / step) * step;
}

float LfoCurveDocument::snapValue (float value, int divisions) noexcept
{
    if (! inRange (value, -1.0f, 1.0f) || ! validDivisions (divisions)) return value;
    const double grid = (static_cast<double> (value) + 1.0) * divisions * 0.5;
    return static_cast<float> (std::floor (grid + 0.5) * 2.0 / divisions - 1.0);
}

bool LfoCurveDocument::flipHorizontal (LfoCurveData& data, int first, int last) noexcept
{
    if (! valid (data) || ! validSelection (first, last)) return false;
    std::reverse (data.points.begin() + first, data.points.begin() + last + 1);
    return true;
}

bool LfoCurveDocument::flipVertical (LfoCurveData& data, int first, int last) noexcept
{
    if (! valid (data) || ! validSelection (first, last)) return false;
    for (int i = first; i <= last; ++i) data.points[static_cast<size_t> (i)] = -data.points[static_cast<size_t> (i)];
    return true;
}

bool LfoCurveDocument::transform (const LfoCurveData& input, int first, int last,
                                  float offsetPoints, float offsetValue, float width, float height,
                                  LfoCurveData& output) noexcept
{
    if (! valid (input) || ! validSelection (first, last)
        || ! inRange (offsetPoints, -32.0f, 32.0f) || ! inRange (offsetValue, -2.0f, 2.0f)
        || ! inRange (width, 0.25f, 4.0f) || ! inRange (height, 0.0f, 4.0f)) return false;
    auto candidate = input;
    if (offsetPoints != 0.0f || offsetValue != 0.0f || width != 1.0f || height != 1.0f)
    {
        const double centre = (first + last) * 0.5;
        for (int target = first; target <= last; ++target)
        {
            const double position = std::clamp (centre + (target - centre - offsetPoints) / width,
                                                 static_cast<double> (first), static_cast<double> (last));
            const int left = static_cast<int> (std::floor (position));
            float sample = input.points[static_cast<size_t> (left)];
            if (input.interpolation == 0 && left < last)
            {
                const float right = input.points[static_cast<size_t> (left + 1)];
                sample = static_cast<float> (sample + (static_cast<double> (right) - sample) * (position - left));
            }
            candidate.points[static_cast<size_t> (target)] = std::clamp (sample * height + offsetValue, -1.0f, 1.0f);
        }
    }
    output = candidate;
    return true;
}

juce::String LfoCurveDocument::serialize (const LfoCurveData& data)
{
    if (! valid (data)) return {};
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ANYVOC_LFO_CURVE schema=\"1\" interpolation=\"";
    xml += data.interpolation == 1 ? "1" : "0";
    xml += "\">\n";
    for (const float value : data.points)
    {
        std::array<char, 64> number {};
        const auto written = std::to_chars (number.data(), number.data() + number.size(), value,
                                            std::chars_format::general, std::numeric_limits<float>::max_digits10);
        if (written.ec != std::errc {}) return {};
        xml += "  <POINT value=\"";
        xml.append (number.data(), written.ptr);
        xml += "\"/>\n";
    }
    xml += "</ANYVOC_LFO_CURVE>\n";
    return juce::String::fromUTF8 (xml.data(), static_cast<int> (xml.size()));
}

juce::Result LfoCurveDocument::parse (const juce::String& text, LfoCurveData& output)
{
    const auto size = text.getNumBytesAsUTF8();
    if (size > maximumFileBytes) return juce::Result::fail ("Curve XML exceeds 16384 bytes.");
    return parseBytes ({ text.toRawUTF8(), size }, output);
}

juce::Result LfoCurveDocument::loadFile (const juce::File& file, LfoCurveData& output)
{
    const auto size = file.getSize();
    if (! file.existsAsFile() || size <= 0 || size > maximumFileBytes)
        return juce::Result::fail ("Curve file must exist and contain 1 to 16384 bytes.");
    juce::FileInputStream input (file);
    if (! input.openedOk()) return input.getStatus();
    // Bounded read with one extra byte detects a file growing after the size check.
    std::array<char, maximumFileBytes + 1> bytes {};
    int count = 0;
    while (count < static_cast<int> (bytes.size()))
    {
        const int read = input.read (bytes.data() + count, static_cast<int> (bytes.size()) - count);
        if (read <= 0) break;
        count += read;
    }
    if (input.getStatus().failed()) return input.getStatus();
    return parseBytes ({ bytes.data(), static_cast<size_t> (count) }, output);
}

juce::Result LfoCurveDocument::saveFile (const juce::File& file, const LfoCurveData& data)
{
    const auto text = serialize (data);
    if (text.isEmpty()) return juce::Result::fail ("Cannot save an invalid curve.");
    if (file == juce::File {} || file.isDirectory() || ! file.getParentDirectory().isDirectory())
        return juce::Result::fail ("Curve destination must be a file in an existing directory.");
    juce::TemporaryFile temporary (file);
    {
        juce::FileOutputStream output (temporary.getFile());
        if (! output.openedOk()) return output.getStatus();
        if (! output.write (text.toRawUTF8(), text.getNumBytesAsUTF8()))
            return juce::Result::fail ("Could not write the curve temporary file.");
        output.flush();
        if (output.getStatus().failed()) return output.getStatus();
    }
    if (! temporary.getFile().replaceFileIn (file))
        return juce::Result::fail ("Could not replace the curve destination; the temporary file was discarded.");
    return juce::Result::ok();
}
}
