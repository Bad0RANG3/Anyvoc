#include "PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Developer-only measurement, not a test that promises a real-time deadline.
// Hardware outputs contain only zeros. The real processor renders to private RAM.
namespace
{
using Tick = juce::int64;
constexpr const char* backend = "Windows Audio"; // WASAPI shared, not exclusive/low-latency mode.

void require (bool condition, const juce::String& message)
{
    if (! condition) throw std::runtime_error (message.toStdString());
}

struct Options
{
    std::string scenario = "baseline";
    double seconds = 60.0, warmup = 3.0, sampleRate = 48000.0;
    int bufferSize = 256;
    juce::String output;
    bool help = false, list = false;
};

void help()
{
    std::cout
        << "AnyvocRealtimeProbe - developer-only silent WASAPI shared callback measurement\n"
        << "  --list                       List shared output endpoints/rates/buffers; no stream opened\n"
        << "  --case baseline|unison|matrix|chaos|chaos-rossler|granular  (default baseline)\n"
        << "  --seconds N                  Measured audio seconds, 0.1..3600 (default 60)\n"
        << "  --warmup N                   Excluded warmup seconds, 0..60 (default 3)\n"
        << "  --sample-rate N              Requested Hz, or 0 for device default (default 48000)\n"
        << "  --buffer-size N              Requested frames, or 0 for default (default 256)\n"
        << "  --output ABSBASE             Required absolute basename; writes ABSBASE.json/.csv\n"
        << "  --help\n\n"
        << "One case per invocation. Fixed 16-voice fixture; no external MIDI/input, editor,\n"
        << "user settings, or audible test output. Existing result files are never overwritten.\n"
        << "An atomic ABSBASE.capture-lock directory reserves the output namespace; a stale\n"
        << "lock after a crash is rejected. Use a new basename for every attempt.\n"
        << "Warmup/measurement round up to whole actual device blocks; raw CSV keeps both.\n"
        << "Exit 0 means a complete valid measurement, even when deadlines were exceeded.\n"
        << "Exit 1 means invalid/error; exit 2 means invalid CLI. Native driver xruns may be\n"
        << "unsupported; callback overruns and scheduling gaps do not prove audible dropouts.\n";
}

double number (const std::string& text)
{
    size_t consumed = 0;
    const auto result = std::stod (text, &consumed);
    require (consumed == text.size() && std::isfinite (result), "Invalid finite numeric argument: " + juce::String (text));
    return result;
}

Options parseOptions (int argc, char** argv)
{
    Options options;
    std::set<std::string> seen;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key (argv[i]);
        require (seen.insert (key).second, "Repeated argument: " + juce::String (key));
        if (key == "--help") { options.help = true; continue; }
        if (key == "--list") { options.list = true; continue; }
        require (i + 1 < argc, "Missing value for " + juce::String (key));
        const std::string value (argv[++i]);
        if (key == "--case") options.scenario = value;
        else if (key == "--seconds") options.seconds = number (value);
        else if (key == "--warmup") options.warmup = number (value);
        else if (key == "--sample-rate") options.sampleRate = number (value);
        else if (key == "--buffer-size")
        {
            const auto n = number (value);
            require (n >= 0 && n <= 8192 && n == std::floor (n), "Buffer must be an integer in 0..8192");
            options.bufferSize = static_cast<int> (n);
        }
        else if (key == "--output") options.output = juce::String (value);
        else throw std::runtime_error ("Unknown argument: " + key);
    }
    const std::set<std::string> cases { "baseline", "unison", "matrix", "chaos", "chaos-rossler", "granular" };
    require (cases.count (options.scenario) != 0, "Unknown case");
    require (options.seconds >= .1 && options.seconds <= 3600, "Seconds must be in 0.1..3600");
    require (options.warmup >= 0 && options.warmup <= 60, "Warmup must be in 0..60");
    require (options.sampleRate == 0 || (options.sampleRate >= 8000 && options.sampleRate <= 192000),
             "Sample rate must be 0 or in 8000..192000");
    require (options.bufferSize == 0 || options.bufferSize >= 16, "Buffer must be 0 or in 16..8192");
    if (! options.help && ! options.list)
        require (options.output.isNotEmpty() && juce::File::isAbsolutePath (options.output), "--output requires an absolute basename");
    return options;
}

struct CaptureReservation
{
    explicit CaptureReservation (const juce::String& base)
        : lockPath (juce::File (base + ".capture-lock").getFullPathName().toWideCharPointer())
    {
        require (juce::File (base + ".json").getParentDirectory().createDirectory().wasOk(), "Cannot create output directory");
        std::error_code error;
        const auto acquired = std::filesystem::create_directory (lockPath, error);
        require (acquired && ! error, "Output namespace is occupied or unavailable (including a stale crash lock); use a new basename");
    }
    ~CaptureReservation()
    {
        std::error_code ignored;
        std::filesystem::remove (lockPath, ignored); // Only our empty lock directory, never recursive.
    }
    std::filesystem::path lockPath;
};

juce::var object() { return juce::var (new juce::DynamicObject()); }
void field (juce::var& target, const char* key, const juce::var& value) { target.getDynamicObject()->setProperty (key, value); }

void set (AnyvocAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    require (parameter != nullptr, "Missing fixture parameter: " + id);
    require (value >= parameter->getNormalisableRange().start && value <= parameter->getNormalisableRange().end,
             "Fixture parameter out of actual host range: " + id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct TemporarySample
{
    juce::File file;
    ~TemporarySample() { if (file.existsAsFile()) file.deleteFile(); }
};

juce::var configure (AnyvocAudioProcessor& processor, const Options& options)
{
    processor.setCurrentProgram (0);
    for (const char* id : { "arpEnabled", "voiceMode", "subLevel", "noiseLevel", "drive", "chorusMix", "delayMix", "reverbMix", "filterEnabled", "filter2Enabled" })
        set (processor, id, 0);
    set (processor, "master", .65f);
    set (processor, "ampAttack", .005f); set (processor, "ampDecay", .02f);
    set (processor, "ampSustain", .8f); set (processor, "ampRelease", .05f);
    for (int o = 1; o <= 3; ++o)
    {
        const auto prefix = "osc" + juce::String (o);
        set (processor, prefix + "Level", o == 1 ? .12f : 0.0f);
        set (processor, prefix + "Position", .37f); set (processor, prefix + "Unison", 1);
    }
    for (int macro = 1; macro <= 8; ++macro) set (processor, "macro" + juce::String (macro), .5f);
    for (int lfo = 1; lfo <= 10; ++lfo) set (processor, "lfo" + juce::String (lfo) + "Mode", 1);

    auto workload = object();
    field (workload, "voices", 16); field (workload, "midi_channel", 1);
    field (workload, "note_first", 48); field (workload, "note_last", 63);
    field (workload, "onset_spacing_samples", 17);
    field (workload, "velocity_formula", "0.4f + 0.03f * voiceIndex (0..15), encoded by JUCE MIDI noteOn");
    field (workload, "fx_mix_zero", true);
    field (workload, "description", "16 sustained polyphonic notes; one wavetable oscillator, unison 1, filters bypassed");

    if (options.scenario == "unison")
    {
        for (int o = 1; o <= 3; ++o)
        {
            const auto prefix = "osc" + juce::String (o);
            set (processor, prefix + "Level", .08f); set (processor, prefix + "Unison", 7);
        }
        set (processor, "filterEnabled", 1); set (processor, "filter2Enabled", 1);
        set (processor, "filterSlope", 1); set (processor, "filter2Slope", 1);
        set (processor, "cutoff", 1200); set (processor, "filter2Cutoff", 3500);
        field (workload, "description", "16 voices; three audible wavetable oscillators, unison 7 each; serial dual 24 dB filters");
    }
    else if (options.scenario == "matrix")
    {
        for (int r = 0; r < 32; ++r)
        {
            const auto prefix = "mod" + juce::String (r + 1);
            processor.setModulationSource (r, r % 3 == 0 ? anyvoc::lfoSourceId (r % 10) : anyvoc::macroSourceId (r % 8));
            processor.setModulationDestination (r, r < 24 ? 28 + (r * 3 + 1) % 8 : r < 28 ? 18 + r % 10 : 7);
            processor.setModulationSource (r, r % 2 == 0 ? 4 : anyvoc::macroSourceId ((r + 2) % 8), true);
            set (processor, prefix + "Amount", r % 2 == 0 ? .15f : -.15f);
            set (processor, prefix + "Polarity", static_cast<float> (r % 3));
            set (processor, prefix + "AuxPolarity", static_cast<float> ((r + 1) % 3));
            set (processor, prefix + "Curve", -.8f + static_cast<float> (r) * 1.6f / 31.0f);
            set (processor, prefix + "AuxCurve", .75f - static_cast<float> (r) * 1.5f / 31.0f);
            require (processor.getModulationRoute (r).source != 0 && processor.getModulationDestination (r) != 0,
                     "Matrix fixture route was not installed");
        }
        field (workload, "description", "16 voices; 32 curved primary/AUX routes with macro and phase feedback, matching macro benchmark topology; actual host-quantised values in parameters");
    }
    else if (options.scenario == "chaos" || options.scenario == "chaos-rossler")
    {
        for (int lfo = 0; lfo < 10; ++lfo)
        {
            const auto prefix = "lfo" + juce::String (lfo + 1);
            set (processor, prefix + "Chaos", options.scenario == "chaos" ? 1.0f : 2.0f);
            set (processor, prefix + "Sync", 0); set (processor, prefix + "AudioRateEnabled", 1);
            set (processor, prefix + "AudioRateHz", 1000);
            processor.setModulationSource (lfo, anyvoc::lfoSourceId (lfo));
            processor.setModulationDestination (lfo, 7);
            set (processor, "mod" + juce::String (lfo + 1) + "Amount", .025f);
        }
        field (workload, "description", options.scenario == "chaos"
            ? "16 voices; all ten LFOs Trigger Lorenz 1000 Hz, Sync off; ten pan routes (host amount quantisation recorded)"
            : "16 voices; all ten LFOs Trigger Rossler 1000 Hz, Sync off; ten pan routes (host amount quantisation recorded)");
    }
    else if (options.scenario == "granular")
    {
        TemporarySample temporary;
        temporary.file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getNonexistentChildFile ("Anyvoc-RealtimeProbe-sample", ".wav", false);
        juce::AudioBuffer<float> audio (2, 96000);
        for (int n = 0; n < audio.getNumSamples(); ++n)
            for (int ch = 0; ch < 2; ++ch)
            {
                const double t = n / 48000.0, phase = ch * .31;
                audio.setSample (ch, n, static_cast<float> (.22 * std::sin (juce::MathConstants<double>::twoPi * 220 * t + phase)
                    + .1 * std::sin (juce::MathConstants<double>::twoPi * 331 * t + phase)));
            }
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = temporary.file.createOutputStream();
        require (stream != nullptr, "Cannot create isolated granular WAV");
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
        require (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "Cannot write granular WAV");
        writer.reset();
        field (workload, "sample_sha256", juce::SHA256 (temporary.file).toHexString());
        field (workload, "sample_formula", "2ch, 96000 frames at 48000 Hz; .22*sin(2*pi*220*t+ch*.31)+.1*sin(2*pi*331*t+ch*.31), PCM24 WAV");
        for (int o = 0; o < 3; ++o)
        {
            require (processor.importSample (o, temporary.file), "Granular sample import failed");
            const auto prefix = "osc" + juce::String (o + 1);
            set (processor, prefix + "Mode", 2); set (processor, prefix + "Level", .08f);
            set (processor, prefix + "Unison", 7); set (processor, prefix + "Loop", 1);
            set (processor, prefix + "GrainSize", .5f); set (processor, prefix + "GrainDensity", 80);
            set (processor, prefix + "GrainSpray", 1);
            require (processor.getSampleName (o).isNotEmpty(), "Granular oscillator has no sample");
        }
        field (workload, "description", "16 voices; three loaded stereo granular oscillators, .5 s grains, 80 Hz density, loop on, unison 7; about 40 simultaneous grains/voice/osc after warmup, not 7 times that count");
    }
    return workload;
}

juce::var snapshotParameters (AnyvocAudioProcessor& processor)
{
    juce::Array<juce::var> values;
    for (auto* base : processor.getParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
        require (parameter != nullptr, "Unexpected non-ranged parameter");
        auto value = object();
        field (value, "id", parameter->getParameterID());
        field (value, "normalised", parameter->getValue());
        field (value, "plain", parameter->convertFrom0to1 (parameter->getValue()));
        values.add (value);
    }
    return values;
}

struct Actual
{
    juce::String name, type;
    double rate = 0;
    int block = 0, inputs = 0, outputs = 0, latency = 0;
};

struct Record
{
    size_t index = 0;
    std::uint64_t startSample = 0;
    int samples = 0;
    bool warmup = false, hostTimeAvailable = false;
    std::uint64_t hostTime = 0, finite = 0, nonfinite = 0, nonzero = 0, hardwareNonzero = 0;
    Tick entry = 0, processStart = 0, processEnd = 0, bodyEnd = 0, interval = 0;
    double squares = 0, peak = 0;
};

size_t checkedRecordCount (size_t warmup, size_t measured)
{
    constexpr size_t memoryLimit = 128u * 1024u * 1024u;
    const auto count = warmup + measured;
    require (count >= measured && count <= 2000000 && count <= memoryLimit / sizeof (Record),
             "Requested record count exceeds 2 million rows / 128 MiB; shorten this invocation");
    return count;
}

enum Failure : unsigned
{
    unexpectedStart = 1u, changedConfiguration = 2u, overflow = 4u, deviceError = 8u,
    unexpectedStop = 16u, nonfiniteAudio = 32u, hardwareAudio = 64u, timeout = 128u,
    processingException = 256u, badClock = 512u
};

class Callback final : public juce::AudioIODeviceCallback
{
public:
    Callback (AnyvocAudioProcessor& p, Actual a, const Options& options)
        : processor (p), actual (std::move (a)), scratch (2, actual.block),
          warmupBlocks (static_cast<size_t> (std::ceil (options.warmup * actual.rate / actual.block))),
          measuredBlocks (static_cast<size_t> (std::ceil (options.seconds * actual.rate / actual.block))),
          records (checkedRecordCount (warmupBlocks, measuredBlocks))
    {
        midi.ensureSize (32768);
        // Prepare and asset/parameter work occur before callback registration.
        processor.setNonRealtime (false);
        processor.setRateAndBufferSizeDetails (actual.rate, actual.block);
        processor.prepareToPlay (actual.rate, actual.block);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        if (starts.fetch_add (1) != 0) fail (unexpectedStart);
        if (device == nullptr || device->getCurrentSampleRate() != actual.rate
            || device->getCurrentBufferSizeSamples() != actual.block
            || device->getActiveInputChannels().countNumberOfSetBits() != 0
            || device->getActiveOutputChannels().countNumberOfSetBits() != 2)
            fail (changedConfiguration);
    }

    void audioDeviceStopped() override
    {
        if (! stopping.load()) fail (unexpectedStop);
    }

    void audioDeviceError (const juce::String&) override { fail (deviceError); }

    void audioDeviceIOCallbackWithContext (const float* const*, int inputs, float* const* outputs,
                                           int outputCount, int samples,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        const auto entry = juce::Time::getHighResolutionTicks();
        // This is the only access to device audio. No test signal is ever copied here.
        if (samples > 0 && outputs != nullptr)
            for (int ch = 0; ch < outputCount; ++ch)
                if (outputs[ch] != nullptr) juce::FloatVectorOperations::clear (outputs[ch], samples);
        if (finished.load (std::memory_order_acquire)) return;
        if (inputs != 0 || outputCount != 2 || outputs == nullptr || outputs[0] == nullptr || outputs[1] == nullptr
            || samples != actual.block || samples <= 0)
        { fail (changedConfiguration); return; }
        if (written >= records.size()) { fail (overflow); return; }

        Record row;
        row.index = written; row.startSample = renderedSamples; row.samples = samples;
        row.warmup = written < warmupBlocks; row.entry = entry;
        row.interval = written == 0 ? 0 : entry - previousEntry;
        row.hostTimeAvailable = context.hostTimeNs != nullptr;
        if (context.hostTimeNs != nullptr) row.hostTime = *context.hostTimeNs;
        midi.clear();
        for (int voice = 0; voice < 16; ++voice)
        {
            const auto onset = static_cast<std::uint64_t> (voice * 17);
            if (onset >= renderedSamples && onset < renderedSamples + static_cast<std::uint64_t> (samples))
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + voice, .4f + .03f * static_cast<float> (voice)),
                               static_cast<int> (onset - renderedSamples));
        }
        row.processStart = juce::Time::getHighResolutionTicks();
        try { processor.processBlock (scratch, midi); }
        catch (...) { fail (processingException); }
        row.processEnd = juce::Time::getHighResolutionTicks();
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < samples; ++n)
            {
                const auto value = static_cast<double> (scratch.getSample (ch, n));
                if (std::isfinite (value))
                {
                    ++row.finite;
                    row.nonzero += value != 0.0 ? 1u : 0u;
                    row.squares += value * value;
                    row.peak = std::max (row.peak, std::abs (value));
                }
                else ++row.nonfinite;
                if (outputs[ch][n] != 0.0f) ++row.hardwareNonzero;
            }
        if (row.nonfinite != 0) failures.fetch_or (nonfiniteAudio);
        if (row.hardwareNonzero != 0) failures.fetch_or (hardwareAudio);
        row.bodyEnd = juce::Time::getHighResolutionTicks();
        if (row.processStart < row.entry || row.processEnd < row.processStart || row.bodyEnd < row.processEnd
            || (written > 0 && row.interval <= 0)) failures.fetch_or (badClock);
        // The final record copy and completion publication are outside body_end.
        records[written++] = row;
        renderedSamples += static_cast<std::uint64_t> (samples);
        previousEntry = entry;
        if (written == records.size() || failures.load() != 0) finished.store (true, std::memory_order_release);
    }

    void fail (unsigned flag) noexcept
    {
        failures.fetch_or (flag);
        finished.store (true, std::memory_order_release);
    }

    AnyvocAudioProcessor& processor;
    Actual actual;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    const size_t warmupBlocks, measuredBlocks;
    std::vector<Record> records; // Written only by audio; read only after removeAudioCallback.
    size_t written = 0;
    std::uint64_t renderedSamples = 0;
    Tick previousEntry = 0;
    std::atomic<bool> finished { false }, stopping { false };
    std::atomic<unsigned> failures { 0 }, starts { 0 };
};

struct Registration
{
    Registration (juce::AudioDeviceManager& m, Callback& c) : manager (m), callback (c)
    {
        manager.addAudioCallback (&callback);
    }
    ~Registration() { stop(); }
    void stop()
    {
        if (! attached) return;
        callback.stopping.store (true);
        manager.removeAudioCallback (&callback);
        attached = false;
    }
    juce::AudioDeviceManager& manager;
    Callback& callback;
    bool attached = true;
};

juce::var statistics (std::vector<double> values, juce::var overBudget)
{
    auto result = object();
    double sum = 0;
    for (const auto value : values) sum += value;
    std::sort (values.begin(), values.end());
    const auto percentile = [&] (double p)
    {
        if (values.empty()) return 0.0;
        return values[static_cast<size_t> (std::ceil (p * static_cast<double> (values.size()))) - 1];
    };
    field (result, "count", static_cast<juce::int64> (values.size()));
    field (result, "mean_ms", values.empty() ? 0.0 : sum / static_cast<double> (values.size()));
    field (result, "p50_ms", percentile (.5)); field (result, "p95_ms", percentile (.95));
    field (result, "p99_ms", percentile (.99)); field (result, "max_ms", values.empty() ? 0.0 : values.back());
    field (result, "over_budget_callbacks", overBudget);
    return result;
}

juce::var summarise (const Callback& callback, Tick frequency)
{
    std::vector<double> process, body, interval;
    juce::int64 processOver = 0, bodyOver = 0, frames = 0, finite = 0, nonfinite = 0, nonzero = 0, hardware = 0;
    double squares = 0, peak = 0;
    const auto milliseconds = [frequency] (Tick ticks) { return static_cast<double> (ticks) * 1000.0 / static_cast<double> (frequency); };
    for (size_t i = 0; i < callback.written; ++i)
    {
        const auto& r = callback.records[i];
        if (r.warmup) continue;
        const auto proc = milliseconds (r.processEnd - r.processStart), total = milliseconds (r.bodyEnd - r.entry);
        const auto budget = 1000.0 * r.samples / callback.actual.rate;
        process.push_back (proc); body.push_back (total);
        if (r.interval != 0) interval.push_back (milliseconds (r.interval));
        processOver += proc > budget ? 1 : 0; bodyOver += total > budget ? 1 : 0;
        frames += r.samples; finite += static_cast<juce::int64> (r.finite); nonfinite += static_cast<juce::int64> (r.nonfinite);
        nonzero += static_cast<juce::int64> (r.nonzero); hardware += static_cast<juce::int64> (r.hardwareNonzero);
        squares += r.squares; peak = std::max (peak, r.peak);
    }
    auto result = object();
    field (result, "process", statistics (std::move (process), processOver));
    field (result, "body", statistics (std::move (body), bodyOver));
    field (result, "interval", statistics (std::move (interval), juce::var()));
    field (result, "measured_frames", frames); field (result, "measured_audio_seconds", frames / callback.actual.rate);
    field (result, "finite_samples", finite); field (result, "nonfinite_samples", nonfinite);
    field (result, "nonzero_samples", nonzero); field (result, "sum_squares", squares);
    field (result, "rms", finite > 0 ? std::sqrt (squares / static_cast<double> (finite)) : 0.0);
    field (result, "peak", peak); field (result, "hardware_nonzero_after_clear", hardware);
    return result;
}

void writeCsv (const juce::File& destination, const Callback& callback)
{
    auto stream = destination.createOutputStream();
    require (stream != nullptr && stream->openedOk(), "Cannot open CSV output");
    stream->writeText ("index,phase,start_sample,num_samples,sample_rate,entry_ticks,process_start_ticks,process_end_ticks,body_end_ticks,interval_ticks,host_time_available,host_time_ns,finite_samples,nonfinite_samples,nonzero_samples,sum_squares,peak,hardware_nonzero_after_clear\n", false, false, "\n");
    for (size_t i = 0; i < callback.written; ++i)
    {
        const auto& r = callback.records[i];
        std::ostringstream line; line.imbue (std::locale::classic()); line << std::setprecision (17);
        line << r.index << ',' << (r.warmup ? "warmup" : "measured") << ',' << r.startSample << ',' << r.samples << ',' << callback.actual.rate << ','
             << r.entry << ',' << r.processStart << ',' << r.processEnd << ',' << r.bodyEnd << ',' << r.interval << ','
             << (r.hostTimeAvailable ? 1 : 0) << ',' << r.hostTime << ',' << r.finite << ',' << r.nonfinite << ',' << r.nonzero << ','
             << r.squares << ',' << r.peak << ',' << r.hardwareNonzero << '\n';
        const auto text = line.str();
        require (stream->write (text.data(), text.size()), "CSV write failed");
    }
    stream->flush();
    require (stream->getStatus().wasOk(), "CSV flush failed");
}

juce::AudioIODeviceType* sharedType (juce::AudioDeviceManager& manager)
{
    for (auto* type : manager.getAvailableDeviceTypes())
        if (type->getTypeName() == backend) return type;
    return nullptr;
}

void listDevices (juce::AudioDeviceManager& manager)
{
    auto* type = sharedType (manager);
    require (type != nullptr, "Windows Audio shared backend is unavailable");
    const auto outputs = type->getDeviceNames (false);
    std::cout << "Backend: " << backend << "; output only; no stream opened\n";
    for (int i = 0; i < outputs.size(); ++i)
    {
        std::cout << (i == type->getDefaultDeviceIndex (false) ? "* " : "  ") << outputs[i] << '\n';
        std::unique_ptr<juce::AudioIODevice> device (type->createDevice (outputs[i], {}));
        if (device == nullptr) { std::cout << "  Cannot query endpoint\n"; continue; }
        std::cout << "  rates:"; for (const auto rate : device->getAvailableSampleRates()) std::cout << ' ' << rate;
        std::cout << "\n  buffers:"; for (const auto size : device->getAvailableBufferSizes()) std::cout << ' ' << size;
        std::cout << "\n  default_buffer: " << device->getDefaultBufferSize() << '\n';
    }
    require (! outputs.isEmpty(), "No shared output endpoints available");
}

int run (const Options& options)
{
    const juce::File jsonFile (options.output + ".json"), csvFile (options.output + ".csv");
    require (! jsonFile.exists() && ! csvFile.exists(), "Output already exists; select a new basename");
    juce::AudioDeviceManager manager;
    auto* type = sharedType (manager);
    require (type != nullptr && ! type->getDeviceNames (false).isEmpty(), "No Windows Audio shared output endpoint");
    // A fresh manager never reads or writes the standalone application's settings.
    manager.setCurrentAudioDeviceType (backend, false);
    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = options.sampleRate; preferred.bufferSize = options.bufferSize;
    preferred.useDefaultInputChannels = false; preferred.inputChannels.clear();
    preferred.useDefaultOutputChannels = false; preferred.outputChannels.setRange (0, 2, true);
    const auto error = manager.initialise (0, 2, nullptr, false, {}, &preferred);
    require (error.isEmpty(), "Device initialisation failed: " + error);
    auto* device = manager.getCurrentAudioDevice();
    require (device != nullptr && device->isOpen(), "Device did not open");
    Actual actual { device->getName(), device->getTypeName(), device->getCurrentSampleRate(),
                    device->getCurrentBufferSizeSamples(), device->getActiveInputChannels().countNumberOfSetBits(),
                    device->getActiveOutputChannels().countNumberOfSetBits(), device->getOutputLatencyInSamples() };
    require (actual.type == backend && actual.inputs == 0 && actual.outputs == 2 && actual.rate > 0 && actual.block > 0 && actual.block <= 8192,
             "Opened configuration is not supported output-only stereo WASAPI shared");
    require ((options.sampleRate == 0 || actual.rate == options.sampleRate)
                 && (options.bufferSize == 0 || actual.block == options.bufferSize),
             "Device did not honour requested rate/buffer (actual " + juce::String (actual.rate) + " Hz / "
                 + juce::String (actual.block) + " frames); use --list or explicit 0 for device default");

    AnyvocAudioProcessor processor;
    const auto workload = configure (processor, options);
    const auto parameters = snapshotParameters (processor);
    const auto frequency = juce::Time::getHighResolutionTicksPerSecond();
    require (frequency > 0, "High-resolution timer is unavailable");
    Callback callback (processor, actual, options);
    const auto nativeStart = device->getXRunCount(), managerStart = manager.getXRunCount();
    std::cout << "Measuring " << options.scenario << ": " << actual.name << ", " << actual.rate << " Hz / " << actual.block
              << " frames, output held at zero.\n";
    Registration registration (manager, callback);
    const auto wallStart = juce::Time::getMillisecondCounterHiRes();
    const auto watchdogMs = ((options.seconds + options.warmup) * 4.0 + 30.0) * 1000.0;
    while (! callback.finished.load (std::memory_order_acquire))
    {
        if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (20)
            || juce::Time::getMillisecondCounterHiRes() - wallStart > watchdogMs)
        { callback.fail (timeout); break; }
    }
    registration.stop(); // Joins this callback via manager lock before reading plain records.
    const auto managerEnd = manager.getXRunCount(); // closeAudioDevice() would reset this counter.
    // Device-change dispatch may have deleted the pointer captured at startup.
    auto* finalDevice = manager.getCurrentAudioDevice();
    const auto nativeEnd = finalDevice != nullptr ? finalDevice->getXRunCount() : -1;
    const auto lastDeviceError = finalDevice != nullptr ? finalDevice->getLastError() : juce::String ("device_missing_at_stop");
    manager.closeAudioDevice();
    processor.releaseResources();

    juce::Array<juce::var> errors;
    const auto bits = callback.failures.load();
    const std::pair<unsigned, const char*> labels[] {
        { unexpectedStart, "device_restarted" }, { changedConfiguration, "configuration_changed" },
        { overflow, "record_capacity_exceeded" }, { deviceError, "device_error_callback" },
        { unexpectedStop, "device_stopped_early" }, { nonfiniteAudio, "nonfinite_processor_output" },
        { hardwareAudio, "hardware_output_not_zero" }, { timeout, "watchdog_or_message_loop_stopped" },
        { processingException, "process_block_exception" }, { badClock, "non_monotonic_clock" }
    };
    for (const auto& label : labels) if ((bits & label.first) != 0) errors.add (label.second);
    if (callback.written != callback.records.size()) errors.add ("incomplete_recording");
    if (callback.starts.load() != 1) errors.add ("unexpected_device_start_count");
    if (lastDeviceError.isNotEmpty()) errors.add ("device_last_error: " + lastDeviceError);
    const bool nativeSupported = nativeStart >= 0 && nativeEnd >= 0;
    if ((nativeStart < 0) != (nativeEnd < 0) || (nativeSupported && nativeEnd < nativeStart) || managerEnd < managerStart)
        errors.add ("xrun_counter_epoch_changed");
    if (nativeSupported && managerEnd - managerStart < nativeEnd - nativeStart)
        errors.add ("manager_xrun_delta_smaller_than_native_delta");
    const auto summary = summarise (callback, frequency);
    if (static_cast<double> (summary["rms"]) <= 1.0e-8 || static_cast<juce::int64> (summary["nonzero_samples"]) == 0)
        errors.add ("measured_fixture_was_silent");
    auto result = object(), requested = object(), deviceInfo = object(), recording = object(), xruns = object();
    field (result, "schema_version", 1); field (result, "status", errors.isEmpty() ? "valid" : "invalid");
    field (result, "case", juce::String (options.scenario));
   #if defined (JucePlugin_VersionString)
    field (result, "anyvoc_version", JucePlugin_VersionString);
   #else
    field (result, "anyvoc_version", "unknown");
   #endif
    field (result, "ticks_per_second", frequency);
    field (requested, "seconds", options.seconds); field (requested, "warmup_seconds", options.warmup);
    field (requested, "sample_rate", options.sampleRate); field (requested, "buffer_size", options.bufferSize);
    field (result, "requested", requested);
    field (deviceInfo, "device_type", actual.type); field (deviceInfo, "device_name", actual.name);
    field (deviceInfo, "sample_rate", actual.rate); field (deviceInfo, "buffer_size", actual.block);
    field (deviceInfo, "input_channels", actual.inputs); field (deviceInfo, "output_channels", actual.outputs);
    field (deviceInfo, "output_latency_samples", actual.latency); field (result, "actual", deviceInfo);
    field (recording, "warmup_blocks_requested", static_cast<juce::int64> (callback.warmupBlocks));
    field (recording, "measured_blocks_requested", static_cast<juce::int64> (callback.measuredBlocks));
    field (recording, "capacity", static_cast<juce::int64> (callback.records.size()));
    field (recording, "recorded_blocks", static_cast<juce::int64> (callback.written));
    field (recording, "warmup_blocks", static_cast<juce::int64> (std::min (callback.written, callback.warmupBlocks)));
    field (recording, "measured_blocks", static_cast<juce::int64> (callback.written > callback.warmupBlocks ? callback.written - callback.warmupBlocks : 0));
    field (recording, "complete", callback.written == callback.records.size()); field (result, "recording", recording);
    field (xruns, "native_supported", nativeSupported); field (xruns, "native_count_start", nativeStart); field (xruns, "native_count_end", nativeEnd);
    field (xruns, "native_delta", nativeSupported ? juce::var (nativeEnd - nativeStart) : juce::var());
    field (xruns, "manager_count_start", managerStart); field (xruns, "manager_count_end", managerEnd);
    field (xruns, "manager_estimated_overruns", std::max (0, managerEnd - managerStart - (nativeSupported ? nativeEnd - nativeStart : 0)));
    field (result, "device_xruns", xruns); field (result, "summary", summary);
    field (result, "errors", errors); field (result, "parameters", parameters); field (result, "workload", workload);
    field (result, "csv_file", csvFile.getFullPathName());
    field (result, "percentile_method", "nearest-rank: sorted[ceil(p*N)-1]");
    field (result, "timing_scope", "process is processBlock only; body includes hardware zeroing, MIDI generation and output inspection, but excludes final record copy/completion atomic and the outer JUCE/driver stack; interval is callback-entry scheduling gap");
    field (result, "limitations", "No sound played or recorded; no native output-only WASAPI xrun support expected; no allocation/lock-freedom or audible-dropout claim. Final finite output does not inspect values before engine sanitisation. Warmup excluded; output-buffer validation and recording add probe overhead. Manager estimates include warmup, outer manager work and a short post-capture detach margin.");
    writeCsv (csvFile, callback);
    require (jsonFile.replaceWithText (juce::JSON::toString (result, false)), "JSON write failed");
    std::cout << (errors.isEmpty() ? "VALID measurement" : "INVALID measurement") << ": " << callback.written << " callbacks; body over budget="
              << static_cast<juce::int64> (summary["body"]["over_budget_callbacks"]) << "; native driver xruns "
              << (nativeSupported ? "supported" : "unsupported") << ".\n" << jsonFile.getFullPathName() << '\n';
    return errors.isEmpty() ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    Options options;
    try { options = parseOptions (argc, argv); }
    catch (const std::exception& error) { std::cerr << "CLI error: " << error.what() << '\n'; help(); return 2; }
    if (options.help) { help(); return 0; }
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    std::unique_ptr<CaptureReservation> reservation;
    bool mayWriteFailure = false;
    try
    {
        if (options.list) { juce::AudioDeviceManager manager; listDevices (manager); return 0; }
        reservation = std::make_unique<CaptureReservation> (options.output);
        require (! juce::File (options.output + ".json").exists() && ! juce::File (options.output + ".csv").exists(),
                 "Output already exists; select a new basename");
        mayWriteFailure = true;
        return run (options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "INVALID measurement: " << error.what() << '\n';
        if (reservation != nullptr && mayWriteFailure)
        {
            const juce::File path (options.output + ".json");
            if (! path.exists())
            {
                auto failure = object(); juce::Array<juce::var> errors; errors.add (juce::String (error.what()));
                field (failure, "schema_version", 1); field (failure, "status", "invalid"); field (failure, "errors", errors);
                path.replaceWithText (juce::JSON::toString (failure, false));
            }
        }
        return 1;
    }
}
