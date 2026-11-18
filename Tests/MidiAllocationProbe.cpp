#if ! defined (ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC) || ! ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC
 #error This probe requires the isolated, instrumented JUCE build.
#endif

#include "MidiAllocationRecorder.h"
#include "PluginProcessor.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace probe = anyvoc::allocation_probe;
constexpr double sampleRate = 48000;
constexpr int maximumBlock = 8192;

void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

juce::var object() { return juce::var (new juce::DynamicObject()); }
void put (juce::var& value, const char* key, const juce::var& field)
{
    value.getDynamicObject()->setProperty (key, field);
}
void writeJson (const juce::File& file, const juce::var& value)
{
    const auto text = juce::JSON::toString (value, false).toStdString() + "\n";
    std::ofstream output (std::filesystem::path (file.getFullPathName().toWideCharPointer()), std::ios::binary);
    require (output.good(), "Cannot open JSON output");
    output.write (text.data(), static_cast<std::streamsize> (text.size()));
    output.flush(); require (output.good(), "Cannot write JSON output");
}

const char* siteName (probe::Site site)
{
    switch (site)
    {
        case probe::Site::heapMalloc: return "heapMalloc";
        case probe::Site::heapCalloc: return "heapCalloc";
        case probe::Site::heapRealloc: return "heapRealloc";
        case probe::Site::midiMessageMalloc: return "midiMessageMalloc";
    }
    return "invalid";
}
int ownerIndex (const probe::Event& event, const void* host)
{
    return event.midiBuffer == nullptr ? 2 : event.midiBuffer == host ? 0 : 1;
}
constexpr const char* ownerNames[] { "host", "other-MidiBuffer", "none" };
struct Totals
{
    std::array<std::array<std::uint64_t, 3>, 4> calls {}, bytes {};
    void add (const probe::Event& event, const void* host)
    {
        const auto site = static_cast<size_t> (event.site), owner = static_cast<size_t> (ownerIndex (event, host));
        ++calls[site][owner]; bytes[site][owner] += event.bytes;
    }
    juce::var json() const
    {
        juce::Array<juce::var> result;
        for (size_t s = 0; s < 4; ++s) for (size_t o = 0; o < 3; ++o)
        {
            if (calls[s][o] == 0) continue;
            auto item = object(); put (item, "site", siteName (static_cast<probe::Site> (s)));
            put (item, "owner", ownerNames[o]); put (item, "count", static_cast<juce::int64> (calls[s][o]));
            put (item, "bytes", static_cast<juce::int64> (bytes[s][o])); result.add (item);
        }
        return result;
    }
};
juce::var eventJson (const probe::Event& event, const void* host, int block = -1)
{
    auto item = object();
    if (block >= 0) put (item, "block", block);
    put (item, "site", siteName (event.site)); put (item, "bytes", static_cast<juce::int64> (event.bytes));
    put (item, "owner", ownerNames[ownerIndex (event, host)]);
    put (item, "before_nonnull", event.before != nullptr); put (item, "after_nonnull", event.after != nullptr);
    put (item, "same_address", event.before != nullptr && event.before == event.after);
    return item;
}

// Synthetic bookkeeping checks are separate from the real allocator controls.
juce::var recorderSelfTests()
{
    auto first = std::make_unique<probe::Capture>(), second = std::make_unique<probe::Capture>();
    require (probe::activeCapture == nullptr && probe::activeMidiBuffer == nullptr, "Unexpected outer recorder gate");
    int ownerA = 0, ownerB = 0;
    {
        probe::CaptureScope outer (*first); probe::MidiBufferScope owner (&ownerA);
        probe::record (probe::Site::heapMalloc, 1, nullptr, &ownerA);
        {
            probe::CaptureScope inner (*second); probe::MidiBufferScope other (&ownerB);
            probe::record (probe::Site::heapCalloc, 2, nullptr, &ownerB);
        }
        require (probe::activeCapture == first.get() && probe::activeMidiBuffer == &ownerA,
                 "Nested recorder/owner scope did not restore its parent");
        probe::record (probe::Site::heapRealloc, 3, &ownerA, &ownerA);
    }
    require (first->count == 2 && second->count == 1 && first->events[1].midiBuffer == &ownerA
                 && second->events[0].midiBuffer == &ownerB && probe::activeCapture == nullptr
                 && probe::activeMidiBuffer == nullptr, "Recorder scope restoration failed");
    first->clear();
    {
        probe::CaptureScope scope (*first);
        for (size_t i = 0; i < first->events.size() + 1; ++i)
            probe::record (probe::Site::heapMalloc, 1, nullptr, &ownerA);
    }
    require (first->count == 8192 && first->overflow == 1, "Recorder capacity overflow was not reported");
    auto result = object(), overflow = object(); put (result, "nested_capture_and_owner_restore", true);
    put (overflow, "passed", true); put (overflow, "count", 8192); put (overflow, "overflow", 1);
    put (overflow, "expected_overflow", 1); put (result, "capacity_overflow", overflow); return result;
}

juce::var allocatorControls()
{
    auto capture = std::make_unique<probe::Capture>(); juce::Array<juce::var> controls;
    const auto finish = [&] (const char* id, bool passed)
    {
        auto control = object(); juce::Array<juce::var> events;
        for (size_t i = 0; i < capture->count; ++i) events.add (eventJson (capture->events[i], nullptr));
        put (control, "id", id); put (control, "passed", passed && capture->overflow == 0);
        put (control, "count", static_cast<juce::int64> (capture->count));
        put (control, "overflow", static_cast<juce::int64> (capture->overflow)); put (control, "events", events);
        controls.add (control);
    };
    { probe::CaptureScope scope (*capture); }
    finish ("scope-noop", capture->count == 0);
    juce::HeapBlock<unsigned char> memory;
    for (int operation = 0; operation < 4; ++operation)
    {
        capture->clear();
        {
            probe::CaptureScope scope (*capture);
            if (operation == 0) memory.malloc (32);
            else if (operation == 1) memory.calloc (64);
            else memory.realloc (operation == 2 ? 64 : 128);
        }
        require (memory.getData() != nullptr, "Allocator control failed to allocate memory");
        const auto expected = operation == 0 ? probe::Site::heapMalloc : operation == 1 ? probe::Site::heapCalloc
                                                                                            : probe::Site::heapRealloc;
        const size_t bytes = operation == 0 ? 32 : operation == 3 ? 128 : 64;
        bool found = false;
        for (size_t i = 0; i < capture->count; ++i)
            found |= capture->events[i].site == expected && capture->events[i].bytes == bytes
                     && capture->events[i].after != nullptr;
        constexpr const char* ids[] { "heap-malloc", "heap-calloc", "heap-realloc-same-size", "heap-realloc-grow" };
        finish (ids[operation], found); memory[0] = 17;
    }
    std::array<juce::uint8, 1024> payload {}; payload.fill (0x35); capture->clear();
    {
        probe::CaptureScope scope (*capture);
        const auto message = juce::MidiMessage::createSysExMessage (payload.data(), static_cast<int> (payload.size()));
        (void) message;
    }
    bool found = false;
    for (size_t i = 0; i < capture->count; ++i)
        found |= capture->events[i].site == probe::Site::midiMessageMalloc && capture->events[i].bytes == 1026
                 && capture->events[i].after != nullptr;
    finish ("midi-message-sysex", found);
    return controls;
}

struct MidiEvent { int sample; juce::MidiMessage message; };
MidiEvent on (int sample, int channel = 2, int note = 60)
{ return { sample, juce::MidiMessage::noteOn (channel, note, static_cast<juce::uint8> (96)) }; }
MidiEvent off (int sample, int channel = 2, int note = 60)
{ return { sample, juce::MidiMessage::noteOff (channel, note) }; }
MidiEvent cc (int sample, int channel, int number, int value)
{ return { sample, juce::MidiMessage::controllerEvent (channel, number, value) }; }
juce::var midiJson (const std::vector<MidiEvent>& events, int start, int samples, bool absolute)
{
    juce::Array<juce::var> result;
    for (const auto& event : events)
        if (absolute || (event.sample >= start && event.sample < start + samples))
        {
            auto item = object(); put (item, absolute ? "sample" : "offset", absolute ? event.sample : event.sample - start);
            put (item, "hex", juce::String::toHexString (event.message.getRawData(), event.message.getRawDataSize()));
            result.add (item);
        }
    return result;
}
void fillMidi (juce::MidiBuffer& midi, const std::vector<MidiEvent>& events, int start, int samples)
{
    midi.clear();
    for (const auto& event : events)
        if (event.sample >= start && event.sample < start + samples)
            require (midi.addEvent (event.message, event.sample - start), "Cannot insert input MIDI event");
}
void set (AnyvocAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id); require (parameter != nullptr, std::string ("Missing parameter ") + id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
void configure (AnyvocAudioProcessor& processor, bool arp)
{
    processor.setCurrentProgram (0);
    for (const char* id : { "osc2Level", "osc3Level", "subLevel", "noiseLevel", "drive", "filterEnabled", "filter2Enabled",
                            "chorusMix", "delayMix", "reverbMix", "osc1Position", "osc1Phase", "osc1PhaseRandom", "osc1RandomUnison", "voiceMode" })
        set (processor, id, 0);
    set (processor, "osc1Level", .5f); set (processor, "osc1Unison", 1); set (processor, "master", .5f);
    set (processor, "ampAttack", .002f); set (processor, "ampDecay", .03f);
    set (processor, "ampSustain", 1); set (processor, "ampRelease", .04f);
    set (processor, "tempo", 120); set (processor, "arpRate", 3); set (processor, "arpGate", .5f);
    set (processor, "arpOctaves", 1); set (processor, "arpEnabled", arp ? 1.0f : 0.0f);
    processor.prepareToPlay (sampleRate, maximumBlock);
}
juce::var parameterJson (AnyvocAudioProcessor& processor)
{
    juce::Array<juce::var> result;
    for (auto* p : processor.getParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p);
        require (parameter != nullptr, "Unexpected non-ranged processor parameter");
        auto item = object(); put (item, "id", parameter->getParameterID());
        put (item, "normalized", static_cast<double> (parameter->getValue())); result.add (item);
    }
    return result;
}

struct Block { int start, samples; const char* action = "none"; };
struct Fixture
{
    const char* id; int frames = 15000; bool warmed = true, arp = true, oracle = false;
    bool rotation = false, longCc = false, lifecycle = false, keyboard = false, sysex = false;
    std::vector<MidiEvent> input, reference; std::vector<Block> blocks;
};
std::vector<Fixture> fixtures()
{
    std::vector<Fixture> result;
    for (const char* id : { "fresh-cold", "fresh-warmed", "rotation", "long-cc", "lifecycle",
                            "keyboard-off", "keyboard-on", "sysex-off", "sysex-on" })
    {
        Fixture f { id }; const std::string name (id);
        f.warmed = name != "fresh-cold"; f.rotation = name == "rotation"; f.longCc = name == "long-cc";
        f.lifecycle = name == "lifecycle"; f.keyboard = name.find ("keyboard-") == 0; f.sysex = name.find ("sysex-") == 0;
        f.arp = name != "keyboard-off" && name != "sysex-off";
        f.oracle = name == "fresh-warmed" || f.rotation || f.longCc;
        f.frames = f.lifecycle ? 22000 : f.longCc ? 19000 : 15000;
        f.input = { on (37), off (f.longCc ? 16000 : 12001) };
        // Explicit MIDI oracle: 3000-sample step, 1500-sample gate. No production clock code is reused.
        f.reference = { on (37), off (1537), on (3037), off (4537), on (6037), off (7537), on (9037), off (10537) };
        if (f.longCc)
        {
            f.reference.insert (f.reference.end(), { on (12037), off (13537), on (15037), off (16000) });
            for (int start : { 0, 8192, 16384 }) for (int n = 0; n < 128; ++n)
            {
                const auto event = cc (start + n, 1 + n % 16, 7, n);
                f.input.push_back (event); f.reference.push_back (event);
            }
        }
        if (f.lifecycle)
            f.input = { on (37), cc (100, 2, 64, 127), off (500), cc (2000, 2, 121, 0), on (2500, 2, 67),
                        cc (3500, 2, 123, 0), on (4200, 2, 65), off (6000, 2, 65), cc (6500, 2, 120, 0),
                        on (8237, 2, 72), cc (9000, 2, 1, 85), { 9500, juce::MidiMessage::pitchWheel (2, 12000) },
                        cc (10000, 2, 121, 0), off (11000, 2, 72), on (12325, 1, 60), off (15000, 1, 60),
                        on (16421, 2, 67), cc (16500, 2, 64, 127), off (17000, 2, 67), cc (19000, 2, 121, 0) };
        if (f.keyboard) f.input.clear();
        if (f.sysex)
        {
            std::array<juce::uint8, 1024> payload {}; payload[0] = 0x7d;
            for (size_t n = 1; n < payload.size(); ++n) payload[n] = static_cast<juce::uint8> (n % 128);
            const auto message = juce::MidiMessage::createSysExMessage (payload.data(), static_cast<int> (payload.size()));
            f.input.push_back ({ 511, message }); f.input.push_back ({ 8192, message });
        }
        const auto less = [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; };
        std::stable_sort (f.input.begin(), f.input.end(), less); std::stable_sort (f.reference.begin(), f.reference.end(), less);
        int index = 0;
        for (int start = 0; start < f.frames;)
        {
            if (f.lifecycle && start % 4096 == 0 && start <= 16384)
            {
                const char* action = start == 4096 ? "disable-arp" : start == 8192 ? "enable-arp"
                                   : start == 12288 ? "prepare" : start == 16384 ? "panic" : "none";
                f.blocks.push_back ({ start, 0, action });
            }
            constexpr int sizes[] { 127, 256, 511 };
            int length = std::min (f.longCc ? maximumBlock : sizes[index++ % 3], f.frames - start);
            if (f.lifecycle && start < 16384) length = std::min (length, (start / 4096 + 1) * 4096 - start);
            if (f.rotation && start < 7500) length = std::min (length, 7500 - start);
            if (f.keyboard && start < 8000) length = std::min (length, 8000 - start);
            f.blocks.push_back ({ start, length }); start += length;
        }
        result.push_back (std::move (f));
    }
    return result;
}

juce::var warmup (AnyvocAudioProcessor& processor, const Fixture& fixture)
{
    juce::Array<juce::var> blocks;
    if (fixture.warmed)
        for (int start = 0; start < 4096; start += 256)
        {
            juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
            processor.processBlock (audio, midi); require (midi.isEmpty(), "Warmup leaked MIDI");
            auto item = object(); put (item, "samples", 256); put (item, "input_midi", juce::Array<juce::var>());
            put (item, "action", "none"); blocks.add (item);
        }
    processor.panic();
    if (fixture.keyboard)
    {
        // Consume hard reset before queuing the UI key; otherwise reset correctly discards that pending key.
        juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi; processor.processBlock (audio, midi);
        require (midi.isEmpty(), "Keyboard reset warmup leaked MIDI");
        auto item = object(); put (item, "samples", 256); put (item, "input_midi", juce::Array<juce::var>());
        put (item, "action", "consume-panic"); blocks.add (item);
    }
    return blocks;
}

juce::var writeAudio (const juce::File& directory, const juce::String& name,
                     const juce::AudioBuffer<float>& audio, const char* id, const char* role)
{
    static_assert (sizeof (float) == 4 && std::numeric_limits<float>::is_iec559, "Requires IEEE754 float32");
    std::vector<juce::uint32> bits (static_cast<size_t> (audio.getNumSamples()) * 2);
    double energy = 0, peak = 0;
    for (int n = 0; n < audio.getNumSamples(); ++n) for (int ch = 0; ch < 2; ++ch)
    {
        const float sample = audio.getSample (ch, n); require (std::isfinite (sample), "Non-finite probe audio");
        peak = std::max (peak, std::abs (static_cast<double> (sample))); energy += static_cast<double> (sample) * sample;
        juce::uint32 raw; std::memcpy (&raw, &sample, sizeof (raw));
        bits[static_cast<size_t> (n) * 2 + ch] = juce::ByteOrder::swapIfBigEndian (raw);
    }
    const double rms = std::sqrt (energy / static_cast<double> (bits.size()));
    require (rms > 1.0e-6 && peak <= 1.001, "Probe audio silent or unbounded");
    const auto file = directory.getChildFile (name); auto stream = file.createOutputStream();
    require (stream != nullptr && stream->openedOk() && stream->write (bits.data(), bits.size() * sizeof (bits[0])), "Raw audio write failed");
    stream->flush(); require (stream->getStatus().wasOk(), "Raw audio flush failed"); stream.reset();
    auto item = object(); put (item, "case_id", id); put (item, "role", role); put (item, "file", name);
    put (item, "frames", audio.getNumSamples()); put (item, "channels", 2);
    put (item, "bytes", static_cast<juce::int64> (bits.size() * 4)); put (item, "sha256", juce::SHA256 (file).toHexString());
    put (item, "rms", rms); put (item, "peak", peak); put (item, "finite", true); return item;
}

void runFixture (const Fixture& fixture, const juce::File& directory, juce::Array<juce::var>& definitions,
                 juce::Array<juce::var>& reports, juce::Array<juce::var>& audioFiles)
{
    auto processor = std::make_unique<AnyvocAudioProcessor>(); configure (*processor, fixture.arp);
    auto definition = object(); put (definition, "id", fixture.id); put (definition, "frames", fixture.frames);
    put (definition, "channels", 2); put (definition, "audio_file", juce::String (fixture.id) + ".f32le");
    put (definition, "oracle_file", fixture.oracle ? juce::var (juce::String (fixture.id) + "-manual-reference.f32le") : juce::var());
    put (definition, "initial_parameters", parameterJson (*processor)); put (definition, "input_events", midiJson (fixture.input, 0, 0, true));
    put (definition, "manual_reference_events", fixture.oracle ? midiJson (fixture.reference, 0, 0, true) : juce::var());
    put (definition, "warmup_blocks", warmup (*processor, fixture));
    put (definition, "warmup_frames", fixture.warmed ? (fixture.keyboard ? 4352 : 4096) : 0);
    put (definition, "before_measured_blocks", fixture.keyboard ? "panic already consumed" : "panic requested outside capture; consumed in first block");
    juce::Array<juce::var> keyboard;
    if (fixture.keyboard)
        for (int sample : { 0, 8000 })
        {
            auto item = object(); put (item, "sample", sample); put (item, "kind", sample == 0 ? "noteOn" : "noteOff");
            put (item, "channel", 2); put (item, "note", 60); put (item, "velocity", sample == 0 ? 96.0 / 127.0 : 0.0); keyboard.add (item);
        }
    put (definition, "keyboard_events", keyboard);
    std::unique_ptr<AnyvocAudioProcessor> reference;
    if (fixture.oracle) { reference = std::make_unique<AnyvocAudioProcessor>(); configure (*reference, false); warmup (*reference, fixture); }
    juce::AudioBuffer<float> actual (2, fixture.frames), expected (2, fixture.frames); actual.clear(); expected.clear();
    auto capture = std::make_unique<probe::Capture>();
    std::array<std::unique_ptr<juce::MidiBuffer>, 3> rotating;
    for (auto& item : rotating) item = std::make_unique<juce::MidiBuffer>();
    juce::Array<juce::var> blockDefinitions, blockReports, allocationEvents;
    Totals totals; std::uint64_t totalCalls = 0, totalBytes = 0, overflow = 0;
    bool hostEmpty = true; int index = 0;
    for (const auto& spec : fixture.blocks)
    {
        const std::string action (spec.action);
        if (action == "disable-arp") set (*processor, "arpEnabled", 0);
        else if (action == "enable-arp") set (*processor, "arpEnabled", 1);
        else if (action == "prepare") processor->prepareToPlay (sampleRate, maximumBlock);
        else if (action == "panic") processor->panic();
        std::unique_ptr<juce::MidiBuffer> fresh; juce::MidiBuffer* host = nullptr;
        const char* policy = fixture.sysex ? "fresh-reserved32768" : "fresh";
        if (fixture.rotation)
        {
            auto& slot = rotating[static_cast<size_t> (index % 3)];
            if (spec.start >= 7500) slot = std::make_unique<juce::MidiBuffer>();
            host = slot.get(); policy = spec.start >= 7500 ? "rotate-3-rebuild-outside-capture" : "rotate-3-retain-capacity";
        }
        else { fresh = std::make_unique<juce::MidiBuffer>(); host = fresh.get(); }
        if (fixture.sysex) host->ensureSize (32768);
        fillMidi (*host, fixture.input, spec.start, spec.samples);
        const char* keyboardAction = nullptr;
        if (fixture.keyboard && spec.start == 0)
        { processor->keyboardState.noteOn (2, 60, 96.0f / 127.0f); keyboardAction = "noteOn"; }
        if (fixture.keyboard && spec.start == 8000)
        { processor->keyboardState.noteOff (2, 60, 0); keyboardAction = "noteOff"; }
        juce::AudioBuffer<float> block (2, spec.samples);
        auto blockDefinition = object(); put (blockDefinition, "index", index); put (blockDefinition, "start", spec.start);
        put (blockDefinition, "samples", spec.samples); put (blockDefinition, "host_policy", policy); put (blockDefinition, "action", spec.action);
        put (blockDefinition, "input_midi", midiJson (fixture.input, spec.start, spec.samples, false));
        put (blockDefinition, "oracle_input_midi", fixture.oracle ? midiJson (fixture.reference, spec.start, spec.samples, false) : juce::var());
        put (blockDefinition, "keyboard_action", keyboardAction == nullptr ? juce::var() : juce::var (keyboardAction));
        blockDefinitions.add (blockDefinition); capture->clear();
        {
            // The only measured operation. All buffers, recorder storage and input events already exist.
            probe::CaptureScope scope (*capture);
            processor->processBlock (block, *host);
        }
        hostEmpty &= host->isEmpty(); overflow += capture->overflow;
        auto blockReport = object(); Totals blockTotals; std::uint64_t blockBytes = 0;
        for (size_t e = 0; e < capture->count; ++e)
        {
            const auto& event = capture->events[e]; totals.add (event, host); blockTotals.add (event, host);
            blockBytes += event.bytes; allocationEvents.add (eventJson (event, host, index));
        }
        totalCalls += capture->count; totalBytes += blockBytes;
        put (blockReport, "index", index); put (blockReport, "start", spec.start); put (blockReport, "samples", spec.samples);
        put (blockReport, "count", static_cast<juce::int64> (capture->count)); put (blockReport, "requested_bytes", static_cast<juce::int64> (blockBytes));
        put (blockReport, "overflow", static_cast<juce::int64> (capture->overflow)); put (blockReport, "by_site_owner", blockTotals.json()); blockReports.add (blockReport);
        for (int ch = 0; ch < 2; ++ch) if (spec.samples > 0) actual.copyFrom (ch, spec.start, block, ch, 0, spec.samples);
        if (reference)
        {
            juce::MidiBuffer midi; fillMidi (midi, fixture.reference, spec.start, spec.samples);
            reference->processBlock (block, midi); require (midi.isEmpty(), "Manual reference leaked host MIDI");
            for (int ch = 0; ch < 2; ++ch) if (spec.samples > 0) expected.copyFrom (ch, spec.start, block, ch, 0, spec.samples);
        }
        ++index;
    }
    bool equal = true;
    if (reference)
        for (int ch = 0; ch < 2; ++ch)
            equal &= std::memcmp (actual.getReadPointer (ch), expected.getReadPointer (ch), static_cast<size_t> (fixture.frames) * sizeof (float)) == 0;
    auto report = object(); put (report, "id", fixture.id); put (report, "host_midi_empty", hostEmpty);
    put (report, "manual_reference_checked", fixture.oracle); put (report, "manual_reference_match", reference ? juce::var (equal) : juce::var());
    put (report, "allocation_count", static_cast<juce::int64> (totalCalls)); put (report, "requested_bytes", static_cast<juce::int64> (totalBytes));
    put (report, "overflow", static_cast<juce::int64> (overflow)); put (report, "allocation_events", allocationEvents);
    put (report, "blocks", blockReports); put (report, "by_site_owner", totals.json());
    bool positive = true;
    if (fixture.keyboard)
        positive = totals.calls[0][0] + totals.calls[1][0] + totals.calls[2][0] > 0;
    if (fixture.sysex)
        positive = totals.calls[3][0] + totals.calls[3][1] + totals.calls[3][2] > 0;
    put (report, "positive_control_required", fixture.keyboard || fixture.sysex);
    put (report, "positive_control_passed", positive); put (report, "valid", hostEmpty && equal && overflow == 0 && positive);
    put (definition, "blocks", blockDefinitions); definitions.add (definition); reports.add (report);
    // Keep raw audio and diagnostic facts even if the handwritten oracle rejects the run.
    audioFiles.add (writeAudio (directory, juce::String (fixture.id) + ".f32le", actual, fixture.id, "actual"));
    if (reference) audioFiles.add (writeAudio (directory, juce::String (fixture.id) + "-manual-reference.f32le", expected, fixture.id, "manual-reference"));
    require (hostEmpty && overflow == 0 && equal && positive, std::string ("Invalid fixture: ") + fixture.id
             + " hostEmpty=" + std::to_string (hostEmpty) + " overflow=" + std::to_string (overflow)
             + " manualMatch=" + std::to_string (equal) + " positive=" + std::to_string (positive));
    std::cout << "CASE " << fixture.id << " valid; captured calls=" << totalCalls << ", requested bytes=" << totalBytes << '\n';
}
}

int main (int argc, char** argv)
{
    juce::File directory; bool claimed = false;
    auto manifest = object(), report = object(), audio = object();
    juce::Array<juce::var> definitions, reports, audioFiles;
    const auto saveProgress = [&]
    {
        put (manifest, "cases", definitions); put (report, "cases", reports); put (audio, "files", audioFiles);
        writeJson (directory.getChildFile ("fixture-manifest.json"), manifest);
        writeJson (directory.getChildFile ("allocation-report.json"), report);
        writeJson (directory.getChildFile ("audio-meta.json"), audio);
    };
    try
    {
        require (argc == 5 && std::string (argv[1]) == "--output" && std::string (argv[3]) == "--label"
                     && (std::string (argv[4]) == "before" || std::string (argv[4]) == "after"),
                 "Usage: AnyvocMidiAllocationProbe --output ABS_NEW_DIR --label before|after");
        const std::filesystem::path path (juce::String::fromUTF8 (argv[2]).toWideCharPointer());
        require (path.is_absolute(), "Output directory must be absolute"); directory = juce::File (juce::String::fromUTF8 (argv[2]));
        std::filesystem::create_directories (path.parent_path());
        require (std::filesystem::create_directory (path), "Output directory must not already exist"); claimed = true;
        put (manifest, "schema", 1); put (manifest, "format", "f32le-interleaved-stereo"); put (manifest, "sample_rate", static_cast<int> (sampleRate));
        put (manifest, "coverage", "Instrumented JUCE allocation sites only; no device, no operator-new interception, no general callback allocation or realtime guarantee");
        put (manifest, "manual_reference_scope", "fresh-warmed, rotation, long-cc only; remaining six cases are old/new raw-audio regressions, not independent MIDI oracles");
        put (report, "schema", 1); put (report, "label", argv[4]); put (report, "valid", false); put (report, "controls_passed", false);
        put (report, "owner_scope", "host means exact current host MidiBuffer identity; other-MidiBuffer is another addEvent owner, not inferred from pointer ranges");
        put (audio, "schema", 1); saveProgress();
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        put (report, "recorder_self_tests", recorderSelfTests());
        const auto controls = allocatorControls(); put (report, "controls", controls); bool passed = true;
        for (const auto& control : *controls.getArray()) passed &= static_cast<bool> (control["passed"]);
        put (report, "controls_passed", passed); saveProgress(); require (passed, "Real allocator controls did not observe all four instrumentation sites");
        const auto cases = fixtures();
        for (const auto& fixture : cases) { runFixture (fixture, directory, definitions, reports, audioFiles); saveProgress(); }
        require (reports.size() == 9, "Incomplete fixture set"); put (report, "valid", true); saveProgress();
        std::cout << "MIDI ALLOCATION DIAGNOSTIC COMPLETE: 9 cases; allocation totals are observations, not zero-allocation acceptance\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (claimed)
            try
            {
                put (report, "valid", false); put (report, "error", error.what()); saveProgress();
                auto failure = object(); put (failure, "valid", false); put (failure, "error", error.what());
                writeJson (directory.getChildFile ("failure.json"), failure);
            }
            catch (...) { std::cerr << "Could not save failure report\n"; }
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
