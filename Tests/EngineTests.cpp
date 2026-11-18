#include "SynthEngine.h"
#include "ModulationView.h"
#include "ChaosLfoModel.h"
#include "LfoCurveDocument.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstring>
#include <functional>
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using anyvoc::SynthEngine;
using anyvoc::SynthSettings;
constexpr double pi = 3.14159265358979323846;

void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

bool sameCurveBits (const anyvoc::LfoCurveData& a, const anyvoc::LfoCurveData& b)
{
    return a.interpolation == b.interpolation
        && std::memcmp (a.points.data(), b.points.data(), sizeof (a.points)) == 0;
}

anyvoc::LfoCurveData curveLandmarks()
{
    anyvoc::LfoCurveData result;
    for (size_t i = 0; i < result.points.size(); ++i) result.points[i] = static_cast<float> (i) / 32.0f - 0.5f;
    result.points[0] = -0.0f;
    const std::array<float, 5> middle { -0.75f, 0.5f, -0.25f, 1.0f, -1.0f };
    std::copy (middle.begin(), middle.end(), result.points.begin() + 12);
    return result;
}

void requireCurveSelection (const anyvoc::LfoCurveData& original, const anyvoc::LfoCurveData& edited,
                            const std::array<float, 5>& expected, const std::string& label)
{
    require (original.interpolation == edited.interpolation, label + " changed interpolation");
    for (int i = 0; i < 33; ++i)
        if (i >= 12 && i <= 16)
            require (edited.points[static_cast<size_t> (i)] == expected[static_cast<size_t> (i - 12)],
                     label + " wrong landmark at " + std::to_string (i));
        else
            require (std::memcmp (&original.points[static_cast<size_t> (i)], &edited.points[static_cast<size_t> (i)], sizeof (float)) == 0,
                     label + " changed an unselected point");
}

void testCurveGridSnapping()
{
    using D = anyvoc::LfoCurveDocument;
    // Independent nearest-neighbour search; equal distances replace the
    // preceding candidate, so ties choose the larger coordinate on both axes.
    for (int divisions : { 1, 2, 4, 8, 16, 32 })
    {
        for (int point = 0; point <= 32; ++point)
        {
            int expected = 0, bestDistance = 100;
            for (int grid = 0; grid <= divisions; ++grid)
            {
                const int candidate = grid * 32 / divisions;
                if (std::abs (point - candidate) <= bestDistance)
                { expected = candidate; bestDistance = std::abs (point - candidate); }
            }
            require (D::snapPoint (point, divisions) == expected, "X snapping did not choose nearest grid/tie");
        }
        for (int n = -64; n <= 64; ++n)
        {
            const float value = n / 64.0f;
            float expected = -1.0f, bestDistance = 3.0f;
            for (int grid = 0; grid <= divisions; ++grid)
            {
                const float candidate = -1.0f + 2.0f * grid / divisions;
                if (std::abs (value - candidate) <= bestDistance)
                { expected = candidate; bestDistance = std::abs (value - candidate); }
            }
            require (D::snapValue (value, divisions) == expected, "Y snapping did not choose nearest grid/tie");
        }
    }
    require (D::snapValue (-.25f, 4) == 0.0f && D::snapValue (.25f, 4) == .5f
             && D::snapValue (0.0f, 1) == 1.0f, "Negative/positive half-grid contract drifted");
    for (int divisions : { -2, 0, 3, 5, 64 })
        require (D::snapPoint (7, divisions) == 7 && D::snapValue (-.23f, divisions) == -.23f,
                 "Invalid grid silently changed a point");
    require (D::snapPoint (-1, 8) == -1 && D::snapPoint (33, 8) == 33
             && D::snapValue (1.1f, 8) == 1.1f && D::snapValue (-1.1f, 8) == -1.1f
             && std::isnan (D::snapValue (std::numeric_limits<float>::quiet_NaN(), 8)),
             "Invalid snap coordinates were silently sanitized");
}

void testCurveSelectionFlips()
{
    using D = anyvoc::LfoCurveDocument;
    auto original = curveLandmarks(); original.interpolation = 1;
    auto changed = original;
    require (D::flipHorizontal (changed, 12, 16), "Selected horizontal flip rejected");
    requireCurveSelection (original, changed, { -1, 1, -.25f, .5f, -.75f }, "Horizontal flip");
    require (D::flipHorizontal (changed, 12, 16) && sameCurveBits (changed, original), "Double horizontal flip was not bit-exact");
    require (D::flipVertical (changed, 12, 16), "Selected vertical flip rejected");
    requireCurveSelection (original, changed, { .75f, -.5f, .25f, -1, 1 }, "Vertical flip");
    require (D::flipVertical (changed, 12, 16) && sameCurveBits (changed, original), "Double vertical flip was not bit-exact");
    require (D::flipHorizontal (changed, 0, 0) && sameCurveBits (changed, original), "Single-point horizontal flip changed data");
    require (D::flipHorizontal (changed) && changed.points[0] == original.points[32]
             && std::signbit (changed.points[32]), "Whole-curve flip wrapped or lost independent endpoints");
    require (D::flipHorizontal (changed) && sameCurveBits (changed, original), "Whole-curve flip did not restore endpoint bits");
    require (D::flipVertical (changed, 0, 0) && ! std::signbit (changed.points[0]), "Single-point vertical flip failed");
    for (const auto selection : { std::pair<int, int> { -1, 2 }, { 3, 2 }, { 0, 33 }, { 33, 33 } })
    {
        const auto before = changed;
        require (! D::flipHorizontal (changed, selection.first, selection.second)
                 && ! D::flipVertical (changed, selection.first, selection.second)
                 && sameCurveBits (changed, before), "Invalid flip modified data");
    }
}

void testCurveTransformLandmarks()
{
    using D = anyvoc::LfoCurveDocument;
    const auto source = curveLandmarks();
    anyvoc::LfoCurveData output;
    require (D::transform (source, 12, 16, 1, 0, 1, 1, output), "Curve move rejected");
    requireCurveSelection (source, output, { -.75f, -.75f, .5f, -.25f, 1 }, "Move right/hold left");
    require (D::transform (source, 12, 16, -1, 0, 1, 1, output), "Curve move left rejected");
    requireCurveSelection (source, output, { .5f, -.25f, 1, -1, -1 }, "Move left/hold right");
    require (D::transform (source, 12, 16, .5f, 0, 1, 1, output), "Fractional move rejected");
    requireCurveSelection (source, output, { -.75f, -.125f, .125f, .375f, 0 }, "Fractional move");
    require (D::transform (source, 12, 16, 0, 0, 2, 1, output), "Curve width rejected");
    requireCurveSelection (source, output, { .5f, .125f, -.25f, .375f, 1 }, "Centre-based linear width");
    auto stepped = source; stepped.interpolation = 1;
    require (D::transform (stepped, 12, 16, 0, 0, 2, 1, output), "Stepped width rejected");
    requireCurveSelection (stepped, output, { .5f, .5f, -.25f, -.25f, 1 }, "Centre-based stepped width");
    require (D::transform (source, 12, 16, 0, 0, .25f, 1, output), "Minimum width rejected");
    requireCurveSelection (source, output, { -.75f, -.75f, -.25f, -1, -1 }, "Narrow width endpoint hold");
    require (D::transform (source, 12, 16, 0, .25f, 1, 2, output), "Height/offset rejected");
    requireCurveSelection (source, output, { -1, 1, -.25f, 1, -1 }, "Zero-pivot height, then offset, then clamp");
    require (D::transform (source, 12, 16, 0, -.5f, 4, 0, output), "Zero height rejected");
    requireCurveSelection (source, output, { -.5f, -.5f, -.5f, -.5f, -.5f }, "Zero-height level");
    for (const float offset : { -32.0f, 32.0f })
    {
        require (D::transform (source, 12, 16, offset, 0, 1, 1, output), "Boundary offset rejected");
        const float expected = offset < 0 ? -1.0f : -.75f;
        requireCurveSelection (source, output, { expected, expected, expected, expected, expected }, "Full offset boundary hold");
    }
    for (const float width : { .25f, 4.0f })
        require (D::transform (source, 14, 14, 32, .5f, width, 2, output) && output.points[14] == 0,
                 "Single-point width/move altered the source lookup");
    auto aliased = source;
    require (D::transform (aliased, 12, 16, .5f, 0, 1, 1, aliased), "Aliased transform rejected");
    requireCurveSelection (source, aliased, { -.75f, -.125f, .125f, .375f, 0 }, "Aliased transform");
}

void testCurveInvalidAndIdentity()
{
    using D = anyvoc::LfoCurveDocument;
    auto source = curveLandmarks();
    source.points[1] = std::numeric_limits<float>::denorm_min();
    source.points[2] = -std::numeric_limits<float>::min();
    for (int interpolation : { 0, 1 })
    {
        source.interpolation = interpolation;
        for (const auto selection : { std::pair<int, int> { 0, 32 }, { 12, 16 }, { 0, 0 } })
        {
            anyvoc::LfoCurveData result;
            require (D::transform (source, selection.first, selection.second, 0, 0, 1, 1, result)
                     && sameCurveBits (source, result), "Identity transform lost float bits or interpolation");
        }
    }
    const auto sentinel = curveLandmarks();
    auto output = sentinel;
    const auto reject = [&] (const anyvoc::LfoCurveData& data, int first, int last, float x, float y, float w, float h)
    {
        require (! D::transform (data, first, last, x, y, w, h, output) && sameCurveBits (sentinel, output),
                 "Invalid transform changed its output");
    };
    for (const auto selection : { std::pair<int, int> { -1, 3 }, { 3, 2 }, { 0, 33 } })
        reject (source, selection.first, selection.second, 0, 0, 1, 1);
    for (int field = 0; field < 4; ++field)
        for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), -100.0f, 100.0f })
        {
            std::array<float, 4> controls { 0, 0, 1, 1 }; controls[static_cast<size_t> (field)] = invalid;
            reject (source, 0, 32, controls[0], controls[1], controls[2], controls[3]);
        }
    reject (source, 0, 32, 32.01f, 0, 1, 1); reject (source, 0, 32, 0, -2.01f, 1, 1);
    reject (source, 0, 32, 0, 0, .249f, 1); reject (source, 0, 32, 0, 0, 4.001f, 1);
    reject (source, 0, 32, 0, 0, 1, -.001f); reject (source, 0, 32, 0, 0, 1, 4.001f);
    for (const float invalid : { -1.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        auto bad = source; bad.points[32] = invalid; const auto before = bad;
        require (! D::valid (bad) && D::serialize (bad).isEmpty(), "Invalid curve point accepted");
        require (! D::flipHorizontal (bad, 12, 16) && ! D::flipVertical (bad, 12, 16)
                 && sameCurveBits (bad, before), "Invalid outside-selection point allowed partial flip");
        reject (bad, 12, 16, 0, 0, 1, 1);
    }
    for (const int invalid : { -1, 2, 99 })
    {
        auto bad = source; bad.interpolation = invalid;
        require (! D::valid (bad) && D::serialize (bad).isEmpty(), "Invalid interpolation accepted");
        reject (bad, 0, 32, 0, 0, 1, 1);
    }
}

juce::String independentCurveXml (const juce::String& number = "0", int count = 33)
{
    juce::String result = "<ANYVOC_LFO_CURVE schema=\"1\" interpolation=\"0\">\n";
    for (int i = 0; i < count; ++i) result += "<POINT value=\"" + number + "\"/>\n";
    return result + "</ANYVOC_LFO_CURVE>";
}

void testCurveCodecRoundTrip()
{
    using D = anyvoc::LfoCurveDocument;
    uint32_t random = 0x74acf0bdu;
    for (int pass = 0; pass < 128; ++pass)
    {
        anyvoc::LfoCurveData data; data.interpolation = pass % 2;
        for (auto& value : data.points)
        {
            do
            {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                std::memcpy (&value, &random, sizeof (value));
            }
            while (! std::isfinite (value) || std::abs (value) > 1);
        }
        data.points[0] = -0.0f; data.points[1] = std::numeric_limits<float>::denorm_min();
        data.points[2] = -std::numeric_limits<float>::min(); data.points[31] = -1; data.points[32] = 1;
        const auto text = D::serialize (data);
        require (text.isNotEmpty() && text.getNumBytesAsUTF8() <= D::maximumFileBytes, "Writer produced empty/oversize curve");
        anyvoc::LfoCurveData roundTrip;
        require (D::parse (text, roundTrip).wasOk() && sameCurveBits (data, roundTrip), "Curve float-bit/interpolation round trip failed");
        // An independent XML implementation verifies the emitted tree/counts.
        const auto xml = juce::XmlDocument::parse (text);
        require (xml != nullptr && xml->hasTagName ("ANYVOC_LFO_CURVE") && xml->getNumAttributes() == 2
                 && xml->getStringAttribute ("schema") == "1" && xml->getIntAttribute ("interpolation") == data.interpolation
                 && xml->getNumChildElements() == 33, "Writer output has the wrong independent XML structure");
        for (int i = 0; i < 33; ++i)
        {
            const auto* child = xml->getChildElement (i);
            require (child->hasTagName ("POINT") && child->getNumAttributes() == 1 && child->getNumChildElements() == 0
                     && static_cast<float> (child->getDoubleAttribute ("value")) == data.points[static_cast<size_t> (i)],
                     "Writer point failed independent XML numeric read");
        }
    }
    juce::String reordered = " \r\n<ANYVOC_LFO_CURVE interpolation = '1'\t schema='1'>\n";
    const std::array<const char*, 6> numbers { "-.5", "+0.25", "1e-1", "0.", "-1", "1" };
    const std::array<float, 6> values { -.5f, .25f, .1f, 0, -1, 1 };
    for (int i = 0; i < 33; ++i) reordered += "<POINT value='" + juce::String (numbers[static_cast<size_t> (i % 6)]) + "'> \t</POINT>\r\n";
    reordered += "</ANYVOC_LFO_CURVE> \n";
    anyvoc::LfoCurveData parsed;
    require (D::parse (reordered, parsed).wasOk() && parsed.interpolation == 1, "Documented XML syntax variants rejected");
    for (size_t i = 0; i < parsed.points.size(); ++i)
        require (parsed.points[i] == values[i % values.size()], "Strict decimal grammar numeric value incorrect");
    const auto base = independentCurveXml();
    const auto exactLimit = base + juce::String::repeatedString (" ", D::maximumFileBytes - static_cast<int> (base.getNumBytesAsUTF8()));
    require (D::parse (exactLimit, parsed).wasOk(), "Exact 16 KiB curve was rejected");
}

void testCurveCodecRejections()
{
    using D = anyvoc::LfoCurveDocument;
    const auto sentinel = curveLandmarks();
    const auto reject = [&] (const juce::String& text)
    {
        auto output = sentinel;
        require (D::parse (text, output).failed() && sameCurveBits (output, sentinel), "Bad curve XML accepted or changed output");
    };
    const auto base = independentCurveXml();
    for (const juce::String number : { "NaN", "Inf", "-Infinity", "1.001", "-1.001", "1e999", "1e-999", "0x1p-1",
                                      "1,0", "1suffix", "", " ", " 0", "0 ", "+", ".", "1e", "1e+", "1/2", "&#48;", "&x;" })
        reject (independentCurveXml (number));
    for (const auto& text : std::vector<juce::String> {
            {}, independentCurveXml ("0", 32), independentCurveXml ("0", 34),
            base.replace ("schema=\"1\"", "schema=\"2\""), base.replace ("schema=\"1\"", "schema=\"1.0\""),
            base.replace ("schema=\"1\"", "schema=\"1\" schema=\"1\""), base.replace ("schema=\"1\"", "unknown=\"1\""),
            base.replace ("interpolation=\"0\"", "interpolation=\"2\""), base.replace ("interpolation=\"0\"", "interpolation=\"00\""),
            base.replace ("interpolation=\"0\"", "interpolation=\"-0\""), base.replace ("interpolation=\"0\"", ""),
            base.replace ("ANYVOC_LFO_CURVE", "SERUM_LFO_CURVE"), base.replace ("POINT", "point"),
            base.replace ("value=\"0\"", "value=\"0\" value=\"0\""), base.replace ("value=\"0\"", "value=\"0\" extra=\"0\""),
            base.replace ("value=\"0\"", "other=\"0\""), base.replace ("<POINT", "text<POINT"),
            base.replace ("<POINT value=\"0\"/>", "<POINT value=\"0\"><X/></POINT>"),
            base + "trailing", base + base, "<!--comment-->" + base, "<?other mode='x'?>" + base,
            "<!DOCTYPE ANYVOC_LFO_CURVE SYSTEM 'file:///not-opened'>" + base,
            "<!DOCTYPE ANYVOC_LFO_CURVE [<!ENTITY x '0'>]>" + base,
            "<?xml version=\"1.1\" encoding=\"UTF-8\"?>" + base,
            " \n<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + base,
            juce::String::charToString (0xfeff) + base, base + juce::String::charToString (0x4e00),
            base + juce::String::repeatedString (" ", D::maximumFileBytes + 1 - static_cast<int> (base.getNumBytesAsUTF8())) })
        reject (text);
}

void testCurveFileTransactions()
{
    using D = anyvoc::LfoCurveDocument;
    juce::TemporaryFile sandbox (".anycurve-tests");
    const auto root = sandbox.getFile();
    require (root.createDirectory().wasOk(), "Could not create isolated curve-file sandbox");
    const auto file = root.getChildFile ("Curve.anycurve");
    const auto original = curveLandmarks();
    require (D::saveFile (file, original).wasOk(), "Initial curve save failed");
    anyvoc::LfoCurveData loaded;
    require (D::loadFile (file, loaded).wasOk() && sameCurveBits (loaded, original), "Saved curve was not loaded exactly");
    auto changed = original; changed.interpolation = 1; changed.points[2] = -.875f;
    require (D::saveFile (file, changed).wasOk() && D::loadFile (file, loaded).wasOk()
             && sameCurveBits (changed, loaded), "Replacing an existing curve failed");
    const auto bytes = file.loadFileAsString();
    auto invalid = changed; invalid.points[7] = std::numeric_limits<float>::infinity();
    require (D::saveFile (file, invalid).failed() && file.loadFileAsString() == bytes, "Invalid save changed existing file");
    require (D::saveFile (root, changed).failed() && file.loadFileAsString() == bytes, "Save over a directory changed contents");
    require (D::saveFile (file.getChildFile ("CannotBeChild.anycurve"), changed).failed(), "Non-directory parent save succeeded");
    const auto missingParent = root.getChildFile ("Absent/Curve.anycurve");
    require (D::saveFile (missingParent, changed).failed() && ! missingParent.getParentDirectory().exists(),
             "Save silently created a missing parent directory");
   #if JUCE_WINDOWS
    {
        // JUCE's Windows output stream denies delete sharing: this exercises
        // actual replacement failure after the sibling temp has been written.
        juce::FileOutputStream locked (file);
        require (locked.openedOk(), "Could not lock existing curve file");
        require (D::saveFile (file, original).failed() && file.loadFileAsString() == bytes,
                 "Failed replacement damaged the existing curve");
    }
   #endif
    require (root.findChildFiles (juce::File::findFiles, false).size() == 1, "Curve save leaked a temporary file");
    const auto sentinel = original;
    const auto rejectFile = [&] (const juce::File& bad)
    {
        auto output = sentinel;
        require (D::loadFile (bad, output).failed() && sameCurveBits (output, sentinel), "Bad file changed curve data");
    };
    rejectFile (root); rejectFile (root.getChildFile ("Missing.anycurve"));
    const auto bad = root.getChildFile ("Bad.anycurve");
    require (bad.replaceWithText (independentCurveXml ("0", 32)), "Could not write malformed fixture"); rejectFile (bad);
    require (bad.replaceWithText (juce::String::repeatedString (" ", D::maximumFileBytes + 1)), "Could not write oversized fixture"); rejectFile (bad);
    std::string binary = independentCurveXml().toStdString(); binary.insert (binary.size() / 2, 1, '\0');
    require (bad.replaceWithData (binary.data(), binary.size()), "Could not write embedded-NUL fixture"); rejectFile (bad);
    binary[binary.size() / 2] = static_cast<char> (0xff);
    require (bad.replaceWithData (binary.data(), binary.size()), "Could not write invalid-UTF8 fixture"); rejectFile (bad);
    require (bad.replaceWithText ({}), "Could not write empty fixture"); rejectFile (bad);
    require (file.loadFileAsString() == bytes, "Rejected file operations damaged the valid asset");
}

struct Stats
{
    double rms = 0.0;
    double peak = 0.0;
    double stereoDifference = 0.0;
};

Stats inspect (const juce::AudioBuffer<float>& audio)
{
    Stats result;
    double energy = 0.0, difference = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto value = audio.getSample (channel, sample);
            require (std::isfinite (value), "Audio contains NaN or infinity");
            result.peak = std::max (result.peak, std::abs (static_cast<double> (value)));
            energy += static_cast<double> (value) * value;
        }

    if (audio.getNumChannels() > 1)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto delta = static_cast<double> (audio.getSample (0, sample))
                             - audio.getSample (1, sample);
            difference += delta * delta;
        }

    const auto count = audio.getNumChannels() * audio.getNumSamples();
    result.rms = count > 0 ? std::sqrt (energy / count) : 0.0;
    result.stereoDifference = audio.getNumSamples() > 0
                           ? std::sqrt (difference / audio.getNumSamples()) : 0.0;
    require (result.peak <= 1.001, "Audio exceeded the engine's bounded output range");
    return result;
}

SynthSettings cleanSettings()
{
    SynthSettings settings;
    for (auto& oscillator : settings.osc)
    {
        oscillator.level = 0.0f;
        oscillator.unison = 1;
        oscillator.position = 0.0f;
        oscillator.detune = 0.0f;
    }
    settings.osc[0].level = 0.7f;
    settings.sub = 0.0f;
    settings.noise = 0.0f;
    settings.master = 0.5f;
    settings.cutoff = 18000.0f;
    settings.resonance = 0.0f;
    settings.amp = { 0.002f, 0.03f, 0.8f, 0.07f };
    settings.chorus = 0.0f;
    settings.delayMix = 0.0f;
    settings.reverb = 0.0f;
    return settings;
}

struct Event
{
    int sample;
    juce::MidiMessage message;
};

juce::AudioBuffer<float> render (SynthEngine& engine, int frames, int blockSize,
                                const std::vector<Event>& events = {})
{
    juce::AudioBuffer<float> result (2, frames);
    result.clear();
    for (int start = 0; start < frames; start += blockSize)
    {
        const auto length = std::min (blockSize, frames - start);
        juce::AudioBuffer<float> block (2, length);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& event : events)
            if (event.sample >= start && event.sample < start + length)
                midi.addEvent (event.message, event.sample - start);
        engine.render (block, midi);
        for (int channel = 0; channel < 2; ++channel)
            result.copyFrom (channel, start, block, channel, 0, length);
    }
    inspect (result);
    return result;
}

double estimateFrequency (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    // Measure successive positive zero crossings, excluding the attack.
    std::vector<double> crossings;
    const auto first = audio.getNumSamples() / 4;
    for (int sample = std::max (first, 1); sample < audio.getNumSamples(); ++sample)
    {
        const double previous = audio.getSample (0, sample - 1);
        const double current = audio.getSample (0, sample);
        if (previous <= 0.0 && current > 0.0)
            crossings.push_back (sample - 1.0 - previous / (current - previous));
    }
    require (crossings.size() > 8, "Too few waveform cycles to measure tuning");
    return (crossings.size() - 1) * sampleRate / (crossings.back() - crossings.front());
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    stream->setPosition (0);
    stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (stream.get(), rate,
                                static_cast<unsigned int> (audio.getNumChannels()), 24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void testNoteLifecycle()
{
    SynthEngine engine;
    engine.prepare (48000.0, 512);
    engine.setSettings (cleanSettings());
    require (inspect (render (engine, 2048, 128)).peak < 1.0e-7,
             "An idle engine produced audio");
    auto active = render (engine, 12000, 127,
                          { { 31, juce::MidiMessage::noteOn (1, 69, 0.8f) } });
    require (inspect (active).rms > 0.01, "MIDI note-on did not produce audible audio");
    for (int sample = 0; sample < 31; ++sample)
        require (active.getSample (0, sample) == 0.0f,
                 "A MIDI note started before its sample offset");
    render (engine, 24000, 251, { { 13, juce::MidiMessage::noteOff (1, 69) } });
    require (inspect (render (engine, 4096, 128)).peak < 1.0e-5,
             "MIDI note-off left a stuck voice after the release");
}

void testRatesAndBlockLengths()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        SynthEngine engine;
        engine.prepare (rate, 2048);
        engine.setSettings (cleanSettings());
        for (const auto block : { 1, 7, 64, 127, 512, 2048 })
        {
            engine.reset();
            auto audio = render (engine, static_cast<int> (rate * 0.12), block,
                                 { { 0, juce::MidiMessage::noteOn (1, 60, 0.85f) } });
            require (inspect (audio).rms > 0.01,
                     "Synth went silent at a supported sample rate or block length");
        }
    }
}

void testSustainPedal()
{
    SynthEngine engine;
    engine.prepare (48000.0, 512);
    engine.setSettings (cleanSettings());
    render (engine, 16000, 257,
            { { 0, juce::MidiMessage::noteOn (1, 60, 0.8f) },
              { 1000, juce::MidiMessage::controllerEvent (1, 64, 127) },
              { 6000, juce::MidiMessage::noteOff (1, 60) } });
    require (inspect (render (engine, 6000, 128)).rms > 0.01,
             "Sustain pedal did not hold a released note");
    render (engine, 16000, 128,
            { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
    require (inspect (render (engine, 2048, 128)).peak < 1.0e-5,
             "Releasing the sustain pedal left a stuck note");
}

void testPitchWheel()
{
    SynthEngine engine;
    engine.prepare (48000.0, 512);
    engine.setSettings (cleanSettings());
    const auto normal = render (engine, 16000, 256,
                                { { 0, juce::MidiMessage::noteOn (1, 69, 0.8f) } });
    const auto bent = render (engine, 16000, 256,
                              { { 0, juce::MidiMessage::pitchWheel (1, 16383) } });
    const auto centre = render (engine, 16000, 256,
                                { { 0, juce::MidiMessage::pitchWheel (1, 8192) } });
    const auto baseFrequency = estimateFrequency (normal, 48000.0);
    const auto bentFrequency = estimateFrequency (bent, 48000.0);
    const auto centreFrequency = estimateFrequency (centre, 48000.0);
    require (std::abs (baseFrequency - 440.0) < 3.0, "MIDI A4 is not tuned to 440 Hz");
    require (bentFrequency > baseFrequency * 1.08 && bentFrequency < baseFrequency * 1.3,
             "Pitch wheel did not raise pitch by the expected musical range");
    require (std::abs (centreFrequency - baseFrequency) < 2.0,
             "Pitch wheel centre did not restore the original pitch");
}

void testResetAndPanic()
{
    SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto settings = cleanSettings();
    settings.chorus = 0.3f;
    settings.delayMix = 0.25f;
    settings.reverb = 0.3f;
    engine.setSettings (settings);
    render (engine, 18000, 256,
            { { 0, juce::MidiMessage::noteOn (1, 60, 0.8f) },
              { 10, juce::MidiMessage::noteOn (1, 64, 0.8f) },
              { 20, juce::MidiMessage::noteOn (1, 67, 0.8f) } });
    engine.reset();
    require (inspect (render (engine, 24000, 128)).peak < 1.0e-6,
             "Reset did not clear voices and effect tails");
    engine.setSettings (cleanSettings());
    render (engine, 12000, 128,
            { { 0, juce::MidiMessage::noteOn (1, 72, 0.9f) },
              { 100, juce::MidiMessage::controllerEvent (1, 64, 127) },
              { 1000, juce::MidiMessage::noteOff (1, 72) },
              { 2000, juce::MidiMessage::allSoundOff (1) } });
    require (inspect (render (engine, 4096, 128)).peak < 1.0e-5,
             "All-sound-off did not silence a pedal-held note");
}

void testUnisonStereo()
{
    SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto settings = cleanSettings();
    settings.osc[0].unison = 7;
    settings.osc[0].detune = 22.0f;
    settings.osc[0].position = 0.6f;
    engine.setSettings (settings);
    const auto audio = render (engine, 24000, 128,
                               { { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) } });
    const auto stats = inspect (audio);
    require (stats.rms > 0.01, "Unison oscillator did not produce sound");
    require (stats.stereoDifference > 0.001, "Unison did not produce stereo width");
}

void testWavetableImport()
{
    constexpr int length = 2048;
    juce::AudioBuffer<float> original (1, length * 2);
    for (int sample = 0; sample < length; ++sample)
    {
        original.setSample (0, sample, static_cast<float> (std::sin (2.0 * pi * sample / length)));
        original.setSample (0, sample + length,
                            static_cast<float> (0.7 * std::sin (2.0 * pi * sample / length)
                                              + 0.3 * std::sin (6.0 * pi * sample / length)));
    }
    juce::TemporaryFile temporary (".wav");
    require (writeWav (temporary.getFile(), original, 48000.0), "Could not write test WAV");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (temporary.getFile()));
    require (reader != nullptr && reader->lengthInSamples == length * 2,
             "A valid WAV wavetable could not be decoded");
    juce::AudioBuffer<float> decoded (1, static_cast<int> (reader->lengthInSamples));
    require (reader->read (&decoded, 0, decoded.getNumSamples(), 0, true, false),
             "Could not read the WAV wavetable samples");
    const auto table = SynthEngine::makeWavetable (decoded, length);
    require (table != nullptr && table->frameCount >= 2, "Multi-frame wavetable import failed");
    SynthEngine engine;
    engine.prepare (48000.0, 512);
    auto settings = cleanSettings();
    engine.setUserWavetable (0, table);
    engine.setSettings (settings);
    const auto first = render (engine, 12000, 128,
                               { { 0, juce::MidiMessage::noteOn (1, 69, 0.8f) } });
    require (inspect (first).rms > 0.01, "Imported WAV wavetable was silent");
    require (std::abs (estimateFrequency (first, 48000.0) - 440.0) < 3.0,
             "Imported single-cycle waves do not track musical pitch");
    settings.osc[0].position = 1.0f;
    engine.setSettings (settings);
    require (inspect (render (engine, 12000, 128)).rms > 0.01,
             "The last imported wavetable frame was silent");
    engine.setUserWavetable (0, nullptr);
    require (inspect (render (engine, 12000, 128)).rms > 0.01,
             "Restoring the factory wavetable was silent");

    for (int sample = 0; sample < length; ++sample)
    {
        const auto sine = static_cast<float> (std::sin (2.0 * pi * sample / length));
        original.setSample (0, sample, .3f + .2f * sine);
        original.setSample (0, sample + length, -.2f + 1.7f * sine);
    }
    const auto edited = SynthEngine::makeWavetable (original, length, false);
    require (edited != nullptr, "An amplitude-preserving table could not be created");
    engine.setUserWavetable (0, edited);
    require (std::abs (engine.getWavetableSample (0, .25f, 0) - .2f) < 2.0e-5f
                 && std::abs (engine.getWavetableSample (0, .25f, 1) - 1.7f) < 2.0e-5f
                 && std::abs (engine.getWavetableSample (0, 0, 0)) < 2.0e-5f,
             "Edited table creation changed amplitude or retained a DC offset");
    const auto snapshot = engine.copyWavetableAudio (0);
    require (snapshot.getNumChannels() == 1 && snapshot.getNumSamples() == 2 * length,
             "Editable engine snapshot was not canonical mono audio");
    require (std::abs (snapshot.getSample (0, length / 4) - .2f) < 2.0e-5f
                 && std::abs (snapshot.getSample (0, length + length / 4) - 1.7f) < 2.0e-5f,
             "Editable snapshot changed the table amplitude");
    engine.reset();
    settings.osc[0].position = 1;
    engine.setSettings (settings);
    require (inspect (render (engine, 6000, 127,
        { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } })).rms > .01,
             "An additive waveform above unity did not render bounded audible output");
}

juce::AudioBuffer<float> makeTone (double rate, double seconds, double frequency, int channels = 1)
{
    juce::AudioBuffer<float> audio (channels, static_cast<int> (std::round (rate * seconds)));
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample (channel, sample,
                static_cast<float> (0.7 * std::sin (2.0 * pi * frequency * (channel + 1) * sample / rate)));
    return audio;
}

void prepareSampleEngine (SynthEngine& engine, const SynthSettings& settings,
                          const juce::AudioBuffer<float>& audio, double sourceRate,
                          double outputRate = 48000.0)
{
    const auto sample = SynthEngine::makeSample (audio, sourceRate);
    require (sample != nullptr, "A valid audio sample was rejected");
    engine.setSettings (settings);
    engine.prepare (outputRate, 512);
    engine.setUserSample (0, sample);
}

void testSamplePitchAndRates()
{
    for (const double sourceRate : { 24000.0, 44100.0, 48000.0, 96000.0 })
    {
        SynthEngine engine;
        auto settings = cleanSettings();
        settings.osc[0].mode = 1;
        settings.osc[0].rootNote = 60;
        const auto source = makeTone (sourceRate, 1.0, 440.0);
        prepareSampleEngine (engine, settings, source, sourceRate);
        auto base = render (engine, 12000, 127,
                            { { 0, juce::MidiMessage::noteOn (1, 60, .85f) } });
        require (inspect (base).rms > .01, "Sample playback was silent");
        const auto baseFrequency = estimateFrequency (base, 48000.0);
        require (std::abs (baseFrequency - 440.0) < 3.0,
                 "Sample source-rate conversion changed pitch: " + std::to_string (baseFrequency));
        engine.reset();
        const auto octave = render (engine, 12000, 257,
                                   { { 0, juce::MidiMessage::noteOn (1, 72, .85f) } });
        require (std::abs (estimateFrequency (octave, 48000.0) - 880.0) < 5.0,
                 "Sample root-note tracking did not transpose by one octave");
    }
}

void testSampleEndLoopAndStereo()
{
    SynthEngine engine;
    auto settings = cleanSettings();
    settings.osc[0].mode = 1;
    const auto source = makeTone (24000.0, .1, 220.0, 2);
    prepareSampleEngine (engine, settings, source, 24000.0);
    const auto start = render (engine, 4800, 128,
                              { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (inspect (start).rms > .01, "One-shot sample did not begin playing");
    require (inspect (start).stereoDifference > .001, "Stereo sample was collapsed to mono");
    render (engine, 6000, 128);
    require (inspect (render (engine, 4096, 128)).peak < 1.0e-5,
             "One-shot sample remained audible after its source ended");
    settings.osc[0].sampleLoop = true;
    engine.setSettings (settings);
    engine.reset();
    render (engine, 24000, 128,
            { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (inspect (render (engine, 12000, 127)).rms > .01,
             "Looped sample did not continue beyond its source duration");
    render (engine, 12000, 128, { { 0, juce::MidiMessage::noteOff (1, 60) } });
    require (inspect (render (engine, 4096, 128)).peak < 1.0e-5,
             "Looped sample did not respect MIDI release");
}

void testSamplePositionAndReverse()
{
    auto source = makeTone (48000.0, .5, 440.0);
    for (int sample = source.getNumSamples() / 2; sample < source.getNumSamples(); ++sample)
        source.setSample (0, sample, static_cast<float> (.7 * std::sin (2.0 * pi * 880 * sample / 48000.0)));
    SynthEngine engine;
    auto settings = cleanSettings();
    settings.osc[0].mode = 1;
    prepareSampleEngine (engine, settings, source, 48000.0);
    const auto forward = render (engine, 4800, 128,
                                { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (std::abs (estimateFrequency (forward, 48000) - 440.0) < 4.0,
             "Forward sample did not begin at the source start");
    settings.osc[0].sampleReverse = true;
    engine.setSettings (settings);
    engine.reset();
    const auto reverse = render (engine, 4800, 128,
                                { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (std::abs (estimateFrequency (reverse, 48000) - 880.0) < 5.0,
             "Reverse sample did not begin at the source end");
    settings.osc[0].sampleReverse = false;
    settings.osc[0].position = .75f;
    engine.setSettings (settings);
    engine.reset();
    const auto repositioned = render (engine, 4800, 128,
                                     { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (std::abs (estimateFrequency (repositioned, 48000) - 880.0) < 5.0,
             "Sample start position did not select the requested source region");
}

void testGranularPositionAndRelease()
{
    auto source = makeTone (48000.0, 1.0, 440.0);
    source.clear (0, source.getNumSamples() / 2);
    auto settings = cleanSettings();
    settings.osc[0].mode = 2;
    settings.osc[0].grainSize = .05f;
    settings.osc[0].grainDensity = 40.0f;
    settings.osc[0].grainSpray = 0.0f;
    settings.osc[0].position = .2f;
    SynthEngine engine;
    prepareSampleEngine (engine, settings, source, 48000.0);
    const auto silentRegion = render (engine, 19200, 128,
                                     { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (inspect (silentRegion).peak < 1.0e-5,
             "Granular scan played outside the selected silent region");
    settings.osc[0].position = .8f;
    engine.setSettings (settings);
    render (engine, 12000, 128);
    require (inspect (render (engine, 19200, 127)).rms > .003,
             "Granular scan did not follow position into the audible region");
    render (engine, 12000, 128, { { 0, juce::MidiMessage::noteOff (1, 60) } });
    require (inspect (render (engine, 4096, 128)).peak < 1.0e-5,
             "Granular voice remained audible after note release");
}

void testSampleAndGranularStress()
{
    auto source = makeTone (48000.0, .3, 330.0, 2);
    for (const int mode : { 1, 2 })
        for (const double outputRate : { 44100.0, 96000.0 })
        {
            SynthEngine engine;
            auto settings = cleanSettings();
            settings.osc[0].mode = mode;
            settings.osc[0].sampleLoop = true;
            settings.osc[0].sampleReverse = true;
            settings.osc[0].unison = 7;
            settings.osc[0].grainSize = .5f;
            settings.osc[0].grainDensity = 80;
            settings.osc[0].grainSpray = 1;
            settings.drive = 1;
            settings.resonance = .95f;
            prepareSampleEngine (engine, settings, source, 48000.0, outputRate);
            std::vector<Event> events;
            for (int note = 0; note < 24; ++note)
                events.push_back ({ note, juce::MidiMessage::noteOn (1, 24 + note * 3, .95f) });
            render (engine, 8192, 127, events);
            engine.reset();
            require (inspect (render (engine, 4096, 128)).peak < 1.0e-6,
                     "Reset did not clear sample/granular voices under load");
            engine.setUserSample (0, nullptr);
            require (inspect (render (engine, 4096, 128,
                { { 0, juce::MidiMessage::noteOn (1, 60, .9f) } })).peak < 1.0e-6,
                     "An empty sample oscillator generated unexpected sound");
        }
}

juce::AudioBuffer<float> renderTransport (SynthEngine& engine, int frames, int blockSize,
                                         double bpm, double ppq,
                                         const std::vector<Event>& events = {})
{
    juce::AudioBuffer<float> result (2, frames);
    result.clear();
    for (int start = 0; start < frames; start += blockSize)
    {
        const int length = std::min (blockSize, frames - start);
        engine.setTransport (bpm, ppq + start / 48000.0 * bpm / 60.0, true, true);
        juce::AudioBuffer<float> block (2, length);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& event : events)
            if (event.sample >= start && event.sample < start + length)
                midi.addEvent (event.message, event.sample - start);
        engine.render (block, midi);
        for (int channel = 0; channel < 2; ++channel)
            result.copyFrom (channel, start, block, channel, 0, length);
    }
    inspect (result);
    return result;
}

double channelRms (const juce::AudioBuffer<float>& audio, int channel)
{
    double energy = 0;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const double value = audio.getSample (channel, sample);
        energy += value * value;
    }
    return std::sqrt (energy / audio.getNumSamples());
}

void testCustomLfoAudioRouting()
{
    SynthEngine engine;
    auto settings = cleanSettings();
    settings.lfoShape[0] = 5;
    settings.lfoPoints[0].fill (1.0f);
    settings.routes[0] = { 1, 7, 1.0f }; // A constant custom LFO pans fully right.
    engine.setSettings (settings);
    engine.prepare (48000.0, 512);
    const auto right = render (engine, 6000, 128,
                               { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    require (channelRms (right, 1) > .01 && channelRms (right, 0) < channelRms (right, 1) * .02,
             "Custom LFO point values did not modulate the audio destination");
    settings.lfoPoints[0].fill (-1.0f);
    engine.setSettings (settings);
    render (engine, 3000, 128);
    const auto left = render (engine, 6000, 128);
    require (channelRms (left, 0) > .01 && channelRms (left, 1) < channelRms (left, 0) * .02,
             "Editing the custom LFO did not update the sounding voice");
}

void testLfoInterpolation()
{
    auto settings = cleanSettings();
    settings.lfoShape[0] = 5;
    settings.lfoSync[0] = true;
    settings.lfoDivision[0] = 0; // 16 beats; this window remains near segment midpoint.
    for (size_t point = 0; point < settings.lfoPoints[0].size(); ++point)
        settings.lfoPoints[0][point] = point % 2 == 0 ? -1.0f : 1.0f;
    settings.routes[0] = { 1, 7, 1.0f };
    SynthEngine linear;
    linear.setSettings (settings);
    linear.prepare (48000, 512);
    const auto interpolated = renderTransport (linear, 480, 128, 120, .25,
        { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    settings.lfoInterpolation[0] = 1;
    SynthEngine stepped;
    stepped.setSettings (settings);
    stepped.prepare (48000, 512);
    const auto held = renderTransport (stepped, 480, 128, 120, .25,
        { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    const auto linearRatio = channelRms (interpolated, 1) / channelRms (interpolated, 0);
    require (linearRatio > .8 && linearRatio < 1.5,
             "Linear custom LFO did not interpolate between adjacent points");
    require (channelRms (held, 0) > .01 && channelRms (held, 1) < channelRms (held, 0) * .03,
             "Step custom LFO did not hold the segment's first point");
}

void testLfoTriggerAndOneShot()
{
    auto settings = cleanSettings();
    settings.lfoSync[0] = true;
    settings.lfoDivision[0] = 4;
    settings.lfoMode[0] = 1;
    SynthEngine triggered;
    triggered.setSettings (settings);
    triggered.prepare (48000, 512);
    renderTransport (triggered, 2400, 128, 120, 3.65);
    require (std::abs (triggered.getLfoPhase (0)) < .0001f,
             "Idle Trigger LFO preview moved before any note was played");
    renderTransport (triggered, 4800, 127, 120, 3.75,
                     { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (std::abs (triggered.getLfoPhase (0) - .2f) < .001f,
             "Trigger LFO did not start at zero independently of host position");
    renderTransport (triggered, 128, 128, 120, 3.95,
                     { { 64, juce::MidiMessage::noteOn (1, 64, .8f) } });
    require (std::abs (triggered.getLfoPhase (0) - 64.0f / 24000.0f) < .0002f,
             "A new note did not restart the displayed per-note LFO at its sample offset");
    renderTransport (triggered, 6000, 128, 120, 3.9553333,
                     { { 0, juce::MidiMessage::noteOff (1, 60) },
                       { 0, juce::MidiMessage::noteOff (1, 64) } });
    const auto releasedTriggerPhase = triggered.getLfoPhase (0);
    renderTransport (triggered, 6000, 127, 120, 4.2053333);
    require (std::abs (triggered.getLfoPhase (0) - releasedTriggerPhase) < .0001f,
             "Trigger LFO preview moved after the last voice ended");
    triggered.reset();
    require (std::abs (triggered.getLfoPhase (0)) < .0001f,
             "Reset did not clear the Trigger LFO preview phase");

    settings.lfoMode[0] = 2;
    settings.lfoShape[0] = 5;
    for (size_t point = 0; point < settings.lfoPoints[0].size(); ++point)
        settings.lfoPoints[0][point] = -1.0f + static_cast<float> (point) / 16.0f;
    settings.routes[0] = { 1, 7, 1.0f };
    SynthEngine envelope;
    envelope.setSettings (settings);
    envelope.prepare (48000, 512);
    renderTransport (envelope, 2400, 128, 120, -.1);
    require (std::abs (envelope.getLfoPhase (0)) < .0001f,
             "Idle Envelope LFO preview moved before any note was played");
    renderTransport (envelope, 30000, 127, 120, 0,
                     { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (std::abs (envelope.getLfoPhase (0) - 1.0f) < .0001f,
             "Envelope LFO did not stop after a single cycle");
    const auto endpoint = renderTransport (envelope, 6000, 128, 120, 1.25);
    require (channelRms (endpoint, 1) > .01 && channelRms (endpoint, 0) < channelRms (endpoint, 1) * .02,
             "Envelope LFO did not hold custom point 32 after completion");
    renderTransport (envelope, 4800, 127, 120, 1.5,
                     { { 0, juce::MidiMessage::noteOn (1, 67, .8f) } });
    require (std::abs (envelope.getLfoPhase (0) - .2f) < .001f,
             "A new note did not restart the completed envelope LFO");
    renderTransport (envelope, 6000, 128, 120, 1.7,
                     { { 0, juce::MidiMessage::noteOff (1, 60) },
                       { 0, juce::MidiMessage::noteOff (1, 67) } });
    const auto releasedEnvelopePhase = envelope.getLfoPhase (0);
    renderTransport (envelope, 6000, 127, 120, 1.95);
    require (std::abs (envelope.getLfoPhase (0) - releasedEnvelopePhase) < .0001f,
             "Envelope LFO preview moved after the last voice ended");
    envelope.reset();
    require (std::abs (envelope.getLfoPhase (0)) < .0001f,
             "Reset did not clear the Envelope LFO preview phase");

    auto freeSettings = cleanSettings();
    freeSettings.lfoRate[0] = 2.0f;
    SynthEngine free;
    free.setSettings (freeSettings);
    free.prepare (48000, 512);
    render (free, 4800, 128);
    const auto idleFreePhase = free.getLfoPhase (0);
    render (free, 2400, 128);
    require (idleFreePhase > .19f && free.getLfoPhase (0) > idleFreePhase + .09f,
             "Free LFO stopped advancing while the instrument was idle");
    free.reset();
    require (std::abs (free.getLfoPhase (0)) < .0001f,
             "Reset did not clear the Free LFO phase");
}

void testHostLfoTempoSeekAndPartition()
{
    auto settings = cleanSettings();
    settings.lfoSync[0] = true;
    settings.lfoDivision[0] = 4;
    settings.lfoSync[1] = true;
    settings.lfoDivision[1] = 8; // One third of a beat.
    settings.routes[0] = { 1, 7, .7f };
    SynthEngine engine;
    engine.setSettings (settings);
    engine.prepare (48000, 512);
    renderTransport (engine, 4800, 127, 120, 0,
                     { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    require (std::abs (engine.getLfoPhase (0) - .2f) < .001f,
             "Beat-synced LFO did not follow host BPM");
    require (std::abs (engine.getLfoPhase (1) - .6f) < .002f,
             "Triplet LFO division did not follow the musical interval");
    renderTransport (engine, 2400, 127, 60, 8.75);
    require (std::abs (engine.getLfoPhase (0) - .8f) < .001f,
             "LFO phase did not follow a transport seek and tempo change");
    renderTransport (engine, 2400, 127, 60, -.25);
    require (std::abs (engine.getLfoPhase (0) - .8f) < .001f,
             "Negative preroll PPQ produced an invalid LFO phase");
    SynthEngine first, second;
    first.setSettings (settings); second.setSettings (settings);
    first.prepare (48000, 512); second.prepare (48000, 512);
    const std::vector<Event> events { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } };
    const auto a = renderTransport (first, 12345, 127, 137, .375, events);
    const auto b = renderTransport (second, 12345, 257, 137, .375, events);
    double largest = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < a.getNumSamples(); ++sample)
            largest = std::max (largest, static_cast<double> (
                std::abs (a.getSample (channel, sample) - b.getSample (channel, sample))));
    require (largest < .0001,
             "Host-synced LFO audio depended on buffer partition; delta=" + std::to_string (largest));
    require (std::abs (first.getLfoPhase (0) - second.getLfoPhase (0)) < .0001f,
             "Host-synced phase depended on buffer partition");
}

SynthSettings filterTestSettings()
{
    auto settings = cleanSettings();
    settings.osc[0].level = .08f;
    settings.amp = { .001f, .01f, 1.0f, .03f };
    settings.cutoff = 500;
    settings.resonance = 0;
    settings.filter2Cutoff = 500;
    settings.filter2Resonance = 0;
    return settings;
}

juce::AudioBuffer<float> steadyFilterAudio (const SynthSettings& settings, int note = 69)
{
    SynthEngine engine;
    engine.setSettings (settings);
    engine.prepare (48000, 512);
    render (engine, 4096, 127, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });
    return render (engine, 8192, 257);
}

double audioDifference (const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
    require (first.getNumChannels() == second.getNumChannels()
                 && first.getNumSamples() == second.getNumSamples(), "Audio comparison dimensions differ");
    double difference = 0;
    for (int channel = 0; channel < first.getNumChannels(); ++channel)
        for (int sample = 0; sample < first.getNumSamples(); ++sample)
            difference = std::max (difference, static_cast<double> (
                std::abs (first.getSample (channel, sample) - second.getSample (channel, sample))));
    return difference;
}

void testDualFilterResponseAndSlope()
{
    auto settings = filterTestSettings();
    settings.filterEnabled = false;
    settings.filter2Enabled = false;
    const auto highDry = inspect (steadyFilterAudio (settings, 93)).rms;
    const auto lowDry = inspect (steadyFilterAudio (settings, 45)).rms;
    require (highDry > .001 && lowDry > .001, "Filter response fixture was silent");
    for (const int filter : { 0, 1 })
    {
        settings.filterEnabled = filter == 0;
        settings.filter2Enabled = filter == 1;
        settings.filterRoutes[0] = filter == 0 ? 1 : 2;
        settings.filterType = settings.filter2Type = 0;
        settings.filterSlope = settings.filter2Slope = 0;
        const auto twelve = inspect (steadyFilterAudio (settings, 93)).rms;
        require (twelve < highDry * .15, "Low-pass filter did not attenuate a tone above cutoff");
        settings.filterSlope = settings.filter2Slope = 1;
        const auto twentyFour = inspect (steadyFilterAudio (settings, 93)).rms;
        require (twentyFour < twelve * .2, "24 dB slope did not add the expected stop-band attenuation");
        settings.filterSlope = settings.filter2Slope = 0;
        settings.filterType = settings.filter2Type = 1;
        require (inspect (steadyFilterAudio (settings, 45)).rms < lowDry * .1,
                 "High-pass filter did not attenuate a tone below cutoff");
        settings.filterType = settings.filter2Type = 2;
        settings.cutoff = settings.filter2Cutoff = 440;
        const auto centre = inspect (steadyFilterAudio (settings, 69)).rms;
        const auto outside = inspect (steadyFilterAudio (settings, 45)).rms;
        require (centre > outside * 1.6, "Band-pass filter did not favour its centre frequency");
        settings.cutoff = settings.filter2Cutoff = 500;
    }
    settings = filterTestSettings();
    const auto single = steadyFilterAudio (settings, 93);
    settings.filter2Enabled = true;
    const auto serial = steadyFilterAudio (settings, 93);
    require (inspect (serial).rms < inspect (single).rms * .2,
             "Serial routing did not process the Both bus through both filters");
    settings.filterRouting = 1;
    require (audioDifference (serial, steadyFilterAudio (settings, 93)) < 1.0e-5,
             "Reversing equal serial filters changed their steady-state result");
    settings.filterRouting = 2;
    settings.filterBalance = .37f;
    require (audioDifference (single, steadyFilterAudio (settings, 93)) < 1.0e-5,
             "Parallel equal filters behaved like a serial cascade or changed gain");
}

void testFilterRoutingAndParallelBalance()
{
    auto settings = filterTestSettings();
    settings.filter2Enabled = true;
    settings.filter2Type = 1;
    settings.filter2Cutoff = 1000;
    settings.filterRouting = 2;
    settings.filterBalance = 0;
    const auto a = steadyFilterAudio (settings);
    settings.filterBalance = 1;
    const auto b = steadyFilterAudio (settings);
    settings.filterBalance = .3f;
    const auto mixed = steadyFilterAudio (settings);
    auto expected = a;
    expected.applyGain (.7f);
    for (int channel = 0; channel < 2; ++channel)
        expected.addFrom (channel, 0, b, channel, 0, b.getNumSamples(), .3f);
    require (audioDifference (mixed, expected) < 1.0e-5,
             "Parallel balance did not crossfade the two filter outputs");
    settings.filterRoutes[0] = 3;
    const auto bypass = steadyFilterAudio (settings);
    settings.filterBalance = 0;
    require (audioDifference (bypass, steadyFilterAudio (settings)) < 1.0e-6,
             "Parallel balance attenuated the bypass bus");
    settings.filterEnabled = settings.filter2Enabled = false;
    settings.filterRoutes[0] = 0;
    require (audioDifference (bypass, steadyFilterAudio (settings)) < 1.0e-6,
             "A disabled filter did not act as an identity stage");

    // Every oscillator, sub and noise route is checked separately. In parallel,
    // the unused branch must contribute nothing for an A-only or B-only source.
    for (int source = 0; source < 5; ++source)
    {
        settings = filterTestSettings();
        for (auto& oscillator : settings.osc) oscillator.level = 0;
        if (source < 3) settings.osc[static_cast<size_t> (source)].level = .06f;
        else if (source == 3) settings.sub = .06f;
        else settings.noise = .06f;
        settings.filter2Enabled = true;
        settings.filterRouting = 2;
        settings.filterRoutes[static_cast<size_t> (source)] = 1;
        settings.filterBalance = 1;
        require (inspect (steadyFilterAudio (settings)).peak < 1.0e-7,
                 "A-only source leaked into the B branch");
        settings.filterRoutes[static_cast<size_t> (source)] = 2;
        settings.filterBalance = 0;
        require (inspect (steadyFilterAudio (settings)).peak < 1.0e-7,
                 "B-only source leaked into the A branch");
        settings.filterRoutes[static_cast<size_t> (source)] = 3;
        const auto dry = steadyFilterAudio (settings);
        require (inspect (dry).rms > .0005, "A routed source was silent on the bypass bus");
        settings.filterRouting = 0;
        require (audioDifference (dry, steadyFilterAudio (settings)) < 1.0e-6,
                 "Changing filter topology processed the bypass bus");
    }
}

void testFilterKeyTrackingAndModulation()
{
    for (const int filter : { 0, 1 })
    {
        auto settings = filterTestSettings();
        settings.filterEnabled = filter == 0;
        settings.filter2Enabled = filter == 1;
        settings.filterRoutes[0] = filter == 0 ? 1 : 2;
        settings.cutoff = settings.filter2Cutoff = 400;
        const auto untrackedOctave = inspect (steadyFilterAudio (settings, 72)).rms;
        settings.filterKeyTrack = settings.filter2KeyTrack = 1;
        const auto reference = inspect (steadyFilterAudio (settings, 60)).rms;
        const auto trackedOctave = inspect (steadyFilterAudio (settings, 72)).rms;
        require (trackedOctave > untrackedOctave * 1.5,
                 "Filter key tracking did not raise cutoff with the played note");
        require (trackedOctave / reference > .9 && trackedOctave / reference < 1.1,
                 "Full key tracking did not preserve timbre across an octave");
        settings.filterKeyTrack = settings.filter2KeyTrack = 0;
        settings.cutoff = settings.filter2Cutoff = 440;
        settings.resonance = settings.filter2Resonance = .05f;
        const auto plain = inspect (steadyFilterAudio (settings)).rms;
        settings.macros[0] = 1;
        settings.routes[0] = { 6, filter == 0 ? 9 : 10, .7f };
        require (inspect (steadyFilterAudio (settings)).rms > plain * 2,
                 "New resonance modulation target did not affect the selected filter");
    }
    auto settings = filterTestSettings();
    settings.filterEnabled = false;
    settings.filter2Enabled = true;
    settings.filterRoutes[0] = 2;
    settings.filter2Cutoff = 100;
    const auto closed = inspect (steadyFilterAudio (settings)).rms;
    settings.macros[0] = 1;
    settings.routes[0] = { 6, 8, .4f };
    require (inspect (steadyFilterAudio (settings)).rms > closed * 3,
             "Filter B cutoff modulation did not open its pass band");
    settings = filterTestSettings();
    settings.filter2Enabled = true;
    settings.filter2Type = 1;
    settings.filterRouting = 2;
    settings.filterBalance = 1;
    const auto fullB = steadyFilterAudio (settings);
    settings.filterBalance = 0;
    settings.macros[0] = 1;
    settings.routes[0] = { 6, 11, 1 };
    require (audioDifference (fullB, steadyFilterAudio (settings)) < 1.0e-6,
             "Balance modulation did not reach the same endpoint as its parameter");
}

void testFilterAutomationAndReset()
{
    SynthEngine engine;
    auto settings = filterTestSettings();
    settings.osc[0].position = .66f;
    settings.filter2Enabled = true;
    settings.resonance = settings.filter2Resonance = .95f;
    engine.setSettings (settings);
    engine.prepare (48000, 512);
    render (engine, 2048, 127, { { 0, juce::MidiMessage::noteOn (1, 48, .9f) },
                               { 7, juce::MidiMessage::noteOn (1, 72, .9f) } });
    for (int change = 0; change < 36; ++change)
    {
        settings.filterRouting = change % 3;
        settings.filterType = (change / 2) % 3;
        settings.filter2Type = (change / 3) % 3;
        settings.filterEnabled = change % 4 != 0;
        settings.filter2Enabled = change % 5 != 0;
        settings.filterSlope = change % 2;
        settings.filter2Slope = (change / 2) % 2;
        settings.cutoff = change % 2 ? 20.0f : 22000.0f;
        settings.filter2Cutoff = change % 2 ? 22000.0f : 20.0f;
        settings.filterBalance = change % 2 ? 0.0f : 1.0f;
        for (size_t source = 0; source < settings.filterRoutes.size(); ++source)
            settings.filterRoutes[source] = (change + static_cast<int> (source)) % 4;
        engine.setSettings (settings);
        render (engine, change % 2 ? 31 : 127, 31); // Faster than a topology crossfade.
    }
    render (engine, 4096, 127);
    engine.reset();
    require (inspect (render (engine, 8192, 127)).peak < 1.0e-7,
             "Reset left dual-filter resonance or transition history audible");

    SynthEngine dormant;
    settings = filterTestSettings();
    settings.osc[0].level = .4f;
    settings.filterType = 2;
    settings.cutoff = 440;
    settings.resonance = .95f;
    settings.filterRoutes[0] = 1;
    dormant.setSettings (settings);
    dormant.prepare (48000, 512);
    require (inspect (render (dormant, 3000, 127,
        { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } })).rms > .005,
             "Dormant-route regression did not excite the resonant filter");
    settings.osc[0].level = 0;
    settings.filterRoutes[0] = 2;
    dormant.setSettings (settings);
    render (dormant, 12000, 127);
    settings.filterRoutes[0] = 1;
    dormant.setSettings (settings);
    require (inspect (render (dormant, 4096, 127)).peak < 1.0e-6,
             "Reactivating a silent source route resurrected a frozen filter tail");
}

void testWarpPreviewLandmarksAndOrder()
{
    SynthEngine engine;
    juce::AudioBuffer<float> sine (1, 2048);
    for (int sample = 0; sample < 2048; ++sample)
        sine.setSample (0, sample, static_cast<float> (.7 * std::sin (2 * pi * sample / 2048)));
    engine.setUserWavetable (0, SynthEngine::makeWavetable (sine, 2048, false));
    const auto warped = [&] (float phase, int first, float firstAmount, int second = 0, float secondAmount = 0)
    {
        return engine.getWarpedWavetableSample (0, phase, 0, { first, second }, { firstAmount, secondAmount });
    };
    // These analytically selected landmarks test the transformations without
    // duplicating their implementation as a second general-purpose warp engine.
    require (std::abs (warped (.5f, 1, 1) - .7 * std::sin (.4 * pi)) < 2.0e-5,
             "Bend+ did not map the midpoint to one fifth of a cycle");
    require (std::abs (warped (.5f, 2, 1) - .7 * std::sin (1.6 * pi)) < 2.0e-5,
             "Bend- did not map the midpoint to four fifths of a cycle");
    require (std::abs (warped (.025f, 3, 1) - .7f) < 2.0e-5f,
             "Pulse-width warp did not compress the first half-cycle to 5 percent");
    require (std::abs (warped (.03125f, 4, 1) - .7f) < 2.0e-5f,
             "Full Sync warp did not create eight source cycles");
    require (std::abs (warped (.125f, 5, 1) - .7f) < 2.0e-5f
                 && std::abs (warped (.875f, 5, 1) - .7f) < 2.0e-5f,
             "Mirror warp did not traverse the source forward and backward");
    require (std::abs (warped (.25f, 6, 1) + .5f) < 2.0e-5f
                 && std::abs (warped (.25f, 6, .5f) - .3f) < 2.0e-5f,
             "Fold did not use its specified triangular fold and dry/wet amount");

    const auto firstThenSecond = warped (.25f, 1, 1, 3, 1);
    const auto reversed = warped (.25f, 3, 1, 1, 1);
    require (std::abs (firstThenSecond - engine.getWavetableSample (0, 23.0f / 83, 0)) < 2.0e-5f,
             "The two warp slots did not process slot 1 followed by slot 2");
    require (std::abs (reversed - engine.getWavetableSample (0, 127.0f / 247, 0)) < 2.0e-5f
                 && std::abs (firstThenSecond - reversed) > .5f,
             "Swapping noncommuting warp stages did not change their order");
    for (int mode = 0; mode <= 6; ++mode)
        for (int point = 0; point < 128; ++point)
        {
            const auto phase = point / 128.0f;
            const auto raw = engine.getWavetableSample (0, phase, 0);
            require (warped (phase, mode, 0, 6 - mode, 0) == raw,
                     "Zero warp amount did not leave the raw preview exactly unchanged");
            require (std::isfinite (warped (phase, mode, 1, 6 - mode, 1)),
                     "A full-depth dual warp generated a non-finite preview");
        }
    require (warped (.125f, 6, std::numeric_limits<float>::quiet_NaN())
                 == engine.getWavetableSample (0, .125f, 0), "Non-finite warp amount did not fall back to identity");
    require (std::abs (engine.getWavetableSample (0, .25f, 0) - .7f) < 2.0e-5f,
             "Previewing warps changed the underlying editable wavetable");
}

juce::AudioBuffer<float> renderWarpFixture (const SynthSettings& settings, int blockSize = 127)
{
    SynthEngine engine;
    engine.setSettings (settings);
    engine.prepare (48000, 512);
    return render (engine, 8192, blockSize, { { 37, juce::MidiMessage::noteOn (1, 60, .8f) } });
}

void testModulationSourceTransformsAndRanges()
{
    const auto check = [] (float actual, float expected, const char* message)
    {
        require (std::isfinite (actual) && std::abs (actual - expected) < 1.0e-7f, message);
    };
    using anyvoc::transformModulationSource;
    for (int source = 1; source < static_cast<int> (anyvoc::modulationSourceCount); ++source)
    {
        const bool bipolar = source <= 2 || (source >= 10 && source <= 17);
        for (const float raw : { -1.0f, -.5f, 0.0f, .25f, .5f, 1.0f })
        {
            const float native = bipolar ? raw : std::max (0.0f, raw);
            require (transformModulationSource (native, source, 0, 0) == native,
                     "A default source transform changed legacy source values");
            const float unipolar = bipolar ? (native + 1.0f) * .5f : native;
            const float signedValue = bipolar ? native : native * 2.0f - 1.0f;
            check (transformModulationSource (raw, source, 1, 0), unipolar,
                   "Unipolar conversion used the wrong native source domain");
            check (transformModulationSource (raw, source, 2, 0), signedValue,
                   "Bipolar conversion used the wrong native source domain");
            check (transformModulationSource (raw, source, 1, .25f), unipolar * unipolar,
                   "Positive curve did not square a unipolar value");
            check (transformModulationSource (raw, source, 2, .25f), signedValue * std::abs (signedValue),
                   "Positive bipolar curve did not retain sign");
            check (transformModulationSource (raw, source, 1, -.25f), std::sqrt (unipolar),
                   "Negative curve did not take a unipolar square root");
            check (transformModulationSource (raw, source, 2, -.25f),
                   std::copysign (std::sqrt (std::abs (signedValue)), signedValue),
                   "Negative bipolar curve did not retain sign");
        }
    }
    check (transformModulationSource (-5, 1, 1, 0), 0, "Raw LFO was not clamped before conversion");
    check (transformModulationSource (5, 6, 2, 0), 1, "Raw macro was not clamped before conversion");
    check (transformModulationSource (1, 0, 2, -1), 0, "Off source generated bipolar DC");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    check (transformModulationSource (nan, 1, 0, 0), 0, "Non-finite raw input was not neutralised");
    check (transformModulationSource (.5f, 6, 0, nan), .5f, "Non-finite curve was not bypassed");
    check (transformModulationSource (.5f, 6, 0, 5), 1.0f / 65536,
           "Positive curve clamp/exponent is incorrect");
    check (transformModulationSource (.5f, 6, 0, -5), std::pow (.5f, 1.0f / 16),
           "Negative curve clamp/exponent is incorrect");
    anyvoc::ModRoute bad { -1, 99, inf, false, -1, 999, 999, nan, inf };
    const auto safe = anyvoc::sanitiseModRoute (bad);
    require (safe.source == 0 && safe.destination == 35 && safe.amount == 0 && ! safe.enabled
                 && safe.polarity == 0 && safe.auxSource == 24 && safe.auxPolarity == 2
                 && safe.curve == 0 && safe.auxCurve == 0,
             "Invalid route fields were not sanitised deterministically");

    for (int source = 1; source < static_cast<int> (anyvoc::modulationSourceCount); ++source)
        for (int destination : { 1, 7, 8, 17 })
            for (float amount : { -.7f, .7f })
            {
                const float base = destination == 1 || destination == 8 ? 1000.0f : .5f;
                anyvoc::ModRoute route { source, destination, amount };
                require (anyvoc::modulationRange (base, route)
                             == anyvoc::modulationRange (destination, base, source, amount),
                         "Default advanced excursion changed a legacy ring endpoint");
            }
    anyvoc::ModRoute range { 6, 7, -.5f, true, 0, 1, 1, .9f, -.9f };
    require (anyvoc::modulationRange (.25f, range) == std::pair<float, float> { -.25f, .25f },
             "Unipolar auxiliary/negative amount ring interval is incorrect");
    range.auxPolarity = 2;
    require (anyvoc::modulationRange (.25f, range) == std::pair<float, float> { -.25f, .75f },
             "Bipolar auxiliary ring did not include both directions");
    range.polarity = 2;
    range.auxSource = 0;
    require (anyvoc::modulationRange (.25f, range) == std::pair<float, float> { -.25f, .75f },
             "Auxiliary Off did not retain the primary's whole interval");
    range.enabled = false;
    require (anyvoc::modulationRange (.25f, range) == std::pair<float, float> { .25f, .25f },
             "Bypassed route still showed nominal excursion");
}

void testAdvancedModulationAudioAndAllSlots()
{
    auto base = cleanSettings();
    base.filterEnabled = false;
    base.macros[0] = .25f;
    base.macros[1] = .5f;
    base.lfoShape[0] = 5;
    base.lfoPoints[0].fill (-.5f);
    struct Scenario { int source, polarity, auxSource, auxPolarity; float curve, auxCurve, expected; };
    const Scenario scenarios[] {
        { 6, 0, 7, 0, 0, 0, .125f },
        { 6, 0, 7, 0, .25f, .25f, .015625f },
        { 6, 2, 7, 0, .25f, .25f, -.0625f },
        { 1, 1, 7, 0, .25f, -.25f, .0625f * std::sqrt (.5f) },
        { 1, 2, 6, 2, .25f, .25f, .0625f },
        { 1, 2, 0, 2, -.25f, 1, -std::sqrt (.5f) },
        { 6, 2, 7, 2, 0, -.25f, 0 },
    };
    for (const auto& scenario : scenarios)
    {
        auto routed = base, direct = base;
        routed.routes.back() = { scenario.source, 7, .8f, true, scenario.polarity,
                                scenario.auxSource, scenario.auxPolarity, scenario.curve, scenario.auxCurve };
        direct.osc[0].pan = scenario.expected * .8f;
        require (audioDifference (renderWarpFixture (routed), renderWarpFixture (direct)) < 1.0e-7,
                 "Advanced route product did not reach the audible pan destination");
    }

    auto single = base;
    single.routes[0] = { 6, 7, .75f, true, 2, 7, 0, .25f, -.25f };
    const auto expected = renderWarpFixture (single);
    for (size_t index = 0; index < anyvoc::modulationRouteCount; ++index)
    {
        auto relocated = base;
        relocated.routes[index] = single.routes[0];
        require (audioDifference (expected, renderWarpFixture (relocated)) == 0,
                 "Modulation slot " + std::to_string (index + 1) + " was ignored or altered the source product");
    }
    auto stacked = base, direct = base;
    for (auto& route : stacked.routes) route = { 7, 7, 1.0f / 32 };
    direct.osc[0].pan = .5f;
    require (audioDifference (renderWarpFixture (stacked), renderWarpFixture (direct)) == 0,
             "All 32 routes did not accumulate into the destination");

    // Independent audio oracle: shape stepped points offline into the OTHER
    // LFO, then use a legacy route. Staggered note-triggered voices exercise both
    // equal-input cache hits and per-voice/input misses without using the helper.
    SynthEngine shapedEngine, referenceEngine;
    shapedEngine.prepare (48000, 512); referenceEngine.prepare (48000, 512);
    auto shaped = base;
    shaped.macros[1] = .6f;
    shaped.lfoShape = { 5, 5 };
    shaped.lfoMode = { 1, 1 };
    shaped.lfoInterpolation = { 1, 1 };
    shaped.lfoRate = { 3.2f, 3.2f };
    for (size_t point = 0; point < 33; ++point)
        shaped.lfoPoints[0][point] = shaped.lfoPoints[1][point]
            = static_cast<float> (static_cast<int> (point % 9) - 4) / 4;
    for (int stage = 0; stage < 5; ++stage)
    {
        shaped.routes = {};
        const size_t slot = stage == 0 || stage == 4 ? 31 : 0;
        const int source = stage == 0 || stage == 4 ? 1 : 2;
        auto& route = shaped.routes[slot];
        route = stage == 1 ? anyvoc::ModRoute { source, 7, .7f, true, 2, 7, 0, -.25f, .25f }
                           : anyvoc::ModRoute { source, 7, .7f, stage != 2, 1, 6, 2, .25f, -.25f };
        auto reference = shaped;
        const size_t referenceLfo = source == 1 ? 1 : 0;
        for (size_t point = 0; point < 33; ++point)
        {
            const float raw = shaped.lfoPoints[static_cast<size_t> (source - 1)][point];
            const float primary = stage == 1 ? std::copysign (std::sqrt (std::abs (raw)), raw)
                                             : ((raw + 1) * .5f) * ((raw + 1) * .5f);
            const float auxiliary = stage == 1 ? .6f * .6f : -std::sqrt (.5f);
            reference.lfoPoints[referenceLfo][point] = primary * auxiliary;
        }
        reference.routes[slot] = { static_cast<int> (referenceLfo + 1), 7, .7f, stage != 2 };
        shapedEngine.setSettings (shaped); referenceEngine.setSettings (reference);
        std::vector<Event> events;
        if (stage == 0)
            for (int voice = 0; voice < 16; ++voice)
                events.push_back ({ 19 + voice * 53, juce::MidiMessage::noteOn (1, 48 + voice, .4f + .03f * voice) });
        require (audioDifference (render (shapedEngine, 8192, 127, events),
                                  render (referenceEngine, 8192, 257, events)) < 2.0e-7,
                 "Cached source transform diverged from the independent stepped-curve audio oracle at stage "
                     + std::to_string (stage));
    }
}

void testAdvancedModulationIdentityAndInvalids()
{
    auto legacy = cleanSettings();
    legacy.osc[0].position = .37f;
    legacy.osc[0].unison = 3;
    legacy.macros = { .2f, .4f, .6f, .8f };
    legacy.routes[0] = { 1, 2, .1f };
    legacy.routes[1] = { 2, 5, .2f };
    legacy.routes[2] = { 3, 1, .3f };
    legacy.routes[3] = { 4, 6, -.1f };
    legacy.routes[4] = { 6, 7, .3f };
    legacy.routes[5] = { 7, 3, .2f };
    legacy.routes[6] = { 8, 9, .1f };
    legacy.routes[7] = { 9, 12, .4f };
    const auto original = renderWarpFixture (legacy);
    auto identity = legacy;
    for (auto& route : identity.routes)
    {
        route.auxPolarity = 2;
        route.auxCurve = .8f; // These have no meaning while auxiliary is Off.
    }
    require (audioDifference (original, renderWarpFixture (identity)) == 0,
             "Unused auxiliary settings changed legacy audio");
    for (size_t index = 8; index < anyvoc::modulationRouteCount; ++index)
        identity.routes[index] = { 1, 7, .9f, false, 2, 7, 2, 1, -1 };
    require (audioDifference (original, renderWarpFixture (identity)) == 0,
             "Bypassed advanced routes were not exact audio identity");
    for (size_t index = 8; index < anyvoc::modulationRouteCount; ++index)
        identity.routes[index] = { 6, 7, 0, true, 2, 1, 1, -1, 1 };
    require (audioDifference (original, renderWarpFixture (identity)) == 0,
             "Zero-amount advanced routes were not exact audio identity");
    for (size_t index = 8; index < anyvoc::modulationRouteCount; ++index)
        identity.routes[index] = { 0, 7, 1, true, 2, 1, 2, -1, 1 };
    require (audioDifference (original, renderWarpFixture (identity)) == 0,
             "Primary Off generated a nonzero bipolar route");
    for (size_t index = 8; index < anyvoc::modulationRouteCount; ++index)
        identity.routes[index] = { 6, 0, 1, true, 2, 1, 2, -1, 1 };
    require (audioDifference (original, renderWarpFixture (identity)) == 0,
             "Destination Off changed audio");

    auto invalid = cleanSettings(), valid = invalid;
    invalid.macros[7] = valid.macros[7] = .2f;
    invalid.osc[2].level = valid.osc[2].level = .2f;
    invalid.osc[2].warpMode[1] = valid.osc[2].warpMode[1] = 6;
    invalid.routes.back() = { 99, 99, 5, true, 99, 99, -5, 5, -5 };
    valid.routes.back() = { 24, 35, 1, true, 2, 24, 0, 1, -1 };
    require (audioDifference (renderWarpFixture (invalid), renderWarpFixture (valid)) == 0,
             "Engine did not clamp all advanced route settings");
    invalid = valid;
    invalid.routes.back().amount = std::numeric_limits<float>::infinity();
    invalid.routes.back().curve = std::numeric_limits<float>::quiet_NaN();
    invalid.routes.back().auxCurve = -std::numeric_limits<float>::infinity();
    valid.routes.back().amount = valid.routes.back().curve = valid.routes.back().auxCurve = 0;
    require (audioDifference (renderWarpFixture (invalid), renderWarpFixture (valid)) == 0,
             "Non-finite route controls propagated into audio");
}

void testAdvancedModulationPartitionAndSmoothing()
{
    auto settings = cleanSettings();
    settings.osc[0].position = .5f;
    settings.osc[0].warpMode[0] = 1;
    settings.macros = { .4f, .7f, .6f, .8f };
    for (size_t index = 0; index < anyvoc::modulationRouteCount; ++index)
        settings.routes[index] = { 1 + static_cast<int> (index % 9), 1 + static_cast<int> (index % 17),
                                  (index % 2 == 0 ? .025f : -.025f), true, static_cast<int> (index % 3),
                                  static_cast<int> (index % 10), static_cast<int> ((index + 1) % 3),
                                  index % 2 == 0 ? .4f : -.4f, index % 3 == 0 ? -.3f : .3f };
    require (audioDifference (renderWarpFixture (settings, 1), renderWarpFixture (settings, 257)) == 0,
             "Advanced matrix rendering depended on block partition");

    auto one = cleanSettings(), last = one;
    one.macros[0] = last.macros[0] = .5f;
    one.routes[0] = last.routes.back() = { 6, 7, -.7f, true, 0, 0, 0, .25f, 0 };
    SynthEngine first, thirtySecond;
    first.setSettings (one); thirtySecond.setSettings (last);
    first.prepare (48000, 512); thirtySecond.prepare (48000, 512);
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    render (first, 2048, 127, note); render (thirtySecond, 2048, 127, note);
    one.routes[0].amount = last.routes.back().amount = .7f;
    first.setSettings (one); thirtySecond.setSettings (last);
    const auto moving = render (first, 4096, 127);
    require (audioDifference (moving, render (thirtySecond, 4096, 257)) == 0,
             "Slot 32 amount smoothing differs from the original slots");
    first.reset(); thirtySecond.reset();
    require (audioDifference (render (first, 4096, 127, note), render (thirtySecond, 4096, 257, note)) == 0,
             "Advanced route preparation did not survive reset");
    one.routes[0].enabled = last.routes.back().enabled = false;
    first.setSettings (one); thirtySecond.setSettings (last);
    require (audioDifference (render (first, 1024, 127), render (thirtySecond, 1024, 257)) == 0,
             "Live route bypass differs across matrix slots");
}

void testExpandedSourceMappingAndAudio()
{
    require (anyvoc::lfoCount == 10 && anyvoc::macroCount == 8 && anyvoc::modulationSourceCount == 25,
             "Expanded source counts do not match the published contract");
    const int lfoIds[] { 1, 2, 10, 11, 12, 13, 14, 15, 16, 17 };
    const int macroIds[] { 6, 7, 8, 9, 21, 22, 23, 24 };
    auto base = cleanSettings();
    for (int index = 0; index < 10; ++index)
    {
        require (anyvoc::lfoSourceId (index) == lfoIds[index]
                     && anyvoc::lfoIndexForSource (lfoIds[index]) == index,
                 "LFO source mapping changed a legacy ID or skipped a new LFO");
        require (base.lfoRate[static_cast<size_t> (index)] == static_cast<float> (index + 1)
                     && base.lfoPoints[static_cast<size_t> (index)] == anyvoc::defaultLfoPoints
                     && base.lfoDivision[static_cast<size_t> (index)] == 4,
                 "A new LFO did not receive its complete defaults");
    }
    for (int index = 0; index < 8; ++index)
        require (anyvoc::macroSourceId (index) == macroIds[index]
                     && anyvoc::macroIndexForSource (macroIds[index]) == index,
                 "Macro source mapping changed a legacy ID or skipped a new macro");
    require (anyvoc::lfoSourceId (-1) == 0 && anyvoc::lfoSourceId (10) == 0
                 && anyvoc::macroSourceId (-1) == 0 && anyvoc::macroSourceId (8) == 0
                 && anyvoc::lfoIndexForSource (18) == -1 && anyvoc::macroIndexForSource (20) == -1,
             "Invalid source/index mapping did not return Off or -1");

    base.filterEnabled = false;
    base.amp = { .001f, .01f, .75f, .1f };
    base.extraEnvelopes[0] = { .001f, .01f, .25f, .1f };
    base.extraEnvelopes[1] = { .001f, .01f, .5f, .1f };
    base.macros = { .5f, .25f, .75f, 1, .125f, .375f, .625f, .25f };
    const float newLfoValues[] { -1, -.75f, -.5f, -.25f, 0, .25f, .5f, .75f };
    for (size_t index = 2; index < anyvoc::lfoCount; ++index)
    {
        base.lfoShape[index] = 5;
        base.lfoPoints[index].fill (newLfoValues[index - 2]);
    }
    for (int source = 10; source <= 24; ++source)
    {
        const float raw = source <= 17 ? newLfoValues[source - 10]
                        : source == 18 ? .75f : source == 19 ? .25f : source == 20 ? .5f
                        : base.macros[static_cast<size_t> (source - 17)];
        auto routed = base, direct = base;
        // Primary curve +.25 squares while retaining bipolar sign; Macro8's
        // auxiliary curve -.25 transforms .25 to .5 independently of source.
        routed.routes.back() = { source, 7, .4f, true, 0, 24, 0, .25f, -.25f };
        direct.osc[0].pan = raw * std::abs (raw) * .5f * .4f;
        require (audioDifference (steadyFilterAudio (routed), steadyFilterAudio (direct)) < 2.0e-7,
                 "New primary source/curve did not reach audio: " + std::to_string (source));
        routed = base; direct = base;
        routed.routes.back() = { 6, 7, .2f, true, 0, source, 2, 0, -.25f };
        const float signedValue = source <= 17 ? raw : 2 * raw - 1;
        direct.osc[0].pan = .5f * std::copysign (std::sqrt (std::abs (signedValue)), signedValue) * .2f;
        require (audioDifference (steadyFilterAudio (routed), steadyFilterAudio (direct)) < 2.0e-7,
                 "New auxiliary source/polarity did not reach audio: " + std::to_string (source));
    }
}

void testExpandedLfoTransportModesAndRandom()
{
    auto settings = cleanSettings();
    SynthEngine phases;
    phases.setSettings (settings); phases.prepare (48000, 512);
    render (phases, 1200, 127);
    for (int index = 0; index < 10; ++index)
        require (std::abs (phases.getLfoPhase (index) - 1199.0f / 48000.0f * (index + 1)) < 1.0e-6f,
                 "An independent Free LFO did not advance at its default rate");
    require (phases.getLfoPhase (-1) == 0 && phases.getLfoPhase (10) == 0,
             "Expanded LFO phase access accepted an invalid index");

    settings.lfoSync[9] = true;
    settings.lfoDivision[9] = 8; // A third of a beat.
    settings.routes[0] = { 17, 7, .7f };
    SynthEngine tempo;
    tempo.setSettings (settings); tempo.prepare (48000, 512);
    renderTransport (tempo, 4800, 127, 120, 0, { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    require (std::abs (tempo.getLfoPhase (9) - 4799.0f * 6.0f / 48000.0f) < .0001f, "LFO10 triplet tempo sync is incorrect");
    renderTransport (tempo, 2400, 127, 60, -.25);
    require (std::abs (tempo.getLfoPhase (9) - .4f) < .0001f, "LFO10 did not follow a negative transport seek");
    SynthEngine first, second;
    first.setSettings (settings); second.setSettings (settings);
    first.prepare (48000, 512); second.prepare (48000, 512);
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } };
    require (audioDifference (renderTransport (first, 12345, 127, 137, .375, note),
                              renderTransport (second, 12345, 257, 137, .375, note)) < .0001,
             "LFO10 synced audio depended on block partition");

    settings.lfoShape[9] = 5;
    settings.lfoDivision[9] = 0;
    for (size_t point = 0; point < 33; ++point) settings.lfoPoints[9][point] = point % 2 == 0 ? -1 : 1;
    settings.routes[0].amount = 1;
    SynthEngine linear, stepped;
    linear.setSettings (settings); linear.prepare (48000, 512);
    const auto interpolated = renderTransport (linear, 480, 128, 120, .25, note);
    settings.lfoInterpolation[9] = 1;
    stepped.setSettings (settings); stepped.prepare (48000, 512);
    const auto held = renderTransport (stepped, 480, 128, 120, .25, note);
    require (channelRms (interpolated, 1) / channelRms (interpolated, 0) > .8
                 && channelRms (held, 1) < channelRms (held, 0) * .03,
             "LFO10 custom linear/step interpolation did not reach the audio route");

    settings.lfoMode[9] = 1;
    settings.lfoDivision[9] = 4;
    settings.lfoShape[9] = 0;
    SynthEngine triggered;
    triggered.setSettings (settings); triggered.prepare (48000, 512);
    renderTransport (triggered, 2400, 128, 120, 3.65);
    require (triggered.getLfoPhase (9) == 0, "LFO10 Trigger phase moved without a note");
    renderTransport (triggered, 4800, 127, 120, 3.75,
                     { { 37, juce::MidiMessage::noteOn (1, 60, .8f) } });
    require (std::abs (triggered.getLfoPhase (9) - 4763.0f / 24000) < .0001f,
             "LFO10 Trigger ignored the MIDI sample offset");
    renderTransport (triggered, 128, 128, 120, 3.95,
                     { { 64, juce::MidiMessage::noteOn (1, 64, .8f) } });
    require (std::abs (triggered.getLfoPhase (9) - 64.0f / 24000) < .0001f,
             "LFO10 preview did not select the newest triggered voice");
    renderTransport (triggered, 6000, 127, 120, 4,
                     { { 0, juce::MidiMessage::noteOff (1, 60) }, { 0, juce::MidiMessage::noteOff (1, 64) } });
    const float releasedPhase = triggered.getLfoPhase (9);
    renderTransport (triggered, 6000, 257, 120, 4.25);
    require (triggered.getLfoPhase (9) == releasedPhase,
             "LFO10 Trigger preview continued after every voice completed release");
    triggered.reset();
    for (int index = 0; index < 10; ++index)
        require (triggered.getLfoPhase (index) == 0, "Reset left an expanded LFO phase stale");

    settings.lfoMode[9] = 2;
    settings.lfoShape[9] = 5;
    settings.lfoInterpolation[9] = 0;
    for (size_t point = 0; point < 33; ++point) settings.lfoPoints[9][point] = -1 + static_cast<float> (point) / 16;
    SynthEngine oneShot;
    oneShot.setSettings (settings); oneShot.prepare (48000, 512);
    renderTransport (oneShot, 30000, 127, 120, 0, note);
    require (oneShot.getLfoPhase (9) == 1, "LFO10 Envelope mode did not stop at its final point");
    const auto endpoint = renderTransport (oneShot, 6000, 128, 120, 1.25);
    require (channelRms (endpoint, 0) < channelRms (endpoint, 1) * .02,
             "LFO10 Envelope mode did not hold custom point32");
    renderTransport (oneShot, 4800, 127, 120, 1.5,
                     { { 0, juce::MidiMessage::noteOn (1, 67, .8f) } });
    require (std::abs (oneShot.getLfoPhase (9) - .2f) < .0001f,
             "A new note did not restart completed LFO10 Envelope mode");
    renderTransport (oneShot, 12000, 127, 120, 1.7,
                     { { 0, juce::MidiMessage::controllerEvent (1, 123, 0) } });
    const float allNotesOffPhase = oneShot.getLfoPhase (9);
    require (inspect (renderTransport (oneShot, 6000, 257, 120, 2.2)).peak == 0
                 && oneShot.getLfoPhase (9) == allNotesOffPhase,
             "All Notes Off left an expanded LFO voice sounding or its preview running");

    std::vector<juce::AudioBuffer<float>> randomAudio;
    for (size_t index = 2; index < anyvoc::lfoCount; ++index)
    {
        auto random = cleanSettings();
        random.lfoShape[index] = 4;
        random.lfoRate[index] = 17;
        random.routes[0] = { anyvoc::lfoSourceId (static_cast<int> (index)), 7, .8f };
        SynthEngine engine;
        engine.setSettings (random); engine.prepare (48000, 512);
        // JUCE Chorus/Reverb initially ramp from their library defaults to our
        // dry settings (50/10 ms). Settle those global gains, then reset voice,
        // LFO and RNG state so the two reset comparisons have identical FX state.
        render (engine, 4096, 127); engine.reset();
        auto audio = render (engine, 8192, 127, note);
        engine.reset();
        require (audioDifference (audio, render (engine, 8192, 257, note)) == 0,
                 "A new Random LFO changed after reset or block repartition");
        for (const auto& previous : randomAudio)
            require (audioDifference (audio, previous) > .001, "Two new Random LFOs share the same sequence");
        randomAudio.push_back (std::move (audio));
    }
}

void testExpandedEnvelopeLifecycle()
{
    const std::vector<Event> lifecycle {
        { 0, juce::MidiMessage::noteOn (1, 60, .8f) }, { 1200, juce::MidiMessage::noteOn (1, 64, .7f) },
        { 2200, juce::MidiMessage::noteOff (1, 64) }, { 2400, juce::MidiMessage::controllerEvent (1, 64, 127) },
        { 3000, juce::MidiMessage::noteOff (1, 60) }, { 4800, juce::MidiMessage::controllerEvent (1, 64, 0) },
        { 6000, juce::MidiMessage::noteOn (1, 67, .9f) }, { 7100, juce::MidiMessage::noteOff (1, 67) },
        { 8000, juce::MidiMessage::noteOn (2, 72, .8f) }, { 8400, juce::MidiMessage::controllerEvent (2, 123, 0) },
        { 9000, juce::MidiMessage::noteOn (1, 60, .8f) }, { 9600, juce::MidiMessage::controllerEvent (1, 120, 0) }
    };
    for (int source : { 18, 19, 20 }) for (int voiceMode : { 0, 1, 2 })
    {
        auto settings = cleanSettings();
        settings.filterEnabled = false;
        settings.voiceMode = voiceMode;
        const anyvoc::EnvelopeSettings envelope { .025f, .04f, .6f, .12f };
        settings.amp = { .005f, .02f, .9f, .4f };
        if (source == 18) settings.amp = envelope;
        else settings.extraEnvelopes[static_cast<size_t> (source - 19)] = envelope;
        settings.filter = envelope;
        settings.routes[0] = { source, 7, .8f, true, 2, 0, 0, .25f, 0 };
        auto reference = settings;
        reference.routes[0].source = 3;
        SynthEngine actual, expected;
        actual.setSettings (settings); expected.setSettings (reference);
        actual.prepare (48000, 512); expected.prepare (48000, 512);
        const auto audio = render (actual, 13000, 127, lifecycle);
        require (inspect (audio).rms > .005
                     && audioDifference (audio, render (expected, 13000, 257, lifecycle)) == 0,
                 "Amp/extra envelope differs from the established per-voice ADSR lifecycle, source="
                     + std::to_string (source) + ", voiceMode=" + std::to_string (voiceMode));
        actual.reset();
        require (inspect (render (actual, 1024, 127)).peak == 0, "Envelope reset left a sounding voice");
    }

    auto base = cleanSettings();
    base.filterEnabled = false;
    base.amp = { .001f, .001f, 1, .5f };
    base.extraEnvelopes[0] = { .001f, .01f, 1, .05f };
    base.extraEnvelopes[1] = { .001f, .01f, 1, .2f };
    for (int source : { 19, 20 })
    {
        auto settings = base;
        settings.routes[0] = { source, 7, 1 };
        SynthEngine engine;
        engine.setSettings (settings); engine.prepare (48000, 512);
        render (engine, 4800, 127, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
        render (engine, 2880, 127, { { 0, juce::MidiMessage::noteOff (1, 60) } });
        const auto release = render (engine, 480, 127);
        require (inspect (release).rms > .001, "Amp voice ended before the independent release fixture");
        const double ratio = channelRms (release, 1) / channelRms (release, 0);
        require (source == 19 ? std::abs (ratio - 1) < .001 : ratio > 2,
                 "Env3 and Env4 release times were coupled or ignored");
        engine.reset();
        render (engine, 4800, 127, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
                                   { 1, juce::MidiMessage::controllerEvent (1, 64, 127) } });
        render (engine, 5000, 127, { { 0, juce::MidiMessage::noteOff (1, 60) } });
        const auto sustained = render (engine, 480, 127);
        require (channelRms (sustained, 0) < channelRms (sustained, 1) * .02,
                 "Sustain pedal did not hold an extra envelope");
        render (engine, 12000, 127, { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
        const auto afterPedal = render (engine, 480, 127);
        require (std::abs (channelRms (afterPedal, 1) / channelRms (afterPedal, 0) - 1) < .001,
                 "Pedal-up did not release an extra envelope");
        settings.amp.release = .02f;
        settings.extraEnvelopes[static_cast<size_t> (source - 19)].release = 1;
        engine.setSettings (settings); engine.reset();
        render (engine, 4800, 127, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
        render (engine, 2000, 127, { { 0, juce::MidiMessage::noteOff (1, 60) } });
        require (inspect (render (engine, 512, 127)).peak == 0,
                 "A longer extra-envelope release incorrectly extended the Amp-controlled voice lifetime");
    }

    for (int source : { 19, 20 }) for (int mode : { 1, 2 })
    {
        auto settings = base;
        settings.voiceMode = mode;
        settings.extraEnvelopes[static_cast<size_t> (source - 19)].attack = .2f;
        settings.routes[0] = { source, 7, 1 };
        SynthEngine engine;
        engine.setSettings (settings); engine.prepare (48000, 512);
        render (engine, 4800, 127, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
        render (engine, 128, 127, { { 0, juce::MidiMessage::noteOn (1, 64, .8f) } }); // Exclude the existing 64-sample voice tail.
        const auto overlap = render (engine, 352, 127);
        const double ratio = channelRms (overlap, 1) / channelRms (overlap, 0);
        require (mode == 1 ? ratio < 1.3 : ratio > 2,
                 "Mono retrigger/Legato did not restart/preserve the extra envelope attack");
    }

    for (int source : { 19, 20 })
    {
        auto settings = base;
        settings.extraEnvelopes[static_cast<size_t> (source - 19)].attack = .2f;
        settings.routes[0] = { source, 7, 1 };
        SynthEngine stolen, fresh;
        stolen.setSettings (settings); fresh.setSettings (settings);
        stolen.prepare (48000, 512); fresh.prepare (48000, 512);
        // The stolen engine's 4800-sample lead-in settles global FX gains. Give
        // its fresh-note reference the same dry gain state before comparing ADSR.
        render (fresh, 4096, 127); fresh.reset();
        std::vector<Event> fill;
        for (int voice = 0; voice < 16; ++voice)
            fill.push_back ({ voice, juce::MidiMessage::noteOn (voice + 1, 48 + voice, .8f) });
        render (stolen, 4800, 127, fill);
        std::vector<Event> replace { { 0, juce::MidiMessage::noteOn (1, 84, .8f) } };
        for (int channel = 2; channel <= 16; ++channel)
            replace.push_back ({ 1, juce::MidiMessage::controllerEvent (channel, 120, 0) });
        render (stolen, 256, 127, replace);
        render (fresh, 256, 127, { { 0, juce::MidiMessage::noteOn (1, 84, .8f) } });
        require (audioDifference (render (stolen, 4096, 127), render (fresh, 4096, 257)) == 0,
                 "Voice stealing retained a previous extra envelope instead of retriggering it");
    }
}

struct EnvelopeAudioProbe
{
    SynthSettings settings;
    SynthEngine actual, reference;
    int source;
    double sampleRate;
    explicit EnvelopeAudioProbe (int sourceIn = 19, double rate = 4096.0) : source (sourceIn), sampleRate (rate)
    {
        settings = cleanSettings(); settings.filterEnabled = false;
        settings.osc[0].mode = 1; settings.osc[0].sampleLoop = true; settings.osc[0].rootNote = 60;
        settings.amp = { .001f, .001f, 1.0f, 2.0f };
    }
    anyvoc::EnvelopeSettings& envelope()
    {
        return source == 18 ? settings.amp : source == 3 ? settings.filter : settings.extraEnvelopes[static_cast<size_t> (source - 19)];
    }
    void set()
    {
        auto dry = settings; dry.routes = {};
        auto wet = dry; wet.routes[0] = { source, 6, .5f };
        actual.setSettings (wet); reference.setSettings (dry);
    }
    void prepare()
    {
        set(); actual.prepare (sampleRate, 512); reference.prepare (sampleRate, 512);
        juce::AudioBuffer<float> dc (1, static_cast<int> (sampleRate));
        for (int i = 0; i < dc.getNumSamples(); ++i) dc.setSample (0, i, .5f);
        const auto sample = SynthEngine::makeSample (dc, sampleRate);
        actual.setUserSample (0, sample); reference.setUserSample (0, sample);
        // Equal settled dry FX gains, independent of oscillator/envelope age.
        render (actual, 4096, 127); render (reference, 4096, 127);
        actual.reset(); reference.reset();
    }
    void transport (double bpm, double ppq = 0, bool playing = true)
    {
        actual.setTransport (bpm, ppq, playing, true); reference.setTransport (bpm, ppq, playing, true);
    }
    std::vector<double> read (int frames, const std::vector<Event>& events = {}, int block = 127)
    {
        const auto a = render (actual, frames, block, events), b = render (reference, frames, block, events);
        std::vector<double> values (static_cast<size_t> (frames));
        for (int n = 0; n < frames; ++n)
        {
            // Undo the two fixed output tanh stages. The common carrier, Amp,
            // velocity and sample fade cancel, leaving 1 + 0.5 * envelope.
            const double dry = std::atanh (std::atanh (b.getSample (0, n)) / settings.master);
            const double wet = std::atanh (std::atanh (a.getSample (0, n)) / settings.master);
            values[static_cast<size_t> (n)] = std::abs (dry) > 1.0e-12 ? (wet / dry - 1.0) * 2.0
                : std::numeric_limits<double>::quiet_NaN();
        }
        return values;
    }
};

void expectEnvelope (double actual, double expected, const std::string& detail)
{
    require (std::isfinite (actual) && std::abs (actual - expected) < 4.0e-6,
             detail + ": expected=" + std::to_string (expected) + ", actual=" + std::to_string (actual));
}

void testDahdsrModelAndAudio()
{
    anyvoc::EnvelopeSettings model;
    const double beats[] { 0, .0625, .125, .25, .5, 1, 2, 4, 8, 16, 1.0 / 3, 1.0 / 6, .75, .375 };
    model.tempoSync = true;
    for (int division = 0; division < 14; ++division) for (int stage = 0; stage < 5; ++stage)
    {
        model.divisions[static_cast<size_t> (stage)] = division;
        require (std::abs (anyvoc::envelopeStageSeconds (model, stage, 120) - beats[division] * .5) < 1.0e-12,
                 "Envelope division is not expressed in quarter-note beats");
    }
    require (anyvoc::envelopeStageSeconds (model, -1, 120) == 0 && anyvoc::envelopeStageSeconds (model, 5, 120) == 0,
             "Envelope stage helper accepted an invalid index");
    for (float t : { 0.0f, .25f, .5f, .75f, 1.0f })
    {
        require (anyvoc::envelopeCurveProgress (t, 0) == t, "Linear curve helper changed the input bits");
        require (std::abs (anyvoc::envelopeCurveProgress (t, .25f) - t * t) < 1.0e-7f
                     && std::abs (anyvoc::envelopeCurveProgress (t, -.25f) - std::sqrt (t)) < 1.0e-7f,
                 "Envelope curves do not use the signed exponent contract");
    }
    require (anyvoc::envelopeCurveProgress (-1, 1) == 0 && anyvoc::envelopeCurveProgress (2, -1) == 1
                 && anyvoc::envelopeCurveProgress (std::numeric_limits<float>::quiet_NaN(), 0) == 0,
             "Envelope curve helper did not bound invalid progress");

    EnvelopeAudioProbe probe;
    auto& e = probe.envelope(); e = { .125f, .125f, .25f, .125f };
    e.delay = .03125f; e.hold = .0625f; e.attackCurve = .25f; e.decayCurve = -.25f; e.releaseCurve = .5f;
    probe.prepare();
    const auto audio = probe.read (2304, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                                           { 1536, juce::MidiMessage::noteOff (1, 60) } });
    for (int n = 1; n < 2304; ++n)
    {
        double expected = 0;
        if (n >= 128 && n < 640) { const double t = (n - 128 + 1) / 512.0; expected = t * t; }
        else if (n >= 640 && n < 896) expected = 1;
        else if (n >= 896 && n < 1408) expected = 1 - .75 * std::sqrt ((n - 896 + 1) / 512.0);
        else if (n >= 1408 && n < 1536) expected = .25;
        else if (n >= 1536 && n < 2048) { const double t = (n - 1536 + 1) / 512.0; expected = .25 * (1 - t * t * t * t); }
        expectEnvelope (audio[static_cast<size_t> (n)], expected, "DAHDSR audio landmark sample " + std::to_string (n));
    }

    EnvelopeAudioProbe instant;
    instant.envelope().tempoSync = true; instant.envelope().divisions.fill (0); instant.envelope().sustain = .375f;
    instant.prepare();
    const auto immediate = instant.read (256, { { 17, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                                               { 129, juce::MidiMessage::noteOff (1, 60) } });
    expectEnvelope (immediate[18], .375, "Instant stages inserted a sample of delay");
    expectEnvelope (immediate[128], .375, "Instant envelope sustain changed");
    expectEnvelope (immediate[129], 0, "Instant release ignored its MIDI offset");
}

void testDahdsrLiveTempoAndControls()
{
    EnvelopeAudioProbe probe;
    auto& e = probe.envelope(); e = { .125f, .125f, .5f, .125f };
    e.tempoSync = true; e.divisions = { 0, 5, 0, 0, 5 };
    probe.prepare(); probe.transport (120);
    auto audio = probe.read (512, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    expectEnvelope (audio.back(), .25, "Synced attack initial quarter");
    probe.set(); probe.transport (60, -123.25, false); // Production order: settings before transport.
    audio = probe.read (1024);
    expectEnvelope (audio.front(), .25 + 1.0 / 4096, "Tempo update took an extra block or seek retriggered");
    expectEnvelope (audio.back(), .5, "Tempo change did not preserve elapsed normalized phase");
    e.attackCurve = .25f; probe.set();
    audio = probe.read (1);
    double phase = .5 + 1.0 / 4096;
    expectEnvelope (audio[0], phase * phase, "Live curve did not use existing attack phase");
    e.divisions[1] = 6; probe.set();
    audio = probe.read (128, {}, 1); phase += 128.0 / 8192;
    expectEnvelope (audio.back(), phase * phase, "Live division restarted attack or used the old rate");
    e.tempoSync = false; probe.set();
    audio = probe.read (64); phase += 64.0 / 512;
    expectEnvelope (audio.back(), phase * phase, "Disabling Sync lost normalized stage progress");
    // Keep the advanced path through a live release, including curvature and duration edits.
    e.releaseCurve = .25f; probe.set();
    const double releaseStart = phase * phase;
    audio = probe.read (128, { { 0, juce::MidiMessage::noteOff (1, 60) } });
    expectEnvelope (audio.back(), releaseStart * (1 - .25 * .25), "Release did not capture the current curved level");
    e.release = .25f; e.releaseCurve = -.25f; probe.set();
    audio = probe.read (128); const double releasePhase = .25 + .125;
    expectEnvelope (audio.back(), releaseStart * (1 - std::sqrt (releasePhase)), "Live release controls restarted/froze the stage");

    // Cross the exact legacy/advanced paths in both directions during attack.
    EnvelopeAudioProbe crossing; crossing.envelope() = { .25f, .125f, .5f, .25f }; crossing.prepare();
    audio = crossing.read (256, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    expectEnvelope (audio.back(), .25, "Legacy initial progress");
    crossing.envelope().attackCurve = .25f; crossing.set();
    audio = crossing.read (256); expectEnvelope (audio.back(), .25, "Legacy-to-curved conversion restarted attack");
    crossing.envelope().attackCurve = 0; crossing.set();
    audio = crossing.read (1); expectEnvelope (audio[0], .25 + 1.0 / 1024, "Curved-to-legacy conversion lost audible level");

    // A time edit changes the remaining wait, never inserts an already passed Delay.
    EnvelopeAudioProbe waiting; waiting.envelope().delay = .125f; waiting.envelope().attack = .125f; waiting.prepare();
    waiting.read (256, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    waiting.envelope().delay = .25f; waiting.set();
    audio = waiting.read (512); expectEnvelope (audio.back(), 0, "Delay edit restarted its progress");
    audio = waiting.read (1); expectEnvelope (audio[0], 1.0 / 512, "Delay endpoint did not advance to Attack");
    waiting.envelope().delay = .5f; waiting.set();
    audio = waiting.read (1); expectEnvelope (audio[0], 2.0 / 512, "Editing passed Delay retriggered attack");

    EnvelopeAudioProbe releaseCrossing; releaseCrossing.envelope() = { .125f, .125f, .5f, .25f }; releaseCrossing.prepare();
    releaseCrossing.read (2048, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    audio = releaseCrossing.read (256, { { 0, juce::MidiMessage::noteOff (1, 60) } });
    expectEnvelope (audio.back(), .375, "Legacy release initial level");
    releaseCrossing.envelope().release = .5f; releaseCrossing.set();
    audio = releaseCrossing.read (256); expectEnvelope (audio.back(), .25, "Default legacy release lost its note-off captured rate");
    releaseCrossing.envelope().releaseCurve = .25f; releaseCrossing.set();
    audio = releaseCrossing.read (512); expectEnvelope (audio.back(), .21875, "Legacy-to-advanced release lost progress/start level");
    releaseCrossing.envelope().releaseCurve = 0; releaseCrossing.set();
    audio = releaseCrossing.read (512);
    expectEnvelope (audio.front(), .21875 * (1 - 1.0 / 512), "Advanced-to-legacy release restarted or jumped level");
    expectEnvelope (audio.back(), 0, "Advanced-to-legacy release lost its remaining duration");

    EnvelopeAudioProbe waits;
    waits.envelope().tempoSync = true; waits.envelope().divisions = { 3, 0, 3, 0, 0 }; waits.envelope().sustain = .5f;
    waits.prepare(); waits.transport (120);
    waits.read (256, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    waits.transport (60); audio = waits.read (512); expectEnvelope (audio.back(), 0, "Synced Delay tempo edit reset phase");
    audio = waits.read (256); expectEnvelope (audio.front(), 1, "Instant Attack did not enter timed Hold on the same sample");
    waits.transport (120); audio = waits.read (384); expectEnvelope (audio.back(), 1, "Synced Hold tempo edit reset phase");
    audio = waits.read (1); expectEnvelope (audio[0], .5, "Instant Decay did not end Hold at its exact sample");
}

void testDahdsrLegatoAndLifecycle()
{
    for (int source : { 3, 18, 19, 20 })
    {
        EnvelopeAudioProbe probe (source); probe.settings.voiceMode = 2;
        auto& e = probe.envelope(); e = { .125f, .125f, 1.0f, .25f };
        e.attackCurve = .25f; e.retriggerLegato = true;
        probe.prepare();
        probe.read (2048, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
        auto audio = probe.read (128, { { 0, juce::MidiMessage::noteOn (1, 64, 1.0f) } });
        expectEnvelope (audio.back(), .0625, "Per-envelope Legato retrigger missing, source " + std::to_string (source));
        audio = probe.read (128, { { 0, juce::MidiMessage::noteOff (1, 64) } });
        expectEnvelope (audio.back(), .25, "Last-note fallback incorrectly retriggered envelope");
        audio = probe.read (128, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
        expectEnvelope (audio.back(), .0625, "Same-note repeated note-on did not retrigger envelope");
    }
    // Other envelopes and oscillator phases remain legato when only Env3 retriggers.
    auto base = cleanSettings(); base.filterEnabled = false; base.voiceMode = 2;
    base.extraEnvelopes[0].attackCurve = .25f;
    auto changed = base; changed.extraEnvelopes[0].retriggerLegato = true;
    const std::vector<Event> legato { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                                    { 1000, juce::MidiMessage::noteOn (1, 67, .8f) },
                                    { 2000, juce::MidiMessage::noteOff (1, 67) } };
    SynthEngine a, b; a.setSettings (base); b.setSettings (changed); a.prepare (48000, 512); b.prepare (48000, 512);
    require (audioDifference (render (a, 4096, 127, legato), render (b, 4096, 257, legato)) == 0,
             "Independent envelope retrigger reset another envelope/oscillator/glide");

    // Note-off in each active stage captures the current level, including a zero Delay.
    for (int off : { 64, 384, 768, 1152, 1536 })
    {
        EnvelopeAudioProbe probe;
        auto& e = probe.envelope(); e = { .125f, .125f, .25f, .125f };
        e.delay = .03125f; e.hold = .0625f; e.attackCurve = .25f;
        probe.prepare();
        const auto lead = probe.read (off, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
        const double start = lead.back();
        const auto tail = probe.read (512, { { 0, juce::MidiMessage::noteOff (1, 60) } });
        expectEnvelope (tail[255], start * .5, "Release from an active DAHDS stage was discontinuous");
        expectEnvelope (tail.back(), 0, "Release stage did not reach zero");
    }
    EnvelopeAudioProbe pedal; pedal.envelope().hold = .125f; pedal.envelope().release = .125f; pedal.prepare();
    pedal.read (2048, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                        { 1000, juce::MidiMessage::controllerEvent (1, 64, 127) },
                        { 1200, juce::MidiMessage::noteOff (1, 60) } });
    auto held = pedal.read (1024); expectEnvelope (held.back(), .7, "Pedal did not hold the advanced sustain");
    auto released = pedal.read (512, { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
    expectEnvelope (released.back(), 0, "Pedal-up did not release advanced envelope");
    pedal.actual.reset(); require (inspect (render (pedal.actual, 512, 127)).peak == 0, "Reset retained advanced envelope voice");

    auto ampDelay = cleanSettings(); ampDelay.filterEnabled = false; ampDelay.amp.delay = .125f;
    SynthEngine silentStart; silentStart.setSettings (ampDelay); silentStart.prepare (4096, 512);
    require (inspect (render (silentStart, 512, 127, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } })).peak == 0,
             "Amp Delay emitted audio before its endpoint");
    require (inspect (render (silentStart, 256, 127)).rms > .001,
             "A silent Amp Delay was incorrectly treated as an idle/dead voice");
    render (silentStart, 1, 1, { { 0, juce::MidiMessage::controllerEvent (1, 120, 0) } });
    require (inspect (render (silentStart, 512, 127)).peak == 0, "CC120 retained an advanced envelope voice");
}

void testDahdsrCompatibilityPartitionAndInvalids()
{
    auto settings = cleanSettings(); settings.noise = .03f; settings.osc[0].unison = 3;
    const auto old = settings;
    for (auto* e : { &settings.amp, &settings.filter, &settings.extraEnvelopes[0], &settings.extraEnvelopes[1] })
    {
        e->divisions.fill (13); e->retriggerLegato = true; // Inactive in Poly; must not activate advanced arithmetic.
    }
    const std::vector<Event> notes { { 7, juce::MidiMessage::noteOn (1, 60, .8f) },
                                  { 800, juce::MidiMessage::noteOn (1, 64, .7f) },
                                  { 1900, juce::MidiMessage::noteOff (1, 60) }, { 2400, juce::MidiMessage::noteOff (1, 64) } };
    auto run = [&] (const SynthSettings& s, int block)
    {
        SynthEngine engine; engine.setSettings (s); engine.prepare (48000, 512);
        return render (engine, 9000, block, notes);
    };
    require (audioDifference (run (old, 127), run (settings, 257)) == 0,
             "Inactive envelope divisions/retrigger changed legacy audio bits");
    for (auto* e : { &settings.amp, &settings.filter, &settings.extraEnvelopes[0], &settings.extraEnvelopes[1] })
    {
        e->delay = .001f; e->hold = .004f; e->attackCurve = .3f; e->decayCurve = -.6f; e->releaseCurve = .8f;
    }
    settings.routes[0] = { 19, 7, .5f }; settings.routes[1] = { 20, 6, .4f };
    require (audioDifference (run (settings, 1), run (settings, 257)) == 0,
             "Advanced envelopes depended on render block partition");
    auto invalid = settings, valid = settings;
    invalid.extraEnvelopes[0].delay = std::numeric_limits<float>::quiet_NaN(); valid.extraEnvelopes[0].delay = 0;
    invalid.extraEnvelopes[0].hold = -1; valid.extraEnvelopes[0].hold = 0;
    invalid.extraEnvelopes[0].attackCurve = 2; valid.extraEnvelopes[0].attackCurve = 1;
    invalid.extraEnvelopes[0].decayCurve = -2; valid.extraEnvelopes[0].decayCurve = -1;
    invalid.extraEnvelopes[0].releaseCurve = std::numeric_limits<float>::infinity(); valid.extraEnvelopes[0].releaseCurve = 0;
    invalid.extraEnvelopes[0].tempoSync = valid.extraEnvelopes[0].tempoSync = true;
    invalid.extraEnvelopes[0].divisions = { -4, 2, -1, 99, 3 }; valid.extraEnvelopes[0].divisions = { 0, 2, 0, 13, 3 };
    require (audioDifference (run (invalid, 127), run (valid, 127)) == 0,
             "Envelope invalid controls escaped sanitization");
}

// Recover modulation from actual audio, independently of the LFO/model helpers.
// A DC sample and settled, bypassed effects make both output tanh stages invertible.
struct LfoAudioProbe
{
    SynthSettings settings = cleanSettings();
    SynthEngine actual, reference;
    juce::AudioBuffer<float> wet, dry;
    double sampleRate = 4096.0;
    LfoAudioProbe()
    {
        settings.filterEnabled = false;
        settings.osc[0].mode = 1; settings.osc[0].sampleLoop = true; settings.osc[0].rootNote = 60;
        settings.amp = { .001f, .001f, 1.0f, 2.0f };
        settings.routes[0] = { 1, 6, .5f };
    }
    void set()
    {
        actual.setSettings (settings);
        auto baseline = settings; baseline.routes = {}; baseline.filterEnabled = baseline.filter2Enabled = false;
        reference.setSettings (baseline);
    }
    void prepare()
    {
        set(); actual.prepare (sampleRate, 512); reference.prepare (sampleRate, 512);
        juce::AudioBuffer<float> dc (1, static_cast<int> (sampleRate));
        for (int n = 0; n < dc.getNumSamples(); ++n) dc.setSample (0, n, .5f);
        const auto sample = SynthEngine::makeSample (dc, sampleRate);
        actual.setUserSample (0, sample); reference.setUserSample (0, sample);
        render (actual, 4096, 127); render (reference, 4096, 127);
        actual.reset(); reference.reset();
    }
    void read (int frames, const std::vector<Event>& events = {}, int blockSize = 127,
               double bpm = 0.0, double ppq = 0.0)
    {
        const auto capture = [&] (SynthEngine& engine)
        {
            juce::AudioBuffer<float> result (2, frames);
            for (int start = 0; start < frames; start += blockSize)
            {
                const int count = std::min (blockSize, frames - start);
                if (bpm > 0.0) engine.setTransport (bpm, ppq + start / sampleRate * bpm / 60.0, true, true);
                juce::AudioBuffer<float> part (2, count);
                juce::MidiBuffer midi;
                for (const auto& event : events)
                    if (event.sample >= start && event.sample < start + count)
                        midi.addEvent (event.message, event.sample - start);
                engine.render (part, midi);
                for (int channel = 0; channel < 2; ++channel) result.copyFrom (channel, start, part, channel, 0, count);
            }
            inspect (result);
            return result;
        };
        wet = capture (actual); dry = capture (reference);
    }
    double raw (const juce::AudioBuffer<float>& audio, int channel, int n) const
    { return std::atanh (std::atanh (audio.getSample (channel, n)) / settings.master); }
    double level (int n) const { return (raw (wet, 0, n) / raw (dry, 0, n) - 1.0) * 2.0; }
    double pan (int n) const { return (std::atan2 (raw (wet, 1, n), raw (wet, 0, n)) * 4.0 / pi - 1.0) * 2.0; }
};

double independentLfoRead (double clock, double offset, int direction)
{
    double p = clock;
    const double fraction = offset - std::floor (offset);
    if (fraction != 0.0) p = p + fraction - std::floor (p + fraction);
    return direction == 1 ? 1 - p : direction == 2 ? (p <= .5 ? 2 * p : 2 - 2 * p) : p;
}

double independentSwing (double ppq, int grid)
{
    const double period = grid == 0 ? 1 : .5, pair = std::floor (ppq / period);
    const double t = ppq / period - pair;
    return period * (pair + (t <= .75 ? t * 2 / 3 : .5 + 2 * (t - .75)));
}

void testLfoPhaseDirectionAndOneShot()
{
    require (anyvoc::lfoReadPhase (1, 0, 0) == 1 && anyvoc::lfoReadPhase (1, -3, 0) == 1
             && anyvoc::lfoReadPhase (1, 1, 1) == 0 && anyvoc::lfoReadPhase (1, 0, 2) == 0,
             "Whole-turn offsets lost a one-shot endpoint");
    require (anyvoc::lfoReadPhase (.125, .25, 1) == .625
             && anyvoc::lfoReadPhase (.125, .25, 2) == .75,
             "Phase must precede Reverse/PingPong");
    require (anyvoc::lfoReadPhase (std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(), 0) == 0, "LFO read helper accepted nonfinite input");
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int index = 0; index < 10; ++index)
    {
        LfoAudioProbe probe;
        const auto i = static_cast<size_t> (index);
        probe.settings.lfoRate[i] = 32; probe.settings.lfoMode[i] = index % 3;
        probe.settings.lfoShape[i] = 5; probe.settings.lfoPhaseOffset[i] = .25f;
        probe.settings.lfoDirection[i] = index % 3;
        probe.settings.lfoInterpolation[i] = index == 9 ? 1 : 0;
        for (int point = 0; point < 33; ++point) probe.settings.lfoPoints[i][static_cast<size_t> (point)] = point / 16.0f - 1;
        probe.settings.routes[0].source = anyvoc::lfoSourceId (index);
        probe.prepare(); probe.read (300, note);
        double finalPhase = 0;
        for (int n = 1; n < 300; ++n)
        {
            double clock = n / 128.0;
            clock = index % 3 == 2 ? std::min (1.0, clock) : clock - std::floor (clock);
            finalPhase = independentLfoRead (clock, .25, index % 3);
            const double expected = index == 9 ? std::floor (finalPhase * 32) / 16 - 1 : 2 * finalPhase - 1;
            expectEnvelope (probe.level (n), expected, "LFO" + std::to_string (index + 1) + " custom phase/direction audio");
        }
        require (std::abs (probe.actual.getLfoPhase (index) - finalPhase) < 1.0e-7,
                 "LFO telemetry is not the actual final read position");
    }
    for (int direction = 0; direction < 3; ++direction)
    {
        LfoAudioProbe probe;
        probe.settings.lfoMode[0] = 2; probe.settings.lfoRate[0] = 32;
        probe.settings.lfoShape[0] = 2; probe.settings.lfoDirection[0] = direction;
        probe.settings.lfoPhaseOffset[0] = 1;
        probe.prepare(); probe.read (257, note);
        expectEnvelope (probe.level (256), direction == 0 ? 1 : -1, "Directional one-shot terminal audio");
        require (probe.actual.getLfoPhase (0) == (direction == 0 ? 1.0f : 0.0f), "One-shot terminal telemetry");
    }
}

void testLfoSwingTransportAndLiveEdits()
{
    require (anyvoc::lfoSwingPpq (.75, 1, 0) == .5 && anyvoc::lfoSwingPpq (.375, 1, 1) == .25
             && anyvoc::lfoSwingPpq (-.25, 1, 0) == -.5, "Swing pair landmarks or negative PPQ are incorrect");
    require (anyvoc::lfoSwingPpq (.123456789, 0, 1) == .123456789
             && anyvoc::lfoSwingPpq (std::numeric_limits<double>::quiet_NaN(), 1, 0) == 0,
             "Swing zero bypass or finite guard failed");
    for (int mode = 0; mode < 3; ++mode) for (int grid = 0; grid < 2; ++grid)
    {
        LfoAudioProbe probe;
        probe.settings.lfoSync[9] = true; probe.settings.lfoFollowSwing[9] = true;
        probe.settings.swingAmount = 1; probe.settings.swingDivision = grid;
        probe.settings.lfoShape[9] = 2; probe.settings.lfoMode[9] = mode;
        probe.settings.routes[0].source = 17;
        probe.prepare();
        constexpr int offset = 37, count = 2600;
        probe.read (count, { { offset, juce::MidiMessage::noteOn (1, 60, .8f) } }, 127, 120, -.25);
        for (int n = offset + 1; n < count; ++n)
        {
            double phase = independentSwing (-.25 + n / 2048.0, grid);
            if (mode != 0) phase -= independentSwing (-.25 + offset / 2048.0, grid);
            phase = mode == 2 ? std::min (1.0, phase) : phase - std::floor (phase);
            expectEnvelope (probe.level (n), 2 * phase - 1, "Swing audio differs from musical grid");
        }
    }
    // A seek changes the increment's global swing region; it cannot restart a triggered clock.
    LfoAudioProbe triggered;
    triggered.settings.lfoSync[0] = triggered.settings.lfoFollowSwing[0] = true;
    triggered.settings.lfoMode[0] = 1; triggered.settings.lfoShape[0] = 2;
    triggered.settings.swingAmount = 1;
    triggered.prepare(); triggered.read (512, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }, 127, 120, 0);
    triggered.read (128, {}, 17, 60, 10.8);
    const double clock = independentSwing (.25, 0) + independentSwing (10.8 + 127.0 / 4096, 0) - independentSwing (10.8, 0);
    expectEnvelope (triggered.level (127), 2 * clock - 1, "Transport seek retriggered a per-voice LFO");
    triggered.settings.lfoPhaseOffset[0] = .25f; triggered.settings.lfoDirection[0] = 1;
    triggered.set(); triggered.read (1, {}, 1, 60, 10.8 + 128.0 / 4096);
    const double nextClock = independentSwing (.25, 0) + independentSwing (10.8 + 128.0 / 4096, 0) - independentSwing (10.8, 0);
    expectEnvelope (triggered.level (0), 2 * independentLfoRead (nextClock, .25, 1) - 1,
                    "Live Phase/Direction edit restarted or lagged the clock");
    LfoAudioProbe oneSample, partitioned;
    oneSample.settings.lfoSync[0] = oneSample.settings.lfoFollowSwing[0] = true;
    oneSample.settings.swingAmount = .7f; oneSample.settings.swingDivision = 1;
    oneSample.settings.lfoDivision[0] = 8;
    oneSample.settings.lfoDirection[0] = 2; oneSample.settings.lfoPhaseOffset[0] = .3f;
    partitioned.settings = oneSample.settings;
    oneSample.prepare(); partitioned.prepare();
    const std::vector<Event> notes { { 37, juce::MidiMessage::noteOn (1, 60, .8f) } };
    oneSample.read (4096, notes, 1, 113, -1.375);
    partitioned.read (4096, notes, 257, 113, -1.375);
    require (audioDifference (oneSample.wet, partitioned.wet) < 1.0e-6,
             "Swing/direction/phase audio depended on host block partition");
}

void testLfoPhaseRoutesFeedbackAndVoices()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int destination = 0; destination < 10; ++destination)
    {
        LfoAudioProbe probe;
        const auto i = static_cast<size_t> (destination);
        probe.settings.lfoMode[i] = 1; probe.settings.lfoRate[i] = 32; probe.settings.lfoShape[i] = 2;
        probe.settings.macros[0] = .5f; probe.settings.macros[1] = .5f;
        probe.settings.routes[0].source = anyvoc::lfoSourceId (destination);
        probe.settings.routes[31] = { 6, anyvoc::lfoPhaseDestinationId (destination), .5f };
        probe.settings.routes[31].auxSource = 7;
        probe.prepare(); probe.read (72, note);
        for (int n = 1; n < 72; ++n)
            expectEnvelope (probe.level (n), 2 * independentLfoRead (n / 128.0, .125, 0) - 1,
                            "A phase destination ignored its primary/auxiliary route");
        require (anyvoc::modulationRange (.25f, probe.settings.routes[31]) == std::make_pair (.25f, .75f),
                 "Phase destination nominal range is missing");
    }
    // Read all nodes before writing any phase sum. Independent recurrence uses
    // previous-sample values, including a self-loop and a two-node cycle.
    LfoAudioProbe cycle;
    cycle.settings.lfoMode[0] = cycle.settings.lfoMode[1] = 1;
    cycle.settings.lfoRate[0] = 8; cycle.settings.lfoRate[1] = 16;
    cycle.settings.lfoShape[0] = cycle.settings.lfoShape[1] = 2;
    cycle.settings.routes[1] = { 2, 18, .25f };
    cycle.settings.routes[2] = { 1, 19, .125f };
    cycle.settings.routes[3] = { 1, 18, .125f };
    cycle.prepare(); cycle.read (200, note, 1);
    float previousA = 0, previousB = 0;
    for (int n = 0; n < 200; ++n)
    {
        const float offsetA = previousB * .25f + previousA * .125f, offsetB = previousA * .125f;
        const float a = 2 * static_cast<float> (independentLfoRead (n / 512.0, offsetA, 0)) - 1;
        const float b = 2 * static_cast<float> (independentLfoRead (n / 256.0, offsetB, 0)) - 1;
        if (n > 0) expectEnvelope (cycle.level (n), a, "Phase feedback differs from independent unit-delay recurrence");
        previousA = a; previousB = b;
    }
    LfoAudioProbe reordered;
    reordered.settings = cycle.settings;
    std::swap (reordered.settings.routes[1], reordered.settings.routes[3]);
    reordered.prepare(); reordered.read (200, note, 127);
    require (audioDifference (cycle.wet, reordered.wet) == 0, "Phase feedback depends on slot order or block size");

    // A global clock is shared, but a velocity-driven read offset is voice-local.
    LfoAudioProbe poly;
    poly.settings.lfoShape[0] = 2; poly.settings.lfoRate[0] = 8;
    poly.settings.routes[1] = { 4, 18, .25f };
    poly.prepare();
    poly.read (80, { { 0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 32) },
                    { 0, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 96) } });
    for (int n = 1; n < 80; ++n)
    {
        const float v1 = 32.0f / 127, v2 = 96.0f / 127;
        const float g1 = .25f + .75f * v1, g2 = .25f + .75f * v2;
        const double a = 2 * independentLfoRead (n / 512.0, v1 * .25f, 0) - 1;
        const double b = 2 * independentLfoRead (n / 512.0, v2 * .25f, 0) - 1;
        expectEnvelope (poly.level (n), (g1 * a + g2 * b) / (g1 + g2), "Free phase offset leaked between voices");
    }
    require (std::abs (poly.actual.getLfoPhase (0) - independentLfoRead (79 / 512.0, (96.0f / 127) * .25f, 0)) < 1e-7,
             "Phase-modulated Free telemetry did not select the newest active voice");

    LfoAudioProbe disabled;
    disabled.settings.lfoShape[0] = 2; disabled.settings.lfoRate[0] = 32;
    disabled.settings.macros[0] = .5f; disabled.settings.routes[1] = { 6, 18, .25f };
    disabled.prepare(); disabled.read (64, note);
    disabled.settings.routes[1].enabled = false; disabled.set(); disabled.read (1, {}, 1);
    expectEnvelope (disabled.level (0), .25, "Disable discarded the pending one-sample phase offset");
    require (disabled.actual.getLfoPhase (0) == .625f,
             "One-sample render after disabling a phase route published the wrong read position");
    disabled.read (1, {}, 1);
    require (disabled.actual.getLfoPhase (0) == 65.0f / 128.0f,
             "Disabled phase route remained in telemetry after its pending sample");

    // A stolen triggered voice must start with zero delayed feedback, just like
    // a fresh voice. After the existing 64-sample tail, their recurrences agree.
    LfoAudioProbe stolen, fresh;
    stolen.settings = cycle.settings; fresh.settings = cycle.settings;
    stolen.prepare(); fresh.prepare();
    std::vector<Event> fill;
    for (int channel = 1; channel <= 16; ++channel)
        fill.push_back ({ channel - 1, juce::MidiMessage::noteOn (channel, 48 + channel, .8f) });
    stolen.read (1024, fill);
    std::vector<Event> replace { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int channel = 2; channel <= 16; ++channel)
        replace.push_back ({ 1, juce::MidiMessage::controllerEvent (channel, 120, 0) });
    stolen.read (512, replace); fresh.read (512, note);
    for (int n = 65; n < 512; ++n)
        expectEnvelope (stolen.level (n), fresh.level (n), "Voice stealing retained delayed phase feedback");
}

float independentAudioLfo (int n, double rate, double sampleRate)
{
    const double cycles = n * rate / sampleRate;
    return std::sin (static_cast<float> (2 * pi) * static_cast<float> (cycles - std::floor (cycles)));
}

void testLfoAudioRateTargetOracles()
{
    const auto note = juce::MidiMessage::noteOn (1, 60, .8f);
    const std::vector<Event> notes { { 0, note } };
    for (float rate : { 20.0f, 997.0f, 1000.0f }) for (bool auxiliary : { false, true })
    {
        LfoAudioProbe probe; probe.sampleRate = 48000;
        probe.settings.lfoAudioRateEnabled[9] = true; probe.settings.lfoAudioRateHz[9] = rate;
        probe.settings.lfoMode[9] = 1;
        probe.settings.routes[0] = { auxiliary ? 6 : 17, 7, .5f };
        probe.settings.macros[0] = 1;
        if (auxiliary) probe.settings.routes[0].auxSource = 17;
        probe.prepare(); probe.read (1024, notes);
        for (int n = 1; n < 1024; ++n)
            expectEnvelope (probe.pan (n), independentAudioLfo (n, rate, probe.sampleRate),
                            "Audio-rate pan was held at control rate or lost the auxiliary dependency");
    }
    LfoAudioProbe chain; chain.sampleRate = 48000;
    chain.settings.lfoMode[0] = chain.settings.lfoMode[9] = 1;
    chain.settings.lfoAudioRateEnabled[9] = true; chain.settings.lfoAudioRateHz[9] = 997;
    chain.settings.lfoShape[0] = 2; chain.settings.lfoRate[0] = 1;
    chain.settings.macros[0] = 1;
    chain.settings.routes[0] = { 1, 7, .5f };
    chain.settings.routes[1] = { 6, 18, .25f }; chain.settings.routes[1].auxSource = 17;
    chain.prepare(); chain.read (1024, notes);
    for (int n = 1; n < 1024; ++n)
    {
        const float offset = independentAudioLfo (n - 1, 997, 48000) * .25f;
        const float expected = 2 * static_cast<float> (independentLfoRead (n / 48000.0, offset, 0)) - 1;
        expectEnvelope (chain.pan (n), expected, "Audio-rate phase-chain dependency stopped before its pan destination");
    }

    // Independent phase integration followed by the public stationary table
    // lookup: only the oscillator waveform is shared, never its pitch control.
    LfoAudioProbe pitch; pitch.sampleRate = 48000;
    pitch.settings.osc[0].mode = 0;
    pitch.settings.lfoMode[9] = 1; pitch.settings.lfoAudioRateEnabled[9] = true; pitch.settings.lfoAudioRateHz[9] = 997;
    pitch.settings.routes[0] = { 17, 5, .2f };
    pitch.prepare(); pitch.read (1024, notes);
    float phase = 0, amplitude = 0;
    const float attack = 1.0f / (pitch.settings.amp.attack * 48000.0f);
    const float velocity = note.getVelocity() / 127.0f;
    const float fundamental = 440.0f * std::exp2 ((60.0f - 69.0f) / 12.0f);
    for (int n = 0; n < 1024; ++n)
    {
        amplitude = std::min (1.0f, amplitude + attack);
        const float modulation = independentAudioLfo (n, 997, 48000) * .2f;
        const float frequency = fundamental * std::exp2 ((modulation * 12.0f) / 12.0f);
        phase += frequency / 48000.0f; if (phase >= 1) phase -= 1;
        const float gain = amplitude * (.25f + .75f * velocity) * .23f;
        const double expected = pitch.actual.getWavetableSample (0, phase, 0) * pitch.settings.osc[0].level
            * std::cos (static_cast<float> (pi) * .25f) * gain * std::min (1.0f, n / 64.0f);
        // The engine's existing mode-transition fade also applies to a new
        // wavetable voice. Its first oscillator sample is deliberately silent.
        require (std::abs (pitch.raw (pitch.wet, 0, n) - expected) < 2.0e-5,
                 "Audio-rate pitch does not match per-sample phase integration at sample " + std::to_string (n)
                    + ": expected=" + std::to_string (expected) + ", actual=" + std::to_string (pitch.raw (pitch.wet, 0, n)));
    }

    // TPT state-variable filter reference using the independently recovered
    // unfiltered oscillator input. Both cutoff and resonance must change on
    // every sample, including the second filter's separate coefficient bank.
    for (int destination : { 1, 8, 9, 10 })
    {
        LfoAudioProbe filter; filter.sampleRate = 48000;
        filter.settings.osc[0].mode = 0;
        const bool second = destination == 8 || destination == 10;
        filter.settings.filterEnabled = ! second; filter.settings.filter2Enabled = second;
        filter.settings.cutoff = filter.settings.filter2Cutoff = 800;
        filter.settings.resonance = filter.settings.filter2Resonance = .2f;
        filter.settings.lfoMode[9] = 1; filter.settings.lfoAudioRateEnabled[9] = true; filter.settings.lfoAudioRateHz[9] = 997;
        filter.settings.routes[0] = { 17, destination, .15f };
        filter.prepare(); filter.read (1024, notes);
        double state1 = 0, state2 = 0;
        amplitude = 0;
        for (int n = 0; n < 1024; ++n)
        {
            amplitude = std::min (1.0f, amplitude + attack);
            const float gain = amplitude * (.25f + .75f * velocity) * .23f;
            const float mod = independentAudioLfo (n, 997, 48000) * .15f;
            const float hz = destination <= 8 ? 800.0f * std::exp2 (mod * 5.0f) : 800.0f;
            const float resonance = destination >= 9 ? .2f + mod : .2f;
            const double g = std::tan (static_cast<float> (pi) * hz / 48000.0f);
            const double damping = 2.0f - 1.94f * resonance;
            const double a1 = 1 / (1 + g * (g + damping)), a2 = g * a1, a3 = g * a2;
            const double input = filter.raw (filter.dry, 0, n) / gain;
            const double v3 = input - state2, v1 = a1 * state1 + a2 * v3;
            const double output = state2 + a2 * state1 + a3 * v3;
            state1 = 2 * v1 - state1; state2 = 2 * output - state2;
            require (std::abs (filter.raw (filter.wet, 0, n) - output * gain) < 4.0e-6,
                     "Audio-rate filter reference failed for destination " + std::to_string (destination)
                        + " at sample " + std::to_string (n));
        }
    }
}

void testLfoAdvancedIdentityPartitionAndLifecycle()
{
    auto base = cleanSettings(); base.filterEnabled = false;
    base.routes[0] = { 1, 7, .5f }; base.lfoRate[0] = 13;
    auto unused = base;
    unused.swingAmount = 1; unused.swingDivision = 1; // Follow is still off.
    for (size_t i = 0; i < 10; ++i)
    {
        unused.lfoAudioRateHz[i] = 997;
        if (i > 0) { unused.lfoPhaseOffset[i] = .3f; unused.lfoDirection[i] = 2; unused.lfoAudioRateEnabled[i] = true; }
    }
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    const auto audio = [&] (SynthSettings settings, int block = 127)
    { SynthEngine engine; engine.setSettings (settings); engine.prepare (48000, 512); return render (engine, 8192, block, note); };
    require (audioDifference (audio (base), audio (unused)) == 0, "Unused advanced LFO controls changed legacy audio");
    unused = base; unused.lfoFollowSwing[0] = true; unused.swingAmount = 1;
    require (audioDifference (audio (base), audio (unused)) == 0, "Unsynced Follow Swing affected audio");
    base.lfoSync[0] = true; unused = base; unused.lfoAudioRateEnabled[0] = true; unused.lfoAudioRateHz[0] = 997;
    require (audioDifference (audio (base), audio (unused)) == 0, "Sync failed to override inactive audio-rate settings");
    unused = base; unused.routes[31] = { 1, 18, 0 };
    require (audioDifference (audio (base), audio (unused)) == 0, "Zero phase route activated a new control path");

    // Random ignores phase/direction even as a shared global cycle wraps. A
    // second periodic LFO has voice-local phase modulation at those same times.
    base.lfoSync[0] = false; base.lfoRate[0] = 40; base.lfoShape[0] = 4;
    base.routes[0] = { 1, 6, .2f }; base.routes[1] = { 2, 7, .2f }; base.routes[2] = { 4, 19, .25f };
    unused = base; unused.lfoPhaseOffset[0] = .625f; unused.lfoDirection[0] = 2;
    unused.routes[31] = { 2, 18, 1 };
    require (audioDifference (audio (base), audio (unused)) == 0, "Random hold sequence changed at a global wrap/phase-route boundary");

    auto advanced = cleanSettings(); advanced.lfoMode[0] = advanced.lfoMode[9] = 1;
    advanced.lfoAudioRateEnabled[9] = true; advanced.lfoAudioRateHz[9] = 997;
    advanced.lfoPhaseOffset[0] = .35f; advanced.lfoDirection[0] = 2;
    advanced.routes[0] = { 1, 7, .4f }; advanced.routes[1] = { 17, 18, .2f }; advanced.routes[2] = { 1, 27, .1f };
    require (audioDifference (audio (advanced, 1), audio (advanced, 257)) == 0,
             "Audio-rate phase feedback depended on buffer partition");
    SynthEngine reset, fresh;
    reset.setSettings (advanced); fresh.setSettings (advanced); reset.prepare (48000, 512); fresh.prepare (48000, 512);
    render (reset, 8192, 127, note); render (fresh, 4096, 127); reset.reset(); fresh.reset();
    require (audioDifference (render (reset, 8192, 127, note), render (fresh, 8192, 257, note)) == 0,
             "Reset retained phase modulation or audio-rate state");
    render (reset, 1, 1, { { 0, juce::MidiMessage::controllerEvent (1, 120, 0) } });
    require (inspect (render (reset, 512, 127)).peak == 0, "All Sound Off left audio-rate phase voices active");

    auto invalid = advanced, valid = advanced;
    invalid.swingAmount = std::numeric_limits<float>::quiet_NaN(); valid.swingAmount = 0;
    invalid.swingDivision = 99; valid.swingDivision = 1;
    invalid.lfoPhaseOffset[0] = std::numeric_limits<float>::infinity(); valid.lfoPhaseOffset[0] = 0;
    invalid.lfoDirection[0] = 99; valid.lfoDirection[0] = 2;
    invalid.lfoAudioRateHz[9] = std::numeric_limits<float>::quiet_NaN(); valid.lfoAudioRateHz[9] = 100;
    require (audioDifference (audio (invalid), audio (valid)) == 0, "Advanced LFO invalid values were not sanitized");
    invalid.lfoAudioRateHz[9] = 5000; valid.lfoAudioRateHz[9] = 1000;
    invalid.lfoPhaseOffset[0] = -1; valid.lfoPhaseOffset[0] = 0;
    require (audioDifference (audio (invalid), audio (valid)) == 0, "Advanced LFO rate/phase limits were not clamped");
}

void testMacroMappingsAndProducts()
{
    require (anyvoc::modulationDestinationCount == 36 && anyvoc::macroDestinationId (-1) == 0
             && anyvoc::macroDestinationId (8) == 0 && anyvoc::macroIndexForDestination (27) == -1
             && anyvoc::macroIndexForDestination (36) == -1 && anyvoc::lfoIndexForPhaseDestination (27) == 9
             && anyvoc::lfoIndexForPhaseDestination (28) == -1 && anyvoc::lfoIndexForPhaseDestination (35) == -1,
             "Macro destination bank overlaps the phase bank");
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } };
    for (int index = 0; index < 8; ++index) for (bool auxiliary : { false, true })
    {
        require (anyvoc::macroDestinationId (index) == 28 + index
                 && anyvoc::macroIndexForDestination (28 + index) == index, "Macro destination mapping");
        LfoAudioProbe probe;
        const int source = anyvoc::macroSourceId (index), donor = anyvoc::macroSourceId ((index + 1) % 8);
        probe.settings.macros.fill (.5f); probe.settings.macros[static_cast<size_t> (index)] = .25f;
        probe.settings.routes[0] = { auxiliary ? 4 : source, 6, .5f };
        if (auxiliary) probe.settings.routes[0].auxSource = source;
        probe.settings.routes[31] = { donor, 28 + index, .5f };
        probe.prepare(); probe.read (32, note);
        for (int n = 1; n < 32; ++n)
            expectEnvelope (probe.level (n), .5, "Macro primary/AUX actual audio or one-sample delay");
        const auto live = probe.actual.getMacroModulationSnapshot();
        require (live.hasActiveVoice && live.voiceId == 1 && live.values[static_cast<size_t> (index)] == .5f,
                 "Macro snapshot did not report the source value actually read");
    }
    LfoAudioProbe product;
    product.settings.macros = { .2f, .25f, .5f, 0, 0, 0, 0, 0 };
    product.settings.routes[0] = { 6, 6, .5f };
    product.settings.routes[1] = { 7, 28, -.5f };
    product.settings.routes[1].polarity = 2; product.settings.routes[1].curve = .25f;
    product.settings.routes[1].auxSource = 8; product.settings.routes[1].auxCurve = -.25f;
    product.prepare(); product.read (32, note);
    const double expected = .2f + .125 * std::sqrt (.5);
    for (int n = 1; n < 32; ++n)
        expectEnvelope (product.level (n), expected, "Incoming primary/AUX curve and polarity product");
    // Add every contribution before the final clamp: clipping each positive
    // route before subtracting the negative one would produce a wrong result.
    for (const auto amounts : { std::array<float, 3> { 1, 1, -1 }, { -1, -1, 1 }, { 1, 1, 1 } })
    {
        LfoAudioProbe sum; sum.settings.macros.fill (1); sum.settings.macros[0] = .25f;
        sum.settings.routes[0] = { 6, 6, .5f };
        for (int r = 0; r < 3; ++r) sum.settings.routes[static_cast<size_t> (r + 1)] = { 7, 28, amounts[static_cast<size_t> (r)] };
        sum.prepare(); sum.read (16, note);
        const float value = std::clamp (.25f + amounts[0] + amounts[1] + amounts[2], 0.0f, 1.0f);
        expectEnvelope (sum.level (15), value, "Macro incoming sum/clamp order");
    }
}

void testMacroDelayedGraphs()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } };
    LfoAudioProbe chain; chain.settings.macros.fill (0); chain.settings.macros[0] = .5f;
    chain.settings.routes[0] = { 24, 6, .5f };
    for (int i = 0; i < 7; ++i) chain.settings.routes[static_cast<size_t> (i + 1)] = { anyvoc::macroSourceId (i), 29 + i, 1 };
    chain.prepare(); chain.read (32, note);
    for (int n = 1; n < 32; ++n) expectEnvelope (chain.level (n), n < 7 ? 0 : .5, "Eight-macro chain lost an edge delay");
    LfoAudioProbe feedback; feedback.settings.macros.fill (0); feedback.settings.macros[0] = 1;
    feedback.settings.routes[0] = { 6, 7, .5f }; feedback.settings.routes[1] = { 6, 28, -1 };
    feedback.prepare(); feedback.read (128, note);
    for (int n = 1; n < 128; ++n)
        expectEnvelope (feedback.pan (n), n % 2 == 0 ? 1 : 0, "Self-feedback was implicit or held at 16-sample control rate");
    // A macro/LFO cycle has two independent edge delays. The reference keeps
    // both preceding sums and never calls a production routing/model helper.
    LfoAudioProbe cycle; cycle.sampleRate = 4096;
    cycle.settings.macros.fill (0); cycle.settings.macros[0] = .5f; cycle.settings.macros[1] = .125f;
    cycle.settings.lfoMode[0] = 1; cycle.settings.lfoShape[0] = 2; cycle.settings.lfoRate[0] = 32;
    cycle.settings.routes[0] = { 6, 7, .5f };
    cycle.settings.routes[1] = { 1, 28, .25f };
    cycle.settings.routes[2] = { 6, 29, .5f };
    cycle.settings.routes[3] = { 4, 18, .5f }; cycle.settings.routes[3].auxSource = 7;
    cycle.prepare(); cycle.read (256, note);
    float previousLfo = 0, previousMacro = 0, previousPhase = 0, finalRead = 0;
    for (int n = 0; n < 256; ++n)
    {
        const float macro0 = std::clamp (.5f + previousLfo, 0.0f, 1.0f);
        const float macro1 = std::clamp (.125f + previousMacro, 0.0f, 1.0f);
        double phase = n / 128.0 + previousPhase; phase -= std::floor (phase);
        const float lfo = 2 * static_cast<float> (phase) - 1;
        if (n > 0) expectEnvelope (cycle.pan (n), macro0, "Macro/LFO phase cycle used a current-sample value");
        finalRead = macro0;
        previousLfo = lfo * .25f; previousMacro = macro0 * .5f; previousPhase = macro1 * .5f;
    }
    const auto live = cycle.actual.getMacroModulationSnapshot();
    require (live.values[0] == finalRead, "Telemetry exposed next-sample pending sums");
}

void testMacroLifecycleAndTelemetry()
{
    auto settings = cleanSettings(); settings.macros.fill (.125f); settings.amp = { .001f, .001f, 1, .01f };
    settings.routes = {}; settings.routes[0] = { 4, 28, .5f };
    SynthEngine poly; poly.setSettings (settings); poly.prepare (4096, 128);
    const auto frame = [&] (SynthEngine& engine, const std::vector<Event>& events = {}, int count = 1)
    { render (engine, count, 127, events); return engine.getMacroModulationSnapshot(); };
    // A MIDI message at the end of an empty block creates a voice, but no
    // source value has actually been read: expose idle base until a sample.
    juce::AudioBuffer<float> empty (2, 0); juce::MidiBuffer pending;
    pending.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0); poly.render (empty, pending);
    require (! poly.getMacroModulationSnapshot().hasActiveVoice
             && poly.getMacroModulationSnapshot().values == settings.macros, "Empty block invented an actual voice read");
    poly.reset();
    auto live = frame (poly, { { 0, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (32)) } });
    require (live.hasActiveVoice && live.voiceId == 1 && live.values[0] == .125f, "New voice inherited incoming macro state");
    live = frame (poly);
    require (live.values[0] == .125f + (32 / 127.0f) * .5f, "Velocity-to-macro voice 1 value");
    pending.clear(); pending.addEvent (juce::MidiMessage::noteOn (2, 64, static_cast<juce::uint8> (96)), 0);
    poly.render (empty, pending);
    require (poly.getMacroModulationSnapshot().voiceId == 1, "Unread new voice replaced the latest actually sampled voice");
    live = frame (poly);
    require (live.voiceId == 2 && live.values[0] == .125f, "Second voice inherited voice 1 macro state");
    live = frame (poly); require (live.values[0] == .125f + (96 / 127.0f) * .5f, "Latest voice telemetry mixed voice sources");
    frame (poly, { { 0, juce::MidiMessage::controllerEvent (2, 64, 127) }, { 0, juce::MidiMessage::noteOff (2, 64) } }, 64);
    require (poly.getMacroModulationSnapshot().voiceId == 2, "Sustained macro voice was discarded");
    live = frame (poly, { { 0, juce::MidiMessage::controllerEvent (2, 123, 0) } });
    require (live.voiceId == 2 && live.values[0] > .125f, "All Notes Off destroyed the release macro state");
    live = frame (poly, {}, 512);
    require (live.voiceId == 1 && live.values[0] == .125f + (32 / 127.0f) * .5f, "Release did not return telemetry to older active voice");
    live = frame (poly, { { 0, juce::MidiMessage::controllerEvent (1, 120, 0) } });
    require (! live.hasActiveVoice && live.voiceId == 0 && live.values == settings.macros, "Panic retained old macro readings");
    for (int voice = 0; voice < 17; ++voice)
    {
        live = frame (poly, { { 0, juce::MidiMessage::noteOn (1, 40 + voice, 1.0f) } });
        require (live.values[0] == .125f, "Voice stealing retained previous macro incoming sum");
        frame (poly, {}, 3);
    }
    poly.reset(); live = poly.getMacroModulationSnapshot();
    require (! live.hasActiveVoice && live.voiceId == 0 && live.values == settings.macros, "Reset getter is stale until the next render");
    for (int mode : { 1, 2 })
    {
        SynthEngine mono; auto configured = settings; configured.voiceMode = mode;
        configured.macros[0] = 1; configured.routes[0] = { 6, 28, -1 };
        configured.amp.retriggerLegato = true;
        mono.setSettings (configured); mono.prepare (4096, 128);
        frame (mono, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } }, 3); // current=1, pending=-1
        live = frame (mono, { { 0, juce::MidiMessage::noteOn (1, 64, 1.0f) } });
        require (live.values[0] == (mode == 1 ? 1.0f : 0.0f), "Mono retrigger/Legato did not reset/preserve macro feedback");
        require (live.voiceId == (mode == 1 ? 2u : 1u), "Legato unexpectedly allocated a new voice");
        live = frame (mono, { { 0, juce::MidiMessage::noteOff (1, 64) } });
        require (live.values[0] == 1.0f, "Last-note fallback macro continuity/retrigger contract");
        configured.voiceMode = 0; mono.setSettings (configured);
        require (! mono.getMacroModulationSnapshot().hasActiveVoice, "Voice mode change left stale macro telemetry");
    }
}

void testMacroFastControlOracles()
{
    const auto note = juce::MidiMessage::noteOn (1, 60, 1.0f);
    const std::vector<Event> notes { { 0, note } };
    const auto macroAt = [] (int n) { return n == 0 ? .5f : .5f + .25f * independentAudioLfo (n - 1, 997, 48000); };
    const auto configure = [] (LfoAudioProbe& probe)
    {
        probe.sampleRate = 48000; probe.settings.macros.fill (0); probe.settings.macros[0] = .5f;
        probe.settings.lfoMode[9] = 1; probe.settings.lfoAudioRateEnabled[9] = true; probe.settings.lfoAudioRateHz[9] = 997;
        probe.settings.routes[1] = { 17, 28, .25f };
    };
    for (bool auxiliary : { false, true })
    {
        LfoAudioProbe pan; configure (pan);
        pan.settings.routes[0] = { auxiliary ? 4 : 6, 7, .5f };
        if (auxiliary) pan.settings.routes[0].auxSource = 6;
        pan.prepare(); pan.read (1027, notes);
        for (int n = 1; n < 1027; ++n) expectEnvelope (pan.pan (n), macroAt (n), "Audio-rate macro primary/AUX pan control");
        // Disable at a non-control-tick boundary. The cached pan must return
        // to base on the first subsequent sample, then resume old cadence.
        pan.settings.routes[1].enabled = false; pan.set(); pan.read (3);
        for (int n = 0; n < 3; ++n) expectEnvelope (pan.pan (n), .5, "Removed macro graph left cached pan modulation");
    }
    LfoAudioProbe phaseChain; configure (phaseChain);
    phaseChain.settings.lfoMode[0] = 1; phaseChain.settings.lfoShape[0] = 2; phaseChain.settings.lfoRate[0] = 1;
    phaseChain.settings.routes[0] = { 1, 7, .5f };
    phaseChain.settings.routes[2] = { 4, 18, .25f }; phaseChain.settings.routes[2].auxSource = 6;
    phaseChain.prepare(); phaseChain.read (1027, notes);
    for (int n = 1; n < 1027; ++n)
    {
        double position = n / 48000.0 + macroAt (n - 1) * .25f; position -= std::floor (position);
        expectEnvelope (phaseChain.pan (n), 2 * static_cast<float> (position) - 1,
                        "Fast AUX dependency failed across LFO-to-macro-to-slow-LFO phase");
    }
    phaseChain.settings.routes[2].enabled = false; phaseChain.set(); phaseChain.read (3);
    double pendingPosition = 1027 / 48000.0 + macroAt (1026) * .25f;
    pendingPosition -= std::floor (pendingPosition);
    expectEnvelope (phaseChain.pan (0), 2 * static_cast<float> (pendingPosition) - 1,
                    "Deleting a phase edge discarded its preceding-sample read");
    expectEnvelope (phaseChain.pan (1), 2 * static_cast<float> (1028 / 48000.0) - 1,
                    "Deleted macro-to-phase edge cached its final pending read beyond one sample");
    LfoAudioProbe pitch; configure (pitch); pitch.settings.osc[0].mode = 0;
    pitch.settings.routes[0] = { 6, 5, .2f }; pitch.prepare(); pitch.read (1024, notes);
    float phase = 0, amplitude = 0;
    const float attack = 1.0f / (.001f * 48000.0f), fundamental = 440 * std::exp2 ((60.0f - 69) / 12);
    for (int n = 0; n < 1024; ++n)
    {
        amplitude = std::min (1.0f, amplitude + attack);
        const float modulation = macroAt (n) * .2f;
        phase += fundamental * std::exp2 ((modulation * 12) / 12) / 48000;
        if (phase >= 1) phase -= 1;
        const float gain = amplitude * .23f;
        const double expected = pitch.actual.getWavetableSample (0, phase, 0) * pitch.settings.osc[0].level
            * std::cos (static_cast<float> (pi) * .25f) * gain * std::min (1.0f, n / 64.0f);
        require (std::abs (pitch.raw (pitch.wet, 0, n) - expected) < 2.0e-5, "Macro pitch was not integrated per sample");
    }
    for (int destination : { 1, 8, 9, 10 })
    {
        LfoAudioProbe filter; configure (filter); filter.settings.osc[0].mode = 0;
        const bool second = destination == 8 || destination == 10;
        filter.settings.filterEnabled = ! second; filter.settings.filter2Enabled = second;
        filter.settings.cutoff = filter.settings.filter2Cutoff = 800;
        filter.settings.resonance = filter.settings.filter2Resonance = .2f;
        filter.settings.routes[0] = { 4, destination, .15f }; filter.settings.routes[0].auxSource = 6;
        filter.prepare(); filter.read (1024, notes);
        double state1 = 0, state2 = 0; amplitude = 0;
        for (int n = 0; n < 1024; ++n)
        {
            amplitude = std::min (1.0f, amplitude + attack); const float gain = amplitude * .23f;
            const float mod = macroAt (n) * .15f;
            const float hz = destination <= 8 ? 800 * std::exp2 (mod * 5) : 800;
            const float resonance = destination >= 9 ? .2f + mod : .2f;
            const double g = std::tan (static_cast<float> (pi) * hz / 48000), damping = 2.0f - 1.94f * resonance;
            const double a1 = 1 / (1 + g * (g + damping)), a2 = g * a1, a3 = g * a2;
            const double input = filter.raw (filter.dry, 0, n) / gain;
            const double v3 = input - state2, v1 = a1 * state1 + a2 * v3, output = state2 + a2 * state1 + a3 * v3;
            state1 = 2 * v1 - state1; state2 = 2 * output - state2;
            require (std::abs (filter.raw (filter.wet, 0, n) - output * gain) < 4.0e-6,
                     "Macro AUX filter oracle failed for destination " + std::to_string (destination));
        }
    }
}

void testMacroSmoothingAndRemoval()
{
    LfoAudioProbe probe; probe.settings.macros.fill (.25f); probe.settings.macros[1] = .5f;
    probe.settings.routes[0] = { 6, 6, .5f }; probe.settings.routes[1] = { 7, 28, .5f };
    probe.prepare(); probe.set(); // consume the first-after-prepare settings snap before automation
    probe.read (1, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    float base = .25f, amount = .5f, preceding = .25f;
    const float alpha = 1.0f - std::exp (-1.0f / static_cast<float> (.015 * probe.sampleRate));
    probe.settings.macros[0] = .625f; probe.settings.routes[1].amount = -.25f; probe.set(); probe.read (257);
    for (int n = 0; n < 257; ++n)
    {
        base += alpha * (.625f - base); amount += alpha * (-.25f - amount);
        expectEnvelope (probe.level (n), std::clamp (base + preceding, 0.0f, 1.0f), "Base and route amount smoothing lost their sample ordering");
        preceding = .5f * amount;
    }
    probe.settings.routes[1].amount = 0; probe.set(); probe.read (9);
    for (int n = 0; n < 9; ++n)
    {
        base += alpha * (.625f - base); amount += alpha * (0 - amount);
        expectEnvelope (probe.level (n), base + preceding, "Target-zero amount incorrectly discarded its smoothing tail");
        preceding = .5f * amount;
    }
    probe.settings.routes[1].source = 0; probe.set(); probe.read (3);
    for (int n = 0; n < 3; ++n)
    {
        base += alpha * (.625f - base);
        expectEnvelope (probe.level (n), base, "Last incoming route Off did not clear its old pending sum immediately");
    }
}

void testMacroPermutationAndCompatibility()
{
    auto base = cleanSettings(); base.filterEnabled = false; base.macros.fill (.25f); base.macros[1] = .75f;
    base.routes = {}; base.routes[0] = { 6, 7, .5f };
    base.routes[1] = { 7, 28, .7f }; base.routes[2] = { 7, 28, -.3f };
    base.routes[3] = { 4, 28, -.2f }; base.routes[4] = { 1, 28, .4f };
    base.routes[8] = { 6, 18, .2f }; base.routes[9] = { 1, 7, .2f };
    base.lfoMode[0] = 1;
    SynthEngine fixed, moved; fixed.setSettings (base); moved.setSettings (base);
    fixed.prepare (48000, 512); moved.prepare (48000, 512);
    fixed.setSettings (base); moved.setSettings (base); // normal later edits must really smooth
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    require (audioDifference (render (fixed, 37, 37, note), render (moved, 37, 37, note)) == 0, "Permutation fixture initial audio");
    base.routes[1].amount = base.routes[2].amount = .125f; // identical descriptors, different in-flight smooth amounts
    base.routes[3].amount = -.5f; base.routes[4].amount = .1f;
    fixed.setSettings (base); moved.setSettings (base);
    require (audioDifference (render (fixed, 11, 11), render (moved, 11, 11)) == 0, "Permutation fixture smoothing audio");
    auto permutation = base;
    for (int pass = 0; pass < 5; ++pass)
    {
        std::rotate (permutation.routes.begin(), permutation.routes.begin() + 7, permutation.routes.end());
        fixed.setSettings (base); moved.setSettings (permutation);
        require (audioDifference (render (fixed, 113, 1), render (moved, 113, 47)) == 0,
                 "Pure row permutation changed a macro graph while amounts were smoothing");
        require (fixed.getMacroModulationSnapshot().values == moved.getMacroModulationSnapshot().values,
                 "Pure row permutation changed actual macro source values");
    }
    const auto audio = [&] (const SynthSettings& settings, int block = 127)
    { SynthEngine engine; engine.setSettings (settings); engine.prepare (48000, 512); return render (engine, 2048, block, note); };
    require (audioDifference (audio (base, 1), audio (base, 257)) == 0, "Macro/phase feedback depended on block partition");
    auto legacy = cleanSettings(); legacy.routes = {};
    legacy.routes[0] = { 1, 7, .2f }; legacy.routes[3] = { 4, 7, -.1f }; legacy.routes[9] = { 2, 1, .2f };
    auto unused = legacy; unused.routes[31] = { 4, 35, .75f };
    require (audioDifference (audio (legacy), audio (unused)) == 0, "Unused macro graph changed unrelated old target audio");
    for (int mode = 0; mode < 3; ++mode)
    {
        unused = legacy; unused.routes[31] = { 6, 28, .75f };
        if (mode == 0) unused.routes[31].enabled = false;
        if (mode == 1) unused.routes[31].source = 0;
        if (mode == 2) unused.routes[31].amount = 0;
        require (audioDifference (audio (legacy), audio (unused)) == 0, "Inactive macro destination changed legacy audio");
    }
    auto invalid = base, valid = base;
    invalid.macros[0] = std::numeric_limits<float>::quiet_NaN(); valid.macros[0] = 0;
    invalid.macros[1] = 8; valid.macros[1] = 1;
    invalid.routes[31] = { 99, 99, std::numeric_limits<float>::infinity() }; valid.routes[31] = { 24, 35, 0 };
    require (audioDifference (audio (invalid), audio (valid)) == 0, "Macro values or expanded destinations escaped sanitization");
    render (fixed, 4096, 127); render (moved, 4096, 127);
    fixed.setSettings (base); moved.setSettings (base); fixed.reset(); moved.reset();
    require (audioDifference (render (fixed, 1024, 1, note), render (moved, 1024, 257, note)) == 0,
             "Reset retained previous macro incoming sums");
}

void testMacroCoherentTelemetry()
{
    SynthEngine engine; auto settings = cleanSettings(); settings.macros.fill (1); settings.routes = {};
    // Each voice alternates a complete eight-value vector between all one and
    // all zero. An independent invariant detects a torn array immediately.
    for (int i = 0; i < 8; ++i) settings.routes[static_cast<size_t> (i)] = { anyvoc::macroSourceId (i), 28 + i, -1 };
    engine.setSettings (settings); engine.prepare (4096, 64);
    std::atomic<bool> finished { false }, bad { false };
    std::atomic<unsigned> reads { 0 };
    std::thread reader ([&]
    {
        while (! finished.load())
        {
            const auto point = engine.getMacroModulationSnapshot();
            if (point.hasActiveVoice != (point.voiceId != 0)) bad.store (true);
            for (float value : point.values) if (value != point.values[0] || (value != 0 && value != 1)) bad.store (true);
            ++reads;
        }
    });
    while (reads.load() == 0) std::this_thread::yield();
    juce::AudioBuffer<float> buffer (2, 1); juce::MidiBuffer midi;
    for (int n = 0; n < 10000; ++n)
    {
        midi.clear();
        if (n % 101 == 0) { engine.reset(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0); }
        if (n % 101 == 99) midi.addEvent (juce::MidiMessage::controllerEvent (1, 120, 0), 0);
        engine.render (buffer, midi);
    }
    finished.store (true); reader.join();
    require (reads.load() > 0 && ! bad.load(), "Macro telemetry did not publish one coherent actual-value/voice snapshot");
}

using ChaosPoint = std::array<double, 3>;

// Independent, fine-grid reference: no production derivative, integration,
// interpolation, initialization or normalization helper is called here.
ChaosPoint referenceChaosDerivative (int shape, const ChaosPoint& p)
{
    if (shape == 6) return { 10 * p[1] - 10 * p[0], 28 * p[0] - p[0] * p[2] - p[1],
                             p[0] * p[1] - 8 * p[2] / 3 };
    return { -p[1] - p[2], p[0] + p[1] / 5, 1.0 / 5 + p[0] * p[2] - 5.7 * p[2] };
}

void referenceChaosAdvance (ChaosPoint& p, int shape, double duration)
{
    constexpr int subdivisions = 32;
    const double h = duration / subdivisions;
    const auto shifted = [] (const ChaosPoint& start, const ChaosPoint& d, double amount)
    { return ChaosPoint { start[0] + amount * d[0], start[1] + amount * d[1], start[2] + amount * d[2] }; };
    for (int n = 0; n < subdivisions; ++n)
    {
        const auto a = referenceChaosDerivative (shape, p);
        const auto b = referenceChaosDerivative (shape, shifted (p, a, h / 2));
        const auto c = referenceChaosDerivative (shape, shifted (p, b, h / 2));
        const auto d = referenceChaosDerivative (shape, shifted (p, c, h));
        for (size_t axis = 0; axis < 3; ++axis)
            p[axis] += h * (a[axis] / 6 + b[axis] / 3 + c[axis] / 3 + d[axis] / 6);
    }
}

std::array<float, 3> referenceChaosNormalized (ChaosPoint p, int shape)
{
    const double xy = shape == 6 ? 10 : 6, z = shape == 6 ? 25 : 10;
    p[2] -= z;
    return { static_cast<float> (p[0] / (xy + std::abs (p[0]))),
             static_cast<float> (p[1] / (xy + std::abs (p[1]))),
             static_cast<float> (p[2] / (z + std::abs (p[2]))) };
}

double chaosPointDifference (const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    double difference = 0;
    for (size_t axis = 0; axis < 3; ++axis) difference = std::max (difference, std::abs (static_cast<double> (a[axis]) - b[axis]));
    return difference;
}

void testChaosNumericalReference()
{
    for (int shape : { 6, 7 })
        for (int index = 0; index < 10; ++index)
        {
            anyvoc::ChaosLfoState model;
            model.reset (shape, index);
            ChaosPoint reference { 1 + .01 * index, shape == 6 ? 1.0 : 0.0, shape == 6 ? 1.0 : 0.0 };
            require (model.rawPoint() == reference, "Chaos initial state depends on something other than LFO index");
            require (model.point() == referenceChaosNormalized (reference, shape), "Chaos XYZ softsign normalization differs");
            if (index != 0 && index != 9) continue;
            // Eight readings per numerical step exercise Hermite interpolation,
            // not only matching RK4 endpoints. Fine reference dt is 1/32768 of
            // a nominal cycle, versus production 1/128.
            for (int n = 0; n <= 1024; ++n)
            {
                const auto point = model.rawPoint();
                for (size_t axis = 0; axis < 3; ++axis)
                    require (std::abs (point[axis] - reference[axis]) < (shape == 6 ? 4.0e-4 : 2.0e-6),
                             "Fixed-grid Chaos/Hermite differs from high-resolution independent ODE");
                model.advance (1.0 / 1024);
                referenceChaosAdvance (reference, shape, (shape == 6 ? 1.0 : 6.0) / 1024);
            }
            require (model.getRecoveryCount() == 0, "Chaos normal initial trajectory activated recovery");
        }
}

void testChaosLongRunAndSampleRates()
{
    for (int shape : { 6, 7 })
    {
        for (int index = 0; index < 10; ++index)
        {
            anyvoc::ChaosLfoState model;
            model.reset (shape, index);
            std::array<float, 65> previous {};
            double difference1 = 0, difference64 = 0;
            for (int cycle = 0; cycle < 2000; ++cycle)
            {
                model.advance (1);
                const auto p = model.point();
                for (float value : p) require (std::isfinite (value) && std::abs (value) < 1, "Chaos escaped normalized bounds");
                for (double value : model.rawPoint()) require (std::isfinite (value) && std::abs (value) < 100, "Chaos escaped its physical attractor");
                if (cycle >= 65)
                {
                    difference1 += std::abs (p[0] - previous[static_cast<size_t> ((cycle - 1) % 65)]);
                    difference64 += std::abs (p[0] - previous[static_cast<size_t> ((cycle - 64) % 65)]);
                }
                previous[static_cast<size_t> (cycle % 65)] = p[0];
            }
            require (model.getRecoveryCount() == 0, "Normal 2000-cycle Chaos run used its safety reset");
            require (difference1 > 20 && difference64 > 20, "Chaos froze or repeated a short nominal-cycle table");
        }
        std::array<float, 3> reference {};
        for (int rate : { 8000, 22050, 44100, 48000, 96000, 192000 })
        {
            anyvoc::ChaosLfoState model;
            model.reset (shape, 9);
            // Two seconds gives identical physical instants at every rate,
            // and 997 Hz crosses multiple integration steps per audio sample.
            for (int n = 0; n < rate * 2; ++n) model.advance (997.0 / rate);
            if (rate == 8000) reference = model.point();
            else require (chaosPointDifference (model.point(), reference) < 2.0e-6, "Chaos trajectory depends on audio sample rate");
            require (model.getRecoveryCount() == 0, "Audio-rate Chaos used safety recovery");
            const auto before = model.point();
            model.advance (-1); model.advance (std::numeric_limits<double>::quiet_NaN());
            model.advance (std::numeric_limits<double>::infinity());
            require (before == model.point(), "Invalid Chaos delta altered the trajectory");
        }
    }
    anyvoc::ChaosLfoState dormant;
    dormant.reset (0, 9); dormant.advance (100);
    require (dormant.point() == std::array<float, 3> {}, "Off Chaos model is not dormant");
}

void testChaosTriggerOneShotAndLegato()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int shape : { 6, 7 })
    {
        auto settings = cleanSettings(); settings.amp.release = 2;
        settings.lfoShape.fill (shape); settings.lfoMode.fill (1); settings.lfoRate.fill (16);
        SynthEngine engine;
        engine.setSettings (settings); engine.prepare (4096, 256);
        render (engine, 1, 1, note);
        for (int index = 0; index < 10; ++index)
        {
            const ChaosPoint initial { 1 + .01 * index, shape == 6 ? 1.0 : 0.0, shape == 6 ? 1.0 : 0.0 };
            require (engine.getLfoChaosPoint (index) == referenceChaosNormalized (initial, shape), "New voice Chaos initial XYZ differs");
        }
        render (engine, 333, 127);
        const auto evolved = engine.getLfoChaosPoint (9);
        // Fill/steal all voices: each newest voice must start from the same
        // LFO-specific initial point, irrespective of age or stolen state.
        for (int voice = 0; voice < 18; ++voice)
        {
            render (engine, 1, 1, { { 0, juce::MidiMessage::noteOn (1, 40 + voice, .7f) } });
            const ChaosPoint initial { 1.09, shape == 6 ? 1.0 : 0.0, shape == 6 ? 1.0 : 0.0 };
            require (engine.getLfoChaosPoint (9) == referenceChaosNormalized (initial, shape), "Voice steal/retrigger retained an old Chaos trajectory");
        }
        require (evolved != engine.getLfoChaosPoint (9), "Trigger Chaos did not evolve");
        settings.lfoMode.fill (2); settings.lfoAudioRateEnabled.fill (true); settings.lfoAudioRateHz.fill (1000);
        engine.setSettings (settings); engine.reset();
        render (engine, 6, 6, note); // Period is 4.096 samples: clip final delta.
        anyvoc::ChaosLfoState endpoint; endpoint.reset (shape, 9); endpoint.advance (1, true);
        require (engine.getLfoChaosPoint (9) == endpoint.point(), "One-shot Chaos overshot its nominal-cycle endpoint");
        render (engine, 1000, 127);
        require (engine.getLfoChaosPoint (9) == endpoint.point(), "One-shot Chaos continued after completion");
        settings.lfoMode.fill (1); settings.lfoAudioRateEnabled.fill (false); settings.voiceMode = 2;
        engine.setSettings (settings); engine.reset();
        SynthEngine continuous; continuous.setSettings (settings); continuous.prepare (4096, 256);
        render (engine, 512, 127, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
            { 83, juce::MidiMessage::noteOn (1, 64, .7f) }, { 170, juce::MidiMessage::noteOff (1, 64) },
            { 230, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 260, juce::MidiMessage::noteOff (1, 60) } });
        render (continuous, 512, 127, note);
        require (engine.getLfoChaosPoint (9) == continuous.getLfoChaosPoint (9), "Legato overlap/fallback/sustain retriggered Chaos");
        render (engine, 31, 31, { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
        render (continuous, 31, 31);
        require (engine.getLfoChaosPoint (9) == continuous.getLfoChaosPoint (9), "Pedal release restarted Chaos while voice released");
        settings.voiceMode = 1; engine.setSettings (settings);
        render (engine, 333, 127, note);
        render (engine, 1, 1, { { 0, juce::MidiMessage::noteOn (1, 64, .7f) } });
        anyvoc::ChaosLfoState initial; initial.reset (shape, 9);
        require (engine.getLfoChaosPoint (9) == initial.point(), "Mono retrigger failed to restart Chaos");
    }
}

void testChaosTransportAndLiveModes()
{
    for (int shape : { 6, 7 })
    {
        auto settings = cleanSettings(); settings.lfoShape[9] = shape; settings.lfoSync[9] = true;
        settings.lfoFollowSwing[9] = true; settings.swingAmount = 1; settings.swingDivision = 1;
        settings.lfoDivision[9] = 6; // Quarter-beat duration; shared sixteenth swing.
        SynthEngine engine; engine.setSettings (settings); engine.prepare (4096, 256);
        anyvoc::ChaosLfoState reference; reference.reset (shape, 9);
        for (int n = 0; n < 1024; ++n)
        {
            // A transport seek changes the speed's grid location, never the ODE
            // initial state. Both positive and negative PPQ are intentional.
            const double ppq = (n < 300 ? .125 : n < 700 ? -25.3 : 167.25) + n / 4096.0 * 2;
            engine.setTransport (120, ppq, true, true);
            render (engine, 1, 1);
            require (chaosPointDifference (engine.getLfoChaosPoint (9), reference.point()) < 2e-6,
                     "Free Chaos aligned/reset to PPQ instead of continuously integrating swing speed");
            const double delta = (independentSwing (ppq + 2.0 / 4096, 1) - independentSwing (ppq, 1)) / .25;
            reference.advance (delta);
        }
        const auto next = reference.point();
        settings.lfoPhaseOffset[9] = .63f; settings.lfoDirection[9] = 2;
        settings.lfoInterpolation[9] = 1; settings.lfoPoints[9].fill (-1);
        settings.lfoAudioRateEnabled[9] = true; settings.lfoAudioRateHz[9] = 997;
        engine.setSettings (settings); render (engine, 1, 1);
        require (chaosPointDifference (engine.getLfoChaosPoint (9), next) < 2e-6,
                 "Inactive phase/drawing/audio settings reset synced Chaos");
        // Mode and algorithm edits reset immediately at the next actual read,
        // even if a note was already sounding. Off then on also starts fresh.
        settings.lfoMode[9] = 1; engine.setSettings (settings);
        render (engine, 1, 1, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
        reference.reset (shape, 9);
        require (engine.getLfoChaosPoint (9) == reference.point(), "Changing Chaos play mode failed to reset");
        render (engine, 100, 127);
        settings.lfoShape[9] = shape == 6 ? 7 : 6; engine.setSettings (settings);
        render (engine, 1, 1); reference.reset (settings.lfoShape[9], 9);
        require (engine.getLfoChaosPoint (9) == reference.point(), "Changing Chaos algorithm retained previous model state");
        settings.lfoShape[9] = 5; engine.setSettings (settings); render (engine, 100, 127);
        require (engine.getLfoChaosPoint (9) == std::array<float, 3> {}, "Chaos Off retained a visible trajectory");
        settings.lfoShape[9] = shape; engine.setSettings (settings); render (engine, 1, 1); reference.reset (shape, 9);
        require (engine.getLfoChaosPoint (9) == reference.point(), "Re-enabled Chaos resumed stale model state");
        settings.amp.release = .002f; engine.setSettings (settings);
        render (engine, 128, 32, { { 0, juce::MidiMessage::noteOff (1, 60) } });
        const auto oldPoint = engine.getLfoChaosPoint (9);
        require (oldPoint != std::array<float, 3> {}, "Idle telemetry fixture never read a Chaos point");
        settings.lfoShape[9] = shape == 6 ? 7 : 6; engine.setSettings (settings);
        require (engine.getLfoChaosPoint (9) == std::array<float, 3> {}, "Idle algorithm edit left previous Chaos point published");
        render (engine, 1024, 127);
        require (engine.getLfoChaosPoint (9) == std::array<float, 3> {}, "Idle Trigger fabricated a read in the new Chaos epoch");
        settings.lfoMode[9] = 2; engine.setSettings (settings); render (engine, 1024, 127);
        require (engine.getLfoChaosPoint (9) == std::array<float, 3> {}, "Idle mode edit restored old trajectory telemetry");
    }
    // Switching to Chaos does not stop the remembered base shape's clock.
    // Compare against uninterrupted base-shape engines after an active note
    // crosses several nominal cycles, including the one-shot endpoint.
    for (int shape : { 2, 4, 5 })
        for (int mode : { 0, 1, 2 })
            for (int chaosShape : { 6, 7 })
            {
                auto settings = cleanSettings(); settings.filterEnabled = false;
                settings.lfoShape[0] = shape; settings.lfoMode[0] = mode; settings.lfoRate[0] = 40;
                settings.routes[0] = { 1, 6, .4f };
                for (size_t p = 0; p < 33; ++p) settings.lfoPoints[0][p] = -.8f + .05f * static_cast<float> (p);
                SynthEngine switched, reference;
                switched.setSettings (settings); reference.setSettings (settings);
                switched.prepare (4096, 256); reference.prepare (4096, 256);
                const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
                render (switched, 37, 37, note); render (reference, 37, 37, note);
                auto chaos = settings; chaos.lfoShape[0] = chaosShape; switched.setSettings (chaos);
                render (switched, 333, 127); render (reference, 333, 127);
                switched.setSettings (settings);
                const auto a = render (switched, 1024, 127), b = render (reference, 1024, 127);
                require (audioDifference (a, b) == 0 && switched.getLfoPhase (0) == reference.getLfoPhase (0),
                         "Chaos froze the remembered periodic/random voice clock or one-shot endpoint");
            }
}

void testChaosAudioRateOracles()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int shape : { 6, 7 })
        for (bool auxiliary : { false, true })
        {
            LfoAudioProbe probe;
            probe.settings.lfoShape[9] = shape; probe.settings.lfoMode[9] = 1;
            probe.settings.lfoAudioRateEnabled[9] = true; probe.settings.lfoAudioRateHz[9] = 997;
            probe.settings.routes[0] = { 17, 7, .5f };
            if (auxiliary)
            {
                probe.settings.macros[0] = 1;
                probe.settings.routes[0].source = 6; probe.settings.routes[0].auxSource = 17;
            }
            probe.prepare(); probe.read (1024, note);
            anyvoc::ChaosLfoState model; model.reset (shape, 9);
            for (int n = 0; n < 1024; ++n)
            {
                if (n >= 64) require (std::abs (probe.pan (n) - model.point()[0]) < 2e-5,
                                     "Chaos primary/auxiliary audio-rate pan did not update per sample");
                model.advance (997.0 / probe.sampleRate);
            }
            LfoAudioProbe chain;
            chain.settings.lfoShape[9] = shape; chain.settings.lfoMode[0] = chain.settings.lfoMode[9] = 1;
            chain.settings.lfoRate[0] = 3;
            chain.settings.lfoAudioRateEnabled[9] = true; chain.settings.lfoAudioRateHz[9] = 997;
            chain.settings.macros[0] = 1;
            chain.settings.routes[0] = { auxiliary ? 6 : 17, 18, .15f };
            if (auxiliary) chain.settings.routes[0].auxSource = 17;
            chain.settings.routes[1] = { 1, 7, .5f };
            chain.prepare(); chain.read (1024, note); model.reset (shape, 9);
            float previous = 0;
            for (int n = 0; n < 1024; ++n)
            {
                const double clock = std::fmod (n * 3.0 / chain.sampleRate, 1.0);
                const float phase = static_cast<float> (independentLfoRead (clock, previous, 0));
                const float expected = std::sin (static_cast<float> (2 * pi) * phase);
                if (n >= 64) require (std::abs (chain.pan (n) - expected) < 3e-5,
                                     "Chaos phase dependency failed to propagate audio-rate quality");
                previous = model.point()[0] * .15f;
                model.advance (997.0 / chain.sampleRate);
            }
        }
}

void testChaosIdentityPartitionAndReset()
{
    auto settings = cleanSettings(); settings.filterEnabled = false; settings.noise = .13f;
    settings.osc[0].unison = 3; settings.routes[0] = { 1, 6, .2f }; settings.lfoShape[0] = 4;
    const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 60, .7f) },
        { 301, juce::MidiMessage::noteOn (1, 64, .8f) }, { 1701, juce::MidiMessage::noteOff (1, 60) } };
    const auto audio = [&] (SynthSettings s, int block)
    { SynthEngine engine; engine.setSettings (s); engine.prepare (48000, 512); return render (engine, 8192, block, notes); };
    auto unused = settings;
    for (size_t i = 1; i < 10; ++i)
    { unused.lfoShape[i] = i % 2 == 0 ? 6 : 7; unused.lfoMode[i] = static_cast<int> (i % 3); unused.lfoAudioRateEnabled[i] = true; unused.lfoAudioRateHz[i] = 997; }
    require (audioDifference (audio (settings, 127), audio (unused, 127)) == 0,
             "Unused Chaos changed legacy noise, random LFO, oscillator RNG or audio");
    settings.lfoShape[0] = 6; settings.lfoShape[9] = 7; settings.lfoMode[0] = settings.lfoMode[9] = 1;
    settings.lfoAudioRateEnabled[0] = settings.lfoAudioRateEnabled[9] = true;
    settings.lfoAudioRateHz[0] = 997; settings.lfoAudioRateHz[9] = 823;
    settings.routes[1] = { 17, 7, .3f }; settings.routes[0].auxSource = 17;
    require (audioDifference (audio (settings, 1), audio (settings, 257)) == 0, "Chaos audio depends on block partition");
    auto ignored = settings; ignored.lfoPhaseOffset.fill (.77f); ignored.lfoDirection.fill (2);
    ignored.routes[30] = { 2, 18, .9f }; ignored.routes[31] = { 1, 27, -.8f };
    require (audioDifference (audio (settings, 127), audio (ignored, 127)) == 0, "Chaos phase/direction/phase target altered audio or update cadence");
    SynthEngine reset, fresh; reset.setSettings (settings); fresh.setSettings (settings);
    reset.prepare (48000, 512); fresh.prepare (48000, 512);
    render (reset, 8192, 127, notes);
    // Match effects' initialization before comparing the synthesis reset.
    // JUCE Chorus starts at mix .5 and ramps to our dry setting over 50 ms;
    // Reverb's default gains also ramp on first render. A used/reset engine
    // has already settled those targets, unlike a never-rendered instance.
    render (fresh, 4096, 127); reset.reset(); fresh.reset();
    require (reset.getLfoChaosPoint (0) == std::array<float, 3> {} && reset.getLfoChaosPoint (9) == std::array<float, 3> {},
             "Reset retained a published Chaos point");
    require (audioDifference (render (reset, 8192, 127, notes), render (fresh, 8192, 257, notes)) == 0,
             "Reset retained Chaos integration state");
    require (reset.getLfoChaosPoint (0) == fresh.getLfoChaosPoint (0)
             && reset.getLfoChaosPoint (9) == fresh.getLfoChaosPoint (9), "Reset Chaos XYZ differs from an identically initialized engine");
    render (reset, 1, 1, { { 0, juce::MidiMessage::controllerEvent (1, 120, 0) } });
    require (inspect (render (reset, 512, 127)).peak == 0, "Chaos retained audio after All Sound Off");
    require (reset.getLfoChaosPoint (-1) == std::array<float, 3> {} && reset.getLfoChaosPoint (10) == std::array<float, 3> {},
             "Invalid Chaos telemetry index was not safe");
}

void testChaosCoherentTelemetry()
{
    // The APVTS/UI can change before setSettings/render reaches its next audio
    // block. Expected metadata must reject the previous algorithm/mode's point
    // during that gap; the unfiltered API remains backwards compatible.
    {
        auto settings = cleanSettings(); settings.lfoShape[0] = 6;
        settings.lfoRate[0] = 16;
        SynthEngine engine; engine.setSettings (settings); engine.prepare (4096, 128);
        render (engine, 97, 32);
        const auto lorenz = engine.getLfoChaosPoint (0);
        const std::array<float, 3> empty {};
        require (lorenz != empty && engine.getLfoChaosPoint (0, 6, 0) == lorenz
                 && engine.getLfoChaosPoint (0, -1, 0) == lorenz && engine.getLfoChaosPoint (0, 6) == lorenz,
                 "Chaos matching metadata or wildcard lost a valid point");
        require (engine.getLfoChaosPoint (0, 7, 0) == empty && engine.getLfoChaosPoint (0, 6, 1) == empty
                 && engine.getLfoChaosPoint (0) == lorenz,
                 "UI selection before the audio update accepted stale Chaos shape/mode metadata");
        settings.lfoShape[0] = 7; engine.setSettings (settings);
        require (engine.getLfoChaosPoint (0, 7, 0) == empty, "New Chaos metadata published a point before any read");
        render (engine, 1, 1);
        const auto rossler = referenceChaosNormalized ({ 1, 0, 0 }, 7);
        require (engine.getLfoChaosPoint (0, 7, 0) == rossler && engine.getLfoChaosPoint (0, 6, 0) == empty,
                 "New audio block did not publish matching Chaos shape metadata");
        require (engine.getLfoChaosPoint (0, 7, 1) == empty && engine.getLfoChaosPoint (0) == rossler,
                 "UI play-mode change accepted the preceding mode's point");
        settings.lfoMode[0] = 1; engine.setSettings (settings);
        render (engine, 1, 1, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } });
        require (engine.getLfoChaosPoint (0, 7, 1) == rossler && engine.getLfoChaosPoint (0, 7, 0) == empty,
                 "New voice point and its play-mode metadata were inconsistent");
        // Undo can expose the old UI choice before the audio block applies it.
        require (engine.getLfoChaosPoint (0, 6, 1) == empty, "Undo accepted the old Rossler point as Lorenz");
        settings.lfoShape[0] = 6; engine.setSettings (settings); render (engine, 1, 1);
        require (engine.getLfoChaosPoint (0, 6, 1) == referenceChaosNormalized ({ 1, 1, 1 }, 6)
                 && engine.getLfoChaosPoint (0, 7, 1) == empty, "Undo's audio update did not replace the metadata epoch");
        engine.reset();
        require (engine.getLfoChaosPoint (0, 6, 1) == empty, "Reset retained a filtered Chaos snapshot");
    }
    constexpr int frames = 8192;
    auto settings = cleanSettings(); settings.lfoShape[0] = 6;
    settings.lfoAudioRateEnabled[0] = true; settings.lfoAudioRateHz[0] = 997;
    SynthEngine engine; engine.setSettings (settings); engine.prepare (4096, 1);
    anyvoc::ChaosLfoState model; model.reset (6, 0);
    std::vector<std::array<float, 3>> possible { {} };
    for (int n = 0; n < frames; ++n) { possible.push_back (model.point()); model.advance (997.0 / 4096); }
    std::sort (possible.begin(), possible.end());
    std::atomic<bool> done { false }, bad { false };
    std::thread writer ([&]
    {
        juce::AudioBuffer<float> block (2, 1); juce::MidiBuffer midi;
        for (int n = 0; n < frames; ++n) engine.render (block, midi);
        done.store (true);
    });
    size_t reads = 0;
    do
    {
        const auto point = engine.getLfoChaosPoint (0);
        if (! std::binary_search (possible.begin(), possible.end(), point)) bad.store (true);
        const auto filtered = engine.getLfoChaosPoint (0, 6, 0);
        if (! std::binary_search (possible.begin(), possible.end(), filtered)
            || engine.getLfoChaosPoint (0, 7, 0) != std::array<float, 3> {}) bad.store (true);
        ++reads;
    } while (! done.load());
    writer.join();
    require (! bad.load() && reads > 0, "Chaos UI telemetry combined coordinates from different DSP samples");
}

void testExpandedSourceIsolationAndInvalids()
{
    auto legacy = cleanSettings();
    legacy.osc[0].unison = 3;
    legacy.osc[0].position = .57f;
    legacy.noise = .15f;
    legacy.routes[0] = { 1, 7, .3f };
    legacy.routes[1] = { 2, 2, .2f };
    legacy.routes[2] = { 6, 6, .1f };
    legacy.macros[0] = .3f;
    auto changed = legacy;
    for (size_t index = 2; index < anyvoc::lfoCount; ++index)
    {
        changed.lfoRate[index] = 3.0f * static_cast<float> (index);
        changed.lfoShape[index] = 4;
        changed.lfoMode[index] = static_cast<int> (index % 3);
        changed.lfoPoints[index].fill (.75f);
    }
    changed.extraEnvelopes = { anyvoc::EnvelopeSettings { .3f, .7f, .8f, .04f },
                               anyvoc::EnvelopeSettings { .2f, .1f, .3f, .2f } };
    for (size_t index = 4; index < anyvoc::macroCount; ++index) changed.macros[index] = 1;
    require (audioDifference (renderWarpFixture (legacy), renderWarpFixture (changed)) == 0,
             "Unused new sources changed legacy oscillator/noise RNG or audible routes");

    auto invalid = cleanSettings(), valid = invalid;
    invalid.lfoRate[9] = 99; valid.lfoRate[9] = 40;
    invalid.lfoShape[9] = 99; valid.lfoShape[9] = 7;
    invalid.lfoMode[9] = 99; valid.lfoMode[9] = 2;
    invalid.lfoDivision[9] = 99; valid.lfoDivision[9] = 11;
    invalid.lfoInterpolation[9] = 99; valid.lfoInterpolation[9] = 1;
    invalid.lfoPoints[9][4] = 2; valid.lfoPoints[9][4] = 1;
    invalid.lfoPoints[9][5] = -2; valid.lfoPoints[9][5] = -1;
    invalid.lfoPoints[9][6] = std::numeric_limits<float>::quiet_NaN();
    invalid.extraEnvelopes[1] = { std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
        -std::numeric_limits<float>::infinity() };
    valid.extraEnvelopes[1] = { .01f, .2f, .7f, .3f };
    invalid.macros[7] = std::numeric_limits<float>::infinity(); valid.macros[7] = 0;
    invalid.routes[0] = valid.routes[0] = { 17, 7, .5f, true, 0, 20, 0, .25f, -.25f };
    invalid.routes[1] = valid.routes[1] = { 24, 6, .2f };
    require (audioDifference (renderWarpFixture (invalid), renderWarpFixture (valid)) == 0,
             "Expanded LFO/envelope/macro controls were not fully sanitised");
}

void testWarpIdentityAndSourceModes()
{
    auto settings = cleanSettings();
    settings.osc[0].position = .57f;
    settings.osc[0].unison = 3;
    settings.osc[0].detune = 9;
    settings.osc[1].level = .12f;
    settings.osc[1].position = .28f;
    const auto original = renderWarpFixture (settings);
    for (int mode = 1; mode <= 6; ++mode)
    {
        auto identity = settings;
        for (auto& oscillator : identity.osc)
        {
            oscillator.warpMode = { mode, 7 - mode };
            oscillator.warpAmount = { 0, 0 };
        }
        require (audioDifference (original, renderWarpFixture (identity)) == 0,
                 "A zero-depth warp changed legacy wavetable audio");
    }
    for (auto& oscillator : settings.osc)
        oscillator.warpAmount = { .71f, 1.0f };
    require (audioDifference (original, renderWarpFixture (settings)) == 0,
             "Off warp modes changed audio with nonzero stored amounts");

    const auto sample = makeTone (48000, .5, 330, 2);
    for (const int mode : { 1, 2 })
    {
        auto unwarpedSettings = cleanSettings();
        unwarpedSettings.osc[0].mode = mode;
        unwarpedSettings.osc[0].sampleLoop = true;
        unwarpedSettings.osc[0].position = .35f;
        auto warpedSettings = unwarpedSettings;
        warpedSettings.osc[0].warpMode = { 4, 6 };
        warpedSettings.osc[0].warpAmount = { 1, 1 };
        SynthEngine unwarpedEngine, warpedEngine;
        prepareSampleEngine (unwarpedEngine, unwarpedSettings, sample, 48000);
        prepareSampleEngine (warpedEngine, warpedSettings, sample, 48000);
        const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
        const auto before = render (unwarpedEngine, 8192, 127, note);
        const auto after = render (warpedEngine, 8192, 127, note);
        require (inspect (before).rms > .001 && audioDifference (before, after) == 0,
                 "Wavetable warp settings altered sample or granular playback");
    }
}

void testWarpModulationDestinations()
{
    for (int destination = 12; destination <= 17; ++destination)
    {
        const int oscillator = (destination - 12) / 2;
        const int slot = (destination - 12) % 2;
        auto direct = cleanSettings();
        for (auto& osc : direct.osc) osc.level = 0;
        direct.osc[static_cast<size_t> (oscillator)].level = .25f;
        direct.osc[static_cast<size_t> (oscillator)].warpMode[static_cast<size_t> (slot)] = 1;
        auto raw = direct;
        direct.osc[static_cast<size_t> (oscillator)].warpAmount[static_cast<size_t> (slot)] = .6f;
        auto modulated = raw;
        modulated.macros[0] = 1;
        modulated.routes[0] = { 6, destination, .6f };
        const auto expected = renderWarpFixture (direct);
        const auto actual = renderWarpFixture (modulated);
        require (audioDifference (expected, actual) < 2.0e-5,
                 "Warp modulation did not reach the correct oscillator and slot: " + std::to_string (destination));
        require (audioDifference (actual, renderWarpFixture (raw)) > .001,
                 "The assigned warp modulation destination had no audible effect");
    }
}

void testWarpAutomationPartitionAndReset()
{
    auto settings = cleanSettings();
    settings.osc[0].warpMode = { 3, 6 };
    settings.osc[0].warpAmount = { .73f, .6f };
    settings.routes[0] = { 1, 12, .2f };
    settings.lfoRate[0] = 2.3f;
    require (audioDifference (renderWarpFixture (settings, 127), renderWarpFixture (settings, 257)) < 1.0e-6,
             "Dual-warp audio depended on the host block partition");
    SynthEngine engine;
    engine.setSettings (settings);
    engine.prepare (48000, 512);
    render (engine, 2048, 127, { { 0, juce::MidiMessage::noteOn (1, 93, .9f) } });
    for (int change = 0; change < 35; ++change)
    {
        settings.osc[0].warpMode = { change % 7, (change + 3) % 7 };
        settings.osc[0].warpAmount = { change % 2 ? 0.0f : 1.0f, change % 3 ? .5f : 1.0f };
        engine.setSettings (settings);
        render (engine, 31, 7);
    }
    settings.osc[0].warpMode = { -5, 100 };
    settings.osc[0].warpAmount = { std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity() };
    engine.setSettings (settings);
    render (engine, 4096, 127);
    engine.reset();
    require (inspect (render (engine, 4096, 127)).peak < 1.0e-7,
             "Reset left warp transitions or voices audible");
}

void testWarpDcAndMipContinuity()
{
    auto settings = cleanSettings();
    settings.filterEnabled = settings.filter2Enabled = false;
    settings.osc[0].level = .25f;
    settings.osc[0].position = 2.0f / 3.0f; // Saw: enough upper partials to expose mip jumps.
    settings.amp = { .001f, .01f, 1.0f, .03f };
    const auto steady = [] (const SynthSettings& value, int note, int samples)
    {
        SynthEngine engine;
        engine.setSettings (value);
        engine.prepare (48000, 512);
        render (engine, 24000, 127, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });
        return render (engine, samples, 257);
    };
    const auto relativeDifference = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double difference = 0, reference = 0;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < a.getNumSamples(); ++sample)
            {
                const auto x = static_cast<double> (a.getSample (channel, sample));
                const auto delta = x - b.getSample (channel, sample);
                difference += delta * delta;
                reference += x * x;
            }
        require (reference > 1.0e-6, "Warp continuity fixture was silent");
        return std::sqrt (difference / reference);
    };
    const auto dry = steady (settings, 84, 8192);
    settings.osc[0].warpMode[0] = 1;
    settings.osc[0].warpAmount[0] = .0001f;
    require (relativeDifference (dry, steady (settings, 84, 8192)) < .08,
             "A near-zero warp abruptly removed upper saw harmonics");
    for (const auto boundary : { std::pair<int, float> { 1, 1.0f / 3.0f },
                                std::pair<int, float> { 4, 1.0f / 7.0f },
                                std::pair<int, float> { 4, 3.0f / 7.0f } })
    {
        settings.osc[0].warpMode[0] = boundary.first;
        settings.osc[0].warpAmount[0] = boundary.second - .0001f;
        const auto below = steady (settings, 84, 8192);
        settings.osc[0].warpAmount[0] = boundary.second + .0001f;
        require (relativeDifference (below, steady (settings, 84, 8192)) < .04,
                 "Warp amount caused a discrete harmonic-level jump at a mip boundary");
    }

    settings.osc[0].warpMode = { 3, 0 };
    settings.osc[0].warpAmount = { .8f, 0 };
    SynthEngine preview;
    double previewMean = 0;
    for (int point = 0; point < 2048; ++point)
        previewMean += preview.getWarpedWavetableSample (0, point / 2048.0f, settings.osc[0].position,
                                                        settings.osc[0].warpMode, settings.osc[0].warpAmount) / 2048;
    require (std::abs (previewMean) > .1, "The asymmetric warp fixture did not contain a DC component");
    const auto centred = steady (settings, 69, 12000); // 110 complete A4 periods.
    double mean = 0;
    for (int sample = 0; sample < centred.getNumSamples(); ++sample)
        mean += centred.getSample (0, sample) / centred.getNumSamples();
    const auto energy = inspect (centred).rms;
    require (energy > .001 && std::abs (mean) < energy * .02,
             "Asymmetric warp retained DC in the late audio window or was silenced");

    // A 1 Hz sine LFO drives PW through its positive half-cycle and then clamps
    // its amount to zero at 0.5 seconds. Subtract an identical dry saw so the
    // oscillator's own rapid edges cannot be mistaken for a correction jump.
    settings.osc[0].warpAmount = { 0, 0 };
    settings.lfoRate[0] = 1;
    settings.lfoShape[0] = 0;
    settings.lfoMode[0] = 0;
    settings.routes[0] = { 1, 12, .9f };
    auto drySettings = settings;
    drySettings.osc[0].warpMode = { 0, 0 };
    SynthEngine modulated, reference;
    modulated.setSettings (settings); reference.setSettings (drySettings);
    modulated.prepare (48000, 512); reference.prepare (48000, 512);
    constexpr int crossing = 24000, length = crossing + 2048;
    const std::vector<Event> onset { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } };
    const auto withWarp = render (modulated, length, 127, onset);
    const auto withoutWarp = render (reference, length, 127, onset);
    const auto residual = [&] (int sample)
    {
        return static_cast<double> (withWarp.getSample (0, sample)) - withoutWarp.getSample (0, sample);
    };
    double beforeCrossing = 0;
    for (int sample = crossing - 64; sample < crossing - 32; ++sample)
        beforeCrossing += residual (sample) / 32;
    require (std::abs (beforeCrossing) > .0001,
             "LFO zero-crossing fixture did not accumulate a measurable DC correction");
    double largestJump = 0;
    for (int sample = crossing - 4; sample <= crossing + 4; ++sample)
        largestJump = std::max (largestJump, std::abs (residual (sample) - residual (sample - 1)));
    const double maximumJump = std::abs (beforeCrossing) * .2 + 1.0e-6;
    require (largestJump < maximumJump,
             "Warp DC correction jumped when the LFO amount crossed zero; jump="
                 + std::to_string (largestJump) + ", limit=" + std::to_string (maximumJump));
    double settledResidual = 0;
    for (int sample = crossing + 480; sample < length; ++sample)
        settledResidual = std::max (settledResidual, std::abs (residual (sample)));
    require (settledResidual < 1.0e-7,
             "Zero-clamped LFO warp did not return to the dry waveform after the correction fade");
    std::cout << "Warp zero-crossing: correction=" << std::abs (beforeCrossing)
              << ", max jump=" << largestJump << ", settled residual=" << settledResidual << '\n';
}

void testFractionalDelayWrap()
{
    auto settings = cleanSettings();
    settings.filterEnabled = settings.filter2Enabled = false;
    settings.osc[0].level = .3f;
    settings.amp = { .0005f, .01f, 1.0f, .03f };
    settings.master = .7f;
    settings.delayTime = .3f;
    settings.delayFeedback = 0;

    // A cosine provides a measurable early attack despite the engine's source
    // startup fade. Keep levels low so the output soft ceiling can be inverted
    // accurately without depending on oscillator internals.
    juce::AudioBuffer<float> cycle (1, 2048);
    for (int sample = 0; sample < cycle.getNumSamples(); ++sample)
        cycle.setSample (0, sample, .9f * static_cast<float> (std::cos (2 * pi * sample / 2048)));
    const auto table = SynthEngine::makeWavetable (cycle, 2048, false);
    require (table != nullptr, "Delay reference wavetable could not be constructed");

    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        SynthEngine dryEngine, wetEngine;
        settings.delayMix = 0;
        dryEngine.setSettings (settings);
        settings.delayMix = .65f;
        wetEngine.setSettings (settings);
        dryEngine.prepare (rate, 512); wetEngine.prepare (rate, 512);
        dryEngine.setUserWavetable (0, table); wetEngine.setUserWavetable (0, table);
        const double delaySamples = static_cast<double> (settings.delayTime) * rate;
        const int ringLength = static_cast<int> (std::ceil (rate * 2.1)) + 8;
        // Continue beyond one complete write-ring revolution and the next
        // zero/read-end boundary, when both neighboring history values exist.
        const int length = ringLength + static_cast<int> (std::ceil (delaySamples)) + 1024;
        // JUCE's reverb dry gain ramps from its constructor value to unity.
        // Let the effects settle in silence so inverse tanh recovers exactly
        // the signal written to the delay, rather than that startup gain ramp.
        constexpr int noteStart = 2048;
        const std::vector<Event> onset { { noteStart, juce::MidiMessage::noteOn (1, 69, 1.0f) } };
        const auto dry = render (dryEngine, length, 127, onset);
        const auto wet = render (wetEngine, length, 127, onset);
        require (dry.getMagnitude (0, noteStart, 64) > .0001f,
                 "Fractional delay fixture must have a measurable early attack");
        require (dry.getMagnitude (0, ringLength - 8, 16) > .001f,
                 "Fractional delay fixture must contain audio around the later ring boundary");

        double maximumError = 0;
        int worstSample = 0;
        for (int channel = 0; channel < 2; ++channel)
        {
            std::vector<double> input (static_cast<size_t> (length));
            std::vector<double> history (static_cast<size_t> (length));
            for (int sample = 0; sample < length; ++sample)
            {
                input[static_cast<size_t> (sample)] = std::atanh (static_cast<double> (dry.getSample (channel, sample)))
                                                      / static_cast<double> (settings.master);
                history[static_cast<size_t> (sample)] = std::tanh (input[static_cast<size_t> (sample)]);
            }
            const auto past = [&] (int sample)
            {
                return sample < 0 ? 0.0 : history[static_cast<size_t> (sample)];
            };
            for (int sample = 0; sample < length; ++sample)
            {
                // The oracle uses an unbounded time line: no circular buffer,
                // modulo, or engine read-index arithmetic is reproduced here.
                const double position = static_cast<double> (sample) - delaySamples;
                const int earlier = static_cast<int> (std::floor (position));
                const double fraction = position - earlier;
                const double echo = past (earlier) * (1 - fraction) + past (earlier + 1) * fraction;
                const double expected = std::tanh ((input[static_cast<size_t> (sample)]
                                                   + echo * settings.delayMix) * settings.master);
                const double error = std::abs (expected - wet.getSample (channel, sample));
                if (error > maximumError)
                {
                    maximumError = error;
                    worstSample = sample;
                }
            }
        }
        require (maximumError < 2.0e-6,
                 "Fractional delay disagreed with the independent timeline at " + std::to_string (rate)
                     + " Hz, sample " + std::to_string (worstSample) + "; error=" + std::to_string (maximumError));
        std::cout << "Delay reference: rate=" << rate << ", maximum error=" << maximumError
                  << ", worst sample=" << worstSample << '\n';
    }
}

SynthSettings voicingSettings (int mode)
{
    auto value = cleanSettings();
    value.voiceMode = mode;
    value.filterEnabled = value.filter2Enabled = false;
    value.osc[0].level = .35f;
    value.master = .4f;
    value.amp = { .001f, .015f, 1.0f, .02f };
    return value;
}

double frequencyAt (const juce::AudioBuffer<float>& audio, int centre, int radius, double rate)
{
    std::vector<double> crossings;
    for (int sample = std::max (1, centre - radius); sample < std::min (audio.getNumSamples(), centre + radius); ++sample)
    {
        const double a = audio.getSample (0, sample - 1), b = audio.getSample (0, sample);
        if (a <= 0 && b > 0) crossings.push_back (sample - 1.0 - a / (b - a));
    }
    require (crossings.size() >= 4, "Too few positive crossings in the local pitch window");
    return rate * (crossings.size() - 1) / (crossings.back() - crossings.front());
}

void expectNote (const juce::AudioBuffer<float>& audio, int note)
{
    const double wanted = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const double measured = estimateFrequency (audio, 48000);
    require (std::abs (measured - wanted) < 2,
             "Monophonic note priority/fallback selected the wrong pitch; expected=" + std::to_string (wanted)
                 + ", measured=" + std::to_string (measured));
}

void testMonoPriorityCountsAndControllers()
{
    for (const int mode : { 1, 2 })
    {
        SynthEngine engine;
        engine.setSettings (voicingSettings (mode)); engine.prepare (48000, 512);
        const auto play = [&] (const std::vector<Event>& events)
        {
            return render (engine, 8000, 127, events);
        };
        expectNote (play ({ { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }), 60);
        expectNote (play ({ { 0, juce::MidiMessage::noteOn (2, 67, .8f) } }), 67);
        expectNote (play ({ { 0, juce::MidiMessage::noteOn (1, 64, .8f) } }), 64);
        expectNote (play ({ { 0, juce::MidiMessage::noteOff (1, 64) } }), 67);
        expectNote (play ({ { 0, juce::MidiMessage::noteOff (1, 60) } }), 67);
        play ({ { 0, juce::MidiMessage::noteOff (2, 67) } });
        require (inspect (render (engine, 2048, 127)).peak < 1.0e-6, "Mono/Legato left a voice after its last release");

        engine.reset();
        play ({ { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
                { 300, juce::MidiMessage::noteOn (1, 60, .8f) },
                { 500, juce::MidiMessage::noteOn (2, 67, .8f) } });
        expectNote (play ({ { 0, juce::MidiMessage::noteOff (2, 67) } }), 60);
        expectNote (play ({ { 0, juce::MidiMessage::noteOff (1, 60) } }), 60);
        play ({ { 0, juce::MidiMessage::noteOff (1, 60) } });
        require (inspect (render (engine, 2048, 127)).peak < 1.0e-6, "Repeated note-on count did not release after matching offs");

        engine.reset();
        play ({ { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
                { 100, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 200, juce::MidiMessage::noteOff (1, 60) },
                { 300, juce::MidiMessage::noteOn (2, 67, .8f) },
                { 400, juce::MidiMessage::controllerEvent (2, 64, 127) },
                { 500, juce::MidiMessage::noteOff (2, 67) } });
        expectNote (play ({ { 0, juce::MidiMessage::controllerEvent (2, 64, 0) } }), 60);
        play ({ { 0, juce::MidiMessage::controllerEvent (1, 121, 0) } });
        require (inspect (render (engine, 2048, 127)).peak < 1.0e-6, "Reset-controllers failed to release that channel's latched note");

        engine.reset();
        play ({ { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
                { 100, juce::MidiMessage::noteOn (2, 67, .8f) },
                { 200, juce::MidiMessage::controllerEvent (2, 64, 127) } });
        expectNote (play ({ { 0, juce::MidiMessage::allNotesOff (2) } }), 60);
        expectNote (play ({ { 0, juce::MidiMessage::noteOn (2, 72, .8f) },
                           { 100, juce::MidiMessage::allSoundOff (2) } }), 60);
        expectNote (play ({ { 0, juce::MidiMessage::controllerEvent (2, 64, 0) },
                           { 1, juce::MidiMessage::controllerEvent (2, 121, 0) } }), 60);
        render (engine, 64, 64, { { 0, juce::MidiMessage::noteOff (1, 60) } });
        require (inspect (render (engine, 64, 64,
            { { 0, juce::MidiMessage::allSoundOff (1) } })).peak < 1.0e-6,
                 "All-sound-off failed to kill a mono release voice after its held-key count reached zero");
        engine.reset();
        require (inspect (play ({ { 0, juce::MidiMessage::noteOff (1, 60) },
                                  { 1, juce::MidiMessage::controllerEvent (2, 64, 0) } })).peak < 1.0e-6,
                 "Reset retained mono note bookkeeping that could revive a voice");
    }
}

void testMonoRetriggerAndLegatoContinuity()
{
    std::array<juce::AudioBuffer<float>, 2> onset;
    for (const int mode : { 1, 2 })
    {
        auto settings = voicingSettings (mode);
        settings.amp = { .12f, .1f, .65f, .03f };
        settings.lfoMode[0] = 1; settings.lfoRate[0] = 1;
        SynthEngine engine;
        engine.setSettings (settings); engine.prepare (48000, 512);
        render (engine, 4800, 127, { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
        onset[static_cast<size_t> (mode - 1)] = render (engine, 1200, 127,
            { { 0, juce::MidiMessage::noteOn (1, 76, .8f) } });
        require (std::abs (engine.getLfoPhase (0) - (mode == 1 ? .025f : .125f)) < .0002f,
                 "Mono retrigger or Legato overlap did not preserve its per-note LFO contract");
        render (engine, 2400, 127, { { 0, juce::MidiMessage::noteOff (1, 76) } });
        require (std::abs (engine.getLfoPhase (0) - (mode == 1 ? .05f : .175f)) < .0002f,
                 "Fallback to a held note did not follow Mono/Legato LFO retrigger behavior");
    }
    require (onset[1].getRMSLevel (0, 512, 512) > onset[0].getRMSLevel (0, 512, 512) * 1.8f,
             "Legato overlap restarted its amplitude attack or Mono failed to retrigger");
}

void testGlidePitchTimingAndInterruptions()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto settings = voicingSettings (2);
        settings.glideTime = .2f;
        const int change = static_cast<int> (rate * .25);
        const auto run = [&] (int block)
        {
            SynthEngine engine; engine.setSettings (settings); engine.prepare (rate, 512);
            return render (engine, static_cast<int> (rate * .6), block,
                { { 0, juce::MidiMessage::noteOn (1, 69, .8f) },
                  { change, juce::MidiMessage::noteOn (1, 81, .8f) } });
        };
        const auto audio = run (127);
        for (const double fraction : { .25, .5, .75 })
        {
            const auto measured = frequencyAt (audio, change + static_cast<int> (rate * .2 * fraction),
                                               static_cast<int> (rate * .008), rate);
            const double expected = 440.0 * std::pow (2.0, fraction);
            require (std::abs (measured / expected - 1) < .015,
                     "Glide did not interpolate in semitones over its requested duration");
        }
        require (std::abs (frequencyAt (audio, change + static_cast<int> (rate * .24),
                                        static_cast<int> (rate * .02), rate) - 880) < 2,
                 "Glide failed to reach the destination after its specified duration");
        require (audioDifference (audio, run (257)) < 1.0e-6,
                 "Glide trajectory depended on host block partition");
    }

    auto settings = voicingSettings (2); settings.glideTime = .2f;
    SynthEngine interrupted; interrupted.setSettings (settings); interrupted.prepare (48000, 512);
    const auto audio = render (interrupted, 33000, 127,
        { { 0, juce::MidiMessage::noteOn (1, 69, .8f) },
          { 12000, juce::MidiMessage::noteOn (1, 81, .8f) },
          { 16800, juce::MidiMessage::noteOn (1, 57, .8f) } });
    const double midway = 440.0 * std::pow (2.0, (66.0 - 69.0) / 12.0);
    require (std::abs (frequencyAt (audio, 21600, 384, 48000) / midway - 1) < .02,
             "Interrupted glide restarted from its old target instead of the pitch currently sounding");

    for (const bool always : { false, true })
    {
        settings.glideAlways = always;
        SynthEngine engine; engine.setSettings (settings); engine.prepare (48000, 512);
        const auto separated = render (engine, 30000, 127,
            { { 0, juce::MidiMessage::noteOn (1, 69, .8f) },
              { 4800, juce::MidiMessage::noteOff (1, 69) },
              { 14400, juce::MidiMessage::noteOn (1, 81, .8f) } });
        require (std::abs (frequencyAt (separated, 2400, 768, 48000) - 440) < 2,
                 "The first Always-glide note incorrectly slid from an uninitialized pitch");
        const double expected = always ? 440.0 * std::sqrt (2.0) : 880.0;
        require (std::abs (frequencyAt (separated, 19200, 384, 48000) / expected - 1) < .015,
                 "Separated notes did not distinguish Always glide from overlap-only glide");
    }
}

double onsetPhase (const juce::AudioBuffer<float>& audio, double frequency = 440.0)
{
    for (int sample = 513; sample < std::min (2048, audio.getNumSamples()); ++sample)
    {
        const double a = audio.getSample (0, sample - 1), b = audio.getSample (0, sample);
        if (a <= 0 && b > 0)
        {
            const double crossing = sample - 1.0 - a / (b - a);
            const double phase = -crossing * frequency / 48000.0;
            return phase - std::floor (phase);
        }
    }
    throw std::runtime_error ("No positive sine crossing available to measure initial oscillator phase");
}

void testOscillatorPhaseAndRandomRange()
{
    const auto renderPhase = [] (const SynthSettings& settings)
    {
        SynthEngine engine; engine.setSettings (settings); engine.prepare (48000, 512);
        // JUCE Chorus starts at its default 50% mix and ramps to our zero-mix
        // setting over 50 ms. Settle it in silence before measuring a pure sine.
        render (engine, 4096, 127);
        return render (engine, 4096, 127, { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    };
    for (int oscillator = 0; oscillator < 3; ++oscillator)
    {
        auto settings = voicingSettings (0);
        settings.osc[0].level = 0; settings.osc[static_cast<size_t> (oscillator)].level = .35f;
        auto& osc = settings.osc[static_cast<size_t> (oscillator)];
        osc.randomUnison = false;
        const double zero = onsetPhase (renderPhase (settings));
        for (const float phase : { .125f, .25f, .5f, .75f, 1.0f })
        {
            osc.phase = phase;
            const auto measured = onsetPhase (renderPhase (settings));
            require (std::abs (std::remainder (measured - zero - phase, 1.0)) < .0002,
                     "Oscillator initial phase did not shift the actual sine by its requested fraction of a cycle; oscillator="
                         + std::to_string (oscillator) + ", requested=" + std::to_string (phase)
                         + ", measured=" + std::to_string (measured) + ", zero=" + std::to_string (zero));
        }
        osc.phase = .15f; osc.phaseRandom = .2f;
        SynthEngine random; random.setSettings (settings); random.prepare (48000, 512);
        render (random, 4096, 127);
        double lowest = 1, highest = 0;
        for (int note = 0; note < 16; ++note)
        {
            const auto audio = render (random, 4096, 127, { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
            double phase = onsetPhase (audio) - zero;
            phase -= std::floor (phase);
            require (phase >= .1498 && phase <= .3502,
                     "Random phase exceeded its requested forward range or ignored the phase offset");
            lowest = std::min (lowest, phase); highest = std::max (highest, phase);
            render (random, 8000, 127, { { 0, juce::MidiMessage::noteOff (1, 69) } });
        }
        require (highest - lowest > .07, "Random phase did not vary between repeated notes");
    }
    auto aligned = voicingSettings (0);
    aligned.osc[0].randomUnison = false; aligned.osc[0].phase = .21f;
    const double singlePhase = onsetPhase (renderPhase (aligned));
    aligned.osc[0].unison = 7;
    require (std::abs (std::remainder (onsetPhase (renderPhase (aligned)) - singlePhase, 1.0)) < .0002,
             "Zero-random unison voices were not aligned to the requested phase");
    const auto deterministic = renderPhase (aligned);
    aligned.osc[0].randomUnison = true;
    require (audioDifference (deterministic, renderPhase (aligned)) > .005,
             "Random-unison mode did not change a seven-voice oscillator's phase distribution");
}

void testVoicingSampleAndGranularContinuity()
{
    const auto sample = makeTone (48000, 1.5, 220);
    for (const int sourceMode : { 1, 2 })
    {
        auto settings = voicingSettings (2);
        settings.osc[0].mode = sourceMode; settings.osc[0].rootNote = 69;
        settings.osc[0].sampleLoop = true; settings.osc[0].position = sourceMode == 2 ? .5f : 0;
        settings.osc[0].grainSize = .5f; settings.osc[0].grainDensity = 2; settings.osc[0].grainSpray = 0;
        for (const int voiceMode : { 1, 2 })
        {
            settings.voiceMode = voiceMode;
            SynthEngine repeated, reference;
            prepareSampleEngine (repeated, settings, sample, 48000);
            prepareSampleEngine (reference, settings, sample, 48000);
            const std::vector<Event> first { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } };
            render (repeated, 9473, 127, first); render (reference, 9473, 127, first);
            const auto actual = render (repeated, 6000, 127,
                { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
            const auto expected = render (reference, 6000, 127);
            const auto difference = audioDifference (actual, expected);
            require (voiceMode == 2 ? difference < 1.0e-7 : difference > .001,
                     "Sample/granular repeated note failed to distinguish Mono restart from Legato continuation");
        }
        settings.voiceMode = 2;
        SynthEngine pitched;
        prepareSampleEngine (pitched, settings, sample, 48000);
        render (pitched, 9473, 127, { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
        const auto octave = render (pitched, 6000, 127,
            { { 0, juce::MidiMessage::noteOn (1, 81, .8f) } });
        require (std::abs (estimateFrequency (octave, 48000) - 440) < 4,
                 "Legato did not retune an already playing sample or active grain to the new note");
    }
}

void testLegacyPolyAndPhaseSourceIsolation()
{
    auto settings = voicingSettings (0);
    settings.osc[0].unison = 5; settings.osc[0].detune = 17; settings.osc[0].position = .6f;
    const auto ordinary = renderWarpFixture (settings);
    settings.glideTime = 1.8f; settings.glideAlways = true;
    require (audioDifference (ordinary, renderWarpFixture (settings)) == 0,
             "Glide settings changed legacy Poly audio");
    const auto sample = makeTone (48000, 1.5, 220);
    for (const int mode : { 1, 2 })
    {
        auto original = voicingSettings (0);
        original.osc[0].mode = mode; original.osc[0].sampleLoop = true;
        original.osc[0].grainDensity = 21; original.osc[0].grainSpray = .3f;
        auto changed = original;
        for (auto& osc : changed.osc) { osc.phase = .7f; osc.phaseRandom = .91f; osc.randomUnison = false; }
        SynthEngine a, b;
        prepareSampleEngine (a, original, sample, 48000); prepareSampleEngine (b, changed, sample, 48000);
        const std::vector<Event> notes { { 37, juce::MidiMessage::noteOn (1, 60, .8f) },
                                          { 2301, juce::MidiMessage::noteOn (1, 67, .7f) } };
        require (audioDifference (render (a, 12000, 127, notes), render (b, 12000, 127, notes)) == 0,
                 "Wavetable phase controls changed sample/granular playback or its random sequence");
    }
}

void testVoicingAutomationSanitizationAndReset()
{
    auto valid = voicingSettings (2);
    auto invalid = valid;
    invalid.glideTime = std::numeric_limits<float>::quiet_NaN();
    for (auto& osc : invalid.osc)
    {
        osc.phase = std::numeric_limits<float>::infinity();
        osc.phaseRandom = std::numeric_limits<float>::quiet_NaN();
    }
    require (audioDifference (renderWarpFixture (valid), renderWarpFixture (invalid)) == 0,
             "Non-finite voicing controls did not fall back to finite default behavior");
    invalid = valid; invalid.glideTime = -3;
    for (auto& osc : invalid.osc) { osc.phase = -1; osc.phaseRandom = -1; }
    require (audioDifference (renderWarpFixture (valid), renderWarpFixture (invalid)) == 0,
             "Negative phase/glide values did not clamp to zero");

    SynthEngine engine; engine.setSettings (valid); engine.prepare (48000, 512);
    render (engine, 4096, 127,
        { { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
          { 1, juce::MidiMessage::noteOn (2, 67, .8f) },
          { 2, juce::MidiMessage::controllerEvent (1, 64, 127) } });
    valid.voiceMode = 0; engine.setSettings (valid);
    render (engine, 4096, 127);
    require (inspect (render (engine, 4096, 127,
        { { 0, juce::MidiMessage::noteOff (2, 67) },
          { 1, juce::MidiMessage::controllerEvent (1, 64, 0) } })).peak < 1.0e-6,
             "Changing voice mode retained old held notes or pedal state");
    for (int change = 0; change < 40; ++change)
    {
        valid.voiceMode = change % 3; valid.glideTime = (change % 5) * .4f;
        valid.osc[0].phase = (change % 7) / 6.0f;
        engine.setSettings (valid);
        render (engine, 97, 97, { { 7, juce::MidiMessage::noteOn (1, 36 + change, .8f) } });
    }
    engine.reset();
    require (inspect (render (engine, 4096, 127)).peak < 1.0e-6,
             "Reset left a tail or note after rapid voicing automation");
}

SynthSettings crossTestSettings()
{
    auto settings = cleanSettings();
    settings.filterEnabled = settings.filter2Enabled = false;
    settings.osc[0].level = .12f;
    settings.amp = { .001f, .01f, 1.0f, .01f };
    return settings;
}

void prepareCrossFixture (SynthEngine& engine, const SynthSettings& settings,
                          float sourceValue = 1.0f, double rate = 48000.0, bool asymmetric = false)
{
    juce::AudioBuffer<float> cycle (1, 2048), source (2, 4096);
    for (int sample = 0; sample < 2048; ++sample)
    {
        const double phase = 2 * pi * sample / 2048;
        cycle.setSample (0, sample, static_cast<float> (.6 * std::sin (phase)
            + (asymmetric ? .18 * std::sin (2 * phase) : 0.0)));
    }
    for (int sample = 0; sample < source.getNumSamples(); ++sample)
        for (int channel = 0; channel < 2; ++channel) source.setSample (channel, sample, sourceValue);
    auto table = SynthEngine::makeWavetable (cycle, 2048, false);
    for (int oscillator = 0; oscillator < 3; ++oscillator) engine.setUserWavetable (oscillator, table);
    engine.setUserSample (1, SynthEngine::makeSample (source, rate));
    engine.setSettings (settings);
    engine.prepare (rate, 512);
}

juce::AudioBuffer<float> steadyCrossFixture (const SynthSettings& settings, float sourceValue = 1.0f,
                                            double rate = 48000.0, bool asymmetric = false)
{
    SynthEngine engine;
    prepareCrossFixture (engine, settings, sourceValue, rate, asymmetric);
    render (engine, static_cast<int> (rate * .5), 127, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } });
    return render (engine, 8192, 257);
}

void testCrossModulationMathematics()
{
    auto settings = crossTestSettings();
    settings.osc[1].mode = 1; settings.osc[1].sampleLoop = true;
    settings.osc[0].crossSource[0] = 1;
    const auto dry = steadyCrossFixture (settings);
    const auto dryRms = inspect (dry).rms;
    require (dryRms > .001, "Cross modulation mathematical reference was silent");
    settings.osc[0].warpAmount[0] = 1;
    settings.osc[0].crossMode[0] = 3;
    const auto amPositive = steadyCrossFixture (settings, 1);
    const auto amZero = steadyCrossFixture (settings, 0);
    const auto amNegative = steadyCrossFixture (settings, -1);
    require (audioDifference (dry, amPositive) < 1.0e-6,
             "Full AM with a +1 source did not preserve the carrier");
    require (std::abs (inspect (amZero).rms / dryRms - .5) < .005,
             "Full AM with a zero source was not half amplitude");
    require (inspect (amNegative).rms == 0,
             "Full AM with a -1 source did not attenuate the carrier");
    settings.osc[0].crossMode[0] = 4;
    require (audioDifference (dry, steadyCrossFixture (settings, 1)) < 1.0e-6,
             "Full RM with a +1 source did not preserve the carrier");
    require (inspect (steadyCrossFixture (settings, 0)).rms == 0,
             "Full RM with a zero source did not suppress the carrier");
    const auto inverted = steadyCrossFixture (settings, -1);
    double correlation = 0, energy = 0;
    for (int sample = 0; sample < dry.getNumSamples(); ++sample)
    {
        correlation += dry.getSample (0, sample) * inverted.getSample (0, sample);
        energy += dry.getSample (0, sample) * dry.getSample (0, sample);
    }
    require (correlation / energy < -.995, "RM with a -1 source failed to invert the carrier");
    settings.osc[0].crossMode[0] = 2; settings.osc[0].warpAmount[0] = .5f;
    const auto halfTurn = steadyCrossFixture (settings, 1);
    require (audioDifference (inverted, halfTurn) < 2.0e-5,
             "PM half-turn did not match the independently inverted sine carrier");
    for (const int crossSlot : { 0, 1 })
        for (const int mode : { 3, 4 })
        {
            auto mixed = settings;
            mixed.osc[0].crossMode = {}; mixed.osc[0].warpMode = { 6, 6 };
            mixed.osc[0].crossSource = { 1, 1 }; mixed.osc[0].warpAmount = { 1, 1 };
            mixed.osc[0].crossMode[static_cast<size_t> (crossSlot)] = mode;
            const auto mixedStats = inspect (steadyCrossFixture (mixed, mode == 3 ? -1.0f : 0.0f));
            std::cout << "Cross mixed zero: slot=" << crossSlot << ", mode=" << mode
                      << ", rms=" << mixedStats.rms << ", peak=" << mixedStats.peak << '\n';
            // The source's first 64 samples fade in. Classic Fold can create a
            // DC transient during that ramp; its 10 Hz blocker decays rather
            // than truncates state. Require a settled residual below -240 dBFS.
            require (mixedStats.peak < 1.0e-12,
                     "Classic delta-DC correction reintroduced a carrier into silent mixed AM/RM; slot="
                     + std::to_string (crossSlot) + " mode=" + std::to_string (mode)
                     + " rms=" + std::to_string (mixedStats.rms) + " peak=" + std::to_string (mixedStats.peak));
        }
    settings.osc[0].crossMode[0] = 1; settings.osc[0].warpAmount[0] = 1;
    constexpr double rate = 56320.0; // A4 is exactly 1/128 turn/sample.
    const auto fast = steadyCrossFixture (settings, 1, rate, true);
    const auto reverse = steadyCrossFixture (settings, -1, rate, true);
    require (std::abs (estimateFrequency (fast, rate) - 3960) < 1,
             "FM +1 source failed the independent 9x-frequency landmark");
    require (std::abs (estimateFrequency (reverse, rate) - 3080) < 1,
             "FM -1 source failed the independent signed -7x-frequency landmark");
    // Phase invariant Im(C2 / C1^2) changes sign under time reversal, independent
    // of initial phase; sine + 0.3*sin(2phase) distinguishes signed FM from abs(FM).
    const auto orientation = [] (const juce::AudioBuffer<float>& audio, double cycles)
    {
        std::complex<double> first {}, second {};
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const double angle = -2 * pi * cycles * sample / audio.getNumSamples();
            first += static_cast<double> (audio.getSample (0, sample)) * std::polar (1.0, angle);
            second += static_cast<double> (audio.getSample (0, sample)) * std::polar (1.0, 2 * angle);
        }
        require (std::abs (first) > 1, "Signed FM orientation fixture lost its fundamental");
        return std::imag (second / (first * first));
    };
    require (orientation (fast, 576) > 0 && orientation (reverse, 448) < 0,
             "FM reversed frequency was rectified instead of traversing the wave backwards");
}

void testCrossTapSourcesAndIsolation()
{
    auto settings = crossTestSettings();
    settings.osc[0].crossMode = { 2, 4 };
    settings.osc[0].crossSource = { 1, 3 };
    settings.osc[0].warpAmount = { .3f, .4f };
    const auto base = renderWarpFixture (settings);
    require (audioDifference (base, renderWarpFixture (crossTestSettings())) > .001,
             "Muted wavetable/sub sources did not drive cross modulation");
    for (const int sourceMode : { 0, 1, 2 })
    {
        settings.osc[1].mode = sourceMode;
        settings.osc[1].sampleLoop = true;
        const auto centre = steadyCrossFixture (settings, .75f);
        auto panned = settings; panned.osc[1].pan = -1;
        require (audioDifference (centre, steadyCrossFixture (panned, .75f)) == 0,
                 "A muted source's left pan changed its pre-pan modulation tap");
        panned.osc[1].pan = 1;
        require (audioDifference (centre, steadyCrossFixture (panned, .75f)) == 0,
                 "A muted source's right pan changed its pre-pan modulation tap");
        auto absent = settings; absent.osc[0].crossMode = {};
        require (audioDifference (centre, steadyCrossFixture (absent, .75f)) > .0001,
                 "A muted sample/granular/wavetable source failed to modulate");
    }
    settings.osc[1].mode = 0; settings.osc[1].randomUnison = false;
    settings.osc[0].crossMode = { 2, 0 };
    const auto one = steadyCrossFixture (settings);
    settings.osc[1].unison = 7;
    require (audioDifference (one, steadyCrossFixture (settings)) < 1.0e-6,
             "A phase-locked source's tap changed gain with unison count");
    settings.osc[0].crossSource[0] = 4;
    const auto noise = steadyCrossFixture (settings);
    require (audioDifference (noise, one) > .001, "Muted noise source did not reach cross modulation");

    // A hard-right source can be made audible without entering the target's left
    // channel. Its modulation tap must remain independent of its output level.
    settings = crossTestSettings(); settings.osc[0].pan = -1;
    settings.osc[0].crossMode[0] = 2; settings.osc[0].crossSource[0] = 1;
    settings.osc[0].warpAmount[0] = .2f; settings.osc[1].pan = 1;
    const auto muted = steadyCrossFixture (settings);
    settings.osc[1].level = 1;
    const auto loud = steadyCrossFixture (settings);
    float leftDifference = 0;
    for (int sample = 0; sample < muted.getNumSamples(); ++sample)
        leftDifference = std::max (leftDifference, std::abs (muted.getSample (0, sample) - loud.getSample (0, sample)));
    require (leftDifference < 1.0e-6 && audioDifference (muted, loud) > .01,
             "Source level changed its pre-level tap or the source fixture was not audible");

    // Opposite stereo channels have a zero mono tap, regardless of their pan.
    settings = crossTestSettings(); settings.osc[1].mode = 1; settings.osc[1].sampleLoop = true;
    settings.osc[0].crossMode[0] = 4; settings.osc[0].crossSource[0] = 1; settings.osc[0].warpAmount[0] = 1;
    SynthEngine stereo;
    prepareCrossFixture (stereo, settings);
    juce::AudioBuffer<float> antiPhase (2, 4096);
    for (int sample = 0; sample < antiPhase.getNumSamples(); ++sample)
    {
        antiPhase.setSample (0, sample, .75f); antiPhase.setSample (1, sample, -.75f);
    }
    stereo.setUserSample (1, SynthEngine::makeSample (antiPhase, 48000));
    require (inspect (render (stereo, 8192, 127, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } })).rms == 0,
             "Stereo sample cross tap was not the mean of its channels");
}

void testCrossIdentityAndTargetModes()
{
    auto settings = crossTestSettings();
    settings.noise = .2f;
    settings.osc[1].mode = 2; settings.osc[1].sampleLoop = true;
    settings.osc[2].mode = 2; settings.osc[2].sampleLoop = true;
    const auto original = steadyCrossFixture (settings);
    for (int mode = 1; mode <= 4; ++mode)
    {
        auto zero = settings;
        for (auto& osc : zero.osc)
        {
            osc.crossMode = { mode, mode }; osc.crossSource = { 1, 2 };
            osc.warpMode = { 4, 6 }; osc.warpAmount = {};
        }
        require (audioDifference (original, steadyCrossFixture (zero)) == 0,
                 "Selected zero-depth Cross changed classic-Off audio or granular RNG");
    }
    auto disconnected = settings;
    disconnected.osc[2].mode = 0;
    disconnected.osc[2].crossMode = { 1, 2 };
    disconnected.osc[2].crossSource = { 1, 1 };
    disconnected.osc[2].warpAmount = { 1, 1 };
    require (audioDifference (original, steadyCrossFixture (disconnected)) == 0,
             "A disconnected muted target started a granular source and changed noise RNG");
    for (const int mode : { 1, 2 })
    {
        auto plain = settings; plain.osc[0].mode = mode;
        auto cross = plain; cross.osc[0].crossMode = { 1, 4 };
        cross.osc[0].crossSource = { 1, 4 }; cross.osc[0].warpAmount = { 1, 1 };
        SynthEngine first, second;
        const auto sample = makeTone (48000, .5, 330, 2);
        prepareSampleEngine (first, plain, sample, 48000);
        prepareSampleEngine (second, cross, sample, 48000);
        const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } };
        require (audioDifference (render (first, 8192, 127, note), render (second, 8192, 127, note)) == 0,
                 "WT-only cross operations changed a sample/granular target");
    }
}

void testCrossCyclesAutomationAndReset()
{
    auto settings = crossTestSettings();
    settings.osc[0].crossMode = { 1, 2 }; settings.osc[0].crossSource = { 1, 0 };
    settings.osc[0].warpAmount = { .22f, .13f };
    settings.osc[1].crossMode = { 2, 4 }; settings.osc[1].crossSource = { 2, 0 };
    settings.osc[1].warpAmount = { .33f, .25f };
    settings.osc[2].crossMode = { 1, 2 }; settings.osc[2].crossSource = { 0, 2 };
    settings.osc[2].warpAmount = { .1f, .15f };
    require (audioDifference (renderWarpFixture (settings, 127), renderWarpFixture (settings, 257)) == 0,
             "Cyclic/self cross modulation depended on host block partition");
    // Relabel the graph A<->C. The physical DSP loop order changes, whereas an
    // explicit previous-sample graph must produce the same audible sequence.
    auto relabelled = settings;
    std::swap (relabelled.osc[0], relabelled.osc[2]);
    for (auto& osc : relabelled.osc)
        for (auto& source : osc.crossSource)
            if (source == 0) source = 2; else if (source == 2) source = 0;
    require (audioDifference (renderWarpFixture (settings), renderWarpFixture (relabelled)) == 0,
             "Cross feedback graph depended on oscillator evaluation order");
    SynthEngine engine;
    prepareCrossFixture (engine, settings);
    render (engine, 8192, 127, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } });
    for (int change = 0; change < 50; ++change)
    {
        settings.osc[0].crossMode = { change % 5, (change + 2) % 5 };
        settings.osc[0].crossSource = { (change + 1) % 5, (change + 4) % 5 };
        settings.osc[0].warpAmount = { change % 2 ? 1.0f : 0.0f, .4f };
        engine.setSettings (settings);
        render (engine, 37, 7);
    }
    engine.reset();
    require (inspect (render (engine, 4096, 127)).peak == 0,
             "Reset retained cross taps, FM phase or transition audio");
    auto settled = settings;
    settled.osc[0].crossMode = { -5, 99 }; settled.osc[0].crossSource = { 99, -4 };
    settled.osc[0].warpAmount = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
    engine.setSettings (settled);
    render (engine, 4096, 127, { { 0, juce::MidiMessage::noteOn (1, 96, 1.0f) },
        { 3072, juce::MidiMessage::controllerEvent (1, 120, 0) } });
    require (inspect (render (engine, 2048, 127)).peak < 1.0e-7,
             "CC120 retained a cross-modulated voice");

    // Returning a running FM oscillator to zero must converge to its original
    // dry phase, not leave a persistent FM phase offset or a late discontinuity.
    auto drySettings = crossTestSettings();
    drySettings.osc[1].mode = 1; drySettings.osc[1].sampleLoop = true;
    auto fmSettings = drySettings;
    fmSettings.osc[0].crossMode[0] = 1; fmSettings.osc[0].crossSource[0] = 1;
    fmSettings.osc[0].warpAmount[0] = .3f;
    SynthEngine dryEngine, fmEngine;
    prepareCrossFixture (dryEngine, drySettings); prepareCrossFixture (fmEngine, fmSettings);
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } };
    render (dryEngine, 4096, 127, note); render (fmEngine, 4096, 127, note);
    fmSettings.osc[0].warpAmount[0] = 0; fmEngine.setSettings (fmSettings);
    render (dryEngine, 48000, 127); render (fmEngine, 48000, 127);
    require (audioDifference (render (dryEngine, 8192, 127), render (fmEngine, 8192, 127)) < 1.0e-6,
             "FM returning to zero did not recover the independent dry phase");
}

void createDemo (const juce::File& directory)
{
    require (directory.createDirectory().wasOk(), "Could not create demo output directory");
    constexpr double rate = 48000.0;
    SynthEngine engine;
    engine.prepare (rate, 512);
    auto settings = cleanSettings();
    settings.osc[0].position = 0.43f;
    settings.osc[0].unison = 5;
    settings.osc[0].detune = 15.0f;
    settings.osc[0].level = 0.5f;
    settings.osc[1].level = 0.17f;
    settings.osc[1].position = 0.73f;
    settings.osc[1].semitone = 12.0f;
    settings.sub = 0.15f;
    settings.cutoff = 3400.0f;
    settings.resonance = 0.18f;
    settings.filterEnv = 0.45f;
    settings.amp = { 0.008f, 0.35f, 0.48f, 0.45f };
    settings.filter = { 0.005f, 0.35f, 0.2f, 0.35f };
    settings.chorus = 0.16f;
    settings.delayMix = 0.16f;
    settings.delayTime = 0.25f;
    settings.delayFeedback = 0.28f;
    settings.reverb = 0.2f;
    settings.reverbSize = 0.65f;
    settings.master = 0.65f;
    engine.setSettings (settings);
    const int chords[4][4] = { { 48, 55, 60, 64 }, { 45, 52, 57, 60 },
                              { 41, 48, 53, 57 }, { 43, 50, 55, 59 } };
    std::vector<Event> events;
    for (int chord = 0; chord < 4; ++chord)
        for (int step = 0; step < 8; ++step)
        {
            const auto start = static_cast<int> ((chord * 2.0 + step * 0.25) * rate);
            const auto note = chords[chord][step % 4] + (step >= 4 ? 12 : 0);
            events.push_back ({ start, juce::MidiMessage::noteOn (1, note, step % 4 == 0 ? 0.9f : 0.73f) });
            events.push_back ({ start + 8400, juce::MidiMessage::noteOff (1, note) });
        }
    const auto audio = render (engine, static_cast<int> (rate * 10.0), 512, events);
    require (inspect (audio).rms > 0.01, "Demo render was silent");
    const auto output = directory.getChildFile ("Anyvoc-Demo.wav");
    require (writeWav (output, audio, rate), "Could not write the demo WAV");
    std::cout << "DEMO " << output.getFullPathName() << '\n';
}
}

int benchmarkAdvancedMatrix()
{
    constexpr double rate = 48000.0, audioSeconds = 10.0;
    constexpr int blockSize = 256, blocks = 1875, rounds = 3;
    constexpr const char* labels[] { "0 routes", "8 legacy routes", "32 curved routes + curved aux",
                                    "32 per-voice curved routes + curved aux" };
    std::array<std::array<double, rounds>, 4> elapsed {};
    double checksum = 0;
    std::cout << "Matrix benchmark: Release; 48 kHz; 16 sustained voices; 256-sample blocks; "
                 "one wavetable oscillator, unison 1; filters/effects off; 10 audio seconds per run; "
                 "3 rounds; staggered onsets and distinct velocities; includes setSettings once per block; "
                 "excludes construction/warmup. Last case uses note-triggered LFOs and per-voice envelope/velocity aux.\n";
    for (int round = 0; round < rounds; ++round)
        for (int step = 0; step < 4; ++step)
        {
            const int scenario = (round + step) % 4; // Rotate order to reduce order/thermal bias.
            auto settings = cleanSettings();
            settings.osc[0].level = .15f;
            settings.osc[0].position = .43f;
            settings.filterEnabled = settings.filter2Enabled = false;
            settings.amp = { .005f, .02f, .8f, .05f };
            settings.filter = { .005f, 20.0f, .1f, .05f };
            settings.macros = { .25f, .4f, .6f, .7f };
            if (scenario == 3) settings.lfoMode = { 1, 1 };
            const int routeCount = scenario == 0 ? 0 : scenario == 1 ? 8 : 32;
            for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex)
            {
                auto& route = settings.routes[static_cast<size_t> (routeIndex)];
                route = { 1 + routeIndex % 9, 7, routeIndex % 2 == 0 ? .025f : -.025f };
                if (scenario >= 2)
                {
                    route.polarity = routeIndex % 3;
                    route.auxSource = 1 + (routeIndex * 5 + 2) % 9;
                    route.auxPolarity = (routeIndex + 1) % 3;
                    route.curve = -.8f + 1.6f * static_cast<float> (routeIndex) / 31;
                    route.auxCurve = .75f - 1.5f * static_cast<float> (routeIndex) / 31;
                    if (scenario == 3)
                    {
                        route.source = 1 + routeIndex % 2;
                        route.auxSource = routeIndex % 2 == 0 ? 3 : 4;
                    }
                }
            }
            SynthEngine engine;
            engine.setSettings (settings);
            engine.prepare (rate, blockSize);
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 79), 0);
            for (int voice = 0; voice < 16; ++voice)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + voice, .4f + .03f * voice), (voice * 17) % blockSize);
                engine.render (block, midi);
                midi.clear();
            }
            for (int warmup = 0; warmup < 48; ++warmup) engine.render (block, midi);
            const auto start = std::chrono::steady_clock::now();
            for (int iteration = 0; iteration < blocks; ++iteration)
            {
                engine.setSettings (settings);
                engine.render (block, midi);
                checksum += block.getSample (0, blockSize - 1);
            }
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            elapsed[static_cast<size_t> (scenario)][static_cast<size_t> (round)] = seconds;
            require (inspect (block).rms > .001, "Matrix benchmark fixture was silent");
            std::cout << std::fixed << std::setprecision (6) << "ROUND " << round + 1 << " " << labels[scenario]
                      << ": wall=" << seconds << "s, wall/audio=" << seconds / audioSeconds << '\n';
        }
    for (size_t scenario = 0; scenario < elapsed.size(); ++scenario)
    {
        auto sorted = elapsed[scenario];
        std::sort (sorted.begin(), sorted.end());
        std::cout << "MEDIAN " << labels[scenario] << ": wall=" << sorted[1] << "s, wall/audio="
                  << sorted[1] / audioSeconds << ", real-time throughput=" << audioSeconds / sorted[1] << "x\n";
    }
    std::cout << "CHECKSUM " << checksum << '\n';
    return 0;
}

int benchmarkChaosLfos()
{
    constexpr int scenarios = 8, rounds = 3, rate = 48000, blockSize = 256, blocks = 375;
    constexpr double audioSeconds = static_cast<double> (blocks * blockSize) / rate;
    constexpr const char* labels[scenarios] { "Sine 10Hz Trigger", "Lorenz 10Hz Trigger", "Rossler 10Hz Trigger",
        "Sine 1000Hz Trigger", "Lorenz 1000Hz Free", "Lorenz 1000Hz Trigger", "Rossler 1000Hz Trigger",
        "Rossler 1000Hz Trigger unrouted" };
    constexpr int shapes[scenarios] { 0, 6, 7, 0, 6, 6, 7, 7 };
    std::array<std::array<double, rounds>, scenarios> elapsed {};
    double checksum = 0;
    std::cout << "Chaos benchmark: Release; 48 kHz; 16 sustained voices with staggered onsets; 10 LFOs; "
        "10 small pan routes; one wavetable oscillator, unison1, filters/effects off; 256-sample blocks; "
        "2 audio seconds/run, 3 rounds; setSettings each block; construction/warmup excluded. "
        "Last case has zero routes but still integrates all 160 voice/LFO states.\n";
    for (int round = 0; round < rounds; ++round)
        for (int order = 0; order < scenarios; ++order)
        {
            const int scenario = (order + round) % scenarios;
            auto settings = cleanSettings(); settings.filterEnabled = settings.filter2Enabled = false;
            settings.osc[0].level = .12f; settings.osc[0].position = .37f;
            settings.amp = { .005f, .02f, .8f, .05f };
            settings.lfoShape.fill (shapes[scenario]);
            settings.lfoMode.fill (scenario == 4 ? 0 : 1);
            settings.lfoRate.fill (10); settings.lfoAudioRateEnabled.fill (scenario >= 3);
            settings.lfoAudioRateHz.fill (1000);
            if (scenario != 7)
                for (int i = 0; i < 10; ++i) settings.routes[static_cast<size_t> (i)] = { anyvoc::lfoSourceId (i), 7, .025f };
            SynthEngine engine; engine.setSettings (settings); engine.prepare (rate, blockSize);
            juce::AudioBuffer<float> block (2, blockSize); juce::MidiBuffer midi;
            for (int voice = 0; voice < 16; ++voice)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + voice, .5f + .02f * voice), (voice * 17) % blockSize);
                engine.render (block, midi); midi.clear();
            }
            for (int warmup = 0; warmup < 48; ++warmup) engine.render (block, midi);
            const auto start = std::chrono::steady_clock::now();
            for (int n = 0; n < blocks; ++n)
            {
                engine.setSettings (settings); engine.render (block, midi);
                checksum += block.getSample (0, blockSize - 1);
            }
            const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            elapsed[static_cast<size_t> (scenario)][static_cast<size_t> (round)] = wall;
            require (inspect (block).rms > .001, "Chaos benchmark fixture is silent");
            std::cout << std::fixed << std::setprecision (6) << "ROUND " << round + 1 << " " << labels[scenario]
                      << ": wall=" << wall << "s, wall/audio=" << wall / audioSeconds << '\n';
        }
    for (size_t scenario = 0; scenario < elapsed.size(); ++scenario)
    {
        auto times = elapsed[scenario]; std::sort (times.begin(), times.end());
        std::cout << "MEDIAN " << labels[scenario] << ": wall=" << times[1] << "s, wall/audio=" << times[1] / audioSeconds
                  << ", real-time throughput=" << audioSeconds / times[1] << "x\n";
    }
    std::cout << "CHECKSUM " << checksum << '\n';
    return 0;
}

int benchmarkMacroTargets()
{
    constexpr double rate = 48000.0, audioSeconds = 2.0;
    constexpr int blockSize = 256, blocks = 375, rounds = 3, scenarios = 4;
    constexpr const char* labels[] { "32 legacy macro-source routes", "32 curved macro feedback/AUX routes",
                                    "997Hz LFO-to-macro pitch/pan", "997Hz LFO-to-macro dual-filter" };
    std::array<std::array<double, rounds>, scenarios> elapsed {};
    double checksum = 0;
    std::cout << "Macro benchmark: Release; 48 kHz; 16 sustained voices, distinct velocities/staggered onsets; "
        "32 routes; one wavetable oscillator, unison1; FX off; filters off except final dual-filter case; "
        "256-sample blocks; 2 audio seconds/run, 3 rounds; includes setSettings each block, excludes construction/warmup. "
        "Curved case includes per-voice primary/AUX curves, macro feedback and phase feedback.\n";
    for (int round = 0; round < rounds; ++round)
        for (int order = 0; order < scenarios; ++order)
        {
            const int scenario = (order + round) % scenarios;
            auto settings = cleanSettings(); settings.filterEnabled = settings.filter2Enabled = scenario == 3;
            settings.cutoff = 1200; settings.filter2Cutoff = 3500;
            settings.osc[0].level = .12f; settings.osc[0].position = .37f;
            settings.amp = { .005f, .02f, .8f, .05f }; settings.macros.fill (.5f);
            settings.lfoMode.fill (1); settings.lfoAudioRateEnabled.fill (scenario >= 2);
            settings.lfoAudioRateHz.fill (997);
            for (int r = 0; r < 32; ++r)
            {
                auto& route = settings.routes[static_cast<size_t> (r)];
                route = { anyvoc::macroSourceId (r % 8), 7, r % 2 == 0 ? .015f : -.01f };
                if (scenario == 1)
                {
                    route.destination = r < 24 ? 28 + (r * 3 + 1) % 8 : r < 28 ? 18 + r % 10 : 7;
                    route.source = r % 3 == 0 ? anyvoc::lfoSourceId (r % 10) : anyvoc::macroSourceId (r % 8);
                    route.amount = r % 2 == 0 ? .15f : -.15f;
                    route.auxSource = r % 2 == 0 ? 4 : anyvoc::macroSourceId ((r + 2) % 8);
                    route.polarity = r % 3; route.auxPolarity = (r + 1) % 3;
                    route.curve = -.8f + r * 1.6f / 31; route.auxCurve = .75f - r * 1.5f / 31;
                }
                else if (scenario >= 2)
                {
                    if (r < 16) route = { anyvoc::lfoSourceId (r % 10), 28 + r % 8, r % 2 == 0 ? .125f : -.125f };
                    else
                    {
                        constexpr int filters[] { 1, 8, 9, 10 };
                        route.destination = scenario == 3 ? filters[r % 4] : r % 2 == 0 ? 5 : 7;
                        route.auxSource = anyvoc::macroSourceId ((r + 1) % 8);
                    }
                }
            }
            SynthEngine engine; engine.setSettings (settings); engine.prepare (rate, blockSize);
            juce::AudioBuffer<float> block (2, blockSize); juce::MidiBuffer midi;
            for (int voice = 0; voice < 16; ++voice)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + voice, .4f + .03f * voice), (voice * 17) % blockSize);
                engine.render (block, midi); midi.clear();
            }
            for (int warmup = 0; warmup < 48; ++warmup) engine.render (block, midi);
            const auto start = std::chrono::steady_clock::now();
            for (int n = 0; n < blocks; ++n)
            {
                engine.setSettings (settings); engine.render (block, midi);
                checksum += block.getSample (0, blockSize - 1);
            }
            const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            elapsed[static_cast<size_t> (scenario)][static_cast<size_t> (round)] = wall;
            require (inspect (block).rms > .001, "Macro benchmark fixture is silent");
            std::cout << std::fixed << std::setprecision (6) << "ROUND " << round + 1 << " " << labels[scenario]
                      << ": wall=" << wall << "s, wall/audio=" << wall / audioSeconds << '\n';
        }
    for (size_t scenario = 0; scenario < elapsed.size(); ++scenario)
    {
        auto times = elapsed[scenario]; std::sort (times.begin(), times.end());
        std::cout << "MEDIAN " << labels[scenario] << ": wall=" << times[1] << "s, wall/audio=" << times[1] / audioSeconds
                  << ", real-time throughput=" << audioSeconds / times[1] << "x\n";
    }
    std::cout << "CHECKSUM " << checksum << '\n';
    return 0;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    if (argc > 1 && std::string (argv[1]) == "--benchmark-macros")
        try { return benchmarkMacroTargets(); }
        catch (const std::exception& error)
        { std::cerr << "FAIL Macro benchmark: " << error.what() << '\n'; return 1; }
    if (argc > 1 && std::string (argv[1]) == "--benchmark-chaos")
        try { return benchmarkChaosLfos(); }
        catch (const std::exception& error)
        { std::cerr << "FAIL Chaos benchmark: " << error.what() << '\n'; return 1; }
    if (argc > 1 && std::string (argv[1]) == "--benchmark-matrix")
        try { return benchmarkAdvancedMatrix(); }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL Matrix benchmark: " << error.what() << '\n';
            return 1;
        }
    const std::vector<std::pair<const char*, std::function<void()>>> tests {
        { "MIDI note lifecycle and event timing", testNoteLifecycle },
        { "Sample rates and variable block lengths", testRatesAndBlockLengths },
        { "Sustain pedal", testSustainPedal },
        { "A4 tuning and pitch wheel", testPitchWheel },
        { "Reset, effect tails and MIDI panic", testResetAndPanic },
        { "Stereo unison", testUnisonStereo },
        { "WAV wavetable import and musical pitch", testWavetableImport },
        { "Sample playback pitch and source sample rates", testSamplePitchAndRates },
        { "Sample one-shot, loop, stereo and note release", testSampleEndLoopAndStereo },
        { "Sample start position and reverse playback", testSamplePositionAndReverse },
        { "Granular scan position and note release", testGranularPositionAndRelease },
        { "Sample/granular bounded polyphony stress", testSampleAndGranularStress },
        { "Drawable LFO point values reach the audio route", testCustomLfoAudioRouting },
        { "Drawable LFO linear and stepped interpolation", testLfoInterpolation },
        { "Per-note LFO trigger and one-shot endpoint", testLfoTriggerAndOneShot },
        { "Host LFO tempo, triplets, seeks and block partition", testHostLfoTempoSeekAndPartition },
        { "Dual-filter spectral response, slopes and serial routing", testDualFilterResponseAndSlope },
        { "Independent source buses, bypass and parallel balance", testFilterRoutingAndParallelBalance },
        { "Filter key tracking and additional modulation targets", testFilterKeyTrackingAndModulation },
        { "Rapid dual-filter automation and reset", testFilterAutomationAndReset },
        { "Warp preview landmarks, identities and slot order", testWarpPreviewLandmarksAndOrder },
        { "Warp identity and unaffected sample/granular playback", testWarpIdentityAndSourceModes },
        { "All six oscillator warp modulation destinations", testWarpModulationDestinations },
        { "Warp block partition, automation sanitization and reset", testWarpAutomationPartitionAndReset },
        { "Warp DC rejection and continuous harmonic-level transitions", testWarpDcAndMipContinuity },
        { "Fractional delay reference and circular read boundaries", testFractionalDelayWrap },
        { "Mono note priority, repeated counts and channel controllers", testMonoPriorityCountsAndControllers },
        { "Mono retrigger and Legato envelope/LFO continuity", testMonoRetriggerAndLegatoContinuity },
        { "Glide pitch, duration, interruptions and separated notes", testGlidePitchTimingAndInterruptions },
        { "Oscillator phase landmarks, random bounds and unison", testOscillatorPhaseAndRandomRange },
        { "Sample/granular Mono restart and Legato continuation", testVoicingSampleAndGranularContinuity },
        { "Poly glide isolation and unchanged sample/granular phase behavior", testLegacyPolyAndPhaseSourceIsolation },
        { "Voicing sanitization, live mode changes and reset", testVoicingAutomationSanitizationAndReset }
        , { "Cross FM signed-frequency and PM/AM/RM mathematical landmarks", testCrossModulationMathematics }
        , { "Cross pre-pan source taps, muted sources and unison averaging", testCrossTapSourcesAndIsolation }
        , { "Cross zero-depth/RNG identity and WT-only target modes", testCrossIdentityAndTargetModes }
        , { "Cross cycles, block partition, topology automation and reset", testCrossCyclesAutomationAndReset }
        , { "Advanced matrix source curves, polarity and nominal ranges", testModulationSourceTransformsAndRanges }
        , { "Advanced matrix products and all 32 audible slots", testAdvancedModulationAudioAndAllSlots }
        , { "Advanced matrix bypass, off, zero, legacy identity and invalid controls", testAdvancedModulationIdentityAndInvalids }
        , { "Advanced matrix block partition, amount smoothing and reset", testAdvancedModulationPartitionAndSmoothing }
        , { "All 25 source mappings and new primary/auxiliary audio", testExpandedSourceMappingAndAudio }
        , { "LFO10 transport, custom/trigger/one-shot and independent random sequences", testExpandedLfoTransportModesAndRandom }
        , { "Amp/Env3/Env4 release, sustain, mono modes and voice stealing", testExpandedEnvelopeLifecycle }
        , { "DAHDSR shared model, independent audio landmarks and Instant stages", testDahdsrModelAndAudio }
        , { "DAHDSR live tempo, durations, curves and legacy transitions", testDahdsrLiveTempoAndControls }
        , { "DAHDSR per-envelope Legato retrigger and stage release lifecycle", testDahdsrLegatoAndLifecycle }
        , { "DAHDSR legacy identity, block partition and invalid controls", testDahdsrCompatibilityPartitionAndInvalids }
        , { "LFO phase/direction independent audio and one-shot endpoints", testLfoPhaseDirectionAndOneShot }
        , { "LFO swing grid, transport seeks, live edits and block partition", testLfoSwingTransportAndLiveEdits }
        , { "All phase destinations, unit-delay feedback and voice isolation", testLfoPhaseRoutesFeedbackAndVoices }
        , { "Audio-rate primary/auxiliary/phase chains and pitch/pan/filter oracles", testLfoAudioRateTargetOracles }
        , { "Advanced LFO legacy identity, random isolation, reset and invalid controls", testLfoAdvancedIdentityPartitionAndLifecycle }
        , { "Chaos independent ODE, RK4 grid and Hermite off-grid reference", testChaosNumericalReference }
        , { "Chaos long-run bounds, nonperiodic evolution and sample-rate invariance", testChaosLongRunAndSampleRates }
        , { "Chaos trigger, stealing, one-shot clipping and Legato lifecycle", testChaosTriggerOneShotAndLegato }
        , { "Chaos free transport/swing continuity and live mode/algorithm changes", testChaosTransportAndLiveModes }
        , { "Chaos audio-rate primary/auxiliary and delayed phase-chain oracles", testChaosAudioRateOracles }
        , { "Chaos unused identity, block partition, reset and inactive phase controls", testChaosIdentityPartitionAndReset }
        , { "Chaos coherent concurrent XYZ telemetry", testChaosCoherentTelemetry }
        , { "Unused expanded source identity and invalid controls", testExpandedSourceIsolationAndInvalids }
        , { "Curve grid nearest coordinates and positive/negative half-grid ties", testCurveGridSnapping }
        , { "Curve selected flips, independent endpoints and exact untouched points", testCurveSelectionFlips }
        , { "Curve move/scale independent landmarks, interpolation and boundary hold", testCurveTransformLandmarks }
        , { "Curve identity bits, aliasing and transactional invalid-input rejection", testCurveInvalidAndIdentity }
        , { "Curve XML independent structure, float-bit round trips and exact size limit", testCurveCodecRoundTrip }
        , { "Curve strict XML grammar, numeric bounds and atomic parse rejection", testCurveCodecRejections }
        , { "Curve file round trips, bounded reads and failed-save preservation", testCurveFileTransactions }
        , { "All eight macro targets, primary/AUX products and sum-before-clamp", testMacroMappingsAndProducts }
        , { "Macro chain/self-feedback and macro/LFO cycle independent sample delays", testMacroDelayedGraphs }
        , { "Macro per-voice lifecycle, stealing, Legato and actual-read telemetry", testMacroLifecycleAndTelemetry }
        , { "Macro audio-rate primary/AUX pitch, pan and dual-filter oracles", testMacroFastControlOracles }
        , { "Macro live base/amount smoothing and immediate graph removal", testMacroSmoothingAndRemoval }
        , { "Macro row permutation during smoothing, legacy identity and partition", testMacroPermutationAndCompatibility }
        , { "Macro coherent concurrent eight-value and voice telemetry", testMacroCoherentTelemetry }
    };
    int failures = 0;
    for (const auto& test : tests)
    {
        try
        {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    if (argc > 1)
        try
        {
            createDemo (juce::File (juce::String::fromUTF8 (argv[1])));
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL Demo WAV: " << error.what() << '\n';
        }
    std::cout << "Engine validation: " << tests.size() << " cases, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
