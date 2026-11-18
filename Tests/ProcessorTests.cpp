#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetLibrary.h"
#include "WavetableDocument.h"
#include "ModulationView.h"
#include "LfoCurveDocument.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

struct PresetTestDirectory
{
    PresetTestDirectory()
        : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("Anyvoc-ProcessorPresetTests", {}, false))
    {
        require (directory.createDirectory().wasOk(), "Could not create isolated preset test directory");
    }
    ~PresetTestDirectory() { directory.deleteRecursively(); }
    juce::File directory;
};

void setParameter (AnyvocAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    require (parameter != nullptr, std::string ("Missing parameter: ") + id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

std::vector<float> parameters (AnyvocAudioProcessor& processor)
{
    std::vector<float> values;
    for (auto* parameter : processor.getParameters())
        values.push_back (parameter->getValue());
    return values;
}

void expectParameters (AnyvocAudioProcessor& processor, const std::vector<float>& expected, const std::string& context = {})
{
    const auto actual = parameters (processor);
    require (actual.size() == expected.size(), "Parameter count changed after state restore");
    for (size_t index = 0; index < actual.size(); ++index)
        if (! std::isfinite (actual[index]) || std::abs (actual[index] - expected[index]) >= 2.0e-5f)
        {
            const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (processor.getParameters()[static_cast<int> (index)]);
            require (false, "Parameter roundtrip mismatch at index " + std::to_string (index)
                     + " id=" + (parameter != nullptr ? parameter->getParameterID().toStdString() : "unknown")
                     + " normalized actual=" + std::to_string (actual[index]) + " expected=" + std::to_string (expected[index])
                     + " " + context);
        }
}

juce::MemoryBlock save (AnyvocAudioProcessor& processor)
{
    juce::MemoryBlock data;
    processor.getStateInformation (data);
    require (data.getSize() > 0, "DAW state was empty");
    return data;
}

juce::ValueTree stateTree (AnyvocAudioProcessor& processor)
{
    const auto data = save (processor);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
    require (xml != nullptr, "DAW state could not be decoded");
    return juce::ValueTree::fromXml (*xml);
}

void restore (AnyvocAudioProcessor& processor, const juce::ValueTree& tree)
{
    const auto xml = tree.createXml();
    require (xml != nullptr, "Test could not create state XML");
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*xml, data);
    processor.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
}

struct Event { int position; juce::MidiMessage message; };

juce::AudioBuffer<float> render (AnyvocAudioProcessor& processor, int samples,
                                const std::vector<Event>& events = {}, int blockLength = 128)
{
    juce::AudioBuffer<float> output (2, samples);
    output.clear();
    for (int start = 0; start < samples; start += blockLength)
    {
        const int length = std::min (samples - start, blockLength);
        juce::AudioBuffer<float> block (2, length);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& event : events)
            if (event.position >= start && event.position < start + length)
                midi.addEvent (event.message, event.position - start);
        processor.processBlock (block, midi);
        require (midi.isEmpty(), "Instrument leaked internal MIDI to the host");
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < length; ++sample)
            {
                const auto value = block.getSample (channel, sample);
                require (std::isfinite (value), "Processor produced NaN or infinity");
                require (std::abs (value) <= 1.001f, "Processor exceeded bounded output range");
                output.setSample (channel, start + sample, value);
            }
    }
    return output;
}

double rms (const juce::AudioBuffer<float>& audio)
{
    double energy = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto value = static_cast<double> (audio.getSample (channel, sample));
            energy += value * value;
        }
    return std::sqrt (energy / (audio.getNumSamples() * audio.getNumChannels()));
}

void audibleSettings (AnyvocAudioProcessor& processor)
{
    setParameter (processor, "ampAttack", .002f);
    setParameter (processor, "ampDecay", .03f);
    setParameter (processor, "ampSustain", .8f);
    setParameter (processor, "ampRelease", .04f);
    setParameter (processor, "chorusMix", 0);
    setParameter (processor, "delayMix", 0);
    setParameter (processor, "reverbMix", 0);
}

void testStateRoundtrip()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (4);
    setParameter (source, "master", .43f);
    setParameter (source, "mod8Source", 8);
    setParameter (source, "mod8Dest", 3);
    setParameter (source, "mod8Amount", -.37f);
    setParameter (source, "macro3", .67f);
    setParameter (source, "osc3Fine", -18.5f);
    const auto expected = parameters (source);
    const auto data = save (source);
    AnyvocAudioProcessor target;
    target.setCurrentProgram (2);
    target.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    expectParameters (target, expected);
    require (target.getCurrentProgram() == 4, "Preset selection was not restored with DAW state");
    const auto second = save (target);
    source.setCurrentProgram (0);
    source.setStateInformation (second.getData(), static_cast<int> (second.getSize()));
    expectParameters (source, expected);
}

void testPresetIdentityAndFileRoundtrip()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor source;
    source.setCurrentProgram (14);
    require (source.getLoadedPresetName() == source.getProgramName (14)
                 && source.getLoadedPresetId() == anyvoc::PresetLibrary::factoryId (source.getProgramName (14)),
             "Factory program selection did not publish its stable identity");
    setParameter (source, "master", .39f);
    setParameter (source, "osc1Warp2Amount", .23f);
    const auto expected = parameters (source);
    const auto preset = files.directory.getChildFile ("Custom Orbit.anyvoc");
    require (source.savePreset (preset), "Could not save named user preset");
    expectParameters (source, expected);
    require (source.getLoadedPresetName() == "Custom Orbit"
                 && source.getLoadedPresetId() == anyvoc::PresetLibrary::userId (preset),
             "Successful save did not publish the destination file's user identity");
    const auto xml = juce::XmlDocument::parse (preset);
    require (xml != nullptr && xml->getStringAttribute ("presetName") == "Custom Orbit"
                 && xml->getStringAttribute ("presetId") == source.getLoadedPresetId(),
             "Preset file stored a stale factory identity");

    const auto renamed = files.directory.getChildFile ("Renamed Orbit.anyvoc");
    require (preset.copyFileTo (renamed), "Could not copy preset under a different filename");
    AnyvocAudioProcessor restored;
    require (restored.loadPreset (renamed), "Could not explicitly load renamed user preset");
    expectParameters (restored, expected);
    require (restored.getLoadedPresetName() == "Renamed Orbit"
                 && restored.getLoadedPresetId() == anyvoc::PresetLibrary::userId (renamed),
             "Explicit load trusted embedded identity instead of the actual source path");
    const auto dawState = save (restored);
    require (renamed.deleteFile(), "Could not remove original preset before DAW restore");
    AnyvocAudioProcessor project;
    project.setStateInformation (dawState.getData(), static_cast<int> (dawState.getSize()));
    expectParameters (project, expected);
    require (project.getLoadedPresetName() == "Renamed Orbit"
                 && project.getLoadedPresetId() == anyvoc::PresetLibrary::userId (renamed),
             "Self-contained DAW state lost user identity when the source preset was missing");
    project.setCurrentProgram (2);
    require (project.getLoadedPresetName() == project.getProgramName (2)
                 && project.getLoadedPresetId() == anyvoc::PresetLibrary::factoryId (project.getProgramName (2)),
             "Factory selection retained the previous user preset identity");

    // A user may rename or move a file containing stale/invalid metadata.
    // Explicit loading derives its identity from that file after sound validation.
    auto foreignIdentity = stateTree (source);
    foreignIdentity.setProperty ("presetName", "", nullptr);
    foreignIdentity.removeProperty ("presetId", nullptr);
    require (foreignIdentity.createXml()->writeTo (preset), "Could not write stale metadata preset fixture");
    require (project.loadPreset (preset) && project.getLoadedPresetName() == "Custom Orbit",
             "Explicit load did not replace unusable embedded identity with the actual file identity");
    expectParameters (project, expected);
}

void testPresetIdentityLegacyAndValidation()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (4);
    setParameter (source, "macro2", .71f);
    const auto expected = parameters (source);
    auto legacy = stateTree (source);
    legacy.removeProperty ("presetName", nullptr);
    legacy.removeProperty ("presetId", nullptr);
    AnyvocAudioProcessor restored;
    restored.setCurrentProgram (14);
    restore (restored, legacy);
    expectParameters (restored, expected);
    require (restored.getLoadedPresetName() == source.getProgramName (4)
                 && restored.getLoadedPresetId() == anyvoc::PresetLibrary::factoryId (source.getProgramName (4)),
             "Metadata-free schema 6 state did not recover its legacy factory identity");
    const auto original = save (restored);
    const auto valid = stateTree (restored);
    const auto rejected = [&] (const juce::ValueTree& state)
    {
        restore (restored, state);
        require (save (restored) == original, "Malformed preset identity partially changed live state");
    };
    for (const char* property : { "presetName", "presetId" })
    {
        auto missing = valid.createCopy();
        missing.removeProperty (property, nullptr);
        rejected (missing);
        for (const juce::String value : { juce::String(), juce::String ("   "),
                                         juce::String::repeatedString ("x", juce::String (property) == "presetName" ? 513 : 4097) })
        {
            auto invalid = valid.createCopy();
            invalid.setProperty (property, value, nullptr);
            rejected (invalid);
        }
    }
    auto corruptSound = valid.createCopy();
    corruptSound.setProperty ("presetName", "Should never publish", nullptr);
    corruptSound.setProperty ("presetId", "user:invalid", nullptr);
    corruptSound.removeChild (0, nullptr);
    rejected (corruptSound);
}

void testPresetIoFailureRollback()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (2);
    const auto destination = files.directory.getChildFile ("Existing Sound.anyvoc");
    require (processor.savePreset (destination), "Could not create existing preset for save rollback");
    juce::MemoryBlock originalFile;
    require (destination.loadFileAsData (originalFile), "Could not read existing preset fixture");
    processor.setCurrentProgram (14);
    setParameter (processor, "master", .31f);
    const auto baseline = save (processor);
    const auto unchanged = [&]
    {
        require (save (processor) == baseline, "Failed preset IO changed sound or loaded identity");
    };

   #if JUCE_WINDOWS
    {
        // JUCE opens an output handle with FILE_SHARE_READ only. Holding it
        // without writing prevents replacement while preserving existing bytes.
        const auto locked = destination.createOutputStream();
        require (locked != nullptr, "Could not lock the existing destination for atomic-save test");
        require (! processor.savePreset (destination), "Saving over an exclusively opened destination unexpectedly succeeded");
        unchanged();
    }
    juce::MemoryBlock afterFailure;
    require (destination.loadFileAsData (afterFailure) && afterFailure == originalFile,
             "Failed atomic preset save damaged the existing destination");
   #endif

    const auto directoryTarget = files.directory.getChildFile ("Directory.anyvoc");
    require (directoryTarget.createDirectory().wasOk(), "Could not create invalid save destination");
    const auto sentinel = directoryTarget.getChildFile ("Keep.txt");
    require (sentinel.replaceWithText ("keep this data"), "Could not create save-failure sentinel");
    require (! processor.savePreset (directoryTarget) && sentinel.loadFileAsString() == "keep this data",
             "Failed directory save replaced existing content");
    unchanged();
    const auto malformed = files.directory.getChildFile ("Malformed.anyvoc");
    require (malformed.replaceWithText ("<ANYVOC broken"), "Could not create malformed preset fixture");
    require (! processor.loadPreset (malformed), "Malformed preset file was accepted");
    unchanged();
    const auto empty = files.directory.getChildFile ("Empty.anyvoc");
    require (empty.replaceWithText ({}) && ! processor.loadPreset (empty), "Empty preset file was accepted");
    unchanged();
    require (! processor.loadPreset (files.directory.getChildFile ("Missing.anyvoc")), "Missing preset file was accepted");
    unchanged();
    require (! processor.loadPreset (directoryTarget), "Directory was accepted as a preset file");
    unchanged();
}

void testDeterministicFactoryPresets()
{
    AnyvocAudioProcessor processor;
    require (processor.getNumPrograms() > 1, "Factory programs were not exposed to the DAW");
    std::mt19937 random (0x41564f43);
    std::uniform_real_distribution<float> value (0.0f, 1.0f);
    for (int program = 0; program < processor.getNumPrograms(); ++program)
    {
        require (processor.getProgramName (program).isNotEmpty(), "Factory program has no name");
        processor.setCurrentProgram (program);
        const auto expected = parameters (processor);
        for (auto* parameter : processor.getParameters())
            parameter->setValueNotifyingHost (value (random));
        processor.setCurrentProgram (program);
        expectParameters (processor, expected);
        require (processor.getCurrentProgram() == program, "Program index did not follow preset selection");
    }
    processor.setCurrentProgram (0);
    for (auto* parameter : processor.getParameters())
        require (std::abs (parameter->getValue() - parameter->getDefaultValue()) < 2.0e-5f,
                 "Init preset did not reset a parameter to its defined default");
    const auto expected = parameters (processor);
    processor.setCurrentProgram (-1);
    processor.setCurrentProgram (processor.getNumPrograms());
    expectParameters (processor, expected);
}

void testFirstMidiAfterStateAndPreset()
{
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (0);
    audibleSettings (processor);
    processor.prepareToPlay (48000.0, 512);
    require (rms (render (processor, 6000,
                          { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } })) > .01,
             "First host note was discarded after selecting a preset");
    const auto data = save (processor);
    processor.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    require (rms (render (processor, 6000,
                          { { 0, juce::MidiMessage::noteOn (1, 72, .8f) } })) > .01,
             "First host note was discarded after DAW state restore");
}

void testEmbeddedWavetable()
{
    constexpr int length = 4096;
    juce::AudioBuffer<float> wave (1, length);
    for (int sample = 0; sample < length; ++sample)
    {
        const auto phase = (sample % 2048) * juce::MathConstants<double>::twoPi / 2048;
        wave.setSample (0, sample, static_cast<float> (.7 * std::sin (phase)
                            + (sample >= 2048 ? .25 * std::sin (phase * 5.0) : 0.0)));
    }
    juce::TemporaryFile wavFile (".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = wavFile.getFile().createOutputStream();
        auto writer = format.createWriterFor (stream,
            juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
        require (writer != nullptr && writer->writeFromAudioSampleBuffer (wave, 0, length),
                 "Could not create WAV import fixture");
    }
    AnyvocAudioProcessor source;
    for (int oscillator = 0; oscillator < 3; ++oscillator)
        require (source.importWavetable (oscillator, wavFile.getFile()), "WAV import failed");
    const auto name = source.getWavetableName (0);
    const auto data = save (source);
    require (wavFile.getFile().deleteFile(), "Could not remove original WAV before embedded restore");
    AnyvocAudioProcessor target;
    target.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    for (int oscillator = 0; oscillator < 3; ++oscillator)
    {
        require (target.getWavetableName (oscillator) == name,
                 "Imported WAV name was not retained without its source file");
        for (int point = 0; point < 64; ++point)
            for (const float position : { 0.0f, .5f, 1.0f })
                require (std::abs (target.getWavetableSample (oscillator, point / 64.0f, position)
                                - source.getWavetableSample (oscillator, point / 64.0f, position)) < 2.0e-4f,
                         "Embedded wavetable samples changed during state restore");
    }
    target.prepareToPlay (48000, 512);
    audibleSettings (target);
    require (rms (render (target, 6000, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } })) > .01,
             "Restored embedded wavetable did not sound");
    juce::TemporaryFile presetFile (".anyvoc");
    target.savePreset (presetFile.getFile());
    AnyvocAudioProcessor fromFile;
    require (fromFile.loadPreset (presetFile.getFile()), "Saved preset file could not be reloaded");
    expectParameters (fromFile, parameters (target));
    require (fromFile.getWavetableName (2) == name, "Preset file lost embedded WAV assets");
    fromFile.setCurrentProgram (0);
    require (fromFile.getWavetableName (0) == "Basic Shapes", "Factory preset did not clear imported assets");
}

void expectTableAudio (const juce::AudioBuffer<float>& actual,
                       const juce::AudioBuffer<float>& expected, double tolerance = 2.0e-5)
{
    require (actual.getNumChannels() == expected.getNumChannels()
                 && actual.getNumSamples() == expected.getNumSamples(), "Editable wavetable dimensions changed");
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int sample = 0; sample < actual.getNumSamples(); ++sample)
            require (std::isfinite (actual.getSample (channel, sample))
                         && std::abs (actual.getSample (channel, sample)
                                     - expected.getSample (channel, sample)) < tolerance,
                     "Editable wavetable changed at sample " + std::to_string (sample));
}

juce::AudioBuffer<float> editedTableFixture()
{
    juce::AudioBuffer<float> audio (1, 4096);
    for (int sample = 0; sample < 2048; ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi * sample / 2048;
        audio.setSample (0, sample, static_cast<float> (.2 * std::sin (angle)));
        audio.setSample (0, 2048 + sample, static_cast<float> (1.6 * std::sin (angle)
                                                            + .3 * std::sin (3 * angle)));
    }
    return audio;
}

std::array<std::uint64_t, 3> wavetableRevisions (const AnyvocAudioProcessor& processor)
{
    return { processor.getWavetableRevision (0), processor.getWavetableRevision (1),
             processor.getWavetableRevision (2) };
}

void testEditedWavetableStateAndExport()
{
    const auto audio = editedTableFixture();
    AnyvocAudioProcessor source;
    for (int oscillator = 0; oscillator < 3; ++oscillator)
    {
        const auto revisions = wavetableRevisions (source);
        require (source.applyEditedWavetable (oscillator, audio, "Drawn / Quiet + Additive"),
                 "Applying a canonical edited wavetable failed");
        for (int slot = 0; slot < 3; ++slot)
            require (slot == oscillator ? source.getWavetableRevision (slot) > revisions[static_cast<size_t> (slot)]
                                        : source.getWavetableRevision (slot) == revisions[static_cast<size_t> (slot)],
                     "Applying a wavetable did not publish only the edited oscillator revision");
        expectTableAudio (source.getEditableWavetable (oscillator), audio);
        require (std::abs (source.getWavetableSample (oscillator, .25f, 0) - .2f) < 2.0e-5f,
                 "Edited low-amplitude frame was implicitly normalized");
        require (std::abs (source.getWavetableSample (oscillator, .25f, 1) - 1.3f) < 2.0e-5f,
                 "Edited additive frame was clipped or normalized");
    }
    auto tree = stateTree (source);
    require (static_cast<int> (tree.getProperty ("schemaVersion")) == 14,
             "Edited table state did not use schema version 14");
    int tables = 0;
    for (int child = 0; child < tree.getNumChildren(); ++child)
        if (const auto table = tree.getChild (child); table.hasType ("WAVETABLE"))
        {
            ++tables;
            require (table.hasProperty ("normalise") && ! static_cast<bool> (table.getProperty ("normalise")),
                     "Edited table state omitted its amplitude-preserving flag");
        }
    require (tables == 3, "Edited state did not contain all three tables");
    const auto data = save (source);
    AnyvocAudioProcessor restored;
    const auto beforeRestore = wavetableRevisions (restored);
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    for (int oscillator = 0; oscillator < 3; ++oscillator)
    {
        require (restored.getWavetableRevision (oscillator) > beforeRestore[static_cast<size_t> (oscillator)],
                 "Restoring DAW state did not publish a new wavetable revision");
        expectTableAudio (restored.getEditableWavetable (oscillator), audio);
        require (restored.getWavetableName (oscillator) == source.getWavetableName (oscillator),
                 "Edited table name changed in DAW state");
    }
    const auto beforeSameNameRestore = wavetableRevisions (restored);
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    for (int oscillator = 0; oscillator < 3; ++oscillator)
        require (restored.getWavetableRevision (oscillator) > beforeSameNameRestore[static_cast<size_t> (oscillator)]
                     && restored.getWavetableName (oscillator) == source.getWavetableName (oscillator),
                 "Restoring a table with the same name did not invalidate stale editor data");
    juce::TemporaryFile presetFile (".anyvoc");
    require (source.savePreset (presetFile.getFile()), "Edited table preset could not be saved");
    AnyvocAudioProcessor fromPreset;
    require (fromPreset.loadPreset (presetFile.getFile()), "Edited table preset could not be loaded");
    expectTableAudio (fromPreset.getEditableWavetable (2), audio);

    juce::TemporaryFile waveFile (".wav");
    require (restored.exportWavetable (1, waveFile.getFile()), "Edited wavetable export failed");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (waveFile.getFile()));
    require (reader != nullptr && reader->numChannels == 1 && reader->lengthInSamples == 4096
                 && reader->usesFloatingPointData, "Export was not a canonical floating-point mono WAV");
    juce::AudioBuffer<float> decoded (1, 4096);
    require (reader->read (&decoded, 0, 4096, 0, true, false), "Exported WAV could not be decoded");
    expectTableAudio (decoded, audio);
    reader.reset();
    AnyvocAudioProcessor imported;
    const auto beforeImport = wavetableRevisions (imported);
    require (imported.importWavetable (0, waveFile.getFile()), "Exported wavetable could not be reimported");
    require (imported.getWavetableRevision (0) > beforeImport[0]
                 && imported.getWavetableRevision (1) == beforeImport[1]
                 && imported.getWavetableRevision (2) == beforeImport[2],
             "Import did not publish only the imported oscillator revision");
    expectTableAudio (imported.getEditableWavetable (0), audio);
    require (std::abs (imported.getWavetableSample (0, .25f, 0) - .2f) < 2.0e-5f
                 && std::abs (imported.getWavetableSample (0, .25f, 1) - 1.3f) < 2.0e-5f,
             "Export/reimport lost the edited amplitude metadata");

    // A generic float WAV has no Anyvoc metadata and keeps the legacy import
    // normalization. Its original peaks must survive embedding before restore.
    juce::TemporaryFile genericFile (".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = genericFile.getFile().createOutputStream();
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (44100).withNumChannels (1).withBitsPerSample (32)
            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        require (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()),
                 "Could not create a generic floating-point WAV fixture");
    }
    AnyvocAudioProcessor generic;
    require (generic.importWavetable (0, genericFile.getFile()), "Generic float WAV import failed");
    require (std::abs (generic.getWavetableSample (0, .25f, 0) - .95f) < 2.0e-5f,
             "Generic WAV import no longer applies legacy peak normalization");
    const auto normalized = generic.getEditableWavetable (0);
    const auto genericState = save (generic);
    AnyvocAudioProcessor genericRestored;
    genericRestored.setStateInformation (genericState.getData(), static_cast<int> (genericState.getSize()));
    expectTableAudio (genericRestored.getEditableWavetable (0), normalized);
    restored.prepareToPlay (48000, 512);
    audibleSettings (restored);
    require (rms (render (restored, 6000, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } })) > .005,
             "Restored edited wavetable did not produce bounded audible output");
    const auto beforeProgram = wavetableRevisions (source);
    source.setCurrentProgram (0);
    for (int oscillator = 0; oscillator < 3; ++oscillator)
        require (source.getWavetableRevision (oscillator) > beforeProgram[static_cast<size_t> (oscillator)],
                 "Selecting a factory program did not invalidate the wavetable editor");
}

void testEditedWavetableRejection()
{
    AnyvocAudioProcessor processor;
    auto valid = editedTableFixture();
    require (processor.applyEditedWavetable (0, valid, "Keep Me"), "Edited rejection fixture failed");
    const auto before = save (processor);
    const auto revisions = wavetableRevisions (processor);
    const auto rejected = [&] (int oscillator, const juce::AudioBuffer<float>& audio)
    {
        require (! processor.applyEditedWavetable (oscillator, audio, "Invalid replacement"),
                 "Invalid edited wavetable was accepted");
        require (save (processor) == before, "Rejected edit changed the saved instrument state");
        require (wavetableRevisions (processor) == revisions, "Rejected edit published a wavetable revision");
    };
    rejected (-1, valid);
    rejected (3, valid);
    rejected (0, juce::AudioBuffer<float>());
    juce::AudioBuffer<float> stereo (2, 2048), partial (1, 2049), oversized (1, 33 * 2048);
    stereo.clear(); partial.clear(); oversized.clear();
    rejected (0, stereo); rejected (0, partial); rejected (0, oversized);
    valid.setSample (0, 17, std::numeric_limits<float>::infinity());
    rejected (0, valid);
    valid.setSample (0, 17, std::numeric_limits<float>::quiet_NaN());
    rejected (0, valid);
    valid.setSample (0, 17, 129.0f);
    rejected (0, valid);
    auto badFlag = stateTree (processor);
    badFlag.getChildWithName ("WAVETABLE").setProperty ("normalise", "invalid", nullptr);
    restore (processor, badFlag);
    require (save (processor) == before, "An invalid normalization flag changed the instrument state");
    require (wavetableRevisions (processor) == revisions, "Rejected DAW state published a wavetable revision");
    juce::TemporaryFile invalidExport (".wav");
    require (invalidExport.getFile().replaceWithText ("Existing file must survive"),
             "Could not create the export rollback fixture");
    require (! processor.exportWavetable (-1, invalidExport.getFile())
                 && ! processor.exportWavetable (3, invalidExport.getFile()),
             "An invalid oscillator slot was exported");
    require (invalidExport.getFile().loadFileAsString() == "Existing file must survive",
             "A rejected export changed its existing destination file");
    expectTableAudio (processor.getEditableWavetable (0), editedTableFixture());
}

void writeSampleFixture (const juce::File& file, double sampleRate = 24000.0,
                         double seconds = .4, int channels = 2)
{
    juce::AudioBuffer<float> audio (channels, static_cast<int> (std::round (seconds * sampleRate)));
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample (channel, sample, static_cast<float> (.7 * std::sin (
                juce::MathConstants<double>::twoPi * 440.0 * (channel + 1) * sample / sampleRate)));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor (stream,
        juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (channels).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()),
             "Could not create sample WAV fixture");
}

void testEmbeddedSampleState()
{
    juce::TemporaryFile sourceFile (".wav");
    writeSampleFixture (sourceFile.getFile());
    AnyvocAudioProcessor source;
    for (int oscillator = 0; oscillator < 3; ++oscillator)
        require (source.importSample (oscillator, sourceFile.getFile()), "Sample WAV import failed");
    audibleSettings (source);
    setParameter (source, "osc1Mode", 1);
    setParameter (source, "osc1Root", 60);
    setParameter (source, "osc1Position", 0);
    setParameter (source, "osc2Mode", 2);
    setParameter (source, "osc2GrainSize", .12f);
    setParameter (source, "osc2GrainDensity", 24);
    setParameter (source, "osc2GrainSpray", .37f);
    setParameter (source, "osc3Loop", 1);
    setParameter (source, "osc3Reverse", 1);
    const auto expected = parameters (source);
    const auto sampleName = source.getSampleName (0);
    const auto data = save (source);
    const auto tree = stateTree (source);
    require (static_cast<int> (tree.getProperty ("schemaVersion")) == 14,
             "Current state was not stored with schema version 14");
    int samples = 0;
    for (int child = 0; child < tree.getNumChildren(); ++child)
        if (tree.getChild (child).hasType ("SAMPLE"))
            ++samples;
    require (samples == 3, "State did not embed all imported sample slots");
    require (sourceFile.getFile().deleteFile(), "Could not remove sample fixture source file");
    AnyvocAudioProcessor restored;
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    expectParameters (restored, expected);
    for (int oscillator = 0; oscillator < 3; ++oscillator)
    {
        require (restored.getSampleName (oscillator) == sampleName,
                 "Embedded sample name was not retained");
        for (int point = 0; point <= 64; ++point)
        {
            const auto original = source.getSamplePreview (oscillator, point / 64.0f);
            const auto result = restored.getSamplePreview (oscillator, point / 64.0f);
            require (std::isfinite (result) && std::abs (result - original) < 5.0e-4f,
                     "Embedded sample waveform changed after the original file was removed");
        }
    }
    restored.prepareToPlay (48000.0, 512);
    require (rms (render (restored, 6000,
                          { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } })) > .01,
             "Sample state restored without an audible instrument");
    juce::TemporaryFile presetFile (".anyvoc");
    require (restored.savePreset (presetFile.getFile()), "Sample preset file could not be saved");
    AnyvocAudioProcessor fromFile;
    require (fromFile.loadPreset (presetFile.getFile()), "Sample preset file could not be loaded");
    expectParameters (fromFile, expected);
    require (fromFile.getSampleName (2) == sampleName, "Preset file lost its embedded sample");
}

bool isSampleParameter (const juce::String& id)
{
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
        for (const char* suffix : { "Mode", "Root", "Loop", "Reverse", "GrainSize", "GrainDensity", "GrainSpray" })
            if (id == "osc" + juce::String (oscillator) + suffix)
                return true;
    return false;
}

bool isDrawableLfoParameter (const juce::String& id)
{
    if (id == "tempo") return true;
    for (int lfo = 1; lfo <= 2; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo);
        for (const char* suffix : { "Sync", "Division", "Mode", "Interpolation", "Kind" })
            if (id == prefix + suffix) return true;
        for (int point = 0; point <= 32; ++point)
            if (id == prefix + "Point" + juce::String (point)) return true;
    }
    return false;
}

bool isDualFilterParameter (const juce::String& id)
{
    for (const char* name : { "filterEnabled", "filter2Enabled", "filter2Type", "filter2Cutoff",
                             "filter2Resonance", "filter2Env", "filterRouting", "filterBalance",
                             "filterSlope", "filter2Slope", "filterKeyTrack", "filter2KeyTrack",
                             "osc1FilterRoute", "osc2FilterRoute", "osc3FilterRoute", "subFilterRoute", "noiseFilterRoute" })
        if (id == name) return true;
    for (int route = 1; route <= 8; ++route)
        if (id == "mod" + juce::String (route) + "DestBank"
            || id == "mod" + juce::String (route) + "FilterTarget") return true;
    return false;
}

bool isWarpParameter (const juce::String& id)
{
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
        for (int slot = 1; slot <= 2; ++slot)
        {
            const auto prefix = "osc" + juce::String (oscillator) + "Warp" + juce::String (slot);
            if (id == prefix + "Mode" || id == prefix + "Amount") return true;
        }
    for (int route = 1; route <= 8; ++route)
        if (id == "mod" + juce::String (route) + "WarpEnabled"
            || id == "mod" + juce::String (route) + "WarpTarget") return true;
    return false;
}

bool isCrossModulationParameter (const juce::String& id)
{
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
        for (int slot = 1; slot <= 2; ++slot)
        {
            const auto prefix = "osc" + juce::String (oscillator) + "Warp" + juce::String (slot);
            if (id == prefix + "CrossMode" || id == prefix + "CrossSource") return true;
        }
    return false;
}

bool isVoicingParameter (const juce::String& id)
{
    if (id == "voiceMode" || id == "glideTime" || id == "glideAlways") return true;
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
        for (const char* suffix : { "Phase", "PhaseRandom", "RandomUnison" })
            if (id == "osc" + juce::String (oscillator) + suffix) return true;
    return false;
}

constexpr int advancedMatrixParameterCount = 24 * 7 + 32 * 6;
constexpr int expandedSourceParameterCount = 8 * 40 + 2 * 4 + 4 + 32 * 2;
constexpr int advancedEnvelopeParameterCount = 4 * 12;
constexpr int chaosParameterCount = 10;
constexpr int macroDestinationParameterCount = 32 * 2;

bool isMacroDestinationParameter (const juce::String& id)
{
    for (int route = 1; route <= 32; ++route)
        if (id == "mod" + juce::String (route) + "MacroEnabled"
            || id == "mod" + juce::String (route) + "MacroTarget") return true;
    return false;
}

bool isChaosParameter (const juce::String& id)
{
    for (int lfo = 1; lfo <= 10; ++lfo) if (id == "lfo" + juce::String (lfo) + "Chaos") return true;
    return false;
}

constexpr int lfoMotionParameterCount = 10 * 5 + 2 + 32 * 2;
const std::array<const char*, 5> lfoMotionFields { "Phase", "Direction", "FollowSwing", "AudioRateEnabled", "AudioRateHz" };

bool isLfoMotionParameter (const juce::String& id)
{
    if (id == "swingAmount" || id == "swingDivision") return true;
    for (int lfo = 1; lfo <= 10; ++lfo) for (const auto* suffix : lfoMotionFields)
        if (id == "lfo" + juce::String (lfo) + suffix) return true;
    for (int route = 1; route <= 32; ++route)
        if (id == "mod" + juce::String (route) + "LfoPhaseEnabled"
            || id == "mod" + juce::String (route) + "LfoPhaseTarget") return true;
    return false;
}
const std::array<const char*, 4> envelopePrefixes { "amp", "env", "env3", "env4" };
const std::array<const char*, 12> advancedEnvelopeFields {
    "Delay", "Hold", "AttackCurve", "DecayCurve", "ReleaseCurve", "TempoSync",
    "DelayDivision", "AttackDivision", "HoldDivision", "DecayDivision", "ReleaseDivision", "RetriggerLegato" };

bool isAdvancedEnvelopeParameter (const juce::String& id)
{
    for (const auto* prefix : envelopePrefixes)
        for (const auto* suffix : advancedEnvelopeFields)
            if (id == juce::String (prefix) + suffix) return true;
    return false;
}

bool isExpandedSourceParameter (const juce::String& id)
{
    for (int lfo = 3; lfo <= 10; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo);
        for (const auto* suffix : { "Rate", "Shape", "Kind", "Sync", "Division", "Mode", "Interpolation" })
            if (id == prefix + suffix) return true;
        for (int point = 0; point <= 32; ++point)
            if (id == prefix + "Point" + juce::String (point)) return true;
    }
    for (int env = 3; env <= 4; ++env)
        for (const auto* suffix : { "Attack", "Decay", "Sustain", "Release" })
            if (id == "env" + juce::String (env) + suffix) return true;
    for (int macro = 5; macro <= 8; ++macro) if (id == "macro" + juce::String (macro)) return true;
    for (int route = 1; route <= 32; ++route)
        if (id == "mod" + juce::String (route) + "SourceExtra"
            || id == "mod" + juce::String (route) + "AuxSourceExtra") return true;
    return false;
}

bool isAdvancedMatrixParameter (const juce::String& id)
{
    for (int route = 1; route <= 32; ++route)
    {
        const auto prefix = "mod" + juce::String (route);
        for (const auto* suffix : { "Enabled", "Polarity", "Curve", "AuxSource", "AuxPolarity", "AuxCurve" })
            if (id == prefix + suffix) return true;
        if (route > 8)
            for (const auto* suffix : { "Source", "Dest", "Amount", "DestBank", "FilterTarget", "WarpEnabled", "WarpTarget" })
                if (id == prefix + suffix) return true;
    }
    return false;
}

void testVoicingStateAndPresetDefaults()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (15);
    require (source.getProgramName (15) == "Lead / Ribbon Glide"
                 && *source.apvts.getRawParameterValue ("voiceMode") == 2
                 && *source.apvts.getRawParameterValue ("glideTime") > .1f
                 && *source.apvts.getRawParameterValue ("glideAlways") == 1,
             "Ribbon Glide did not expose the intended playable voicing defaults");
    setParameter (source, "glideTime", .43f);
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
    {
        const auto prefix = "osc" + juce::String (oscillator);
        setParameter (source, (prefix + "Phase").toRawUTF8(), .13f * oscillator);
        setParameter (source, (prefix + "PhaseRandom").toRawUTF8(), .21f * oscillator);
        setParameter (source, (prefix + "RandomUnison").toRawUTF8(), 0);
    }
    const auto expected = parameters (source);
    const auto state = save (source);
    AnyvocAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    expectParameters (restored, expected);
    require (static_cast<int> (stateTree (restored).getProperty ("schemaVersion")) == 14,
             "Voicing state did not use the current schema 14");
    juce::TemporaryFile file (".anyvoc");
    require (source.savePreset (file.getFile()) && restored.loadPreset (file.getFile()),
             "Voicing user preset did not save/load");
    expectParameters (restored, expected);
    restored.setCurrentProgram (0);
    int count = 0;
    for (auto* base : restored.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base); parameter != nullptr
            && isVoicingParameter (parameter->getParameterID()))
        {
            ++count;
            require (parameter->getValue() == parameter->getDefaultValue(), "Init retained modified phase/glide parameters");
        }
    require (count == 12, "Voicing parameter set did not contain exactly its twelve appended controls");
    const juce::StringArray order { "voiceMode", "glideTime", "glideAlways", "osc1Phase", "osc1PhaseRandom",
        "osc1RandomUnison", "osc2Phase", "osc2PhaseRandom", "osc2RandomUnison", "osc3Phase", "osc3PhaseRandom", "osc3RandomUnison" };
    const auto& all = restored.getParameters();
    for (int index = 0; index < order.size(); ++index)
    {
        const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (
            all[all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount - advancedEnvelopeParameterCount - expandedSourceParameterCount - advancedMatrixParameterCount - 12 - order.size() + index]);
        require (parameter != nullptr && parameter->getParameterID() == order[index],
                 "New voicing parameters were inserted into the old host automation order");
    }
}

void testVersionSixVoicingMigration()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (2);
    setParameter (source, "master", .39f);
    const auto expected = parameters (source);
    const auto current = stateTree (source);
    for (const int version : { 1, 2, 3, 4, 5, 6 })
    {
        auto legacy = current.createCopy(); legacy.setProperty ("schemaVersion", version, nullptr);
        for (int child = legacy.getNumChildren(); --child >= 0;)
        {
            const auto id = legacy.getChild (child).getProperty ("id").toString();
            if (isCrossModulationParameter (id) || isVoicingParameter (id) || (version < 6 && isWarpParameter (id))
                || (version < 5 && isDualFilterParameter (id)) || (version < 3 && isDrawableLfoParameter (id))
                || (version < 2 && isSampleParameter (id))) legacy.removeChild (child, nullptr);
        }
        AnyvocAudioProcessor restored;
        restored.setCurrentProgram (15);
        for (auto* base : restored.getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
                parameter != nullptr && isVoicingParameter (parameter->getParameterID()))
                parameter->setValueNotifyingHost (1.0f - parameter->getDefaultValue());
        restore (restored, legacy);
        expectParameters (restored, expected);
        const auto accepted = save (restored);
        auto incomplete = legacy.createCopy();
        incomplete.removeChild (incomplete.getChildWithProperty ("id", "master"), nullptr);
        restore (restored, incomplete);
        require (save (restored) == accepted, "Voicing migration accepted a missing preexisting parameter");
    }
    AnyvocAudioProcessor live;
    live.setCurrentProgram (15);
    const auto before = save (live);
    for (const char* id : { "voiceMode", "glideTime", "osc1Phase", "osc3RandomUnison" })
    {
        auto incomplete = current.createCopy();
        incomplete.removeChild (incomplete.getChildWithProperty ("id", id), nullptr);
        restore (live, incomplete);
        require (save (live) == before, "Current schema accepted a missing voicing parameter");
    }
}

void testVoicingParametersReachHostAudio()
{
    const auto setup = [] (AnyvocAudioProcessor& processor, int mode, float glide)
    {
        processor.setCurrentProgram (0); audibleSettings (processor);
        for (const char* id : { "osc2Level", "osc3Level", "subLevel", "noiseLevel", "filterEnabled", "filter2Enabled" })
            setParameter (processor, id, 0);
        setParameter (processor, "ampSustain", 1);
        setParameter (processor, "voiceMode", static_cast<float> (mode));
        setParameter (processor, "glideTime", glide);
        processor.prepareToPlay (48000, 512);
    };
    const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 69, .8f) },
                                      { 12000, juce::MidiMessage::noteOn (1, 81, .8f) } };
    const auto spectral = [] (const juce::AudioBuffer<float>& audio, double frequency)
    {
        double real = 0, imaginary = 0;
        for (int sample = 22000; sample < 30000; ++sample)
        {
            const double angle = juce::MathConstants<double>::twoPi * frequency * sample / 48000;
            real += audio.getSample (0, sample) * std::cos (angle);
            imaginary += audio.getSample (0, sample) * std::sin (angle);
        }
        return std::hypot (real, imaginary);
    };
    AnyvocAudioProcessor poly, mono, glide;
    setup (poly, 0, 0); setup (mono, 1, 0); setup (glide, 2, .2f);
    const auto polyAudio = render (poly, 32000, notes, 127);
    const auto monoAudio = render (mono, 32000, notes, 127);
    const auto glideAudio = render (glide, 32000, notes, 127);
    require (spectral (polyAudio, 440) > spectral (polyAudio, 880) * .3
                 && spectral (monoAudio, 440) < spectral (monoAudio, 880) * .05,
             "Host voice-mode parameters did not change Poly stacking to Mono last-note audio");
    std::vector<double> crossings;
    for (int sample = 16416; sample < 17184; ++sample)
    {
        const double a = glideAudio.getSample (0, sample - 1), b = glideAudio.getSample (0, sample);
        if (a <= 0 && b > 0) crossings.push_back (sample - 1.0 - a / (b - a));
    }
    require (crossings.size() > 4, "Host glide output did not contain measurable sine cycles");
    const double pitch = 48000 * (crossings.size() - 1) / (crossings.back() - crossings.front());
    require (std::abs (pitch - 440 * std::sqrt (2.0)) < 12,
             "Host glide-time parameter did not reach the expected semitone midpoint in rendered audio");
}

void testLegacyWavetableNormalization()
{
    AnyvocAudioProcessor source;
    juce::AudioBuffer<float> quiet (1, 2048);
    for (int sample = 0; sample < 2048; ++sample)
        quiet.setSample (0, sample, static_cast<float> (.2 * std::sin (
            juce::MathConstants<double>::twoPi * sample / 2048)));
    require (source.applyEditedWavetable (0, quiet, "Legacy Quiet"), "Legacy amplitude fixture failed");
    const auto current = stateTree (source);
    for (const int version : { 1, 2, 3, 4, 5, 6, 7 })
    {
        auto legacy = current.createCopy();
        legacy.setProperty ("schemaVersion", version, nullptr);
        for (int child = legacy.getNumChildren(); --child >= 0;)
        {
            auto item = legacy.getChild (child);
            const auto id = item.getProperty ("id").toString();
            if (isCrossModulationParameter (id) || (version < 3 && isDrawableLfoParameter (id)) || (version < 2 && isSampleParameter (id))
                || (version < 5 && isDualFilterParameter (id)) || (version < 6 && isWarpParameter (id))
                || (version < 7 && isVoicingParameter (id)))
                legacy.removeChild (child, nullptr);
            else if (item.hasType ("WAVETABLE"))
                item.removeProperty ("normalise", nullptr);
        }
        AnyvocAudioProcessor restored;
        restore (restored, legacy);
        require (restored.getWavetableName (0) == source.getWavetableName (0),
                 "Legacy table was rejected by state migration for version " + std::to_string (version));
        require (std::abs (restored.getWavetableSample (0, .25f, 0) - .95f) < 2.0e-5f,
                 "A missing normalization flag did not retain legacy behavior for version " + std::to_string (version));
        expectParameters (restored, parameters (source));
    }
}

void testLegacyStateMigration()
{
    AnyvocAudioProcessor legacySource;
    legacySource.setCurrentProgram (4);
    setParameter (legacySource, "master", .37f);
    const auto expected = parameters (legacySource);
    auto legacy = stateTree (legacySource);
    legacy.setProperty ("schemaVersion", 1, nullptr);
    for (int child = legacy.getNumChildren(); --child >= 0;)
        if (const auto id = legacy.getChild (child).getProperty ("id").toString();
            isSampleParameter (id) || isDrawableLfoParameter (id) || isDualFilterParameter (id) || isWarpParameter (id))
            legacy.removeChild (child, nullptr);
    AnyvocAudioProcessor target;
    target.setCurrentProgram (2);
    for (auto* base : target.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isSampleParameter (parameter->getParameterID()) || isDrawableLfoParameter (parameter->getParameterID())
                || isDualFilterParameter (parameter->getParameterID()) || isWarpParameter (parameter->getParameterID()))
                parameter->setValueNotifyingHost (1.0f);
    restore (target, legacy);
    expectParameters (target, expected);
    require (target.getCurrentProgram() == 4, "Version 1 migration lost the program selection");
    for (auto* base : target.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isSampleParameter (parameter->getParameterID()) || isDrawableLfoParameter (parameter->getParameterID())
                || isDualFilterParameter (parameter->getParameterID()) || isWarpParameter (parameter->getParameterID()))
                require (std::abs (parameter->getValue() - parameter->getDefaultValue()) < 2.0e-5f,
                         "Version 1 state inherited a stale parameter added by a later version");
    const auto migrated = save (target);
    auto incompleteLegacy = legacy.createCopy();
    incompleteLegacy.removeChild (incompleteLegacy.getChildWithProperty ("id", "master"), nullptr);
    restore (target, incompleteLegacy);
    require (save (target) == migrated, "Version 1 migration accepted a missing original parameter");
    auto incompleteCurrent = stateTree (target);
    incompleteCurrent.removeChild (incompleteCurrent.getChildWithProperty ("id", "osc1GrainSize"), nullptr);
    restore (target, incompleteCurrent);
    require (save (target) == migrated, "Current state accepted a missing sample parameter");
}

void testDrawableLfoStateRoundtrip()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (0);
    source.setLfoShape (0, 5);
    source.setLfoShape (1, 4);
    setParameter (source, "tempo", 93);
    setParameter (source, "lfo1Sync", 1);
    setParameter (source, "lfo1Division", 8);
    setParameter (source, "lfo1Mode", 2);
    setParameter (source, "lfo1Interpolation", 1);
    for (int point = 0; point <= 32; ++point)
    {
        const auto id = "lfo1Point" + juce::String (point);
        setParameter (source, id.toRawUTF8(), -1.0f + point / 16.0f);
    }
    require (source.assignModulation (1, 7, .65f), "Custom LFO could not be assigned to an audio destination");
    const auto expected = parameters (source);
    const auto data = save (source);
    AnyvocAudioProcessor restored;
    restored.setCurrentProgram (2);
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    expectParameters (restored, expected);
    require (restored.getLfoShape (0) == 5 && restored.getLfoShape (1) == 4,
             "Drawable/random shape selection did not survive DAW state restore");
    juce::TemporaryFile preset (".anyvoc");
    require (source.savePreset (preset.getFile()), "Drawable LFO preset could not be saved");
    require (restored.loadPreset (preset.getFile()), "Drawable LFO preset could not be reloaded");
    expectParameters (restored, expected);
    const auto before = save (restored);
    auto malformed = stateTree (restored);
    malformed.getChildWithProperty ("id", "lfo1Point17").setProperty ("value", "Infinity", nullptr);
    restore (restored, malformed);
    require (save (restored) == before, "Non-finite LFO point corrupted the current sound");
}

void testVersionTwoMigrationAndShapeAutomation()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (4);
    auto* shape = source.apvts.getParameter ("lfo1Shape");
    require (shape != nullptr && shape->getNumSteps() == 4,
             "Legacy LFO Shape automation no longer has four choices");
    require (std::abs (shape->convertFrom0to1 (1.0f / 3.0f) - 1.0f) < .0001f
                 && std::abs (shape->convertFrom0to1 (2.0f / 3.0f) - 2.0f) < .0001f,
             "Legacy normalized shape automation changed its Triangle/Saw mapping");
    shape->setValueNotifyingHost (2.0f / 3.0f);
    require (source.getLfoShape (0) == 2, "Legacy Saw selection no longer selects the same shape");
    const auto expected = parameters (source);
    auto legacy = stateTree (source);
    legacy.setProperty ("schemaVersion", 2, nullptr);
    for (int child = legacy.getNumChildren(); --child >= 0;)
        if (const auto id = legacy.getChild (child).getProperty ("id").toString();
            isDrawableLfoParameter (id) || isDualFilterParameter (id) || isWarpParameter (id))
            legacy.removeChild (child, nullptr);
    AnyvocAudioProcessor target;
    for (auto* base : target.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isDrawableLfoParameter (parameter->getParameterID()) || isDualFilterParameter (parameter->getParameterID())
                || isWarpParameter (parameter->getParameterID()))
                parameter->setValueNotifyingHost (1.0f);
    restore (target, legacy);
    expectParameters (target, expected);
    require (target.getLfoShape (0) == 2, "Version 2 state did not preserve its original LFO shape");
    const auto migrated = save (target);
    auto incomplete = legacy.createCopy();
    incomplete.removeChild (incomplete.getChildWithProperty ("id", "osc1GrainSize"), nullptr);
    restore (target, incomplete);
    require (save (target) == migrated, "Version 2 migration accepted a missing sample-era parameter");
    auto incompleteCurrent = stateTree (target);
    incompleteCurrent.removeChild (incompleteCurrent.getChildWithProperty ("id", "lfo1Point17"), nullptr);
    restore (target, incompleteCurrent);
    require (save (target) == migrated, "Version 3 state accepted a missing drawable LFO point");
    for (int selected = 0; selected < 6; ++selected)
    {
        target.setLfoShape (0, selected);
        require (target.getLfoShape (0) == selected, "Effective shape selection did not reach the requested mode");
    }
}

void testDualFilterStateAndAudio()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (13);
    setParameter (source, "filterEnabled", 0);
    setParameter (source, "filter2Enabled", 1);
    setParameter (source, "filter2Type", 2);
    setParameter (source, "filter2Cutoff", 1730);
    setParameter (source, "filter2Resonance", .63f);
    setParameter (source, "filter2Env", -.42f);
    setParameter (source, "filterRouting", 2);
    setParameter (source, "filterBalance", .73f);
    setParameter (source, "filterSlope", 1);
    setParameter (source, "filter2Slope", 1);
    setParameter (source, "filterKeyTrack", .35f);
    setParameter (source, "filter2KeyTrack", .81f);
    const std::array<const char*, 5> routeIds {
        "osc1FilterRoute", "osc2FilterRoute", "osc3FilterRoute", "subFilterRoute", "noiseFilterRoute" };
    for (int route = 0; route < 5; ++route)
        setParameter (source, routeIds[static_cast<size_t> (route)], static_cast<float> (route % 4));
    source.setModulationDestination (0, 8);
    source.setModulationDestination (1, 11);
    const auto expected = parameters (source);
    const auto state = save (source);
    AnyvocAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    expectParameters (restored, expected);
    require (restored.getModulationDestination (0) == 8 && restored.getModulationDestination (1) == 11,
             "Extended filter modulation targets changed during DAW state restore");
    juce::TemporaryFile preset (".anyvoc");
    require (source.savePreset (preset.getFile()) && restored.loadPreset (preset.getFile()),
             "Dual-filter preset could not be saved and reloaded");
    expectParameters (restored, expected);
    restored.setCurrentProgram (0);
    for (auto* base : restored.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isDualFilterParameter (parameter->getParameterID()))
                require (std::abs (parameter->getValue() - parameter->getDefaultValue()) < 2.0e-5f,
                         "Init did not reset a dual-filter parameter to its default");

    audibleSettings (restored);
    setParameter (restored, "subLevel", 0);
    setParameter (restored, "filterEnabled", 0);
    setParameter (restored, "filter2Enabled", 1);
    setParameter (restored, "filter2Cutoff", 100);
    restored.prepareToPlay (48000, 512);
    render (restored, 4096, { { 0, juce::MidiMessage::noteOn (1, 69, .8f) } });
    const auto closed = rms (render (restored, 4096));
    setParameter (restored, "filter2Cutoff", 16000);
    render (restored, 4096);
    const auto open = rms (render (restored, 4096));
    require (open > .005 && open > closed * 5,
             "Host automation of Filter B cutoff did not reach the audio engine");
    setParameter (restored, "filter2Cutoff", 100);
    setParameter (restored, "osc1FilterRoute", 3);
    render (restored, 4096);
    require (rms (render (restored, 4096)) > closed * 5,
             "Host source routing did not bypass the closed filter");
}

void testVersionFourMigrationAndDestinationAutomation()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (4);
    for (int route = 1; route <= 8; ++route)
    {
        const auto id = "mod" + juce::String (route) + "Dest";
        auto* destination = source.apvts.getParameter (id);
        require (destination != nullptr && destination->getNumSteps() == 8,
                 "Legacy modulation destination automation no longer has eight choices");
        for (int choice = 0; choice < 8; ++choice)
            require (std::abs (destination->convertFrom0to1 (choice / 7.0f) - choice) < .0001f,
                     "Normalized legacy modulation destinations changed mapping");
    }
    setParameter (source, "mod1Source", 6);
    setParameter (source, "mod1Dest", 7);
    setParameter (source, "mod1Amount", -.31f);
    const auto expected = parameters (source);
    auto legacy = stateTree (source);
    legacy.setProperty ("schemaVersion", 4, nullptr);
    for (int child = legacy.getNumChildren(); --child >= 0;)
        if (const auto id = legacy.getChild (child).getProperty ("id").toString();
            isDualFilterParameter (id) || isWarpParameter (id))
            legacy.removeChild (child, nullptr);
    AnyvocAudioProcessor target;
    target.setCurrentProgram (13);
    for (auto* base : target.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isDualFilterParameter (parameter->getParameterID()) || isWarpParameter (parameter->getParameterID()))
                parameter->setValueNotifyingHost (1.0f);
    restore (target, legacy);
    expectParameters (target, expected);
    require (target.getModulationDestination (0) == 7,
             "Version 4 migration changed the original pan modulation destination");
    const auto beforeInvalid = save (target);
    auto incompleteOld = legacy.createCopy();
    incompleteOld.removeChild (incompleteOld.getChildWithProperty ("id", "lfo1Point17"), nullptr);
    restore (target, incompleteOld);
    require (save (target) == beforeInvalid, "Version 4 migration accepted a missing pre-existing LFO parameter");
    for (const char* id : { "filter2Cutoff", "osc1FilterRoute", "mod1DestBank", "mod8FilterTarget" })
    {
        auto incomplete = stateTree (target);
        incomplete.removeChild (incomplete.getChildWithProperty ("id", id), nullptr);
        restore (target, incomplete);
        require (save (target) == beforeInvalid, "Version 5 state accepted a missing dual-filter parameter");
    }
}

void testAdditionalModulationDestinations()
{
    AnyvocAudioProcessor processor;
    setParameter (processor, "mod1Dest", 7);
    processor.setModulationDestination (0, 8);
    require (processor.getModulationDestination (0) == 8
                 && processor.apvts.getRawParameterValue ("mod1Dest")->load() == 7,
             "Selecting an extended destination rewrote the legacy automation parameter");
    processor.setModulationDestination (0, 7);
    require (processor.getModulationDestination (0) == 7
                 && processor.apvts.getRawParameterValue ("mod1DestBank")->load() == 0,
             "Returning to an original destination did not select the original bank");
    for (int destination = 0; destination <= 27; ++destination)
    {
        processor.setModulationDestination (7, destination);
        require (processor.getModulationDestination (7) == destination,
                 "Effective modulation destination setter did not reach the requested target");
    }
    const auto beforeInvalid = save (processor);
    processor.setModulationDestination (-1, 8);
    processor.setModulationDestination (static_cast<int> (anyvoc::modulationRouteCount), 8);
    processor.setModulationDestination (0, -1);
    processor.setModulationDestination (0, 36);
    require (save (processor) == beforeInvalid, "Invalid destination selection changed the matrix");
    processor.setCurrentProgram (0);
    for (int destination = 8; destination <= 11; ++destination)
        require (processor.assignModulation (1, destination, .2f), "An extended filter target could not be assigned");
    for (int route = 0; route < 4; ++route)
        require (processor.getModulationDestination (route) == route + 8,
                 "Assignments reused an occupied extended route with an empty legacy destination");
    require (processor.assignModulation (1, 9, -.7f), "Duplicate extended modulation assignment was rejected");
    require (std::abs (processor.apvts.getRawParameterValue ("mod2Amount")->load() - .2f) < 1.0e-6f
                 && processor.apvts.getRawParameterValue ("mod5Source")->load() == 0,
             "Duplicate extended assignment changed depth or consumed a new route");
    for (int destination = 1; destination <= 4; ++destination)
        require (processor.assignModulation (2, destination, -.3f), "Mixed original/filter matrix could not be filled");
    for (int route = 9; route <= 32; ++route)
    {
        setParameter (processor, ("mod" + juce::String (route) + "Source").toRawUTF8(), 4);
        setParameter (processor, ("mod" + juce::String (route) + "Dest").toRawUTF8(), 6);
    }
    require (! processor.assignModulation (3, 11), "An extended assignment overwrote a full matrix");
    const auto data = save (processor);
    AnyvocAudioProcessor restored;
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    expectParameters (restored, parameters (processor));
    for (int route = 0; route < 8; ++route)
        require (restored.getModulationDestination (route) == processor.getModulationDestination (route),
                 "A mixed-bank modulation target changed after state restore");
}

void testWarpStateAndPreview()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (14);
    const auto raw = source.getEditableWavetable (0);
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
        for (int slot = 1; slot <= 2; ++slot)
        {
            const auto prefix = "osc" + juce::String (oscillator) + "Warp" + juce::String (slot);
            const int index = (oscillator - 1) * 2 + slot - 1;
            setParameter (source, (prefix + "Mode").toRawUTF8(), static_cast<float> (index + 1));
            setParameter (source, (prefix + "Amount").toRawUTF8(), .15f + index * .13f);
        }
    source.setModulationDestination (0, 12);
    source.setModulationDestination (1, 17);
    expectTableAudio (source.getEditableWavetable (0), raw);
    const auto expected = parameters (source);
    const auto data = save (source);
    AnyvocAudioProcessor restored;
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    expectParameters (restored, expected);
    require (restored.getModulationDestination (0) == 12 && restored.getModulationDestination (1) == 17,
             "Warp modulation targets changed in DAW state");
    double previewDifference = 0;
    for (int oscillator = 0; oscillator < 3; ++oscillator)
        for (int point = 0; point < 64; ++point)
        {
            const auto phase = point / 64.0f;
            const auto original = source.getWarpedWavetableSample (oscillator, phase, .4f);
            const auto result = restored.getWarpedWavetableSample (oscillator, phase, .4f);
            require (std::isfinite (result) && std::abs (original - result) < 2.0e-5f,
                     "Warped waveform preview changed after state restore");
            previewDifference += std::abs (result - restored.getWavetableSample (oscillator, phase, .4f));
        }
    require (previewDifference > 1, "Warp parameters did not reach the processor's waveform preview");
    juce::TemporaryFile preset (".anyvoc");
    require (source.savePreset (preset.getFile()) && restored.loadPreset (preset.getFile()),
             "Dual-warp preset could not be saved and reloaded");
    expectParameters (restored, expected);
    restored.setCurrentProgram (0);
    for (auto* base : restored.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isWarpParameter (parameter->getParameterID()))
                require (std::abs (parameter->getValue() - parameter->getDefaultValue()) < 2.0e-5f,
                         "Init did not clear an oscillator warp or its modulation target");
}

void testVersionFiveWarpMigration()
{
    AnyvocAudioProcessor source;
    source.setCurrentProgram (13);
    source.setModulationDestination (0, 11);
    for (int route = 1; route <= 8; ++route)
    {
        const auto prefix = "mod" + juce::String (route);
        auto* original = source.apvts.getParameter (prefix + "Dest");
        auto* bank = source.apvts.getParameter (prefix + "DestBank");
        auto* filter = source.apvts.getParameter (prefix + "FilterTarget");
        require (original != nullptr && original->getNumSteps() == 8
                     && bank != nullptr && bank->getNumSteps() == 2
                     && filter != nullptr && filter->getNumSteps() == 4,
                 "Adding warp destinations changed a legacy automation choice count");
        require (bank->convertFrom0to1 (1) == 1
                     && std::abs (filter->convertFrom0to1 (2.0f / 3) - 2) < .0001f,
                 "Legacy destination bank or filter target normalized automation changed");
    }
    const auto expected = parameters (source);
    auto legacy = stateTree (source);
    legacy.setProperty ("schemaVersion", 5, nullptr);
    for (int child = legacy.getNumChildren(); --child >= 0;)
        if (isWarpParameter (legacy.getChild (child).getProperty ("id").toString()))
            legacy.removeChild (child, nullptr);
    AnyvocAudioProcessor target;
    target.setCurrentProgram (14);
    for (auto* base : target.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (isWarpParameter (parameter->getParameterID())) parameter->setValueNotifyingHost (1);
    restore (target, legacy);
    expectParameters (target, expected);
    require (target.getModulationDestination (0) == 11,
             "Version 5 migration changed a filter destination into a warp destination");
    const auto before = save (target);
    auto incompleteLegacy = legacy.createCopy();
    incompleteLegacy.removeChild (incompleteLegacy.getChildWithProperty ("id", "mod1FilterTarget"), nullptr);
    restore (target, incompleteLegacy);
    require (save (target) == before, "Version 5 migration accepted a missing pre-existing filter target");
    for (const char* id : { "osc1Warp1Mode", "osc3Warp2Amount", "mod1WarpEnabled", "mod8WarpTarget" })
    {
        auto incomplete = stateTree (target);
        incomplete.removeChild (incomplete.getChildWithProperty ("id", id), nullptr);
        restore (target, incomplete);
        require (save (target) == before, "Version 6 state accepted a missing warp parameter");
    }
    auto nonFinite = stateTree (target);
    nonFinite.getChildWithProperty ("id", "osc2Warp1Amount").setProperty ("value", "NaN", nullptr);
    restore (target, nonFinite);
    require (save (target) == before, "Non-finite warp state changed the current sound");
}

void testWarpModulationAssignmentsAndAudio()
{
    AnyvocAudioProcessor processor;
    for (int destination = 12; destination <= 17; ++destination)
        require (processor.assignModulation (1, destination, .25f), "A warp modulation destination could not be assigned");
    for (int route = 0; route < 6; ++route)
        require (processor.getModulationDestination (route) == route + 12,
                 "Warp assignment reused a route whose legacy destination was empty");
    require (processor.assignModulation (1, 14, -.7f)
                 && std::abs (processor.apvts.getRawParameterValue ("mod3Amount")->load() - .25f) < 1.0e-6f
                 && processor.apvts.getRawParameterValue ("mod7Source")->load() == 0,
             "Duplicate warp assignment changed depth or consumed another route");
    require (processor.assignModulation (2, 1, .1f) && processor.assignModulation (2, 8, .1f),
             "A matrix mixing all destination families could not be filled");
    for (int route = 9; route <= 32; ++route)
    {
        setParameter (processor, ("mod" + juce::String (route) + "Source").toRawUTF8(), 4);
        setParameter (processor, ("mod" + juce::String (route) + "Dest").toRawUTF8(), 6);
    }
    const auto full = save (processor);
    require (! processor.assignModulation (2, 17) && ! processor.assignModulation (1, 36),
             "A warp assignment overwrote a full matrix or accepted an invalid target");
    require (save (processor) == full, "Rejected warp assignment changed the matrix");
    AnyvocAudioProcessor restored;
    restored.setStateInformation (full.getData(), static_cast<int> (full.getSize()));
    expectParameters (restored, parameters (processor));
    for (int route = 0; route < 8; ++route)
        require (restored.getModulationDestination (route) == processor.getModulationDestination (route),
                 "Warp modulation destination did not survive state restore");
    restored.setModulationDestination (0, 9);
    require (restored.getModulationDestination (0) == 9
                 && restored.apvts.getRawParameterValue ("mod1WarpEnabled")->load() == 0,
             "Selecting a filter target did not disable the warp destination override");
    restored.setModulationDestination (0, 7);
    require (restored.getModulationDestination (0) == 7, "Selecting an original target left the warp override active");

    AnyvocAudioProcessor direct, modulated;
    for (auto* instrument : { &direct, &modulated })
    {
        audibleSettings (*instrument);
        setParameter (*instrument, "subLevel", 0);
        setParameter (*instrument, "filterEnabled", 0);
        setParameter (*instrument, "osc1Warp1Mode", 6);
    }
    setParameter (direct, "osc1Warp1Amount", .65f);
    setParameter (modulated, "macro1", 1);
    require (modulated.assignModulation (6, 12, .65f), "Macro could not be assigned to oscillator A warp 1");
    direct.prepareToPlay (48000, 512); modulated.prepareToPlay (48000, 512);
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    render (direct, 12000, note); render (modulated, 12000, note);
    const auto expectedAudio = render (direct, 4096);
    const auto actualAudio = render (modulated, 4096);
    require (rms (actualAudio) > .005, "Warp-modulated processor was silent");
    double difference = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < actualAudio.getNumSamples(); ++sample)
            difference = std::max (difference, static_cast<double> (
                std::abs (actualAudio.getSample (channel, sample) - expectedAudio.getSample (channel, sample))));
    require (difference < .0002, "Warp modulation did not reach the same audio result as its direct amount parameter");
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    PositionInfo position;
};

void testProcessorHostAndManualTempo()
{
    AnyvocAudioProcessor processor;
    setParameter (processor, "tempo", 90);
    setParameter (processor, "lfo1Sync", 1);
    setParameter (processor, "lfo1Division", 4);
    processor.prepareToPlay (48000, 512);
    render (processor, 128);
    require (! processor.isTempoFromHost() && std::abs (processor.getTempo() - 90.0f) < .01f,
             "Standalone/manual tempo was not applied");
    TestPlayHead playHead;
    playHead.position.setBpm (150.0);
    playHead.position.setPpqPosition (2.25);
    playHead.position.setIsPlaying (true);
    processor.setPlayHead (&playHead);
    render (processor, 128);
    require (processor.isTempoFromHost() && std::abs (processor.getTempo() - 150.0f) < .01f,
             "Host tempo did not override the manual tempo");
    require (std::abs (processor.getLfoPhase (0) - .2566667f) < .002f,
             "Host PPQ was not passed through to the synced LFO");
    setParameter (processor, "lfo1Division", 7);
    playHead.position.setBpm (600.0);
    playHead.position.setPpqPosition (0.0);
    render (processor, 128);
    require (processor.isTempoFromHost() && std::abs (processor.getTempo() - 600.0f) < .01f,
             "A valid 600 BPM host tempo was clamped to the manual tempo range");
    require (std::abs (processor.getLfoPhase (0) - .2133333f) < .002f,
             "High host BPM was not preserved by the 1/32-note LFO clock");
    setParameter (processor, "lfo1Division", 4);
    playHead.position.setBpm (10.0);
    render (processor, 128);
    require (processor.isTempoFromHost() && std::abs (processor.getTempo() - 10.0f) < .01f,
             "A valid 10 BPM host tempo was clamped to the manual tempo range");
    require (std::abs (processor.getLfoPhase (0) - (128.0f / 48000.0f * (10.0f / 60.0f))) < .0001f,
             "Low host BPM was not preserved by the LFO clock");
    playHead.position.setBpm (std::numeric_limits<double>::quiet_NaN());
    render (processor, 128);
    require (! processor.isTempoFromHost() && std::abs (processor.getTempo() - 90.0f) < .01f,
             "Invalid host BPM did not fall back to the manual tempo");
    playHead.position.setBpm (juce::Optional<double> {});
    render (processor, 128);
    require (! processor.isTempoFromHost() && std::abs (processor.getTempo() - 90.0f) < .01f,
             "Missing host BPM did not fall back to the manual tempo");
    processor.setPlayHead (nullptr);
    setParameter (processor, "tempo", 105);
    render (processor, 128);
    require (! processor.isTempoFromHost() && std::abs (processor.getTempo() - 105.0f) < .01f,
             "Manual tempo did not resume after disconnecting the playhead");
}

void testSampleImportRejection()
{
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (3);
    const auto original = save (processor);
    juce::TemporaryFile tooLong (".wav");
    writeSampleFixture (tooLong.getFile(), 8000.0, 20.1, 1);
    require (! processor.importSample (0, tooLong.getFile()), "A sample longer than 20 seconds was accepted");
    juce::TemporaryFile validFile (".wav");
    writeSampleFixture (validFile.getFile());
    require (! processor.importSample (-1, validFile.getFile())
                 && ! processor.importSample (3, validFile.getFile()),
             "An invalid sample oscillator index was accepted");
    juce::TemporaryFile invalidFile (".wav");
    require (invalidFile.getFile().replaceWithText ("This is not audio"), "Could not create malformed WAV fixture");
    require (! processor.importSample (0, invalidFile.getFile()), "Malformed WAV sample was accepted");
    require (save (processor) == original, "Rejected sample import changed the current preset");
    auto malformed = stateTree (processor);
    malformed.getChildWithProperty ("id", "master").setProperty ("value", .123f, nullptr);
    juce::ValueTree asset ("SAMPLE");
    asset.setProperty ("osc", 0, nullptr);
    asset.setProperty ("name", "Corrupt sample", nullptr);
    asset.setProperty ("wav", "invalid sample bytes", nullptr);
    malformed.addChild (asset, -1, nullptr);
    restore (processor, malformed);
    require (save (processor) == original, "Malformed embedded sample did not roll back the state atomically");
}

void testModulationAssignment()
{
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (0);
    require (processor.assignModulation (1, 1, .4f), "A valid modulation assignment failed");
    require (processor.assignModulation (1, 1, -.8f), "Duplicate modulation assignment was rejected");
    require (std::abs (processor.apvts.getRawParameterValue ("mod1Amount")->load() - .4f) < 1.0e-5f,
             "A duplicate drag overwrote the existing modulation depth");
    require (processor.apvts.getRawParameterValue ("mod2Source")->load() == 0,
             "Duplicate modulation assignment consumed another route");
    for (int source = 2; source <= 8; ++source)
        require (processor.assignModulation (source, 1, .1f * source), "An available modulation route was not used");
    for (int route = 9; route <= 32; ++route)
    {
        setParameter (processor, ("mod" + juce::String (route) + "Source").toRawUTF8(), 4);
        setParameter (processor, ("mod" + juce::String (route) + "Dest").toRawUTF8(), 6);
    }
    const auto full = save (processor);
    require (! processor.assignModulation (9, 1), "Assignment unexpectedly overwrote a full modulation matrix");
    require (! processor.assignModulation (0, 1) && ! processor.assignModulation (25, 1)
                 && ! processor.assignModulation (1, 0) && ! processor.assignModulation (1, 36),
             "Invalid modulation source or destination was accepted");
    require (save (processor) == full, "Rejected modulation assignment changed existing routes");
    setParameter (processor, "mod3Dest", 0);
    require (processor.assignModulation (9, 2, -.25f), "A route with an empty destination was not reused");
    require (processor.apvts.getRawParameterValue ("mod3Source")->load() == 9
                 && processor.apvts.getRawParameterValue ("mod3Dest")->load() == 2,
             "Modulation assignment did not reuse the first available route");
    const auto state = save (processor);
    AnyvocAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    expectParameters (restored, parameters (processor));
}

void testMalformedStateRejection()
{
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (3);
    const auto original = save (processor);
    const auto valid = stateTree (processor);
    const auto assertUnchanged = [&] (const juce::ValueTree& broken, const char* name)
    {
        restore (processor, broken);
        require (save (processor) == original, std::string ("Malformed state changed the live preset: ") + name);
    };
    auto wrongVersion = valid.createCopy();
    wrongVersion.setProperty ("schemaVersion", 999, nullptr);
    assertUnchanged (wrongVersion, "unknown schema version");
    juce::ValueTree wrongType ("UNRELATED_PLUGIN");
    assertUnchanged (wrongType, "wrong plugin identity");
    auto missingParameter = valid.createCopy();
    missingParameter.removeChild (0, nullptr);
    assertUnchanged (missingParameter, "missing parameter");
    auto duplicateParameter = valid.createCopy();
    duplicateParameter.addChild (duplicateParameter.getChild (0).createCopy(), -1, nullptr);
    assertUnchanged (duplicateParameter, "duplicate parameter");
    auto notNumeric = valid.createCopy();
    notNumeric.getChildWithProperty ("id", "cutoff").setProperty ("value", "invalid-number", nullptr);
    assertUnchanged (notNumeric, "non-numeric value");
    auto nonFinite = valid.createCopy();
    nonFinite.getChildWithProperty ("id", "cutoff").setProperty ("value", "NaN", nullptr);
    assertUnchanged (nonFinite, "non-finite value");
    auto badAsset = valid.createCopy();
    badAsset.getChildWithProperty ("id", "master").setProperty ("value", .123f, nullptr);
    juce::ValueTree asset ("WAVETABLE");
    asset.setProperty ("osc", 0, nullptr);
    asset.setProperty ("name", "broken", nullptr);
    asset.setProperty ("wav", "not a WAV base64", nullptr);
    badAsset.addChild (asset, -1, nullptr);
    assertUnchanged (badAsset, "invalid embedded asset must roll back parameter changes");
    const char garbage[] = "This is not a serialized plugin state";
    processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
    processor.setStateInformation (original.getData(), 0);
    require (save (processor) == original, "Malformed binary state changed the live preset");
}

void testArpeggiatorLifecycle()
{
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (0);
    audibleSettings (processor);
    setParameter (processor, "arpEnabled", 1);
    setParameter (processor, "arpRate", 2);
    setParameter (processor, "arpOctaves", 2);
    setParameter (processor, "arpGate", .5f);
    processor.prepareToPlay (48000.0, 512);
    const auto notes = render (processor, 24000,
        { { 0, juce::MidiMessage::noteOn (1, 60, .85f) },
          { 0, juce::MidiMessage::noteOn (1, 64, .8f) },
          { 0, juce::MidiMessage::noteOn (1, 67, .8f) } }, 257);
    require (rms (notes) > .01, "Arpeggiator did not generate audible notes");
    render (processor, 18000,
        { { 0, juce::MidiMessage::noteOff (1, 60) },
          { 0, juce::MidiMessage::noteOff (1, 64) },
          { 0, juce::MidiMessage::noteOff (1, 67) } }, 127);
    require (rms (render (processor, 4096)) < 1.0e-6,
             "Arpeggiator retained a note after all keys were released");
    render (processor, 12000,
        { { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
          { 100, juce::MidiMessage::controllerEvent (1, 64, 127) },
          { 4000, juce::MidiMessage::noteOff (1, 60) } });
    const auto heldRms = rms (render (processor, 12000));
    require (heldRms > .005, "Arp sustain pedal did not retain held notes; RMS=" + std::to_string (heldRms));
    render (processor, 12000,
        { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
    require (rms (render (processor, 4096)) < 1.0e-6,
             "Arp sustain pedal release left stuck notes");
}

void testArpBlockPartitionAndChannelPanic()
{
    const auto configure = [] (AnyvocAudioProcessor& processor)
    {
        processor.setCurrentProgram (0);
        audibleSettings (processor);
        setParameter (processor, "arpEnabled", 1);
        setParameter (processor, "arpRate", 2);
        setParameter (processor, "arpGate", .5f);
        processor.prepareToPlay (48000.0, 512);
    };
    AnyvocAudioProcessor first, second;
    configure (first);
    configure (second);
    const std::vector<Event> events {
        { 0, juce::MidiMessage::noteOn (1, 60, .8f) },
        { 100, juce::MidiMessage::controllerEvent (1, 64, 127) },
        { 4000, juce::MidiMessage::noteOff (1, 60) },
        { 18000, juce::MidiMessage::controllerEvent (1, 64, 0) }
    };
    const auto a = render (first, 24000, events, 127);
    const auto b = render (second, 24000, events, 257);
    double difference = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < a.getNumSamples(); ++sample)
            difference = std::max (difference, static_cast<double> (
                std::abs (a.getSample (channel, sample) - b.getSample (channel, sample))));
    require (difference < 1.0e-5, "Arp sustain timing depended on host block boundaries; delta="
                                  + std::to_string (difference));
    for (const int controller : { 120, 123 })
    {
        AnyvocAudioProcessor processor;
        configure (processor);
        render (processor, 1024, { { 0, juce::MidiMessage::noteOn (2, 60, .8f) } });
        render (processor, 12000, { { 0, juce::MidiMessage::controllerEvent (2, controller, 0) } });
        require (rms (render (processor, 4096)) < 1.0e-6,
                 "Arp panic on the input MIDI channel left a generated note active");
    }
}

void testParameterStress()
{
    AnyvocAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    std::mt19937 random (0x5155414c);
    std::uniform_real_distribution<float> value (0.0f, 1.0f);
    for (int trial = 0; trial < 18; ++trial)
    {
        processor.panic();
        render (processor, 128);
        for (auto* parameter : processor.getParameters())
            parameter->setValueNotifyingHost (trial == 0 ? 0.0f : trial == 1 ? 1.0f : value (random));
        render (processor, 4096,
            { { 0, juce::MidiMessage::noteOn (1, 24 + trial, .95f) },
              { 7, juce::MidiMessage::noteOn (1, 60 + trial, .95f) },
              { 13, juce::MidiMessage::noteOn (1, 96, .95f) },
              { 2048, juce::MidiMessage::pitchWheel (1, trial % 2 == 0 ? 0 : 16383) } }, 127);
    }
    processor.panic();
    require (rms (render (processor, 4096)) < 1.0e-6, "Panic did not clear stressed processor audio");
}

juce::Button* findButton (juce::Component& component, const juce::String& text)
{
    // Open overlays move to the front of the child list. Their hidden siblings
    // still contain identical buttons, so only traverse the visible hierarchy.
    // isShowing() requires a native peer and is unsuitable for these snapshots.
    if (! component.isVisible()) return nullptr;
    if (auto* button = dynamic_cast<juce::Button*> (&component))
        if (button->getButtonText() == text)
            return button;
    for (int child = 0; child < component.getNumChildComponents(); ++child)
        if (auto* found = findButton (*component.getChildComponent (child), text))
            return found;
    return nullptr;
}

template <typename Type>
Type* findComponent (juce::Component& component, const juce::String& id)
{
    if (! component.isVisible()) return nullptr;
    if (component.getComponentID() == id)
        if (auto* match = dynamic_cast<Type*> (&component)) return match;
    for (int child = 0; child < component.getNumChildComponents(); ++child)
        if (auto* match = findComponent<Type> (*component.getChildComponent (child), id)) return match;
    return nullptr;
}

void testPresetBrowserSilentSelectionAndExplicitLoad()
{
    PresetTestDirectory files;
    const auto library = files.directory.getChildFile ("Library");
    const auto preferences = files.directory.getChildFile ("Preferences.xml");
    require (library.createDirectory().wasOk(), "Could not create isolated browser library");
    AnyvocAudioProcessor fixture;
    fixture.setCurrentProgram (14);
    setParameter (fixture, "master", .41f);
    const auto expectedUser = parameters (fixture);
    const auto userFile = library.getChildFile ("Browser Custom.anyvoc");
    const auto missingFile = library.getChildFile ("Missing Sound.anyvoc");
    require (fixture.savePreset (userFile) && userFile.copyFileTo (missingFile)
                 && library.getChildFile ("Invalid Sound.anyvoc").replaceWithText ("<ANYVOC broken"),
             "Could not create browser load fixtures");

    AnyvocAudioProcessor processor, audioReference;
    processor.setCurrentProgram (2);
    setParameter (processor, "master", .37f);
    const auto baseline = save (processor);
    audioReference.setStateInformation (baseline.getData(), static_cast<int> (baseline.getSize()));
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, library, preferences);
    editor->setVisible (true); editor->setSize (1120, 760);
    auto* browse = findButton (*editor, "BROWSE");
    require (browse != nullptr && static_cast<bool> (browse->onClick), "Preset browser entry point was missing");
    browse->onClick();
    auto* results = findComponent<juce::ListBox> (*editor, "presetResults");
    auto* search = findComponent<juce::TextEditor> (*editor, "presetSearch");
    auto* source = findComponent<juce::ComboBox> (*editor, "presetSource");
    auto* favourite = findComponent<juce::Button> (*editor, "presetFavourite");
    auto* load = findComponent<juce::Button> (*editor, "presetLoad");
    require (results != nullptr && search != nullptr && source != nullptr && favourite != nullptr && load != nullptr,
             "Browser controls were not exposed with their expected accessible identities");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
    while (results->getModel()->getNumRows() != processor.getNumPrograms() + 3
           && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (2);
        browse->onClick(); // Refresh the visible snapshot without requiring a native window/message loop.
    }
    require (results->getModel()->getNumRows() == processor.getNumPrograms() + 3,
             "Browser did not publish its asynchronous user-file scan");
    const auto filter = [&] (const juce::String& query, int sourceId)
    {
        source->setSelectedId (sourceId, juce::dontSendNotification);
        source->onChange();
        search->setText (query, false); search->onTextChange();
    };
    const auto unchanged = [&]
    {
        require (save (processor) == baseline, "Browsing or favouriting a result changed the current sound/identity");
        require (! processor.canUndo() && ! processor.canRedo(),
                 "Browsing, selecting or favouriting a preset created sound history");
    };
    int orbitRow = -1;
    for (int row = 0; row < results->getModel()->getNumRows(); ++row)
        if (results->getModel()->getNameForRow (row).startsWith ("Folded Orbit /")) orbitRow = row;
    require (orbitRow >= 0, "Browser row identity was unavailable for selection-preservation test");
    results->selectRow (orbitRow);
    const auto selectedName = results->getModel()->getNameForRow (results->getSelectedRow());
    const auto selectionPreserved = [&]
    {
        require (results->getSelectedRow() >= 0
                     && results->getModel()->getNameForRow (results->getSelectedRow()) == selectedName,
                 "Browser lost a still-matching selected identity when results changed");
    };
    filter ("motion", 2);
    selectionPreserved();
    auto* sort = findComponent<juce::ComboBox> (*editor, "presetSort");
    require (sort != nullptr, "Browser sort control was unavailable");
    sort->setSelectedId (2, juce::dontSendNotification); sort->onChange();
    selectionPreserved();
    filter ({}, 1);
    require (library.getChildFile ("Aardvark.anyvoc").replaceWithText ("indexed only"),
             "Could not create newly indexed browser fixture");
    auto* refresh = findComponent<juce::Button> (*editor, "presetRefresh");
    require (refresh != nullptr, "Browser refresh action was unavailable");
    refresh->onClick();
    const auto refreshDeadline = juce::Time::getMillisecondCounterHiRes() + 5000;
    while (results->getModel()->getNumRows() != processor.getNumPrograms() + 4
           && juce::Time::getMillisecondCounterHiRes() < refreshDeadline)
    {
        juce::Thread::sleep (2); browse->onClick();
    }
    require (results->getModel()->getNumRows() == processor.getNumPrograms() + 4,
             "Browser refresh did not publish the newly added file");
    selectionPreserved();
    unchanged();
    processor.prepareToPlay (48000, 512); audioReference.prepareToPlay (48000, 512);
    const std::vector<Event> onset { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    render (processor, 4096, onset); render (audioReference, 4096, onset);
    filter ("folded orbit", 2); // Factory only.
    require (results->getModel()->getNumRows() == 1, "Browser search/source filters did not isolate a factory sound");
    results->selectRow (0);
    unchanged();
    favourite->onClick();
    require (favourite->getToggleState() && preferences.existsAsFile(), "Browser favourite did not persist to isolated preferences");
    unchanged();
    auto* next = findComponent<juce::Button> (*editor, "presetNext");
    require (next != nullptr, "Browser result navigation was missing");
    next->onClick();
    unchanged();
    const auto continuing = render (processor, 4096);
    const auto reference = render (audioReference, 4096);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < continuing.getNumSamples(); ++sample)
            require (std::abs (continuing.getSample (channel, sample) - reference.getSample (channel, sample)) < 1.0e-6f,
                     "Silent browser selection reset or altered a currently sounding note");

    load->onClick();
    require (processor.getLoadedPresetId() == anyvoc::PresetLibrary::factoryId (processor.getProgramName (14)),
             "Explicit LOAD SOUND did not load the selected factory sound");
    filter ("browser custom", 3); // User only.
    require (results->getModel()->getNumRows() == 1, "Browser did not isolate the valid user preset");
    results->selectRow (0); load->onClick();
    expectParameters (processor, expectedUser);
    require (processor.getLoadedPresetId() == anyvoc::PresetLibrary::userId (userFile),
             "Explicit browser load did not publish the selected user identity");
    const auto loadedState = save (processor);
    filter ("missing sound", 3);
    require (results->getModel()->getNumRows() == 1 && missingFile.deleteFile(),
             "Could not remove the selected user file after indexing");
    results->selectRow (0); load->onClick();
    require (save (processor) == loadedState, "Loading a deleted browser result changed the current sound");
    filter ("invalid sound", 3); results->selectRow (0); load->onClick();
    require (save (processor) == loadedState, "Loading malformed browser content changed the current sound");
    filter ("nothing matches this phrase", 1);
    require (results->getModel()->getNumRows() == 0 && ! load->isEnabled() && ! favourite->isEnabled(),
             "Empty browser results retained a stale load/favourite action");
    filter ("northern lights", 2);
    require (results->getModel()->getNumRows() == 1 && static_cast<bool> (search->onReturnKey),
             "Browser keyboard loading hook was missing");
    search->onReturnKey();
    require (processor.getLoadedPresetId() == anyvoc::PresetLibrary::factoryId (processor.getProgramName (2)),
             "Return in browser search did not explicitly load the selected result");
}

void testOpenWavetableEditorStateReplacement()
{
    AnyvocAudioProcessor processor;
    const auto original = editedTableFixture();
    require (processor.applyEditedWavetable (0, original, "Same Table Name"), "Editor fixture table failed to apply");
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    require (editor != nullptr, "Processor did not create an editor for state-replacement validation");
    editor->setVisible (true);
    editor->setSize (1120, 760);
    auto* edit = findButton (*editor, "EDIT");
    require (edit != nullptr && static_cast<bool> (edit->onClick), "Wavetable editor button was unavailable");
    edit->onClick();
    auto* apply = findButton (*editor, "APPLY");
    require (apply != nullptr && static_cast<bool> (apply->onClick), "Wavetable Lab did not expose its apply action");

    auto replacement = original;
    replacement.applyGain (.5f);
    AnyvocAudioProcessor project;
    require (project.applyEditedWavetable (0, replacement, "Same Table Name"), "Replacement project table failed to apply");
    const auto state = save (project);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    const auto restoredRevision = processor.getWavetableRevision (0);
    apply->onClick(); // The old Lab still holds its opening table until the next UI timer tick.
    require (processor.getWavetableRevision (0) == restoredRevision,
             "A stale open wavetable editor republished over newer DAW state");
    expectTableAudio (processor.getEditableWavetable (0), replacement);
    require (processor.getWavetableName (0) == "Same Table Name",
             "The stale editor changed the restored table name");
}

void testVoicingEditorControls()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                              files.directory.getChildFile ("Settings.xml"));
    editor->setVisible (true); editor->setSize (1120, 760);
    auto* open = findComponent<juce::Button> (*editor, "openVoicing");
    require (open != nullptr && static_cast<bool> (open->onClick), "Voicing entry point was missing");
    open->onClick();
    auto* mode = findComponent<juce::ComboBox> (*editor, "voiceModeChoice");
    auto* glide = findComponent<juce::Slider> (*editor, "glideTime");
    auto* always = findComponent<juce::Button> (*editor, "glideAlways");
    require (mode != nullptr && glide != nullptr && always != nullptr && ! glide->isEnabled() && ! always->isEnabled(),
             "Poly mode did not disable inapplicable glide controls");
    require (! always->getToggleState(), "Init unexpectedly enabled Always glide");
    processor.setCurrentProgram (15);
    open->onClick();
    require (*processor.apvts.getRawParameterValue ("glideAlways") == 1 && always->getToggleState(),
             "Loading Ribbon Glide while Always was disabled left a stale unchecked control");
    processor.setCurrentProgram (0);
    open->onClick();
    require (! always->getToggleState(), "Returning to Init left stale enabled Always-glide state");
    mode->setSelectedId (3, juce::sendNotificationSync);
    require (*processor.apvts.getRawParameterValue ("voiceMode") == 2 && glide->isEnabled() && always->isEnabled(),
             "Voicing mode UI did not update the host parameter and control availability");
    glide->setValue (.31, juce::sendNotificationSync);
    always->setToggleState (true, juce::sendNotificationSync);
    require (std::abs (*processor.apvts.getRawParameterValue ("glideTime") - .31f) < .0001f
                 && *processor.apvts.getRawParameterValue ("glideAlways") == 1,
             "Glide UI changes were not attached to stored parameters");
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
    {
        const auto prefix = "osc" + juce::String (oscillator);
        auto* phase = findComponent<juce::Slider> (*editor, prefix + "Phase");
        auto* random = findComponent<juce::Slider> (*editor, prefix + "PhaseRandom");
        auto* unison = findComponent<juce::Button> (*editor, prefix + "RandomUnison");
        require (phase != nullptr && random != nullptr && unison != nullptr && phase->isEnabled(),
                 "Wavetable oscillator phase controls were unavailable");
        phase->setValue (.25, juce::sendNotificationSync);
        random->setValue (.35, juce::sendNotificationSync);
        unison->setToggleState (false, juce::sendNotificationSync);
        require (std::abs (*processor.apvts.getRawParameterValue (prefix + "Phase") - .25f) < .0001f
                     && std::abs (*processor.apvts.getRawParameterValue (prefix + "PhaseRandom") - .35f) < .0001f
                     && *processor.apvts.getRawParameterValue (prefix + "RandomUnison") == 0,
                 "Phase UI changes did not reach all three oscillator parameters");
        require (phase->getTextFromValue (.5).contains ("180")
                     && std::abs (phase->getValueFromText ("90 deg") - .25) < .0001,
                 "Normalized phase was not presented and parsed as degrees");
    }
    setParameter (processor, "osc1Mode", 1);
    open->onClick();
    require (! findComponent<juce::Slider> (*editor, "osc1Phase")->isEnabled()
                 && ! findComponent<juce::Button> (*editor, "osc1RandomUnison")->isEnabled()
                 && findComponent<juce::Slider> (*editor, "osc2Phase")->isEnabled(),
             "Sample mode did not disable only its inapplicable wavetable phase controls");
    setParameter (processor, "osc1RandomUnison", 1);
    setParameter (processor, "osc1Mode", 0);
    open->onClick();
    require (findComponent<juce::Button> (*editor, "osc1RandomUnison")->getToggleState(),
             "A parameter update while Sample disabled Random Unison was lost on Wavetable reopen");
    setParameter (processor, "osc1Mode", 1); open->onClick();
    setParameter (processor, "osc1RandomUnison", 0);
    setParameter (processor, "osc1Mode", 0); open->onClick();
    require (! findComponent<juce::Button> (*editor, "osc1RandomUnison")->getToggleState(),
             "Disabled Random Unison ignored a later external off-state update");
}

float plainParameter (AnyvocAudioProcessor& processor, const juce::String& id)
{
    const auto* value = processor.apvts.getRawParameterValue (id);
    require (value != nullptr, "Missing history fixture parameter: " + id.toStdString());
    return value->load();
}

void expectHistoryResult (const juce::Result& result, const char* action)
{
    require (result.wasOk(), std::string (action) + ": " + result.getErrorMessage().toStdString());
}

std::string historyStateDiagnostic (AnyvocAudioProcessor& processor, const juce::MemoryBlock& expected)
{
    const auto actual = save (processor);
    juce::String details = "undo=" + juce::String (processor.getUndoCount())
        + ", redo=" + juce::String (processor.getRedoCount())
        + ", active=" + juce::String (processor.isUserEditActive() ? 1 : 0)
        + ", canUndo=" + juce::String (processor.canUndo() ? 1 : 0)
        + ", canRedo=" + juce::String (processor.canRedo() ? 1 : 0)
        + ", bytesEqual=" + juce::String (actual == expected ? 1 : 0);
    if (actual == expected) return details.toStdString();
    auto actualXml = juce::AudioProcessor::getXmlFromBinary (actual.getData(), static_cast<int> (actual.getSize()));
    auto expectedXml = juce::AudioProcessor::getXmlFromBinary (expected.getData(), static_cast<int> (expected.getSize()));
    if (! actualXml || ! expectedXml) return (details + "; XML decode failed").toStdString();
    const auto a = juce::ValueTree::fromXml (*actualXml), e = juce::ValueTree::fromXml (*expectedXml);
    int differences = 0;
    for (int property = 0; property < e.getNumProperties(); ++property)
    {
        const auto name = e.getPropertyName (property);
        if (a.getProperty (name) != e.getProperty (name))
        {
            details += "; root " + name.toString() + " expected=" + e.getProperty (name).toString()
                + " actual=" + a.getProperty (name).toString();
            ++differences;
        }
    }
    if (a.getNumChildren() != e.getNumChildren())
        details += "; child counts expected=" + juce::String (e.getNumChildren())
            + " actual=" + juce::String (a.getNumChildren());
    for (int child = 0; child < e.getNumChildren() && differences < 6; ++child)
    {
        const auto expectedChild = e.getChild (child);
        if (expectedChild.hasType ("PARAM"))
        {
            const auto id = expectedChild.getProperty ("id").toString();
            const auto actualChild = a.getChildWithProperty ("id", id);
            if (actualChild.getProperty ("value") != expectedChild.getProperty ("value"))
            {
                details += "; parameter " + id + " expected=" + expectedChild.getProperty ("value").toString()
                    + " actual=" + actualChild.getProperty ("value").toString()
                    + " live=" + juce::String (plainParameter (processor, id), 12);
                if (const auto* parameter = processor.apvts.getParameter (id))
                    details += " normalizedDelta=" + juce::String (parameter->getValue()
                        - parameter->convertTo0to1 (static_cast<float> (expectedChild.getProperty ("value"))), 12);
                ++differences;
            }
        }
        else
        {
            juce::ValueTree actualChild;
            for (int index = 0; index < a.getNumChildren(); ++index)
                if (a.getChild (index).getType() == expectedChild.getType()
                    && a.getChild (index).getProperty ("osc") == expectedChild.getProperty ("osc"))
                    actualChild = a.getChild (index);
            if (! actualChild.isEquivalentTo (expectedChild))
            {
                details += "; asset " + expectedChild.getType().toString() + " osc="
                    + expectedChild.getProperty ("osc").toString()
                    + " payloadEqual=" + juce::String (actualChild.getProperty ("wav") == expectedChild.getProperty ("wav") ? 1 : 0)
                    + " nameExpected=" + expectedChild.getProperty ("name").toString()
                    + " nameActual=" + actualChild.getProperty ("name").toString();
                ++differences;
            }
        }
    }
    if (differences == 0) details += "; root/parameter/asset values match; equivalentTree=" + juce::String (a.isEquivalentTo (e) ? 1 : 0);
    return details.toStdString();
}

juce::MemoryBlock normallyRestoredProjectState (const juce::MemoryBlock& project)
{
    // Skew-range normalization can canonicalize a serialized float by one ULP
    // during an ordinary project restore, while the live parameter is unchanged.
    // Compare history boundaries with an independent ordinary restore, retaining
    // exact metadata/asset/parameter bytes instead of relaxing all comparisons.
    // The existing state-roundtrip tests separately check parameter semantics.
    AnyvocAudioProcessor reference;
    reference.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
    return save (reference);
}

void testHistoryGroupingAndBranches()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    require (! processor.canUndo() && ! processor.canRedo() && ! processor.isUserEditActive(),
             "A new processor already contained user history");
    const auto cutoff = plainParameter (processor, "cutoff");
    const auto resonance = plainParameter (processor, "resonance");
    const auto revision = processor.getHistoryRevision();
    processor.beginUserEdit ("Filter sweep");
    for (int step = 0; step < 100; ++step)
        processor.setUserParameter ("cutoff", 800.0f + 40.0f * step);
    processor.beginUserEdit ("Nested resonance");
    processor.setUserParameter ("resonance", .71f);
    processor.endUserEdit();
    require (processor.isUserEditActive(), "Closing an inner edit prematurely closed the outer drag");
    processor.endUserEdit();
    require (! processor.isUserEditActive() && processor.getUndoCount() == 1
                 && processor.getRedoCount() == 0 && processor.getUndoName() == "Filter sweep"
                 && processor.getHistoryRevision() > revision,
             "A drag and its nested control changes were not one labelled history step");
    const auto finalCutoff = plainParameter (processor, "cutoff");
    const auto finalResonance = plainParameter (processor, "resonance");
    expectHistoryResult (processor.undoUserEdit(), "Undo grouped drag");
    require (std::abs (plainParameter (processor, "cutoff") - cutoff) < .01f
                 && std::abs (plainParameter (processor, "resonance") - resonance) < 1.0e-5f
                 && processor.getRedoName() == "Filter sweep",
             "One undo did not restore both starting control values");

    // Returning to the exact starting value is not an edit, even if a drag
    // travelled through intermediate values. It must not destroy redo.
    processor.beginUserEdit ("Unchanged drag");
    processor.setUserParameter ("cutoff", 1000);
    processor.setUserParameter ("cutoff", cutoff);
    processor.endUserEdit();
    processor.setUserParameter ("resonance", resonance);
    processor.setUserParameter ("missingParameter", .5f);
    processor.setUserParameter ("cutoff", std::numeric_limits<float>::quiet_NaN());
    processor.setUserParameter ("cutoff", std::numeric_limits<float>::infinity());
    processor.selectFactoryPreset (-1);
    processor.selectFactoryPreset (0); // Already-equivalent Init is also a no-op.
    require (! processor.importSample (0, files.directory.getChildFile ("missing.wav"))
                 && ! processor.importWavetable (0, files.directory.getChildFile ("missing.wav"))
                 && ! processor.loadPreset (files.directory.getChildFile ("missing.anyvoc")),
             "A missing source unexpectedly produced a successful edit");
    juce::AudioBuffer<float> invalid (1, 17); invalid.clear();
    require (! processor.applyEditedWavetable (0, invalid, "Invalid"), "Invalid wavetable edit was accepted");
    require (processor.getUndoCount() == 0 && processor.getRedoCount() == 1,
             "An unchanged or failed edit destroyed the redo branch");
    expectHistoryResult (processor.redoUserEdit(), "Redo grouped drag after no-ops");
    require (std::abs (plainParameter (processor, "cutoff") - finalCutoff) < .01f
                 && std::abs (plainParameter (processor, "resonance") - finalResonance) < 1.0e-5f,
             "Redo did not restore the completed drag");
    expectHistoryResult (processor.undoUserEdit(), "Undo before branching");
    processor.setUserParameter ("master", .31f);
    require (processor.getUndoCount() == 1 && ! processor.canRedo(),
             "A genuinely new edit did not replace the abandoned redo branch");
    expectHistoryResult (processor.undoUserEdit(), "Undo branch");
    require (processor.undoUserEdit().failed(), "Undo succeeded with no earlier edit");
}

void testHistoryCompoundEdits()
{
    AnyvocAudioProcessor processor;
    const auto initial = parameters (processor);
    processor.beginUserEdit ("Draw LFO 1");
    processor.setLfoShape (0, 5);
    processor.setUserParameter ("lfo1Interpolation", 1);
    for (int point = 0; point <= 32; ++point)
        processor.setUserParameter ("lfo1Point" + juce::String (point), -1.0f + point / 16.0f);
    for (int point = 8; point <= 16; ++point)
        processor.setUserParameter ("lfo1Point" + juce::String (point), .8f);
    processor.endUserEdit();
    const auto drawn = parameters (processor);
    require (processor.getUndoCount() == 1 && processor.getUndoName() == "Draw LFO 1",
             "Custom LFO selection and its 33-point stroke fragmented into separate edits");
    expectHistoryResult (processor.undoUserEdit(), "Undo LFO stroke");
    expectParameters (processor, initial);
    expectHistoryResult (processor.redoUserEdit(), "Redo LFO stroke");
    expectParameters (processor, drawn);

    const int beforeAssignment = processor.getUndoCount();
    require (processor.assignModulation (1, 17, .61f), "Could not assign extended warp modulation");
    const auto routed = parameters (processor);
    require (processor.getUndoCount() == beforeAssignment + 1 && processor.getModulationDestination (0) == 17,
             "Route source, destination bank and amount were not a single edit");
    require (processor.assignModulation (1, 17, -.5f)
                 && std::abs (plainParameter (processor, "mod1Amount") - .61f) < 1.0e-5f
                 && processor.getUndoCount() == beforeAssignment + 1,
             "Duplicate modulation assignment changed its depth or created history");
    require (! processor.assignModulation (0, 17, .3f) && ! processor.assignModulation (1, 36, .3f),
             "Invalid modulation assignment was accepted");
    expectHistoryResult (processor.undoUserEdit(), "Undo compound route");
    expectParameters (processor, drawn);
    expectHistoryResult (processor.redoUserEdit(), "Redo compound route");
    expectParameters (processor, routed);

    const int beforeDestination = processor.getUndoCount();
    processor.setModulationDestination (0, 9);
    require (processor.getUndoCount() == beforeDestination + 1 && processor.getModulationDestination (0) == 9,
             "Switching destination banks did not form a single edit");
    expectHistoryResult (processor.undoUserEdit(), "Undo destination bank switch");
    expectParameters (processor, routed);

    processor.setCurrentProgram (0);
    const auto table = editedTableFixture();
    require (processor.applyEditedWavetable (0, table, "Identical table"), "Could not create no-op asset fixture");
    processor.setUserParameter ("master", .39f);
    expectHistoryResult (processor.undoUserEdit(), "Create redo branch before identical asset apply");
    require (processor.applyEditedWavetable (0, table, "Identical table")
                 && processor.getUndoCount() == 1 && processor.getRedoCount() == 1,
             "Applying byte-identical table audio and name destroyed an unrelated redo branch");
}

void testHistoryHostIsolationAndBoundaries()
{
    AnyvocAudioProcessor processor;
    setParameter (processor, "master", .44f);
    require (! processor.canUndo(), "Host parameter automation was recorded as a user edit");
    const auto originalCutoff = plainParameter (processor, "cutoff");
    processor.beginUserEdit ("User cutoff");
    processor.setUserParameter ("cutoff", 1200);
    setParameter (processor, "macro2", .83f); // Host automation during the drag.
    setParameter (processor, "master", .37f);
    processor.endUserEdit();
    setParameter (processor, "macro3", .69f); // Host automation after the drag.
    expectHistoryResult (processor.undoUserEdit(), "Undo while unrelated host values are newer");
    require (std::abs (plainParameter (processor, "cutoff") - originalCutoff) < .01f
                 && std::abs (plainParameter (processor, "macro2") - .83f) < 1.0e-5f
                 && std::abs (plainParameter (processor, "macro3") - .69f) < 1.0e-5f
                 && std::abs (plainParameter (processor, "master") - .37f) < 1.0e-5f,
             "Undo reverted unrelated host automation captured during or after a user gesture");
    expectHistoryResult (processor.redoUserEdit(), "Redo around unrelated automation");
    processor.beginUserEdit ("Two controls");
    processor.setUserParameter ("cutoff", 2300);
    processor.setUserParameter ("resonance", .67f);
    processor.endUserEdit();
    setParameter (processor, "cutoff", 3200);
    setParameter (processor, "cutoff", 2300); // Same value, different host ownership epoch.
    const auto conflicted = save (processor);
    require (processor.undoUserEdit().failed() && save (processor) == conflicted
                 && ! processor.canUndo() && ! processor.canRedo(),
             "Conflicted undo partially changed the sound or ignored a same-value host epoch");

    processor.setUserParameter ("master", .28f);
    const auto validProject = save (processor);
    const auto normallyRestored = normallyRestoredProjectState (validProject);
    auto invalidProject = stateTree (processor);
    invalidProject.setProperty ("schemaVersion", 999, nullptr);
    processor.beginUserEdit ("Still active");
    processor.setUserParameter ("macro1", .7f);
    restore (processor, invalidProject);
    require (processor.isUserEditActive(), "Rejected project data canceled a valid pending user edit");
    processor.endUserEdit();
    require (processor.getUndoCount() == 2, "Rejected project data altered existing history");
    processor.beginUserEdit ("Must be discarded");
    processor.setUserParameter ("master", .9f);
    processor.setStateInformation (validProject.getData(), static_cast<int> (validProject.getSize()));
    require (! processor.canUndo() && ! processor.canRedo()
                 && save (processor) == normallyRestored,
             "External project restore retained pending edits or history from the previous sound; "
                 + historyStateDiagnostic (processor, normallyRestored));
    processor.endUserEdit();
    require (! processor.canUndo() && ! processor.isUserEditActive(),
             "A stale end-gesture committed an edit across a project boundary");
    processor.setUserParameter ("master", .36f);
    processor.beginUserEdit ("Old program gesture");
    processor.setUserParameter ("macro1", .5f);
    processor.setCurrentProgram (2);
    require (! processor.canUndo() && ! processor.canRedo(),
             "Host program selection failed to establish a sound-history boundary");
    processor.endUserEdit();
    require (! processor.isUserEditActive(), "A physical gesture remained open after the program boundary and release");

    processor.setUserParameter ("master", .32f);
    const auto withHistory = save (processor);
    AnyvocAudioProcessor reopened;
    reopened.setStateInformation (withHistory.getData(), static_cast<int> (withHistory.getSize()));
    require (! reopened.canUndo() && ! reopened.canRedo(), "Undo stacks leaked into the DAW project state");
}

void testHistoryReentrantHostCallbacks()
{
    struct OneShotListener final : juce::AudioProcessorParameter::Listener
    {
        OneShotListener (juce::AudioProcessorParameter& p, std::function<void()> f)
            : parameter (p), action (std::move (f)) { parameter.addListener (this); }
        ~OneShotListener() override { parameter.removeListener (this); }
        void parameterValueChanged (int, float) override
        {
            if (fired) return;
            fired = true;
            action();
        }
        void parameterGestureChanged (int, bool) override {}
        juce::AudioProcessorParameter& parameter;
        std::function<void()> action;
        bool fired = false;
    };
    AnyvocAudioProcessor processor;
    auto* cutoff = processor.apvts.getParameter ("cutoff");
    require (cutoff != nullptr, "Missing reentrant callback parameter");
    processor.setUserParameter ("master", .34f);
    {
        OneShotListener listener (*cutoff, [&] { setParameter (processor, "cutoff", 3500); });
        processor.beginUserEdit ("Host overrides this edit");
        processor.setUserParameter ("resonance", .52f);
        processor.setUserParameter ("cutoff", 1200);
        processor.endUserEdit();
        require (listener.fired && std::abs (plainParameter (processor, "cutoff") - 3500) < .01f
                     && std::abs (plainParameter (processor, "resonance") - .52f) < 1.0e-5f
                     && ! processor.canUndo() && ! processor.canRedo(),
                 "A synchronous same-parameter host callback was misclassified as user provenance");
    }
    AnyvocAudioProcessor fixture;
    fixture.setCurrentProgram (14);
    setParameter (fixture, "master", .27f);
    const auto project = save (fixture);
    const auto normallyRestored = normallyRestoredProjectState (project);
    const auto restoreInsideCallback = [&]
    {
        processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
        require (processor.getUndoCount() == 0 && processor.getRedoCount() == 0,
                 "Reentrant project boundary did not clear stale history");
    };
    {
        OneShotListener listener (*cutoff, restoreInsideCallback);
        processor.beginUserEdit ("Reentrant project load");
        processor.setUserParameter ("cutoff", 1600);
        processor.endUserEdit();
        require (listener.fired && save (processor) == normallyRestored && ! processor.isUserEditActive()
                     && ! processor.canUndo() && ! processor.canRedo(),
                 "Finishing an interrupted user write corrupted the newly restored project; listenerFired="
                     + std::to_string (listener.fired) + "; " + historyStateDiagnostic (processor, normallyRestored));
    }
    processor.setUserParameter ("cutoff", 1800);
    {
        OneShotListener listener (*cutoff, restoreInsideCallback);
        const auto result = processor.undoUserEdit();
        require (listener.fired && result.failed() && save (processor) == normallyRestored
                     && ! processor.canUndo() && ! processor.canRedo(),
                 "Undo continued replaying a freed or stale entry after a reentrant project restore; listenerFired="
                     + std::to_string (listener.fired) + ", result=" + result.getErrorMessage().toStdString()
                     + "; " + historyStateDiagnostic (processor, normallyRestored));
    }
    processor.setCurrentProgram (0);
    processor.beginUserEdit ("Paired parameters");
    processor.setUserParameter ("cutoff", 1400);
    processor.setUserParameter ("resonance", .63f);
    processor.endUserEdit();
    {
        OneShotListener listener (*cutoff, [&] { setParameter (processor, "resonance", .91f); });
        const auto result = processor.undoUserEdit();
        require (listener.fired && result.failed() && ! processor.canUndo() && ! processor.canRedo()
                     && std::abs (plainParameter (processor, "resonance") - .91f) < 1.0e-5f,
                 "Undo overwrote a later parameter automated by an earlier replay callback");
    }
    {
        // Reproduce the APVTS cache window without any editor or modulation
        // ring: the new parameter value is already published to this listener,
        // while APVTS may still cache the previous value. Restoring that exact
        // previous value must reconcile the actual parameter, not just its tree.
        AnyvocAudioProcessor direct;
        setParameter (direct, "cutoff", 1000);
        const auto originalProject = save (direct);
        const auto expectedProject = normallyRestoredProjectState (originalProject);
        auto* directCutoff = direct.apvts.getParameter ("cutoff");
        OneShotListener listener (*directCutoff, [&]
        {
            direct.setStateInformation (originalProject.getData(), static_cast<int> (originalProject.getSize()));
        });
        direct.setUserParameter ("cutoff", 2000);
        require (listener.fired && ! direct.isUserEditActive() && ! direct.canUndo() && ! direct.canRedo()
                     && save (direct) == expectedProject,
                 "A direct user setter survived same-initial-value project replacement inside its callback; actual parameter="
                     + juce::String (directCutoff->convertFrom0to1 (directCutoff->getValue()), 12).toStdString()
                     + "; " + historyStateDiagnostic (direct, expectedProject));
    }
}

void testHistoryWorkerPublicationBoundary()
{
    struct BlockingParameterListener final : juce::AudioProcessorParameter::Listener
    {
        explicit BlockingParameterListener (juce::AudioProcessorParameter& p) : parameter (p)
            { parameter.addListener (this); }
        ~BlockingParameterListener() override { parameter.removeListener (this); }
        void parameterValueChanged (int, float) override
        {
            if (fired.exchange (true)) return;
            entered.signal();
            if (! release.wait (5000)) timedOut.store (true);
        }
        void parameterGestureChanged (int, bool) override {}
        juce::AudioProcessorParameter& parameter;
        juce::WaitableEvent entered, release;
        std::atomic<bool> fired { false }, timedOut { false };
    };
    struct ReleaseAndJoin
    {
        ~ReleaseAndJoin() { finish(); }
        void finish()
        {
            gate.release.signal();
            if (worker.joinable()) worker.join();
        }
        BlockingParameterListener& gate;
        std::thread& worker;
    };
    AnyvocAudioProcessor fixture;
    fixture.setCurrentProgram (10); // Carries a prepared sample as well as parameters.
    require (fixture.applyEditedWavetable (2, editedTableFixture(), "Worker table"),
             "Could not create worker publication asset fixture");
    setParameter (fixture, "master", .27f);
    const auto project = save (fixture);
    const auto normallyRestored = normallyRestoredProjectState (project);
    const auto table = fixture.getEditableWavetable (2);
    for (const bool hadPhysicalGesture : { false, true })
    {
        AnyvocAudioProcessor processor;
        processor.setUserParameter ("master", .36f);
        if (hadPhysicalGesture)
        {
            processor.beginUserEdit ("Physical gesture before host restore");
            processor.setUserParameter ("cutoff", 1700);
        }
        auto* parameter = processor.apvts.getParameter ("master");
        require (parameter != nullptr, "Worker publication gate parameter was missing");
        BlockingParameterListener gate (*parameter);
        std::exception_ptr workerError;
        std::thread worker ([&]
        {
            try { processor.setStateInformation (project.getData(), static_cast<int> (project.getSize())); }
            catch (...) { workerError = std::current_exception(); }
        });
        ReleaseAndJoin cleanup { gate, worker };
        require (gate.entered.wait (5000), "Worker restore did not reach the controlled publication boundary");
        // The worker owns the APVTS publication lock here. These UI operations
        // must use the publication guard, not snapshot or wait on that lock.
        require (processor.getUndoCount() == 0 && processor.getRedoCount() == 0
                     && ! processor.canUndo() && ! processor.canRedo()
                     && processor.undoUserEdit().failed() && processor.redoUserEdit().failed(),
                 "History was available against a partially published host project");
        const auto macroBefore = plainParameter (processor, "macro2");
        processor.beginUserEdit ("UI action during publication");
        processor.setUserParameter ("macro2", .91f);
        processor.endUserEdit();
        require (plainParameter (processor, "macro2") == macroBefore,
                 "A UI setter wrote into a project while the host was publishing it");
        cleanup.finish();
        require (! gate.timedOut.load() && workerError == nullptr,
                 "Worker publication deadlocked or threw while UI history was queried");
        if (hadPhysicalGesture) processor.endUserEdit();
        require (! processor.isUserEditActive() && ! processor.canUndo() && ! processor.canRedo()
                     && save (processor) == normallyRestored,
                 "Completion retained mixed-state history or an unbalanced pre-restore physical scope; hadPhysicalGesture="
                     + std::to_string (hadPhysicalGesture) + "; " + historyStateDiagnostic (processor, normallyRestored));
        expectTableAudio (processor.getEditableWavetable (2), table);
        for (int point = 0; point <= 64; ++point)
            require (std::abs (processor.getSamplePreview (0, point / 64.0f)
                              - fixture.getSamplePreview (0, point / 64.0f)) < 5.0e-4f,
                     "Worker restore paired saved asset bytes with the wrong prepared sample");
        processor.setUserParameter ("master", .41f);
        require (processor.getUndoCount() == 1, "History could not resume after publication and gesture release");
        expectHistoryResult (processor.undoUserEdit(), "Undo first post-publication edit");
        require (save (processor) == normallyRestored, "First post-publication undo crossed back into the old project; "
                     + historyStateDiagnostic (processor, normallyRestored));
    }
}

void testHistoryAssetsAndDeletedSources()
{
    PresetTestDirectory files;
    const auto waveFile = files.directory.getChildFile ("Drawn Source.wav");
    const auto sampleFile = files.directory.getChildFile ("Recorded Source.wav");
    const auto presetFile = files.directory.getChildFile ("Embedded Sound.anyvoc");
    AnyvocAudioProcessor fixture;
    require (fixture.applyEditedWavetable (0, editedTableFixture(), "Drawn Source")
                 && fixture.exportWavetable (0, waveFile), "Could not create history wavetable source");
    writeSampleFixture (sampleFile);
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (2);
    const auto initial = save (processor);
    const auto initialRevisions = wavetableRevisions (processor);
    require (processor.importWavetable (0, waveFile), "Could not import history wavetable");
    const auto importedTable = processor.getEditableWavetable (0);
    const auto waveName = processor.getWavetableName (0);
    const auto afterWave = save (processor);
    require (processor.getUndoCount() == 1 && waveFile.deleteFile(), "Wavetable import was not one undoable action");
    const auto importedRevision = processor.getWavetableRevision (0);
    expectHistoryResult (processor.undoUserEdit(), "Undo imported table without its file");
    require (save (processor) == initial && processor.getWavetableRevision (0) > importedRevision
                 && processor.getWavetableRevision (1) == initialRevisions[1]
                 && processor.getWavetableRevision (2) == initialRevisions[2],
             "Table undo failed to restore the old sound or touched unrelated oscillator revisions");
    expectHistoryResult (processor.redoUserEdit(), "Redo imported table without its file");
    expectTableAudio (processor.getEditableWavetable (0), importedTable);
    require (processor.getWavetableName (0) == waveName && save (processor) == afterWave,
             "Table redo depended on reopening the deleted source file");
    require (processor.importSample (1, sampleFile), "Could not import history sample");
    const auto sampleName = processor.getSampleName (1);
    std::array<float, 65> samplePreview {};
    for (int point = 0; point <= 64; ++point)
        samplePreview[static_cast<size_t> (point)] = processor.getSamplePreview (1, point / 64.0f);
    require (sampleFile.deleteFile() && processor.getUndoCount() == 2,
             "Sample import did not form one independent history step");
    expectHistoryResult (processor.undoUserEdit(), "Undo deleted sample source");
    require (save (processor) == afterWave, "Sample undo modified the retained wavetable or parameters");
    expectHistoryResult (processor.redoUserEdit(), "Redo deleted sample source");
    require (processor.getSampleName (1) == sampleName, "Sample redo lost its retained asset identity");
    for (int point = 0; point <= 64; ++point)
        require (processor.getSamplePreview (1, point / 64.0f) == samplePreview[static_cast<size_t> (point)],
                 "Sample redo reconstructed different audio after its source file was removed");
    const auto countBeforeSave = processor.getUndoCount();
    require (processor.savePreset (presetFile) && processor.getUndoCount() == countBeforeSave,
             "Saving a preset unexpectedly created a sound-history step");
    const auto savedSound = save (processor);
    processor.selectFactoryPreset (0);
    require (processor.getUndoCount() == countBeforeSave + 1, "User INIT was not one sound-history step");
    const auto initializedSound = save (processor);
    require (processor.loadPreset (presetFile) && processor.getUndoCount() == countBeforeSave + 2,
             "Explicit preset loading fragmented its parameters and embedded assets");
    require (presetFile.deleteFile(), "Could not remove preset source before undo/redo");
    expectHistoryResult (processor.undoUserEdit(), "Undo preset load after source deletion");
    require (save (processor) == initializedSound, "Undo preset load did not restore the complete prior sound");
    expectHistoryResult (processor.redoUserEdit(), "Redo preset load after source deletion");
    require (save (processor) == savedSound, "Redo preset load depended on the deleted preset/WAV files");
    expectHistoryResult (processor.undoUserEdit(), "Undo load before undoing INIT");
    expectHistoryResult (processor.undoUserEdit(), "Undo INIT");
    require (save (processor) == savedSound, "Undo INIT lost the user sound's embedded assets or identity");
}

void testHistorySaveIdentityAndBudgets()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (2);
    const auto beforeSelection = parameters (processor);
    processor.selectFactoryPreset (14);
    const auto selected = parameters (processor);
    const auto file = files.directory.getChildFile ("Newer Saved Identity.anyvoc");
    require (processor.savePreset (file) && processor.getUndoCount() == 1, "Save polluted preset history");
    const auto fileBytes = file.loadFileAsString();
    const auto savedIdentity = processor.getLoadedPresetId();
    expectHistoryResult (processor.undoUserEdit(), "Undo preset while preserving a newer save identity");
    expectParameters (processor, beforeSelection);
    require (processor.getLoadedPresetId() == savedIdentity, "Preset undo overwrote a newer successful save identity");
    expectHistoryResult (processor.redoUserEdit(), "Redo preset while preserving a newer save identity");
    expectParameters (processor, selected);
    require (processor.getLoadedPresetId() == savedIdentity && file.loadFileAsString() == fileBytes,
             "History replay rewound the saved identity or modified the preset file");
    processor.setUserParameter ("master", .29f);
    const auto otherFile = files.directory.getChildFile ("Latest Save.anyvoc");
    require (processor.savePreset (otherFile), "Could not save scalar-history identity fixture");
    const auto latestIdentity = processor.getLoadedPresetId();
    expectHistoryResult (processor.undoUserEdit(), "Undo scalar after Save As");
    require (processor.getLoadedPresetId() == latestIdentity && otherFile.existsAsFile(),
             "Scalar undo reversed Save As identity or deleted its file");

    AnyvocAudioProcessor bounded;
    bounded.setHistoryLimits (3, 32u * 1024u * 1024u);
    for (int index = 0; index < 6; ++index) bounded.setUserParameter ("master", .2f + .1f * index);
    require (bounded.getUndoCount() == 3, "History count limit did not evict complete oldest actions");
    for (int index = 0; index < 3; ++index) expectHistoryResult (bounded.undoUserEdit(), "Undo bounded history");
    require (std::abs (plainParameter (bounded, "master") - .4f) < 1.0e-5f && ! bounded.canUndo(),
             "Bounded history retained the wrong oldest undo boundary");
    for (int index = 0; index < 3; ++index) expectHistoryResult (bounded.redoUserEdit(), "Redo bounded history");
    require (std::abs (plainParameter (bounded, "master") - .7f) < 1.0e-5f,
             "Evicting history corrupted the retained redo sequence");

    const auto sampleFile = files.directory.getChildFile ("Memory Source.wav");
    writeSampleFixture (sampleFile);
    AnyvocAudioProcessor memory;
    require (memory.importSample (0, sampleFile), "Could not import history memory fixture");
    const auto assetBytes = memory.getHistoryBytes();
    require (assetBytes >= 19200u * 2u * sizeof (float),
             "History budget omitted the prepared 48kHz stereo sample payload");
    for (int index = 0; index < 40; ++index) memory.setUserParameter ("master", .2f + .01f * index);
    require (memory.getUndoCount() == 41 && memory.getHistoryBytes() < assetBytes * 2,
             "Scalar edits repeatedly charged or copied the same retained sample payload");
    memory.setHistoryLimits (64, 1024);
    require (memory.getHistoryBytes() <= 1024, "Reducing the byte budget retained excessive asset history");
    require (memory.importSample (1, sampleFile) && memory.getSampleName (1).isNotEmpty()
                 && ! memory.canUndo() && ! memory.canRedo() && memory.getHistoryBytes() <= 1024,
             "Oversized sound edit failed or left a partially undoable history action");
}

void testHistoryAudioContinuity()
{
    for (const int program : { 2, 10, 11 })
    {
        AnyvocAudioProcessor processor, reference;
        processor.setCurrentProgram (program);
        audibleSettings (processor);
        setParameter (processor, "ampRelease", .3f);
        setParameter (processor, "lfo1Mode", 1);
        setParameter (processor, "lfo1Rate", 1.3f);
        setParameter (processor, "osc1Loop", 1);
        const auto originalCutoff = plainParameter (processor, "cutoff");
        const auto sound = save (processor);
        // Decode the same embedded assets on both sides; this isolates history
        // continuity from the factory sample's initial PCM encoding precision.
        processor.setStateInformation (sound.getData(), static_cast<int> (sound.getSize()));
        reference.setStateInformation (sound.getData(), static_cast<int> (sound.getSize()));
        processor.prepareToPlay (48000, 128); reference.prepareToPlay (48000, 128);
        render (processor, 4096); render (reference, 4096);
        const std::vector<Event> onset { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
        render (processor, 8192, onset); render (reference, 8192, onset);
        const auto revisions = wavetableRevisions (processor);
        const auto compare = [&] (const char* stage)
        {
            const auto actual = render (processor, 4096, {}, 127);
            const auto expected = render (reference, 4096, {}, 127);
            double maximumError = 0;
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < actual.getNumSamples(); ++sample)
                    maximumError = std::max (maximumError, static_cast<double> (std::abs (
                        actual.getSample (channel, sample) - expected.getSample (channel, sample))));
            require (rms (expected) > 1.0e-4 && maximumError < 2.0e-6
                         && std::abs (processor.getLfoPhase (0) - reference.getLfoPhase (0)) < 1.0e-6f,
                     std::string (stage) + " reset or discontinuously changed active source mode "
                         + std::to_string (program) + "; audio delta=" + std::to_string (maximumError));
            require (wavetableRevisions (processor) == revisions,
                     "Parameter-only history replay republished unrelated wavetable assets");
        };
        processor.setUserParameter ("cutoff", 1100); setParameter (reference, "cutoff", 1100);
        compare ("User change");
        expectHistoryResult (processor.undoUserEdit(), "Undo while source is sounding");
        setParameter (reference, "cutoff", originalCutoff);
        compare ("Undo");
        expectHistoryResult (processor.redoUserEdit(), "Redo while source is sounding");
        setParameter (reference, "cutoff", 1100);
        compare ("Redo");
    }
}

void refreshEditorTimers()
{
    // These tests use offscreen components, without a native application loop.
    // First acknowledge an older posted callback: JUCE's timer thread may be
    // waiting for it before it advances a newly created editor's countdown.
    juce::Timer::callPendingTimersSynchronously();
    for (int tick = 0; tick < 12; ++tick)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
}

void testHistoryUiProvenanceAndGestures()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                              files.directory.getChildFile ("Settings.xml"));
    editor->setVisible (true); editor->setSize (1120, 760);
    auto* undo = findComponent<juce::Button> (*editor, "soundUndo");
    auto* redo = findComponent<juce::Button> (*editor, "soundRedo");
    auto* cutoff = findComponent<juce::Slider> (*editor, "cutoff");
    require (undo != nullptr && redo != nullptr && cutoff != nullptr
                 && static_cast<bool> (undo->onClick) && static_cast<bool> (redo->onClick)
                 && static_cast<bool> (cutoff->onDragStart) && static_cast<bool> (cutoff->onDragEnd),
             "History header actions or the slider's real gesture callbacks were unavailable");
    require (! undo->isEnabled() && ! redo->isEnabled() && ! processor.canUndo(),
             "Opening the editor initialized spurious sound history");
    const auto initialCutoff = plainParameter (processor, "cutoff");
    cutoff->onDragStart();
    for (int step = 0; step < 50; ++step) cutoff->setValue (900 + step * 30, juce::sendNotificationSync);
    require (processor.isUserEditActive() && ! processor.canUndo(),
             "The GUI slider did not hold one transaction for the physical drag");
    cutoff->onDragEnd();
    require (processor.getUndoCount() == 1 && std::abs (plainParameter (processor, "cutoff") - 2370) < .01f,
             "Fifty GUI drag updates were not one undoable parameter change");
    setParameter (processor, "master", .38f);
    auto* master = findComponent<juce::Slider> (*editor, "master");
    require (master != nullptr && std::abs (master->getValue() - .38) < 1.0e-5
                 && processor.getUndoCount() == 1,
             "Host-to-GUI synchronization became a user action or left the knob stale");
    refreshEditorTimers();
    require (undo->isEnabled() && ! redo->isEnabled(), "History header did not reflect a completed edit");
    undo->onClick();
    require (std::abs (plainParameter (processor, "cutoff") - initialCutoff) < .01f
                 && std::abs (plainParameter (processor, "master") - .38f) < 1.0e-5f
                 && processor.getUndoCount() == 0 && processor.getRedoCount() == 1,
             "The header Undo action changed unrelated host state or was recorded again by its UI attachment");
    refreshEditorTimers();
    require (! undo->isEnabled() && redo->isEnabled(), "Header buttons did not switch availability after undo");
    redo->onClick();
    require (processor.getUndoCount() == 1 && processor.getRedoCount() == 0,
             "Header redo recursively generated user edits through parameter attachments");

    // Exercise the real editor dispatcher with explicit JUCE modifiers. This
    // checks shortcut semantics independently of native input-injection timing.
    const juce::KeyPress undoKey ('Z', juce::ModifierKeys::ctrlModifier, 0);
    const juce::KeyPress redoKey ('Y', juce::ModifierKeys::ctrlModifier, 0);
    const juce::KeyPress shiftRedoKey ('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
    const auto expectShortcutState = [&] (bool undone)
    {
        require (processor.getUndoCount() == (undone ? 0 : 1)
                     && processor.getRedoCount() == (undone ? 1 : 0)
                     && std::abs (plainParameter (processor, "cutoff") - (undone ? initialCutoff : 2370)) < .01f
                     && std::abs (plainParameter (processor, "master") - .38f) < 1.0e-5f,
                 "Keyboard history dispatch changed the wrong action or unrelated host automation");
    };
    require (editor->keyPressed (undoKey), "Editor did not recognize Ctrl+Z");
    expectShortcutState (true);
    require (editor->keyPressed (redoKey), "Editor did not recognize Ctrl+Y");
    expectShortcutState (false);
    require (editor->keyPressed (undoKey) && editor->keyPressed (shiftRedoKey),
             "Editor did not recognize Ctrl+Shift+Z after undo");
    expectShortcutState (false);
    const auto beforeOtherKeys = save (processor);
    require (! editor->keyPressed (juce::KeyPress ('Z', juce::ModifierKeys{}, 0))
                 && ! editor->keyPressed (juce::KeyPress ('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0))
                 && ! editor->keyPressed (juce::KeyPress ('S', juce::ModifierKeys::ctrlModifier, 0))
                 && save (processor) == beforeOtherKeys,
             "The history dispatcher consumed an unmodified, Alt-modified or unrelated shortcut");
    // No native peer is created by this offscreen suite. JUCE cannot give a
    // TextEditor genuine keyboard focus here, so this deliberately does not
    // claim to cover OS event routing or the focused TextEditor's own undo.

    const auto click = [&] (const char* text)
    {
        auto* button = findButton (*editor, text);
        require (button != nullptr && static_cast<bool> (button->onClick),
                 std::string ("Missing history UI action: ") + text);
        button->onClick();
    };
    click ("VOICING");
    auto* mode = findComponent<juce::ComboBox> (*editor, "voiceModeChoice");
    auto* always = findComponent<juce::Button> (*editor, "glideAlways");
    require (mode != nullptr && always != nullptr, "History voicing controls were missing");
    mode->setSelectedId (3, juce::sendNotificationSync);
    require (processor.getUndoCount() == 2 && plainParameter (processor, "voiceMode") == 2,
             "A GUI choice change was not recorded exactly once");
    always->setToggleState (true, juce::sendNotificationSync);
    require (processor.getUndoCount() == 3 && plainParameter (processor, "glideAlways") == 1,
             "A GUI toggle change was not recorded exactly once");
    expectHistoryResult (processor.undoUserEdit(), "Undo GUI toggle");
    require (! always->getToggleState() && processor.getUndoCount() == 2,
             "Toggle undo did not refresh the control without creating another action");
    click ("DONE"); click ("MOD"); click ("EDIT LFO 1");
    const auto beforeCurve = parameters (processor);
    const int beforeCurveCount = processor.getUndoCount();
    click ("RAMP");
    require (processor.getUndoCount() == beforeCurveCount + 1 && processor.getLfoShape (0) == 5,
             "The GUI LFO curve command did not group shape and point changes into one action");
    expectHistoryResult (processor.undoUserEdit(), "Undo GUI LFO curve");
    expectParameters (processor, beforeCurve);
    click ("DONE"); click ("SYNTH");
    cutoff = findComponent<juce::Slider> (*editor, "cutoff");
    require (cutoff != nullptr, "Cutoff slider disappeared after closing modulation details");
    const int beforeClose = processor.getUndoCount();
    cutoff->onDragStart(); cutoff->setValue (4100, juce::sendNotificationSync);
    const auto activeSound = save (processor);
    const int activeUndoCount = processor.getUndoCount(), activeRedoCount = processor.getRedoCount();
    for (const auto& key : { undoKey, redoKey, shiftRedoKey })
        require (editor->keyPressed (key) && processor.isUserEditActive()
                     && processor.getUndoCount() == activeUndoCount && processor.getRedoCount() == activeRedoCount
                     && save (processor) == activeSound,
                 "A recognized history shortcut replayed or committed an unfinished slider drag");
    editor.reset();
    require (! processor.isUserEditActive() && processor.getUndoCount() == beforeClose + 1,
             "Closing an editor mid-drag left an unfinishable history transaction");
}

void testHistoryWavetableRevisionAndStaleLab()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    const auto original = editedTableFixture();
    require (processor.applyEditedWavetable (0, original, "Unchanged Name"), "Could not create history Lab fixture");
    const auto project = save (processor);
    processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                              files.directory.getChildFile ("Settings.xml"));
    editor->setVisible (true); editor->setSize (1120, 760);
    auto* edit = findButton (*editor, "EDIT");
    require (edit != nullptr && static_cast<bool> (edit->onClick), "Wavetable Lab was unavailable for history test");
    edit->onClick();
    auto* apply = findButton (*editor, "APPLY");
    require (apply != nullptr && static_cast<bool> (apply->onClick), "Wavetable Lab apply action was unavailable");
    auto replacement = original;
    replacement.applyGain (.4f);
    require (processor.applyEditedWavetable (0, replacement, "Unchanged Name"), "Could not edit table with Lab open");
    const auto changedRevision = processor.getWavetableRevision (0);
    expectHistoryResult (processor.undoUserEdit(), "Undo table while its older Lab remains open");
    const auto undoneRevision = processor.getWavetableRevision (0);
    require (undoneRevision > changedRevision, "Table undo did not publish a fresh revision for the same asset name");
    expectTableAudio (processor.getEditableWavetable (0), original);
    const auto count = processor.getUndoCount();
    const auto redoCount = processor.getRedoCount();
    apply->onClick();
    require (processor.getWavetableRevision (0) == undoneRevision && processor.getUndoCount() == count
                 && processor.getRedoCount() == redoCount,
             "An obsolete Lab republished its snapshot or destroyed redo after global table undo");
    expectHistoryResult (processor.redoUserEdit(), "Redo table after rejected stale Lab apply");
    expectTableAudio (processor.getEditableWavetable (0), replacement);
}

struct MeasuredHarmonic
{
    double sine = 0, cosine = 0;
    double amplitude() const { return std::hypot (sine, cosine); }
    double phase() const { return std::atan2 (cosine, sine) / juce::MathConstants<double>::twoPi; }
};

MeasuredHarmonic measureHarmonic (const juce::AudioBuffer<float>& audio, int frame, int harmonic)
{
    constexpr int length = anyvoc::WavetableDocument::frameLength;
    require (audio.getNumChannels() == 1 && audio.getNumSamples() >= (frame + 1) * length,
             "Spectral integration fixture was not canonical mono audio");
    MeasuredHarmonic result;
    for (int sample = 0; sample < length; ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi * harmonic * sample / length;
        const auto value = audio.getSample (0, frame * length + sample);
        result.sine += value * std::sin (angle) * 2.0 / length;
        result.cosine += value * std::cos (angle) * 2.0 / length;
    }
    return result;
}

void expectHarmonic (const juce::AudioBuffer<float>& audio, int frame, int harmonic,
                     double amplitude, double phase)
{
    const auto measured = measureHarmonic (audio, frame, harmonic);
    require (std::abs (measured.amplitude() - amplitude) < 2.0e-5
                 && (amplitude < 1.0e-6 || std::abs (std::remainder (measured.phase() - phase, 1.0)) < 2.0e-5),
             "Independent harmonic projection mismatch: frame=" + std::to_string (frame)
                 + " harmonic=" + std::to_string (harmonic) + " amplitude=" + std::to_string (measured.amplitude())
                 + " phase=" + std::to_string (measured.phase()));
}

juce::AudioBuffer<float> spectralIntegrationFixture()
{
    constexpr int length = anyvoc::WavetableDocument::frameLength;
    juce::AudioBuffer<float> audio (1, 3 * length);
    for (int frame = 0; frame < 3; ++frame)
        for (int sample = 0; sample < length; ++sample)
        {
            const auto turn = static_cast<double> (sample) / length;
            const auto sine = [&] (int harmonic, double phase)
                { return std::sin (juce::MathConstants<double>::twoPi * (harmonic * turn + phase)); };
            audio.setSample (0, frame * length + sample, static_cast<float> (
                .2 * sine (1, .1 + frame * .05) + .15 * sine (7, -.2)
                + .09 * sine (127, .3) + .07 * sine (961, .1) + .09 * sine (1023, -.3)));
        }
    return audio;
}

void testSpectralAssetsThroughProcessorAndFiles()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    auto initial = spectralIntegrationFixture();
    // A phase-only edit must also preserve a partial above unity through the
    // floating-point asset path. Additive peak normalization is not implicit.
    for (int sample = 0; sample < 2048; ++sample)
        initial.addSample (0, sample, static_cast<float> (.97 * std::sin (
            juce::MathConstants<double>::twoPi * (7.0 * sample / 2048 - .2))));
    require (processor.applyEditedWavetable (0, initial, "Phase original"), "Could not install phase integration fixture");
    const auto cleanState = save (processor);
    processor.setStateInformation (cleanState.getData(), static_cast<int> (cleanState.getSize()));
    anyvoc::WavetableDocument document;
    require (document.load (processor.getEditableWavetable (0)), "Could not load phase editor model");
    require (document.setHarmonics (0, {
        { 1, .32f, .23f, true, true }, { 7, 0, -.18f, false, true },
        { 127, 0, -.41f, false, true }, { 1023, .16f, .37f, true, true }
    }), "Could not edit independent harmonic magnitudes and phases");
    require (processor.applyEditedWavetable (0, document.data(), "Phase edited") && processor.getUndoCount() == 1,
             "Applying one spectral batch was not one global sound edit");
    const auto check = [&] (AnyvocAudioProcessor& target, int oscillator)
    {
        const auto wave = target.getEditableWavetable (oscillator);
        expectHarmonic (wave, 0, 1, .32, .23);
        expectHarmonic (wave, 0, 7, 1.12, -.18);
        expectHarmonic (wave, 0, 127, .09, -.41);
        expectHarmonic (wave, 0, 961, .07, .1);
        expectHarmonic (wave, 0, 1023, .16, .37);
        expectHarmonic (wave, 1, 1023, .09, -.3);
        expectHarmonic (wave, 2, 1, .2, .2);
    };
    check (processor, 0);
    const auto state = save (processor);
    AnyvocAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (restored, 0);
    expectParameters (restored, parameters (processor));
    const auto preset = files.directory.getChildFile ("Phase persistence.anyvoc");
    const auto waveFile = files.directory.getChildFile ("Phase persistence.wav");
    require (processor.savePreset (preset) && restored.loadPreset (preset) && preset.deleteFile(),
             "Could not save/load/delete self-contained phase preset");
    check (restored, 0);
    require (processor.exportWavetable (0, waveFile) && restored.importWavetable (1, waveFile)
                 && waveFile.deleteFile(), "Could not export/import/delete spectral WAV source");
    check (restored, 1);
    expectHistoryResult (restored.undoUserEdit(), "Undo imported spectral WAV after deleting its file");
    check (restored, 0);
    expectHistoryResult (restored.redoUserEdit(), "Redo imported spectral WAV after deleting its file");
    check (restored, 1);
    expectHistoryResult (processor.undoUserEdit(), "Undo phase edit after Save As");
    expectHarmonic (processor.getEditableWavetable (0), 0, 1, .2, .1);
    expectHarmonic (processor.getEditableWavetable (0), 0, 7, 1.12, -.2);
    expectHistoryResult (processor.redoUserEdit(), "Redo phase edit after Save As");
    check (processor, 0);
}

void clickVisible (juce::Component& editor, const char* text)
{
    auto* button = findButton (editor, text);
    require (button != nullptr && static_cast<bool> (button->onClick),
             std::string ("Missing spectral editor action: ") + text);
    button->onClick();
}

void paintSpectralBin (juce::Component& canvas, int page, int harmonic, double value, bool phase)
{
    const int bins = std::min (64, 1023 - page * 64);
    const int local = harmonic - (page * 64 + 1);
    require (local >= 0 && local < bins, "Spectral test targeted a bin outside the visible page");
    const float x = 48.0f + (static_cast<float> (local) + .5f) / bins * (canvas.getWidth() - 62.0f);
    const float fraction = static_cast<float> (phase ? value + .5 : value);
    const juce::Point<float> point { x, 22.0f + (1 - fraction) * (canvas.getHeight() - 42.0f) };
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &canvas, &canvas, now, point, now, 1, false);
    canvas.mouseDown (event);
    canvas.mouseUp (event);
}

void testSpectralEditorPagesAndPhaseHistory()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    setParameter (processor, "osc1Position", 0);
    require (processor.applyEditedWavetable (0, spectralIntegrationFixture(), "High harmonics"),
             "Could not install spectral GUI fixture");
    const auto state = save (processor);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    std::unique_ptr<AnyvocAudioProcessorEditor> editor;
    const auto open = [&]
    {
        editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                            files.directory.getChildFile ("Settings.xml"));
        editor->setVisible (true); editor->setSize (1120, 760);
        clickVisible (*editor, "EDIT");
    };
    const auto selectPage = [&] (const char* view)
    {
        clickVisible (*editor, view);
        auto* page = findComponent<juce::ComboBox> (*editor, "wavetableHarmonicPage");
        require (page != nullptr && page->getNumItems() == 16, "All 1023 harmonic bins were not reachable by paging");
        page->setSelectedId (16, juce::sendNotificationSync);
    };
    open();
    const auto beforeNavigation = save (processor);
    const auto revisions = wavetableRevisions (processor);
    selectPage ("PHASE");
    auto* mode = findComponent<juce::ComboBox> (*editor, "wavetableMorphMode");
    require (mode != nullptr, "Spectral morph mode navigation was unavailable");
    mode->setSelectedId (2, juce::sendNotificationSync);
    require (save (processor) == beforeNavigation && ! processor.canUndo()
                 && wavetableRevisions (processor) == revisions,
             "Phase view, harmonic page or morph-mode navigation changed the sound/history");
    auto* canvas = findComponent<juce::Component> (*editor, "osc1WavetableCanvas");
    require (canvas != nullptr, "High-frequency phase canvas was unavailable");
    paintSpectralBin (*canvas, 15, 1023, .25, true);
    require (processor.getUndoCount() == 1 && ! processor.isUserEditActive(),
             "A highest-bin phase stroke did not commit one complete sound edit");
    expectHarmonic (processor.getEditableWavetable (0), 0, 1023, .09, .25);
    expectHarmonic (processor.getEditableWavetable (0), 0, 961, .07, .1);
    auto* reset = findComponent<juce::Button> (*editor, "wavetablePhaseReset");
    require (reset != nullptr && static_cast<bool> (reset->onClick), "Current-page phase reset action was unavailable");
    reset->onClick();
    require (processor.getUndoCount() == 2, "Resetting the current phase page fragmented history");
    const auto resetWave = processor.getEditableWavetable (0);
    expectHarmonic (resetWave, 0, 961, .07, 0);
    expectHarmonic (resetWave, 0, 1023, .09, 0);
    expectHarmonic (resetWave, 0, 1, .2, .1);
    expectHarmonic (resetWave, 0, 127, .09, .3);
    const auto resetRevision = processor.getWavetableRevision (0);
    reset->onClick();
    require (processor.getUndoCount() == 2 && processor.getWavetableRevision (0) == resetRevision,
             "Repeating a phase reset published an empty sound edit");
    expectHistoryResult (processor.undoUserEdit(), "Undo page-only phase reset");
    editor.reset(); open();
    selectPage ("HARMONICS");
    require (processor.getUndoCount() == 1 && processor.getRedoCount() == 1,
             "Reopening and paging the spectral editor discarded redo history");
    canvas = findComponent<juce::Component> (*editor, "osc1WavetableCanvas");
    require (canvas != nullptr, "High-frequency magnitude canvas was unavailable");
    paintSpectralBin (*canvas, 15, 1023, .4, false);
    require (processor.getUndoCount() == 2 && ! processor.canRedo(), "A high-page amplitude edit did not form a new branch");
    expectHarmonic (processor.getEditableWavetable (0), 0, 1023, .4, .25);
    selectPage ("PHASE");
    paintSpectralBin (*canvas, 15, 1000, .25, true);
    require (processor.getUndoCount() == 2, "A phase-only stroke gave a silent bin energy or created empty history");
    expectHarmonic (processor.getEditableWavetable (0), 0, 1000, 0, 0);
    auto* apply = findButton (*editor, "APPLY");
    require (apply != nullptr && static_cast<bool> (apply->onClick), "Spectral Lab apply action was missing");
    expectHistoryResult (processor.undoUserEdit(), "Undo high-frequency edit while the old Lab is open");
    const auto afterUndo = save (processor);
    const auto revision = processor.getWavetableRevision (0);
    apply->onClick();
    require (save (processor) == afterUndo && processor.getWavetableRevision (0) == revision
                 && processor.getUndoCount() == 1 && processor.getRedoCount() == 1,
             "A stale spectral Lab overwrote a globally undone sound");
}

juce::AudioBuffer<float> oppositePhaseMorphFixture()
{
    juce::AudioBuffer<float> audio (1, 3 * 2048);
    for (int sample = 0; sample < 2048; ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi * sample / 2048;
        const auto endpoint = static_cast<float> (.4 * std::sin (angle) + .15 * std::sin (3 * angle));
        audio.setSample (0, sample, endpoint);
        audio.setSample (0, 2048 + sample, static_cast<float> (.2 * std::cos (2 * angle)));
        audio.setSample (0, 4096 + sample, -endpoint);
    }
    return audio;
}

void testSpectralMorphUiIntegration()
{
    PresetTestDirectory files;
    for (const int modeId : { 1, 2 })
    {
        AnyvocAudioProcessor processor;
        setParameter (processor, "osc1Position", 0);
        require (processor.applyEditedWavetable (0, oppositePhaseMorphFixture(), "Opposite phase anchors"),
                 "Could not install spectral morph GUI fixture");
        const auto state = save (processor);
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const auto before = processor.getEditableWavetable (0);
        auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                                files.directory.getChildFile ("Settings.xml"));
        editor->setVisible (true); editor->setSize (1120, 760); clickVisible (*editor, "EDIT");
        auto* mode = findComponent<juce::ComboBox> (*editor, "wavetableMorphMode");
        auto* from = findComponent<juce::ComboBox> (*editor, "wavetableMorphFrom");
        auto* to = findComponent<juce::ComboBox> (*editor, "wavetableMorphTo");
        require (mode != nullptr && from != nullptr && to != nullptr, "Spectral morph controls were unavailable");
        const auto navigationState = save (processor);
        mode->setSelectedId (modeId, juce::sendNotificationSync);
        from->setSelectedId (1, juce::sendNotificationSync); to->setSelectedId (3, juce::sendNotificationSync);
        require (save (processor) == navigationState && ! processor.canUndo(),
                 "Selecting morph mode or anchor indices changed the instrument");
        clickVisible (*editor, "MORPH");
        require (processor.getUndoCount() == 1, "One morph operation did not group all altered frames into one sound edit");
        const auto after = processor.getEditableWavetable (0);
        for (int sample = 0; sample < 2048; ++sample)
            require (std::abs (after.getSample (0, sample) - before.getSample (0, sample)) < 2.0e-6f
                         && std::abs (after.getSample (0, 4096 + sample) - before.getSample (0, 4096 + sample)) < 2.0e-6f,
                     "MORPH changed an anchor frame");
        if (modeId == 1)
            require (after.getRMSLevel (0, 2048, 2048) < 1.0e-6f,
                     "Waveform morph failed to cancel exactly opposite endpoint waves");
        else
        {
            expectHarmonic (after, 1, 1, .4, -.25);
            expectHarmonic (after, 1, 3, .15, -.25);
            require (after.getRMSLevel (0, 2048, 2048) > .29f,
                     "Spectral interpolation canceled the opposite-phase midpoint instead of preserving partial energy");
        }
        expectHistoryResult (processor.undoUserEdit(), "Undo complete morph operation");
        expectTableAudio (processor.getEditableWavetable (0), before);
        expectHistoryResult (processor.redoUserEdit(), "Redo complete morph operation");
        expectTableAudio (processor.getEditableWavetable (0), after);
    }
}

juce::MouseEvent modulationMouseEvent (juce::Component& component, juce::Point<float> position,
                                       juce::Point<float> origin, int clicks = 1, bool fine = false)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), position,
             juce::ModifierKeys::leftButtonModifier | (fine ? juce::ModifierKeys::shiftModifier : 0), 1, 0, 0, 0, 0,
             &component, &component, now, origin, now, clicks, position != origin };
}

juce::Point<float> modulationRingPoint (juce::Component& ring)
{
    // Discover a real annular input point rather than duplicating the renderer's
    // radius formula. The control must leave the knob's interior available.
    for (int y = 0; y < ring.getHeight(); ++y)
        if (ring.hitTest (ring.getWidth() / 2, y))
            return { static_cast<float> (ring.getWidth() / 2), static_cast<float> (y) };
    throw std::runtime_error ("Selected modulation ring had no usable annular hit target");
}

bool modulationControlVisible (const juce::Component* component)
{
    // Offscreen integration editors deliberately have no native peer. JUCE's
    // isShowing() requires one; visibility through the ancestor chain is the
    // relevant condition for these direct event tests and PNG captures.
    if (component == nullptr) return false;
    for (auto* parent = component; parent != nullptr; parent = parent->getParentComponent())
        if (! parent->isVisible()) return false;
    return true;
}

void selectModulationSource (juce::Component& editor, int source)
{
    auto* chip = findComponent<juce::Component> (editor, "modSource" + juce::String (source));
    require (modulationControlVisible (chip), "Modulation source selection chip was missing");
    const auto point = chip->getLocalBounds().toFloat().getCentre();
    const auto event = modulationMouseEvent (*chip, point, point);
    chip->mouseDown (event); chip->mouseUp (event);
}

struct ModulationGestureProbe final : juce::AudioProcessorParameter::Listener
{
    explicit ModulationGestureProbe (juce::AudioProcessorParameter& p) : parameter (p)
        { parameter.addListener (this); }
    ~ModulationGestureProbe() override { parameter.removeListener (this); }
    void parameterValueChanged (int, float) override
    {
        ++values;
        if (onValue && ! valueFired) { valueFired = true; onValue(); }
    }
    void parameterGestureChanged (int, bool starting) override
    {
        if (starting)
        {
            ++starts;
            if (onStart && ! startFired) { startFired = true; onStart(); }
        }
        else
        {
            ++ends;
            if (onEnd && ! endFired) { endFired = true; onEnd(); }
        }
    }
    juce::AudioProcessorParameter& parameter;
    std::function<void()> onStart, onValue, onEnd;
    int starts = 0, ends = 0, values = 0;
    bool startFired = false, valueFired = false, endFired = false;
};

void installModulationRingFixture (AnyvocAudioProcessor& processor)
{
    processor.setCurrentProgram (0);
    setParameter (processor, "cutoff", 1200);
    setParameter (processor, "mod1Source", 1);
    setParameter (processor, "mod1Dest", 1);
    setParameter (processor, "mod1Amount", .25f);
    const auto clean = save (processor);
    processor.setStateInformation (clean.getData(), static_cast<int> (clean.getSize()));
}

void testModulationNominalRanges()
{
    const auto expect = [] (int destination, float base, int source, float amount, float low, float high)
    {
        const auto range = anyvoc::modulationRange (destination, base, source, amount);
        const auto tolerance = 1.0e-5f * std::max (1.0f, std::max (std::abs (low), std::abs (high)));
        require (std::abs (range.first - low) < tolerance && std::abs (range.second - high) < tolerance,
                 "Nominal modulation range disagreed with independently derived DSP endpoints");
    };
    // +/- 20% is +/- one octave for an LFO cutoff route, while a unipolar
    // source reaches from the base to only one of those limits.
    expect (1, 1000, 1, .2f, 500, 2000);
    expect (8, 1000, 3, -.2f, 500, 1000);
    expect (8, 1000, 6, .2f, 1000, 2000);
    expect (2, .4f, 2, -.3f, .1f, .7f);
    expect (11, .5f, 9, -.3f, .2f, .5f);
    // The model reports unclipped endpoints; clipping belongs to the display.
    expect (12, .9f, 1, .4f, .5f, 1.3f);
    expect (9, .8f, 3, .4f, .8f, 1.2f);
}

void testModulationRingSelectionAndHistory()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor, reference;
    // A duplicate pair is legal through the matrix. The first row must be the
    // deterministic edit target; its displayed amount must not be a row sum.
    for (auto* target : { &processor, &reference })
    {
        installModulationRingFixture (*target);
        setParameter (*target, "mod2Source", 1); setParameter (*target, "mod2Dest", 1);
        setParameter (*target, "mod2Amount", -.37f);
        setParameter (*target, "mod3Source", 6); setParameter (*target, "mod3Dest", 2);
        setParameter (*target, "mod3Amount", .4f);
    }
    const auto sharedFixture = save (processor);
    processor.setStateInformation (sharedFixture.getData(), static_cast<int> (sharedFixture.getSize()));
    reference.setStateInformation (sharedFixture.getData(), static_cast<int> (sharedFixture.getSize()));
    require (parameters (processor) == parameters (reference),
             "Symmetric source-selection fixtures had different parameters before opening the editor; "
                 + historyStateDiagnostic (processor, save (reference)));
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                              files.directory.getChildFile ("Settings.xml"));
    editor->setVisible (true); editor->setSize (1120, 760);
    const auto before = save (processor);
    const auto revision = processor.getHistoryRevision();
    require (parameters (processor) == parameters (reference),
             "Opening the editor changed source-selection reference parameters; "
                 + historyStateDiagnostic (processor, save (reference)));
    processor.prepareToPlay (48000, 128); reference.prepareToPlay (48000, 128);
    const std::vector<Event> onset { { 0, juce::MidiMessage::noteOn (1, 60, .75f) } };
    render (processor, 2048, onset); render (reference, 2048, onset);
    for (const int source : { 2, 3, 6, 7, 8, 9, 1 }) selectModulationSource (*editor, source);
    require (save (processor) == before && processor.getHistoryRevision() == revision
                 && ! processor.canUndo(), "Selecting modulation sources modified sound or history");
    const auto actualAudio = render (processor, 2048), referenceAudio = render (reference, 2048);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < actualAudio.getNumSamples(); ++sample)
            require (actualAudio.getSample (channel, sample) == referenceAudio.getSample (channel, sample),
                     "Source selection interrupted or changed an active note");
    // The shared lookup intentionally searches only visible components. Capture
    // the macro ring while selected, then check it hides when returning to LFO 1.
    selectModulationSource (*editor, 6);
    auto* positionRing = findComponent<juce::Component> (*editor, "osc1PositionModulationRing");
    selectModulationSource (*editor, 1);
    auto* ring = findComponent<juce::Component> (*editor, "cutoffModulationRing");
    require (modulationControlVisible (ring) && positionRing != nullptr && ! positionRing->isVisible(),
             "Selected source did not isolate its routed control rings");
    require (ring->getTitle().contains ("1") && ring->getTitle().containsIgnoreCase ("LFO"),
             "Selected ring did not identify its source and matrix row");
    const auto point = modulationRingPoint (*ring);
    require (! ring->hitTest (ring->getWidth() / 2, juce::roundToInt (point.y) + 12),
             "Modulation ring stole input from the base knob interior");
    auto* baseControl = findComponent<juce::Slider> (*editor, "cutoff");
    require (baseControl != nullptr, "Modulation ring lost its base control");
    const auto baseArea = ring->getLocalArea (baseControl, baseControl->getLookAndFeel().getSliderLayout (*baseControl).sliderBounds);
    const auto centre = baseArea.toFloat().getCentre();
    const auto radius = centre.getDistanceFrom (point.translated (0, 2));
    const auto gap = centre.getPointOnCircumference (radius, juce::MathConstants<float>::pi * 1.125f);
    require (! ring->hitTest (juce::roundToInt (gap.x), juce::roundToInt (gap.y)),
             "The unpainted lower-left gap intercepted input outside the ring's 270-degree arc");
    auto* amount = processor.apvts.getParameter ("mod1Amount");
    require (amount != nullptr, "Modulation amount parameter missing");
    ModulationGestureProbe probe (*amount);
    const auto base = plainParameter (processor, "cutoff");
    ring->mouseDown (modulationMouseEvent (*ring, point, point));
    for (int step = 1; step <= 50; ++step)
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -.5f * step), point));
    require (processor.isUserEditActive() && ! processor.canUndo(),
             "Modulation depth drag did not retain one active sound transaction");
    ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -25), point));
    const auto edited = plainParameter (processor, "mod1Amount");
    require (std::abs (edited - .25f) > .01f && plainParameter (processor, "cutoff") == base
                 && std::abs (plainParameter (processor, "mod2Amount") + .37f) < 1.0e-6f
                 && processor.getUndoCount() == 1 && ! processor.isUserEditActive()
                 && probe.starts == 1 && probe.ends == 1,
             "A ring drag changed its base/duplicate route or fragmented history/host gestures");
    expectHistoryResult (processor.undoUserEdit(), "Undo one complete modulation-ring drag");
    require (std::abs (plainParameter (processor, "mod1Amount") - .25f) < 1.0e-6f
                 && plainParameter (processor, "cutoff") == base, "Ring undo restored the wrong parameter");
    expectHistoryResult (processor.redoUserEdit(), "Redo one complete modulation-ring drag");
    require (plainParameter (processor, "mod1Amount") == edited, "Ring redo changed the final depth");
    ring->mouseDoubleClick (modulationMouseEvent (*ring, point, point, 2));
    require (std::abs (plainParameter (processor, "mod1Amount")) < 1.0e-6f
                 && processor.getUndoCount() == 2 && plainParameter (processor, "mod1Source") == 1,
             "Resetting modulation depth removed the route or produced incomplete history");
    refreshEditorTimers();
    require (modulationControlVisible (ring), "A zero-depth route became impossible to edit");
    const auto resetRevision = processor.getHistoryRevision();
    ring->mouseDoubleClick (modulationMouseEvent (*ring, point, point, 2));
    require (processor.getUndoCount() == 2 && processor.getHistoryRevision() == resetRevision,
             "Resetting an already-zero modulation depth created empty history");
    const auto dragDepth = [&] (float travel, bool fine)
    {
        ring->mouseDown (modulationMouseEvent (*ring, point, point));
        const auto end = point.translated (0, travel);
        ring->mouseDrag (modulationMouseEvent (*ring, end, point, 1, fine));
        ring->mouseUp (modulationMouseEvent (*ring, end, point, 1, fine));
        require (! processor.isUserEditActive() && probe.starts == probe.ends
                     && plainParameter (processor, "cutoff") == base,
                 "Depth precision/boundary drag changed the base or left an incomplete gesture");
        return plainParameter (processor, "mod1Amount");
    };
    const auto normalDepth = dragDepth (50, false);
    require (normalDepth < -.01f, "Dragging down from zero did not create a negative modulation depth");
    ring->mouseDoubleClick (modulationMouseEvent (*ring, point, point, 2));
    const auto fineDepth = dragDepth (50, true);
    require (fineDepth < 0 && std::abs (fineDepth) < std::abs (normalDepth) * .5f,
             "Shift-drag did not provide a finer modulation depth adjustment in the same direction");
    require (std::abs (dragDepth (100000, false) + 1.0f) < 1.0e-6f
                 && std::abs (dragDepth (-100000, false) - 1.0f) < 1.0e-6f,
             "Extreme modulation depth drags escaped or failed to reach the signed amount limits");
    selectModulationSource (*editor, 6);
    require (! ring->isVisible() && modulationControlVisible (positionRing), "Macro selection left stale LFO rings visible");
}

void testModulationRingHostReplacement()
{
    PresetTestDirectory files;
    for (const int boundary : { 0, 1, 2 })
    {
        AnyvocAudioProcessor processor, replacement;
        installModulationRingFixture (processor); installModulationRingFixture (replacement);
        // Same route, source and initial amount deliberately defeat identity-
        // only checks. A full project boundary must still invalidate the drag.
        setParameter (replacement, "master", .29f);
        const auto project = save (replacement);
        const auto expected = normallyRestoredProjectState (project);
        auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                                  files.directory.getChildFile ("Settings.xml"));
        editor->setVisible (true); editor->setSize (1120, 760);
        auto* ring = findComponent<juce::Component> (*editor, "cutoffModulationRing");
        require (modulationControlVisible (ring), "Restore test modulation ring unavailable");
        const auto point = modulationRingPoint (*ring);
        ModulationGestureProbe probe (*processor.apvts.getParameter ("mod1Amount"));
        float parameterImmediatelyAfterRestore = 0, rawImmediatelyAfterRestore = 0;
        const auto load = [&]
        {
            processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
            const auto* amountParameter = processor.apvts.getParameter ("mod1Amount");
            parameterImmediatelyAfterRestore = amountParameter->convertFrom0to1 (amountParameter->getValue());
            rawImmediatelyAfterRestore = plainParameter (processor, "mod1Amount");
        };
        if (boundary == 0) probe.onStart = load;
        if (boundary == 1) probe.onValue = load;
        ring->mouseDown (modulationMouseEvent (*ring, point, point));
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -20), point));
        if (boundary == 2) load();
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -55), point));
        ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -55), point));
        require ((boundary != 0 || probe.startFired) && (boundary != 1 || probe.valueFired)
                     && save (processor) == expected && ! processor.isUserEditActive()
                     && ! processor.canUndo() && ! processor.canRedo() && probe.starts == probe.ends,
                 "A stale modulation drag overwrote restored project state or left an open gesture at boundary "
                     + std::to_string (boundary) + "; starts=" + std::to_string (probe.starts)
                     + ", ends=" + std::to_string (probe.ends) + ", startFired=" + std::to_string (probe.startFired)
                     + ", valueFired=" + std::to_string (probe.valueFired)
                     + ", active=" + std::to_string (processor.isUserEditActive())
                     + ", undo=" + std::to_string (processor.getUndoCount())
                     + ", redo=" + std::to_string (processor.getRedoCount()) + "; "
                     + "immediate parameter=" + juce::String (parameterImmediatelyAfterRestore, 12).toStdString()
                     + ", immediate raw=" + juce::String (rawImmediatelyAfterRestore, 12).toStdString() + "; "
                     + historyStateDiagnostic (processor, expected));
    }
}

void testModulationRingAutomationAndCancellation()
{
    PresetTestDirectory files;
    for (const int change : { 0, 1, 2, 3, 4 })
    {
        AnyvocAudioProcessor processor;
        installModulationRingFixture (processor);
        auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                                  files.directory.getChildFile ("Settings.xml"));
        editor->setVisible (true); editor->setSize (1120, 760);
        auto* ring = findComponent<juce::Component> (*editor, "cutoffModulationRing");
        require (modulationControlVisible (ring), "Automation test modulation ring unavailable");
        const auto point = modulationRingPoint (*ring);
        ModulationGestureProbe probe (*processor.apvts.getParameter ("mod1Amount"));
        if (change == 4) probe.onValue = [&] { setParameter (processor, "mod1Amount", -.41f); };
        ring->mouseDown (modulationMouseEvent (*ring, point, point));
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -12), point));
        if (change == 0) setParameter (processor, "mod1Source", 2);
        if (change == 1) setParameter (processor, "mod1Dest", 2);
        if (change == 2) setParameter (processor, "mod1Amount", -.41f);
        if (change == 3)
        {
            setParameter (processor, "mod1WarpTarget", 0);
            setParameter (processor, "mod1WarpEnabled", 1);
        }
        const auto automated = save (processor);
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -60), point));
        ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -60), point));
        require (save (processor) == automated && ! processor.isUserEditActive() && probe.starts == probe.ends
                     && (change != 4 || (probe.valueFired && std::abs (plainParameter (processor, "mod1Amount") + .41f) < 1.0e-6f)),
                 "Continued ring movement overwrote host routing/depth automation at case " + std::to_string (change));
    }
    for (const int cancel : { 0, 1, 2, 3, 4 })
    {
        AnyvocAudioProcessor processor;
        installModulationRingFixture (processor);
        auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                                  files.directory.getChildFile ("Settings.xml"));
        editor->setVisible (true); editor->setSize (1120, 760);
        auto* ring = findComponent<juce::Component> (*editor, "cutoffModulationRing");
        require (modulationControlVisible (ring), "Cancellation test modulation ring unavailable");
        const auto point = modulationRingPoint (*ring);
        ModulationGestureProbe probe (*processor.apvts.getParameter ("mod1Amount"));
        ring->mouseDown (modulationMouseEvent (*ring, point, point));
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -18), point));
        const auto accepted = save (processor);
        if (cancel == 0) require (ring->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)), "Ring did not accept Escape");
        if (cancel == 1) ring->setVisible (false);
        if (cancel == 2) selectModulationSource (*editor, 2);
        if (cancel == 3) editor.reset();
        if (cancel == 4) ring->focusLost (juce::Component::focusChangedDirectly);
        if (cancel != 3)
        {
            ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -65), point));
            ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -65), point));
        }
        require (save (processor) == accepted && ! processor.isUserEditActive()
                     && processor.getUndoCount() == 1 && probe.starts == 1 && probe.ends == 1,
                 "Ring cancellation discarded an accepted edit, kept writing or leaked a gesture at case "
                     + std::to_string (cancel));
        expectHistoryResult (processor.undoUserEdit(), "Undo a cancelled modulation-ring gesture");
        require (std::abs (plainParameter (processor, "mod1Amount") - .25f) < 1.0e-6f,
                 "Cancelled ring gesture did not restore its initial depth with Undo");
    }
}

void testCrossModulationStateAndParameterCompatibility()
{
    AnyvocAudioProcessor source;
    const auto& all = source.getParameters();
    require (all.size() > 12, "Cross-modulation parameters were not appended");
    const auto* previousLast = dynamic_cast<const juce::RangedAudioParameter*> (
        all[all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount - advancedEnvelopeParameterCount - expandedSourceParameterCount - advancedMatrixParameterCount - 13]);
    require (previousLast != nullptr && previousLast->getParameterID() == "osc3RandomUnison",
             "Cross-modulation controls were inserted into preexisting host automation order");
    int appended = 0;
    for (int oscillator = 0; oscillator < 3; ++oscillator)
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto prefix = "osc" + juce::String (oscillator + 1) + "Warp" + juce::String (slot + 1);
            auto* classic = source.apvts.getParameter (prefix + "Mode");
            require (classic != nullptr && classic->getNumSteps() == 7 && classic->getVersionHint() == 6,
                     "Extending the warp menu changed the legacy host mode parameter");
            for (int choice = 0; choice <= 6; ++choice)
                require (std::abs (classic->convertFrom0to1 (choice / 6.0f) - choice) < 1.0e-6f,
                         "A normalized legacy warp automation point selected a different classic mode");
            for (const auto* suffix : { "CrossMode", "CrossSource" })
            {
                auto* parameter = source.apvts.getParameter (prefix + suffix);
                const auto* atIndex = dynamic_cast<const juce::RangedAudioParameter*> (
                    all[all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount - advancedEnvelopeParameterCount - expandedSourceParameterCount - advancedMatrixParameterCount - 12 + appended]);
                require (parameter != nullptr && parameter == atIndex && parameter->getNumSteps() == 5
                             && parameter->getVersionHint() == 8 && parameter->getDefaultValue() == 0,
                         "Cross-modulation parameter order, choice range, version hint or default changed");
                for (int choice = 0; choice <= 4; ++choice)
                {
                    parameter->setValueNotifyingHost (choice / 4.0f);
                    require (plainParameter (source, (prefix + suffix).toRawUTF8()) == choice,
                             "Normalized cross-mode/source automation selected the wrong choice");
                }
                ++appended;
            }
            const int index = oscillator * 2 + slot;
            setParameter (source, (prefix + "Mode").toRawUTF8(), static_cast<float> (index + 1));
            setParameter (source, (prefix + "CrossMode").toRawUTF8(), static_cast<float> (index % 4 + 1));
            setParameter (source, (prefix + "CrossSource").toRawUTF8(), static_cast<float> (index % 5));
            setParameter (source, (prefix + "Amount").toRawUTF8(), .1f * (index + 1));
            require (source.getWarpMode (oscillator, slot) == 7 + index % 4,
                     "Unified mode lookup did not select the appended cross mode");
        }
    const auto expected = parameters (source);
    const auto state = save (source);
    require (static_cast<int> (stateTree (source).getProperty ("schemaVersion")) == 14,
             "Cross-modulation state did not advertise schema 14");
    AnyvocAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    expectParameters (restored, expected);
    juce::TemporaryFile file (".anyvoc");
    require (source.savePreset (file.getFile()) && restored.loadPreset (file.getFile()),
             "Cross-modulation preset failed to save/load");
    expectParameters (restored, expected);
    setParameter (restored, "osc1Warp1Mode", 4);
    require (restored.getWarpMode (0, 0) == 7, "Legacy host automation silently disabled a selected cross mode");
    setParameter (restored, "osc1Warp1CrossMode", 0);
    require (restored.getWarpMode (0, 0) == 4, "Disabling cross modulation lost the remembered classic mode");
    restored.setCurrentProgram (0);
    for (auto* base : restored.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
            parameter != nullptr && isCrossModulationParameter (parameter->getParameterID()))
            require (parameter->getValue() == parameter->getDefaultValue(), "Init retained a cross mode or source");
    require (restored.getNumPrograms() == 28 && restored.getProgramName (16) == "Keys / Phase Alloy"
                 && restored.getProgramName (17) == "Bass / Vector Steel",
             "The new cross-modulation presets displaced previous program indices");
    setParameter (restored, "osc1Warp1Mode", 6);
    setParameter (restored, "osc1Warp1Amount", .7f);
    setParameter (restored, "osc1Warp1CrossMode", 2);
    for (int point = 0; point < 64; ++point)
        require (restored.getWarpedWavetableSample (0, point / 64.0f, .4f)
                     == restored.getWavetableSample (0, point / 64.0f, .4f),
                 "Static cross-mode preview displayed the hidden remembered classic warp as live modulation");
}

void testVersionSevenCrossModulationMigration()
{
    AnyvocAudioProcessor original;
    original.setCurrentProgram (2); setParameter (original, "master", .39f);
    const auto expected = parameters (original);
    const auto current = stateTree (original);
    for (const int version : { 1, 2, 3, 4, 5, 6, 7 })
    {
        auto legacy = current.createCopy(); legacy.setProperty ("schemaVersion", version, nullptr);
        for (int child = legacy.getNumChildren(); --child >= 0;)
        {
            const auto id = legacy.getChild (child).getProperty ("id").toString();
            if (isCrossModulationParameter (id) || (version < 7 && isVoicingParameter (id))
                || (version < 6 && isWarpParameter (id)) || (version < 5 && isDualFilterParameter (id))
                || (version < 3 && isDrawableLfoParameter (id)) || (version < 2 && isSampleParameter (id)))
                legacy.removeChild (child, nullptr);
        }
        AnyvocAudioProcessor restored;
        restored.setCurrentProgram (17);
        for (auto* base : restored.getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
                parameter != nullptr && isCrossModulationParameter (parameter->getParameterID()))
                parameter->setValueNotifyingHost (1);
        restore (restored, legacy);
        expectParameters (restored, expected);
        const auto accepted = save (restored);
        auto incomplete = legacy.createCopy();
        incomplete.removeChild (incomplete.getChildWithProperty ("id", "master"), nullptr);
        restore (restored, incomplete);
        require (save (restored) == accepted, "Legacy cross-mode migration accepted a missing older parameter");
    }
    AnyvocAudioProcessor live;
    live.setCurrentProgram (17);
    const auto retained = save (live);
    for (const auto* id : { "osc1Warp1CrossMode", "osc1Warp2CrossSource", "osc3Warp2CrossMode", "osc3Warp2CrossSource" })
    {
        auto incomplete = current.createCopy();
        incomplete.removeChild (incomplete.getChildWithProperty ("id", id), nullptr);
        restore (live, incomplete);
        require (save (live) == retained, "Schema 8 accepted a missing cross-mode/source parameter");
    }
    for (const auto* badValue : { "NaN", "Infinity", "not a number" })
    {
        auto invalid = current.createCopy();
        invalid.getChildWithProperty ("id", "osc2Warp1CrossMode").setProperty ("value", badValue, nullptr);
        restore (live, invalid);
        require (save (live) == retained, "Malformed cross-mode state partially replaced the sound");
    }
    auto duplicate = current.createCopy();
    duplicate.addChild (duplicate.getChildWithProperty ("id", "osc3Warp2CrossSource").createCopy(), -1, nullptr);
    restore (live, duplicate);
    require (save (live) == retained, "Duplicate cross-source state replaced the sound");
}

void testUnifiedWarpModeHistoryAndHostBoundaries()
{
    AnyvocAudioProcessor processor;
    setParameter (processor, "osc1Warp1Mode", 3);
    setParameter (processor, "osc1Warp1CrossSource", 1);
    ModulationGestureProbe classic (*processor.apvts.getParameter ("osc1Warp1Mode"));
    ModulationGestureProbe cross (*processor.apvts.getParameter ("osc1Warp1CrossMode"));
    processor.setWarpMode (0, 0, 7);
    require (processor.getWarpMode (0, 0) == 7 && plainParameter (processor, "osc1Warp1Mode") == 3
                 && processor.getUndoCount() == 1 && ! processor.isUserEditActive(),
             "Selecting FM did not retain the classic mode as one completed history edit");
    expectHistoryResult (processor.undoUserEdit(), "Undo cross-mode selection");
    require (processor.getWarpMode (0, 0) == 3, "Undo did not restore the preceding classic mode");
    expectHistoryResult (processor.redoUserEdit(), "Redo cross-mode selection");
    processor.setWarpMode (0, 0, 6);
    require (processor.getWarpMode (0, 0) == 6 && plainParameter (processor, "osc1Warp1CrossMode") == 0
                 && processor.getUndoCount() == 2, "Returning to a different classic mode fragmented the two writes");
    expectHistoryResult (processor.undoUserEdit(), "Undo grouped cross-to-classic selection");
    require (processor.getWarpMode (0, 0) == 7 && plainParameter (processor, "osc1Warp1Mode") == 3,
             "Undo restored only one half of a unified mode selection");
    const auto untouched = save (processor);
    const auto revision = processor.getHistoryRevision();
    processor.setWarpMode (0, 0, 7);
    processor.setWarpMode (-1, 0, 8); processor.setWarpMode (3, 0, 8);
    processor.setWarpMode (0, -1, 8); processor.setWarpMode (0, 2, 8);
    processor.setWarpMode (0, 0, -1); processor.setWarpMode (0, 0, 11);
    require (save (processor) == untouched && processor.getHistoryRevision() == revision && processor.canRedo()
                 && classic.starts == classic.ends && cross.starts == cross.ends,
             "Unchanged/invalid mode selection altered a hidden mode, history or host gesture ownership");
    for (const bool duringValue : { false, true })
    {
        AnyvocAudioProcessor target;
        setParameter (target, "osc1Warp1Mode", 3);
        setParameter (target, "osc1Warp1CrossMode", 2);
        const auto replacement = save (target), expected = normallyRestoredProjectState (replacement);
        ModulationGestureProbe oldMode (*target.apvts.getParameter ("osc1Warp1Mode"));
        ModulationGestureProbe newMode (*target.apvts.getParameter ("osc1Warp1CrossMode"));
        const auto load = [&] { target.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize())); };
        if (duringValue) oldMode.onValue = load; else oldMode.onStart = load;
        target.setWarpMode (0, 0, 6);
        require ((duringValue ? oldMode.valueFired : oldMode.startFired) && save (target) == expected
                     && ! target.isUserEditActive() && ! target.canUndo() && ! target.canRedo()
                     && oldMode.starts == oldMode.ends && newMode.starts == newMode.ends,
                 "A paired mode setter continued after reentrant project replacement; "
                     + historyStateDiagnostic (target, expected));
    }
    {
        AnyvocAudioProcessor target;
        setParameter (target, "osc1Warp1Mode", 3); setParameter (target, "osc1Warp1CrossMode", 2);
        ModulationGestureProbe oldMode (*target.apvts.getParameter ("osc1Warp1Mode"));
        ModulationGestureProbe crossMode (*target.apvts.getParameter ("osc1Warp1CrossMode"));
        oldMode.onValue = [&] { setParameter (target, "osc1Warp1CrossMode", 4); };
        target.setWarpMode (0, 0, 6);
        require (oldMode.valueFired && target.getWarpMode (0, 0) == 10
                     && plainParameter (target, "osc1Warp1CrossMode") == 4
                     && ! target.isUserEditActive() && oldMode.starts == oldMode.ends && crossMode.starts == crossMode.ends,
                 "Paired warp-mode selection overwrote newer related-parameter host automation");
    }
}

void prepareCrossModulationFixture (AnyvocAudioProcessor& processor)
{
    processor.setCurrentProgram (0); audibleSettings (processor);
    for (const auto* id : { "osc2Level", "osc3Level", "subLevel", "noiseLevel", "filterEnabled", "filter2Enabled", "drive" })
        setParameter (processor, id, 0);
    setParameter (processor, "osc1Level", .55f); setParameter (processor, "master", .55f);
    setParameter (processor, "ampSustain", 1); setParameter (processor, "ampRelease", .06f);
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
    {
        const auto prefix = "osc" + juce::String (oscillator);
        setParameter (processor, (prefix + "Position").toRawUTF8(), 0);
        setParameter (processor, (prefix + "Unison").toRawUTF8(), 1);
        setParameter (processor, (prefix + "Phase").toRawUTF8(), 0);
        setParameter (processor, (prefix + "PhaseRandom").toRawUTF8(), 0);
        setParameter (processor, (prefix + "RandomUnison").toRawUTF8(), 0);
    }
    setParameter (processor, "osc2Semi", 12);
    setParameter (processor, "osc1Warp1CrossSource", 1);
}

double crossAudioDifference (const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
    require (first.getNumSamples() == second.getNumSamples(), "Cross-modulation audio comparison length changed");
    double energy = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < first.getNumSamples(); ++sample)
        {
            const double difference = first.getSample (channel, sample) - second.getSample (channel, sample);
            energy += difference * difference;
        }
    return std::sqrt (energy / (2 * first.getNumSamples()));
}

void testCrossModulationProcessorAudio()
{
    const auto audioFor = [] (int mode, float depth, float sourceSemi, float carrierLevel = .55f)
    {
        AnyvocAudioProcessor processor; prepareCrossModulationFixture (processor);
        setParameter (processor, "osc1Warp1CrossMode", static_cast<float> (mode));
        setParameter (processor, "osc1Warp1Amount", depth);
        setParameter (processor, "osc2Semi", sourceSemi);
        setParameter (processor, "osc1Level", carrierLevel);
        processor.prepareToPlay (48000, 128);
        return render (processor, 12000, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }, 127);
    };
    const auto dry = audioFor (0, 0, 12);
    require (rms (dry) > .005, "Cross-modulation carrier fixture was silent");
    for (int mode = 1; mode <= 4; ++mode)
    {
        const auto zero = audioFor (mode, 0, 12);
        require (crossAudioDifference (zero, dry) == 0,
                 "Zero cross-modulation depth altered the dry carrier for mode " + std::to_string (mode));
        const auto active = audioFor (mode, .55f, 12);
        require (rms (active) > .001 && crossAudioDifference (active, dry) > .001,
                 "A host cross-mode/amount parameter did not reach audible carrier modulation");
        require (crossAudioDifference (active, audioFor (mode, .55f, 7)) > .0005,
                 "Changing a muted source oscillator's pitch did not affect cross modulation");
    }
    require (rms (audioFor (2, .55f, 12, 0)) == 0,
             "A muted source leaked directly into output when the carrier was also muted");
    AnyvocAudioProcessor direct, modulated;
    for (auto* processor : { &direct, &modulated })
    {
        prepareCrossModulationFixture (*processor);
        setParameter (*processor, "osc1Warp1CrossMode", 2);
    }
    setParameter (direct, "osc1Warp1Amount", .4f);
    setParameter (modulated, "macro1", 1);
    require (modulated.assignModulation (6, 12, .4f), "Could not route a macro to a cross-mode warp amount");
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    direct.prepareToPlay (48000, 128); modulated.prepareToPlay (48000, 128);
    render (direct, 16000, note); render (modulated, 16000, note);
    require (crossAudioDifference (render (direct, 4096), render (modulated, 4096)) < .0002,
             "The existing warp modulation destination failed to control cross-mode depth");
}

void testUnifiedWarpEditorControls()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor, files.directory,
                                                              files.directory.getChildFile ("Settings.xml"));
    editor->setVisible (true); editor->setSize (1120, 760); clickVisible (*editor, "WARP");
    auto* mode = findComponent<juce::ComboBox> (*editor, "osc1Warp1Mode");
    auto* source = findComponent<juce::ComboBox> (*editor, "osc1Warp1CrossSource");
    require (mode != nullptr && mode->getNumItems() == 11 && source != nullptr && source->getNumItems() == 5,
             "Warp Lab did not expose all classic/cross modes and source choices");
    const juce::StringArray modeLabels { "Off", "Bend +", "Bend -", "Pulse Width", "Sync", "Mirror", "Fold",
                                        "FM - Frequency", "PM - Phase", "AM - Amplitude", "RM - Ring" };
    const juce::StringArray sourceLabels { "OSC A", "OSC B", "OSC C", "SUB", "NOISE" };
    for (int index = 0; index < modeLabels.size(); ++index)
        require (mode->getItemText (index) == modeLabels[index], "Unified mode menu label/order changed");
    for (int index = 0; index < sourceLabels.size(); ++index)
        require (source->getItemText (index) == sourceLabels[index], "Cross-source menu label/order changed");
    require (! source->isEnabled(), "A classic warp incorrectly enabled its cross-source selector");
    const auto before = save (processor);
    require (! processor.canUndo(), "Opening Warp Lab changed the sound");
    mode->setSelectedItemIndex (8, juce::sendNotificationSync); // PM
    refreshEditorTimers();
    require (processor.getWarpMode (0, 0) == 8 && source->isEnabled() && processor.getUndoCount() == 1,
             "Unified PM selection did not update its host parameter, availability and history");
    source->setSelectedItemIndex (1, juce::sendNotificationSync);
    require (plainParameter (processor, "osc1Warp1CrossSource") == 1 && processor.getUndoCount() == 2,
             "Source selector did not create one undoable source change");
    expectHistoryResult (processor.undoUserEdit(), "Undo cross-source selection");
    expectHistoryResult (processor.undoUserEdit(), "Undo unified PM selection");
    refreshEditorTimers();
    require (save (processor) == before && mode->getSelectedItemIndex() == 0 && ! source->isEnabled(),
             "Global Undo did not reconcile the unified mode/source controls");
    const auto undoCount = processor.getUndoCount();
    setParameter (processor, "osc1Warp1CrossMode", 4); setParameter (processor, "osc1Warp1CrossSource", 4);
    refreshEditorTimers();
    require (mode->getSelectedItemIndex() == 10 && source->getSelectedItemIndex() == 4 && source->isEnabled()
                 && processor.getUndoCount() == undoCount,
             "Host RM/source automation failed to refresh controls or became a user edit");
    mode->setSelectedItemIndex (4, juce::sendNotificationSync);
    refreshEditorTimers();
    require (processor.getWarpMode (0, 0) == 4 && plainParameter (processor, "osc1Warp1CrossMode") == 0
                 && ! source->isEnabled(), "Returning to Sync failed to disable cross modulation and its source control");
}

const std::array<const char*, 19> matrixRouteFields {
    "Source", "Dest", "Amount", "DestBank", "FilterTarget", "WarpEnabled", "WarpTarget",
    "Enabled", "Polarity", "Curve", "AuxSource", "AuxPolarity", "AuxCurve", "SourceExtra", "AuxSourceExtra", "LfoPhaseEnabled", "LfoPhaseTarget", "MacroEnabled", "MacroTarget" };

using MatrixPayload = std::array<float, 19>;

MatrixPayload matrixPayload (AnyvocAudioProcessor& processor, int route)
{
    MatrixPayload result {};
    const auto prefix = "mod" + juce::String (route + 1);
    for (size_t field = 0; field < result.size(); ++field)
    {
        auto* parameter = processor.apvts.getParameter (prefix + matrixRouteFields[field]);
        require (parameter != nullptr, "A complete matrix route parameter was missing");
        result[field] = parameter->getValue();
    }
    return result;
}

void expectMatrixPayload (AnyvocAudioProcessor& processor, int route, const MatrixPayload& expected)
{
    const auto actual = matrixPayload (processor, route);
    for (size_t field = 0; field < expected.size(); ++field)
        require (std::abs (actual[field] - expected[field]) < 2.0e-6f,
                 "Matrix route " + std::to_string (route + 1) + " lost field " + matrixRouteFields[field]);
}

void configureMatrixPayload (AnyvocAudioProcessor& processor, int route)
{
    const auto prefix = "mod" + juce::String (route + 1);
    const std::array<float, 19> values {
        static_cast<float> (route % 9 + 1), static_cast<float> (route % 7 + 1), -.83f + route * .049f,
        static_cast<float> (route % 2), static_cast<float> (route % 4), static_cast<float> ((route + 1) % 2),
        static_cast<float> (route % 6), static_cast<float> ((route + 1) % 2), static_cast<float> (route % 3),
        -.72f + route * .037f, static_cast<float> ((route + 3) % 10), static_cast<float> ((route + 2) % 3),
        .81f - route * .041f, static_cast<float> (route % 16), static_cast<float> ((route + 5) % 16),
        static_cast<float> (route % 2), static_cast<float> ((route + 3) % 10),
        static_cast<float> (route % 2), static_cast<float> ((route + 5) % 8) };
    for (size_t field = 0; field < values.size(); ++field)
        setParameter (processor, (prefix + matrixRouteFields[field]).toRawUTF8(), values[field]);
}

void clearMatrixHistory (AnyvocAudioProcessor& processor)
{
    const auto state = save (processor);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    require (! processor.canUndo() && ! processor.canRedo(), "Matrix test fixture retained setup history");
}

void testAdvancedMatrixParameterCompatibility()
{
    AnyvocAudioProcessor processor;
    require (anyvoc::modulationRouteCount == 32, "The matrix no longer exposes 32 routes");
    // This is the frozen v0.12 host-ID order, independent of the new layout's
    // grouping. Inserting a control anywhere in this prefix breaks old host automation.
    juce::StringArray legacy;
    const auto append = [&] (const juce::String& prefix, std::initializer_list<const char*> fields)
    {
        for (const auto* field : fields) legacy.add (prefix + field);
    };
    for (int osc = 1; osc <= 3; ++osc)
        append ("osc" + juce::String (osc), { "Position", "Level", "Semi", "Fine", "Unison", "Detune", "Pan" });
    append ({}, { "subLevel", "noiseLevel", "filterType", "cutoff", "resonance", "filterEnv" });
    for (const auto* envelope : { "amp", "env" }) append (envelope, { "Attack", "Decay", "Sustain", "Release" });
    for (int lfo = 1; lfo <= 2; ++lfo) append ("lfo" + juce::String (lfo), { "Rate", "Shape" });
    for (int macro = 1; macro <= 4; ++macro) legacy.add ("macro" + juce::String (macro));
    for (int route = 1; route <= 8; ++route) append ("mod" + juce::String (route), { "Source", "Dest", "Amount" });
    append ({}, { "drive", "chorusMix", "delayMix", "delayTime", "delayFeedback", "reverbMix", "reverbSize", "master",
                   "arpEnabled", "arpRate", "arpOctaves", "arpGate" });
    for (int osc = 1; osc <= 3; ++osc)
        append ("osc" + juce::String (osc), { "Mode", "Root", "Loop", "Reverse", "GrainSize", "GrainDensity", "GrainSpray" });
    legacy.add ("tempo");
    for (int lfo = 1; lfo <= 2; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo);
        append (prefix, { "Kind", "Sync", "Division", "Mode", "Interpolation" });
        for (int point = 0; point <= 32; ++point) legacy.add (prefix + "Point" + juce::String (point));
    }
    append ({}, { "filterEnabled", "filter2Enabled", "filter2Type", "filter2Cutoff", "filter2Resonance", "filter2Env",
                   "filterRouting", "filterBalance", "filterSlope", "filter2Slope", "filterKeyTrack", "filter2KeyTrack",
                   "osc1FilterRoute", "osc2FilterRoute", "osc3FilterRoute", "subFilterRoute", "noiseFilterRoute" });
    for (int route = 1; route <= 8; ++route) append ("mod" + juce::String (route), { "DestBank", "FilterTarget" });
    for (int osc = 1; osc <= 3; ++osc) for (int slot = 1; slot <= 2; ++slot)
        append ("osc" + juce::String (osc) + "Warp" + juce::String (slot), { "Mode", "Amount" });
    for (int route = 1; route <= 8; ++route) append ("mod" + juce::String (route), { "WarpEnabled", "WarpTarget" });
    append ({}, { "voiceMode", "glideTime", "glideAlways" });
    for (int osc = 1; osc <= 3; ++osc) append ("osc" + juce::String (osc), { "Phase", "PhaseRandom", "RandomUnison" });
    for (int osc = 1; osc <= 3; ++osc) for (int slot = 1; slot <= 2; ++slot)
        append ("osc" + juce::String (osc) + "Warp" + juce::String (slot), { "CrossMode", "CrossSource" });
    const auto& all = processor.getParameters();
    require (all.size() == legacy.size() + advancedMatrixParameterCount + expandedSourceParameterCount + advancedEnvelopeParameterCount + lfoMotionParameterCount + chaosParameterCount + macroDestinationParameterCount,
             "Advanced matrix did not append exactly 360 host controls");
    for (int index = 0; index < legacy.size(); ++index)
    {
        const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (all[index]);
        require (parameter != nullptr && parameter->getParameterID() == legacy[index]
                     && parameter->getVersionHint() < 9,
                 "A v0.12 host parameter moved at index " + std::to_string (index));
    }
    int next = legacy.size();
    for (int route = 9; route <= 32; ++route)
        for (size_t field = 0; field < 7; ++field)
        {
            const auto id = "mod" + juce::String (route) + matrixRouteFields[field];
            const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (all[next++]);
            auto* original = processor.apvts.getParameter ("mod1" + juce::String (matrixRouteFields[field]));
            require (parameter != nullptr && parameter->getParameterID() == id && parameter->getVersionHint() == 9
                         && parameter->getNumSteps() == original->getNumSteps()
                         && parameter->getDefaultValue() == original->getDefaultValue()
                         && parameter->getNormalisableRange().interval == original->getNormalisableRange().interval,
                     "Extended route changed its host order, default or legacy field range");
            for (const float normalized : { 0.0f, .25f, .5f, .75f, 1.0f })
                require (parameter->convertFrom0to1 (normalized) == original->convertFrom0to1 (normalized),
                         "An extended row does not preserve the original normalized field mapping");
        }
    for (int route = 1; route <= 32; ++route)
        for (size_t field = 7; field < 13; ++field)
        {
            const auto id = "mod" + juce::String (route) + matrixRouteFields[field];
            const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (all[next++]);
            const bool enabled = field == 7, curve = field == 9 || field == 12;
            require (parameter != nullptr && parameter->getParameterID() == id && parameter->getVersionHint() == 9
                         && parameter->convertFrom0to1 (parameter->getDefaultValue()) == (enabled ? 1 : 0),
                     "Advanced control host order, version hint or neutral default changed");
            if (curve)
                require (parameter->convertFrom0to1 (0) == -1 && parameter->convertFrom0to1 (.5f) == 0
                             && parameter->convertFrom0to1 (1) == 1,
                         "A curve lost its symmetric signed automation range");
            else
                require (parameter->getNumSteps() == (enabled ? 2 : field == 10 ? 10 : 3),
                         "Advanced source/polarity/bypass choice count changed");
        }
    require (next == all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount - advancedEnvelopeParameterCount - expandedSourceParameterCount, "The complete v0.13 host prefix changed");
    for (int route = 1; route <= 8; ++route)
    {
        const auto prefix = "mod" + juce::String (route);
        const std::array<int, 7> steps { 10, 8, 0, 2, 4, 2, 6 }, hints { 1, 1, 1, 5, 5, 6, 6 };
        for (size_t field = 0; field < 7; ++field)
        {
            auto* parameter = processor.apvts.getParameter (prefix + matrixRouteFields[field]);
            require (parameter->getVersionHint() == hints[field], "A legacy matrix field acquired a new host version hint");
            if (field == 2)
                require (parameter->convertFrom0to1 (0) == -1 && parameter->convertFrom0to1 (.5f) == 0
                             && parameter->convertFrom0to1 (1) == 1, "Legacy signed amount automation changed");
            else for (int choice = 0; choice < steps[field]; ++choice)
                require (parameter->getNumSteps() == steps[field]
                             && parameter->convertFrom0to1 (choice / static_cast<float> (steps[field] - 1)) == choice,
                         "A legacy matrix choice was renumbered by the expansion");
        }
    }
}

void testAdvancedMatrixStateAndMigration()
{
    AnyvocAudioProcessor source;
    for (int route = 0; route < 32; ++route) configureMatrixPayload (source, route);
    setParameter (source, "master", .41f);
    const auto expected = parameters (source);
    const auto state = save (source);
    require (static_cast<int> (stateTree (source).getProperty ("schemaVersion")) == 14,
             "Advanced matrix state did not advertise schema 14");
    AnyvocAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    expectParameters (restored, expected);
    for (int route = 0; route < 32; ++route)
    {
        const auto decoded = restored.getModulationRoute (route);
        const auto prefix = "mod" + juce::String (route + 1);
        require (decoded.source == source.getModulationSource (route)
                     && decoded.destination == source.getModulationDestination (route)
                     && std::abs (decoded.amount - plainParameter (source, prefix + "Amount")) < 1.0e-6f
                     && decoded.enabled == (plainParameter (source, prefix + "Enabled") > .5f)
                     && decoded.polarity == plainParameter (source, prefix + "Polarity")
                     && decoded.auxSource == source.getModulationSource (route, true)
                     && decoded.auxPolarity == plainParameter (source, prefix + "AuxPolarity")
                     && std::abs (decoded.curve - plainParameter (source, prefix + "Curve")) < 1.0e-6f
                     && std::abs (decoded.auxCurve - plainParameter (source, prefix + "AuxCurve")) < 1.0e-6f,
                 "A matrix route getter disagreed with saved host controls");
    }
    juce::TemporaryFile preset (".anyvoc");
    require (source.savePreset (preset.getFile()) && restored.loadPreset (preset.getFile()),
             "An advanced 32-route user preset did not save/load");
    expectParameters (restored, expected);
    restored.setCurrentProgram (0);
    for (auto* base : restored.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
            parameter != nullptr && isAdvancedMatrixParameter (parameter->getParameterID()))
            require (parameter->getValue() == parameter->getDefaultValue(), "Init retained an advanced route setting");

    AnyvocAudioProcessor legacySource;
    legacySource.setCurrentProgram (2); setParameter (legacySource, "master", .39f);
    setParameter (legacySource, "mod8Source", 9); setParameter (legacySource, "mod8Dest", 7);
    setParameter (legacySource, "mod8Amount", -.23f);
    const auto legacyExpected = parameters (legacySource);
    const auto current = stateTree (legacySource);
    for (int version = 1; version <= 13; ++version)
    {
        auto legacy = current.createCopy(); legacy.setProperty ("schemaVersion", version, nullptr);
        for (int child = legacy.getNumChildren(); --child >= 0;)
        {
            const auto id = legacy.getChild (child).getProperty ("id").toString();
            if (isMacroDestinationParameter (id) || (version < 13 && isChaosParameter (id)) || (version < 12 && isLfoMotionParameter (id)) || (version < 11 && isAdvancedEnvelopeParameter (id)) || (version < 10 && isExpandedSourceParameter (id))
                || (version < 9 && isAdvancedMatrixParameter (id)) || (version < 8 && isCrossModulationParameter (id))
                || (version < 7 && isVoicingParameter (id)) || (version < 6 && isWarpParameter (id))
                || (version < 5 && isDualFilterParameter (id)) || (version < 3 && isDrawableLfoParameter (id))
                || (version < 2 && isSampleParameter (id))) legacy.removeChild (child, nullptr);
        }
        AnyvocAudioProcessor target;
        for (int route = 0; route < 32; ++route) configureMatrixPayload (target, route);
        for (auto* base : target.getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
                parameter != nullptr && (isAdvancedEnvelopeParameter (parameter->getParameterID())
                    || isLfoMotionParameter (parameter->getParameterID()) || isChaosParameter (parameter->getParameterID()) || isMacroDestinationParameter (parameter->getParameterID()))) parameter->setValueNotifyingHost (1);
        restore (target, legacy);
        expectParameters (target, legacyExpected);
        const auto retained = save (target);
        auto incomplete = legacy.createCopy();
        incomplete.removeChild (incomplete.getChildWithProperty ("id", "mod8Amount"), nullptr);
        restore (target, incomplete);
        require (save (target) == retained,
                 "Matrix migration accepted a missing original route field in schema " + std::to_string (version));
    }
    restored.setCurrentProgram (17);
    const auto retained = save (restored);
    const auto full = stateTree (source);
    for (const auto* id : { "mod9Source", "mod32Amount", "mod32WarpTarget", "mod1Enabled", "mod8Polarity",
                           "mod13Curve", "mod24AuxSource", "mod31AuxPolarity", "mod32AuxCurve" })
    {
        auto incomplete = full.createCopy();
        incomplete.removeChild (incomplete.getChildWithProperty ("id", id), nullptr);
        restore (restored, incomplete);
        require (save (restored) == retained, std::string ("Schema 10 accepted a missing matrix control: ") + id);
        auto duplicate = full.createCopy();
        duplicate.addChild (duplicate.getChildWithProperty ("id", id).createCopy(), -1, nullptr);
        restore (restored, duplicate);
        require (save (restored) == retained, std::string ("Duplicate matrix control partially replaced state: ") + id);
    }
    for (const auto* id : { "mod9Source", "mod1Enabled", "mod31Curve", "mod32AuxCurve", "mod32AuxSource" })
        for (const auto* bad : { "NaN", "Infinity", "not a number" })
        {
            auto invalid = full.createCopy();
            invalid.getChildWithProperty ("id", id).setProperty ("value", bad, nullptr);
            restore (restored, invalid);
            require (save (restored) == retained, "Malformed matrix value changed the current sound");
        }
}

void testAdvancedMatrixCapacityAndReuse()
{
    AnyvocAudioProcessor processor;
    for (int route = 0; route < 32; ++route)
    {
        const int source = 1 + route / 17, destination = 1 + route % 17;
        require (processor.assignModulation (source, destination, -.4f + route * .02f),
                 "An available matrix row could not be assigned at row " + std::to_string (route + 1));
        const auto result = processor.getModulationRoute (route);
        require (result.source == source && result.destination == destination && result.enabled,
                 "Assignment skipped or overwrote the first available row");
    }
    setParameter (processor, "mod1Enabled", 0); setParameter (processor, "mod32Enabled", 0);
    const auto full = save (processor);
    require (! processor.assignModulation (9, 17, .8f) && save (processor) == full,
             "A full matrix reused an occupied bypassed row");
    setParameter (processor, "mod32Polarity", 2); setParameter (processor, "mod32Curve", .31f);
    setParameter (processor, "mod32AuxSource", 7); setParameter (processor, "mod32AuxPolarity", 1);
    setParameter (processor, "mod32AuxCurve", -.48f);
    clearMatrixHistory (processor);
    auto reenabled = matrixPayload (processor, 31); reenabled[7] = 1;
    require (processor.assignModulation (2, 15, -.9f), "Dragging onto a bypassed duplicate route was rejected");
    expectMatrixPayload (processor, 31, reenabled);
    require (processor.getUndoCount() == 1, "Re-enabling a duplicate route was not a single user action");
    expectHistoryResult (processor.undoUserEdit(), "Undo duplicate route re-enable");
    require (! processor.getModulationRoute (31).enabled, "Undo did not restore the duplicate route's bypass state");
    expectHistoryResult (processor.redoUserEdit(), "Redo duplicate route re-enable");
    const auto count = processor.getUndoCount();
    const auto unchanged = save (processor);
    require (processor.assignModulation (2, 15, -.9f) && processor.getUndoCount() == count
                 && save (processor) == unchanged, "An active duplicate route lost its configured depth/transform");

    configureMatrixPayload (processor, 12);
    setParameter (processor, "mod13Source", 0); // Empty source with stale warp bank, bypass and transforms.
    setParameter (processor, "mod13SourceExtra", 0);
    require (processor.assignModulation (9, 16, -.25f), "A vacant source row could not be reused");
    const auto reused = processor.getModulationRoute (12);
    require (reused.source == 9 && reused.destination == 16 && std::abs (reused.amount + .25f) < 1.0e-6f
                 && reused.enabled && reused.polarity == 0 && reused.curve == 0 && reused.auxSource == 0
                 && reused.auxPolarity == 0 && reused.auxCurve == 0
                 && plainParameter (processor, "mod13Dest") == 0 && plainParameter (processor, "mod13DestBank") == 0
                 && plainParameter (processor, "mod13FilterTarget") == 0
                 && plainParameter (processor, "mod13SourceExtra") == 0 && plainParameter (processor, "mod13AuxSourceExtra") == 0
                 && plainParameter (processor, "mod13LfoPhaseEnabled") == 0 && plainParameter (processor, "mod13LfoPhaseTarget") == 0
                 && plainParameter (processor, "mod13MacroEnabled") == 0 && plainParameter (processor, "mod13MacroTarget") == 0,
             "A reused row inherited stale hidden destination fields, bypass, polarity or auxiliary control");
    const auto beforeInvalid = save (processor);
    const auto history = processor.getHistoryRevision();
    processor.setModulationDestination (-1, 1); processor.setModulationDestination (32, 1);
    processor.resetModulationRoute (-1); processor.resetModulationRoute (32);
    processor.moveModulationRoute (-1, 0); processor.moveModulationRoute (0, 32);
    require (save (processor) == beforeInvalid && processor.getHistoryRevision() == history,
             "An invalid matrix index changed sound or history");
}

void testAdvancedMatrixResetAndReorderHistory()
{
    AnyvocAudioProcessor processor;
    std::vector<MatrixPayload> original;
    for (int route = 0; route < 32; ++route)
    {
        configureMatrixPayload (processor, route);
        // Legacy amounts are continuous. Moving one into an appended row must
        // preserve sub-percent detail instead of silently quantising it to .01.
        if (route == 0) setParameter (processor, "mod1Amount", -.83137f);
        original.push_back (matrixPayload (processor, route));
    }
    clearMatrixHistory (processor);
    processor.moveModulationRoute (0, 31);
    for (int route = 0; route < 32; ++route)
        expectMatrixPayload (processor, route, original[static_cast<size_t> ((route + 1) % 32)]);
    require (processor.getUndoCount() == 1 && ! processor.isUserEditActive(),
             "Moving through all 32 rows fragmented one edit into multiple undo actions");
    expectHistoryResult (processor.undoUserEdit(), "Undo full matrix insertion move");
    for (int route = 0; route < 32; ++route) expectMatrixPayload (processor, route, original[static_cast<size_t> (route)]);
    expectHistoryResult (processor.redoUserEdit(), "Redo full matrix insertion move");
    processor.moveModulationRoute (31, 7);
    for (int route = 0; route < 32; ++route)
        expectMatrixPayload (processor, route, original[static_cast<size_t> (route < 7 ? route + 1 : route == 7 ? 0 : route)]);
    require (processor.getUndoCount() == 2, "A reverse insertion move failed to group its displaced rows");
    const auto beforeReset = matrixPayload (processor, 7);
    processor.resetModulationRoute (7);
    for (const auto* suffix : matrixRouteFields)
    {
        auto* parameter = processor.apvts.getParameter ("mod8" + juce::String (suffix));
        require (parameter->getValue() == parameter->getDefaultValue(), "Clear route did not reset its complete 17-field payload");
    }
    require (processor.getUndoCount() == 3, "Clear route was not one complete undo action");
    expectHistoryResult (processor.undoUserEdit(), "Undo route clear");
    expectMatrixPayload (processor, 7, beforeReset);
    expectHistoryResult (processor.redoUserEdit(), "Redo route clear");
    const auto unchanged = save (processor);
    const auto revision = processor.getHistoryRevision();
    processor.resetModulationRoute (7); processor.moveModulationRoute (6, 6);
    require (save (processor) == unchanged && processor.getHistoryRevision() == revision,
             "Clearing an already-default row or moving a row onto itself created empty history");
}

void testAdvancedMatrixHostBoundaries()
{
    for (const bool move : { false, true }) for (int boundary = 0; boundary < 3; ++boundary)
    {
        AnyvocAudioProcessor processor;
        configureMatrixPayload (processor, 0); configureMatrixPayload (processor, 1);
        clearMatrixHistory (processor);
        const auto replacement = save (processor), expected = normallyRestoredProjectState (replacement);
        std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
        for (int route = 1; route <= (move ? 2 : 1); ++route)
            for (const auto* suffix : matrixRouteFields)
                probes.push_back (std::make_unique<ModulationGestureProbe> (
                    *processor.apvts.getParameter ("mod" + juce::String (route) + suffix)));
        const auto load = [&] { processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize())); };
        auto& trigger = *probes.front();
        if (boundary == 0) trigger.onStart = load;
        else if (boundary == 1) trigger.onValue = load;
        else trigger.onEnd = load;
        if (move) processor.moveModulationRoute (0, 1); else processor.resetModulationRoute (0);
        require ((boundary == 0 ? trigger.startFired : boundary == 1 ? trigger.valueFired : trigger.endFired)
                     && save (processor) == expected && ! processor.isUserEditActive()
                     && ! processor.canUndo() && ! processor.canRedo(),
                 "A matrix helper continued across a synchronous host project replacement; "
                     + historyStateDiagnostic (processor, expected));
        for (const auto& probe : probes)
            require (probe->starts == probe->ends, "A matrix helper left a host gesture open after project replacement");
    }
    for (const bool move : { false, true }) for (int boundary = 0; boundary < 3; ++boundary)
    {
        AnyvocAudioProcessor processor;
        configureMatrixPayload (processor, 0); configureMatrixPayload (processor, 1);
        clearMatrixHistory (processor);
        ModulationGestureProbe trigger (*processor.apvts.getParameter ("mod1Source"));
        ModulationGestureProbe edited (*processor.apvts.getParameter (move ? "mod2AuxCurve" : "mod1AuxCurve"));
        std::vector<float> hostResult;
        const auto automate = [&]
        {
            setParameter (processor, move ? "mod2AuxCurve" : "mod1AuxCurve", -.93f);
            hostResult = parameters (processor);
        };
        if (boundary == 0) trigger.onStart = automate;
        else if (boundary == 1) trigger.onValue = automate;
        else trigger.onEnd = automate;
        if (move) processor.moveModulationRoute (0, 1); else processor.resetModulationRoute (0);
        require (! hostResult.empty(), "Matrix host-automation fixture did not run at the requested callback boundary");
        expectParameters (processor, hostResult);
        require (! processor.isUserEditActive() && trigger.starts == trigger.ends && edited.starts == edited.ends,
                 "A matrix helper overwrote related host automation or kept an interrupted gesture active");
    }
}

void testAdvancedMatrixProcessorAudio()
{
    struct Case { int polarity, auxiliary, auxPolarity; float curve, auxCurve, macro1, macro2, amount; bool enabled; float expected; };
    // Independent constants: curve +.25 squares, -.25 square-roots, and
    // bipolar remapping happens before either curve. The target is A Warp 1.
    const std::array<Case, 11> cases {{
        { 0, 0, 0, 0, 0, .25f, .5f, .4f, true, .1f },
        { 0, 7, 0, 0, 0, .25f, .5f, .4f, true, .05f },
        { 0, 7, 0, .25f, .25f, .25f, .5f, .64f, true, .01f },
        { 2, 0, 0, 0, 0, .25f, .5f, .4f, true, -.2f },
        { 2, 0, 0, .25f, 0, .25f, .5f, .4f, true, -.1f },
        { 0, 7, 2, 0, .25f, .25f, .25f, .32f, true, -.02f },
        { 0, 7, 2, 0, 0, .25f, .5f, .4f, true, 0 },
        { 0, 7, 0, 0, 0, .25f, 0, .4f, true, 0 },
        { 0, 7, 0, 0, 0, .25f, .5f, .4f, false, 0 },
        { 0, 0, 2, -.25f, .75f, .25f, 0, .4f, true, .2f },
        { 1, 7, 1, 0, 0, .25f, .5f, -.4f, true, -.05f }
    }};
    const auto prepare = [] (AnyvocAudioProcessor& processor)
    {
        prepareCrossModulationFixture (processor);
        setParameter (processor, "osc1Warp1Mode", 6);
        setParameter (processor, "osc1Warp1Amount", .35f);
    };
    for (size_t index = 0; index < cases.size(); ++index)
    {
        const auto item = cases[index];
        AnyvocAudioProcessor direct, routed;
        prepare (direct); prepare (routed);
        setParameter (direct, "osc1Warp1Amount", .35f + item.expected);
        // The legacy Warp parameter has a .01 host grid, while the DSP's
        // modulation sum is continuous. Oracle cases deliberately land on the
        // host grid so both instruments receive the same intended final depth.
        require (std::abs (plainParameter (direct, "osc1Warp1Amount") - (.35f + item.expected)) < 1.0e-6f,
                 "The direct audio oracle was quantised away from its calculated target");
        // Deliberately exercise the final row, not merely a legacy first-eight route.
        setParameter (routed, "mod32Source", 6); routed.setModulationDestination (31, 12);
        setParameter (routed, "mod32Amount", item.amount); setParameter (routed, "mod32Enabled", item.enabled ? 1.0f : 0.0f);
        setParameter (routed, "mod32Polarity", static_cast<float> (item.polarity));
        setParameter (routed, "mod32Curve", item.curve); setParameter (routed, "mod32AuxSource", static_cast<float> (item.auxiliary));
        setParameter (routed, "mod32AuxPolarity", static_cast<float> (item.auxPolarity));
        setParameter (routed, "mod32AuxCurve", item.auxCurve);
        setParameter (routed, "macro1", item.macro1); setParameter (routed, "macro2", item.macro2);
        direct.prepareToPlay (48000, 128); routed.prepareToPlay (48000, 128);
        const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
        render (direct, 24000, note); render (routed, 24000, note);
        const auto expected = render (direct, 4096), actual = render (routed, 4096);
        const auto difference = crossAudioDifference (actual, expected);
        require (rms (actual) > .005 && difference < .0002,
                 "Advanced route controls did not produce independently calculated target audio in case " + std::to_string (index)
                     + "; rms=" + std::to_string (rms (actual)) + "; difference=" + std::to_string (difference)
                     + "; expected-offset=" + std::to_string (item.expected));
    }
}

void dispatchEditorActions()
{
    // Route navigation replaces the outgoing panel asynchronously, after its
    // button callback returns. A fixed 2 ms pump may be spent entirely in an
    // earlier timer. Wait for a FIFO marker behind the requested UI actions.
    const auto complete = std::make_shared<bool> (false);
    require (juce::MessageManager::callAsync ([complete] { *complete = true; }), "Could not post the editor queue marker");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (! *complete && juce::Time::getMillisecondCounterHiRes() < deadline)
        require (juce::MessageManager::getInstance()->runDispatchLoopUntil (2), "Editor message loop unexpectedly stopped");
    require (*complete, "Editor actions did not complete before their queue deadline");
}

void clickMatrixControl (juce::Component& editor, const juce::String& id)
{
    auto* button = findComponent<juce::Button> (editor, id);
    require (modulationControlVisible (button) && button->isEnabled() && static_cast<bool> (button->onClick),
             "Unavailable matrix action: " + id.toStdString());
    button->onClick();
    dispatchEditorActions();
}

void testAdvancedMatrixEditorNavigationAndControls()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    configureMatrixPayload (processor, 0); configureMatrixPayload (processor, 31);
    clearMatrixHistory (processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor (
        new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1008, 684);
    auto* mod = findButton (*editor, "MOD");
    require (mod != nullptr && static_cast<bool> (mod->onClick), "Matrix page button unavailable");
    mod->onClick();
    auto* viewport = findComponent<juce::Viewport> (*editor, "MatrixViewport");
    require (modulationControlVisible (viewport) && viewport->getViewPositionY() == 0,
             "The initial compact matrix page was unavailable");
    auto* matrix = findComponent<juce::Component> (*editor, "ModulationMatrix");
    require (matrix != nullptr && ! matrix->hitTest (100, 100) && matrix->hitTest (100, 300),
             "Compact matrix failed to pass through its uncovered LFO area or cover its route card");
    for (int route = 1; route <= 32; ++route)
    {
        const auto prefix = "mod" + juce::String (route);
        auto* source = findComponent<juce::ComboBox> (*editor, prefix + "Source");
        auto* destination = findComponent<juce::ComboBox> (*editor, prefix + "Destination");
        auto* amount = findComponent<juce::Slider> (*editor, prefix + "Amount");
        auto* enabled = findComponent<juce::Button> (*editor, prefix + "Enabled");
        require (source != nullptr && source->getNumItems() == 25 && destination != nullptr && destination->getNumItems() == 36
                     && amount != nullptr && amount->getMinimum() == -1 && amount->getMaximum() == 1 && enabled != nullptr
                     && findComponent<juce::Button> (*editor, prefix + "Edit") != nullptr
                     && findComponent<juce::Button> (*editor, prefix + "Clear") != nullptr
                     && findComponent<juce::Component> (*editor, "ModulationRouteHandle" + juce::String (route)) != nullptr,
                 "A matrix row omitted its source, complete destinations, signed amount, bypass, detail, clear or handle");
    }
    const auto original = save (processor);
    for (int page = 1; page <= 3; ++page) clickMatrixControl (*editor, "MatrixNextPage");
    auto* lastSource = findComponent<juce::ComboBox> (*editor, "mod32Source");
    auto* firstSource = findComponent<juce::ComboBox> (*editor, "mod1Source");
    require (viewport->getLocalBounds().contains (viewport->getLocalArea (lastSource, lastSource->getLocalBounds()))
                 && ! viewport->getLocalBounds().intersects (viewport->getLocalArea (firstSource, firstSource->getLocalBounds()))
                 && ! findComponent<juce::Button> (*editor, "MatrixNextPage")->isEnabled()
                 && save (processor) == original && ! processor.canUndo(),
             "Paging failed to expose row 32, hide earlier rows, or stay separate from sound/history");
    findComponent<juce::Button> (*editor, "mod32Edit")->onClick();
    clearMatrixHistory (processor);
    dispatchEditorActions();
    require (! modulationControlVisible (findComponent<juce::Component> (*editor, "MatrixRouteDetail")),
             "A queued detail action survived a host project replacement");
    clickMatrixControl (*editor, "mod32Edit");
    auto* detail = findComponent<juce::Component> (*editor, "MatrixRouteDetail");
    auto* polarity = findComponent<juce::ComboBox> (*editor, "mod32Polarity");
    auto* curve = findComponent<juce::Slider> (*editor, "mod32Curve");
    auto* auxiliary = findComponent<juce::ComboBox> (*editor, "mod32AuxSource");
    auto* auxPolarity = findComponent<juce::ComboBox> (*editor, "mod32AuxPolarity");
    auto* auxCurve = findComponent<juce::Slider> (*editor, "mod32AuxCurve");
    require (modulationControlVisible (detail) && polarity != nullptr && polarity->getNumItems() == 3
                 && curve != nullptr && curve->getMinimum() == -1 && curve->getMaximum() == 1
                 && auxiliary != nullptr && auxiliary->getNumItems() == 25 && auxPolarity != nullptr
                 && auxPolarity->getNumItems() == 3 && auxCurve != nullptr
                 && ! findComponent<juce::Button> (*editor, "MatrixRouteDetailNext")->isEnabled(),
             "The final row's detailed response and auxiliary controls were unavailable");
    for (auto* control : std::initializer_list<juce::Component*> { polarity, curve, auxiliary, auxPolarity, auxCurve })
        require (detail->getLocalBounds().contains (detail->getLocalArea (control, control->getLocalBounds())),
                 "An advanced route control escaped its detail panel at minimum editor size");
    polarity->setSelectedItemIndex (2, juce::sendNotificationSync);
    curve->onDragStart(); curve->setValue (.4, juce::sendNotificationSync); curve->onDragEnd();
    auxiliary->setSelectedItemIndex (7, juce::sendNotificationSync);
    refreshEditorTimers();
    require (auxPolarity->isEnabled() && auxCurve->isEnabled(), "Choosing AUX left its response controls disabled");
    auxPolarity->setSelectedItemIndex (2, juce::sendNotificationSync);
    auxCurve->setValue (-.3, juce::sendNotificationSync);
    const auto edited = processor.getModulationRoute (31);
    require (edited.polarity == 2 && std::abs (edited.curve - .4f) < 1.0e-6f && edited.auxSource == 7
                 && edited.auxPolarity == 2 && std::abs (edited.auxCurve + .3f) < 1.0e-6f,
             "Detailed matrix UI edits did not reach their stored host parameters");
    const int undoCount = processor.getUndoCount();
    setParameter (processor, "mod32AuxSource", 0); setParameter (processor, "mod32Polarity", 1);
    require (auxiliary->getSelectedItemIndex() == 0,
             "Auxiliary source UI read the stale APVTS cache during host publication; actual="
                 + std::to_string (auxiliary->getSelectedItemIndex()) + " expected=0");
    refreshEditorTimers();
    require (auxiliary->getSelectedItemIndex() == 0 && polarity->getSelectedItemIndex() == 1
                 && ! auxPolarity->isEnabled() && ! auxCurve->isEnabled() && processor.getUndoCount() == undoCount,
             "Host updates left stale detailed controls or became user history; mod32AuxSource UI="
                 + std::to_string (auxiliary->getSelectedItemIndex()) + " effective=" + std::to_string (processor.getModulationSource (31, true))
                 + " legacy=" + std::to_string (plainParameter (processor, "mod32AuxSource"))
                 + " extra=" + std::to_string (plainParameter (processor, "mod32AuxSourceExtra"))
                 + " polarityUI=" + std::to_string (polarity->getSelectedItemIndex())
                 + " auxPolarityEnabled=" + std::to_string (auxPolarity->isEnabled()) + " auxCurveEnabled=" + std::to_string (auxCurve->isEnabled())
                 + " undo actual=" + std::to_string (processor.getUndoCount()) + " expected=" + std::to_string (undoCount));
    setParameter (processor, "mod32AuxCurve", .64f);
    setParameter (processor, "mod32AuxSource", 6); refreshEditorTimers();
    require (auxCurve->isEnabled() && std::abs (auxCurve->getValue() - .64) < 1.0e-5,
             "An auxiliary curve update was lost while its UI was disabled");
    auto* bypass = findComponent<juce::Button> (*editor, "mod32EnabledDetail");
    require (bypass != nullptr, "Detailed route bypass was missing");
    const bool beforeBypass = processor.getModulationRoute (31).enabled;
    bypass->setToggleState (! beforeBypass, juce::sendNotificationSync);
    require (processor.getModulationRoute (31).enabled != beforeBypass && processor.getUndoCount() == undoCount + 1,
             "Detailed route bypass was not one undoable parameter edit");
    expectHistoryResult (processor.undoUserEdit(), "Undo detail route bypass");
    refreshEditorTimers();
    require (bypass->getToggleState() == beforeBypass, "Undo did not reconcile the detailed bypass button");
    clickMatrixControl (*editor, "MatrixDetailClose");
    require (modulationControlVisible (viewport)
                 && viewport->getLocalBounds().contains (viewport->getLocalArea (lastSource, lastSource->getLocalBounds())),
             "Closing route detail lost the prior compact page");
    clickMatrixControl (*editor, "MatrixExpand");
    require (viewport->getHeight() > 300 && modulationControlVisible (findComponent<juce::Button> (*editor, "MatrixCompact")),
             "Expanded matrix did not provide its larger scrollable view");
    require (matrix->hitTest (100, 100) && matrix->hitTest (100, 300),
             "Expanded matrix allowed clicks through to covered LFO or route controls");
    viewport->setViewPosition (0, 0);
    clickMatrixControl (*editor, "mod1Edit");
    for (int route = 1; route <= 32; ++route)
    {
        require (modulationControlVisible (findComponent<juce::ComboBox> (*editor, "mod" + juce::String (route) + "Polarity")),
                 "Detail navigation skipped expected route " + std::to_string (route));
        if (route < 32) clickMatrixControl (*editor, "MatrixRouteDetailNext");
    }
    clickMatrixControl (*editor, "MatrixDetailClose");
    viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight());
    require (viewport->getLocalBounds().contains (viewport->getLocalArea (lastSource, lastSource->getLocalBounds())),
             "Expanded scrolling cannot reach the complete last matrix row");
    clickMatrixControl (*editor, "MatrixCompact");
    require (modulationControlVisible (findComponent<juce::Button> (*editor, "MatrixExpand")),
             "Expanded matrix could not return to compact controls");
}

void testAdvancedMatrixEditorMoveAndCancellation()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    configureMatrixPayload (processor, 0); configureMatrixPayload (processor, 1);
    clearMatrixHistory (processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor (
        new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1120, 760); findButton (*editor, "MOD")->onClick();
    const auto first = matrixPayload (processor, 0), second = matrixPayload (processor, 1);
    auto* handle = findComponent<juce::Component> (*editor, "ModulationRouteHandle1");
    require (modulationControlVisible (handle) && handle->keyPressed (juce::KeyPress (juce::KeyPress::downKey)),
             "A matrix route handle did not accept its keyboard move");
    expectMatrixPayload (processor, 0, second); expectMatrixPayload (processor, 1, first);
    require (processor.getUndoCount() == 1, "A keyboard matrix move was not one complete history action");
    expectHistoryResult (processor.undoUserEdit(), "Undo matrix handle move");
    expectMatrixPayload (processor, 0, first); expectMatrixPayload (processor, 1, second);
    auto* secondHandle = findComponent<juce::Component> (*editor, "ModulationRouteHandle2");
    const auto start = handle->getLocalBounds().toFloat().getCentre();
    const auto end = handle->getLocalPoint (secondHandle, secondHandle->getLocalBounds().getCentre()).toFloat();
    const std::array<const char*, 11> hostMoveFields { "AuxCurve", "Source", "SourceExtra", "AuxSource", "AuxSourceExtra", "Dest", "WarpTarget", "LfoPhaseEnabled", "LfoPhaseTarget", "MacroEnabled", "MacroTarget" };
    const std::array<float, 11> hostMoveValues { -.94f, 9, 15, 9, 15, 7, 5, 0, 9, 0, 7 };
    for (int scenario = -1; scenario < static_cast<int> (hostMoveFields.size()); ++scenario)
    {
        handle->mouseDown (modulationMouseEvent (*handle, start, start));
        handle->mouseDrag (modulationMouseEvent (*handle, end, start));
        if (scenario < 0) clearMatrixHistory (processor);
        else
        {
            const auto id = "mod2" + juce::String (hostMoveFields[static_cast<size_t> (scenario)]);
            setParameter (processor, id.toRawUTF8(), hostMoveValues[static_cast<size_t> (scenario)]);
        }
        const auto expected = parameters (processor);
        handle->mouseUp (modulationMouseEvent (*handle, end, start));
        expectParameters (processor, expected, "Matrix handle cancellation; "
                          + (scenario < 0 ? std::string ("project replacement") : std::string ("mod2") + hostMoveFields[static_cast<size_t> (scenario)]));
        require (! processor.isUserEditActive(), "A cancelled matrix drag left a sound edit active");
    }
    findButton (*editor, "SYNTH")->onClick();
    processor.setModulationSource (0, 1); processor.setModulationDestination (0, 1);
    setParameter (processor, "mod1Amount", .2f); clearMatrixHistory (processor);
    selectModulationSource (*editor, 1); refreshEditorTimers();
    auto* ring = findComponent<juce::Component> (*editor, "cutoffModulationRing");
    require (modulationControlVisible (ring), "Advanced cancellation fixture did not expose its modulation ring");
    const auto point = modulationRingPoint (*ring);
    for (const auto* suffix : { "Enabled", "Polarity", "Curve", "AuxSource", "AuxPolarity", "AuxCurve", "AuxSourceExtra", "SourceExtra", "LfoPhaseTarget", "MacroTarget", "MacroEnabled" })
    {
        ModulationGestureProbe probe (*processor.apvts.getParameter ("mod1Amount"));
        ring->mouseDown (modulationMouseEvent (*ring, point, point));
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -10), point));
        auto* parameter = processor.apvts.getParameter ("mod1" + juce::String (suffix));
        parameter->setValueNotifyingHost (parameter->getValue() > .5f ? 0.0f : 1.0f);
        const auto expected = parameters (processor);
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -50), point));
        ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -50), point));
        expectParameters (processor, expected, std::string ("Matrix ring cancellation after mod1") + suffix);
        require (! processor.isUserEditActive() && probe.starts == probe.ends,
                 "A modulation ring continued after its route's response or bypass changed through host automation");
    }
}

void testAdvancedMatrixUiHostCallbacks()
{
    PresetTestDirectory files;
    const auto makeEditor = [&] (AnyvocAudioProcessor& processor)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (
            new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
        editor->setVisible (true); editor->setSize (1120, 760);
        findButton (*editor, "MOD")->onClick(); clickMatrixControl (*editor, "mod1Edit");
        return editor;
    };
    const auto edit = [] (juce::Component& editor, int control)
    {
        if (control >= 3)
        {
            auto* slider = findComponent<juce::Slider> (editor, control == 3 ? "mod1Curve" : "mod1AmountDetail");
            require (modulationControlVisible (slider), "Matrix slider callback fixture unavailable");
            slider->setValue (control == 3 ? .44 : -.36, juce::sendNotificationSync);
        }
        else if (control == 2)
        {
            auto* button = findComponent<juce::Button> (editor, "mod1EnabledDetail");
            require (modulationControlVisible (button), "Matrix bypass callback fixture unavailable");
            button->setToggleState (false, juce::sendNotificationSync);
        }
        else
        {
            auto* box = findComponent<juce::ComboBox> (editor, control == 0 ? "mod1Polarity" : "mod1AuxSource");
            require (modulationControlVisible (box), "Matrix choice callback fixture unavailable");
            box->setSelectedItemIndex (control == 0 ? 2 : 7, juce::sendNotificationSync);
        }
    };
    const std::array<const char*, 5> ids { "mod1Polarity", "mod1AuxSource", "mod1Enabled", "mod1Curve", "mod1Amount" };
    for (int control = 0; control < 5; ++control) for (int boundary = 0; boundary < 3; ++boundary)
    {
        AnyvocAudioProcessor processor;
        auto editor = makeEditor (processor);
        const auto replacement = save (processor), expected = normallyRestoredProjectState (replacement);
        ModulationGestureProbe probe (*processor.apvts.getParameter (ids[static_cast<size_t> (control)]));
        const auto load = [&] { processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize())); };
        if (boundary == 0) probe.onStart = load; else if (boundary == 1) probe.onValue = load; else probe.onEnd = load;
        edit (*editor, control);
        require ((boundary == 0 ? probe.startFired : boundary == 1 ? probe.valueFired : probe.endFired)
                     && save (processor) == expected && probe.starts == probe.ends
                     && ! processor.isUserEditActive() && ! processor.canUndo() && ! processor.canRedo(),
                 "A matrix choice/bypass/slider UI callback overwrote a host project or retained stale history; "
                     + historyStateDiagnostic (processor, expected));
    }
    for (const int control : { 0, 1, 3, 4 }) for (int boundary = 0; boundary < 3; ++boundary)
    {
        AnyvocAudioProcessor processor;
        auto editor = makeEditor (processor);
        ModulationGestureProbe probe (*processor.apvts.getParameter (ids[static_cast<size_t> (control)]));
        std::vector<float> expected;
        const auto automate = [&]
        {
            setParameter (processor, ids[static_cast<size_t> (control)], control == 0 ? 1.0f : control == 1 ? 3.0f : -.72f);
            expected = parameters (processor);
        };
        if (boundary == 0) probe.onStart = automate; else if (boundary == 1) probe.onValue = automate; else probe.onEnd = automate;
        edit (*editor, control);
        require (! expected.empty(), "Matrix UI host automation fixture did not fire");
        expectParameters (processor, expected);
        if (control < 2)
        {
            auto* box = findComponent<juce::ComboBox> (*editor, ids[static_cast<size_t> (control)]);
            require (box != nullptr && box->getSelectedItemIndex() == (control == 0 ? 1 : 3),
                     "A matrix choice failed to reconcile newer host automation");
        }
        else
        {
            auto* slider = findComponent<juce::Slider> (*editor, control == 3 ? "mod1Curve" : "mod1AmountDetail");
            require (slider != nullptr && std::abs (slider->getValue() + .72) < 1.0e-5,
                     "A matrix slider failed to reconcile newer host automation");
        }
        require (probe.starts == probe.ends && ! processor.isUserEditActive(),
                 "A matrix UI control kept a gesture open after newer host automation");
    }
    for (int control = 0; control < 5; ++control) for (int boundary = 0; boundary < 3; ++boundary)
    {
        AnyvocAudioProcessor processor;
        ModulationGestureProbe probe (*processor.apvts.getParameter (ids[static_cast<size_t> (control)]));
        // JUCE iterates parameter listeners by reverse index. Register this
        // host-like observer before GUI attachments so removing those later
        // listeners during a callback cannot shift this observer into a second notification.
        auto editor = makeEditor (processor);
        const auto destroy = [&] { editor.reset(); };
        if (boundary == 0) probe.onStart = destroy; else if (boundary == 1) probe.onValue = destroy; else probe.onEnd = destroy;
        edit (*editor, control);
        const float before = control == 2 ? 1.0f : 0.0f;
        const float changed = control == 0 ? 2.0f : control == 1 ? 7.0f : control == 2 ? 0.0f : control == 3 ? .44f : -.36f;
        require (editor == nullptr && probe.starts == probe.ends && ! processor.isUserEditActive()
                     && std::abs (plainParameter (processor, ids[static_cast<size_t> (control)]) - (boundary == 0 ? before : changed)) < 1.0e-6f,
                 "Destroying the editor during a matrix UI callback left a stale write or gesture; control="
                     + std::to_string (control) + "; boundary=" + std::to_string (boundary)
                     + "; alive=" + std::to_string (editor != nullptr) + "; starts=" + std::to_string (probe.starts)
                     + "; ends=" + std::to_string (probe.ends) + "; active=" + std::to_string (processor.isUserEditActive())
                     + "; actual=" + std::to_string (plainParameter (processor, ids[static_cast<size_t> (control)]))
                     + "; expected=" + std::to_string (boundary == 0 ? before : changed));
    }
    for (const int control : { 3, 4 }) for (int scenario = 0; scenario < 7; ++scenario)
    {
        AnyvocAudioProcessor processor;
        const auto id = ids[static_cast<size_t> (control)];
        ModulationGestureProbe probe (*processor.apvts.getParameter (id));
        auto editor = makeEditor (processor);
        const auto replacement = save (processor);
        auto* slider = findComponent<juce::Slider> (*editor, control == 3 ? "mod1Curve" : "mod1AmountDetail");
        require (slider != nullptr && static_cast<bool> (slider->onDragStart) && static_cast<bool> (slider->onDragEnd),
                 "Matrix continuous slider fixture lacked gesture callbacks");
        const juce::Component::SafePointer<juce::Slider> safe (slider);
        const auto load = [&] { processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize())); };
        const auto automate = [&] { setParameter (processor, id, -.72f); };
        const auto destroy = [&] { editor.reset(); };
        if (scenario == 0) probe.onStart = load;
        if (scenario == 2) probe.onStart = automate;
        if (scenario == 4) probe.onStart = destroy;
        if (scenario == 5) probe.onValue = destroy;
        if (scenario == 6) probe.onEnd = destroy;
        slider->onDragStart();
        if (safe != nullptr) safe->setValue (.2, juce::sendNotificationSync);
        if (scenario == 1) load();
        if (scenario == 3) automate();
        if (safe != nullptr) safe->setValue (.7, juce::sendNotificationSync);
        if (safe != nullptr) safe->onDragEnd();
        const float expected = scenario <= 1 || scenario == 4 ? 0.0f : scenario <= 3 ? -.72f : scenario == 5 ? .2f : .7f;
        require (std::abs (plainParameter (processor, id) - expected) < 1.0e-6f
                     && ! processor.isUserEditActive() && probe.starts == probe.ends
                     && (scenario < 4 || editor == nullptr),
                 "A continuous matrix slider retained a stale drag after restore, automation or editor destruction");
    }
}

void testExpandedSourceParameterCompatibility()
{
    AnyvocAudioProcessor processor;
    require (anyvoc::lfoCount == 10 && anyvoc::macroCount == 8 && anyvoc::modulationSourceCount == 25,
             "Expanded modulator/source counts changed");
    const auto& all = processor.getParameters();
    int next = all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount - advancedEnvelopeParameterCount - expandedSourceParameterCount;
    const auto parameter = [&] (const juce::String& id)
    {
        auto* result = dynamic_cast<juce::RangedAudioParameter*> (all[next++]);
        require (result != nullptr && result->getParameterID() == id && result->getVersionHint() == 10,
                 "A schema 10 parameter moved or lost its host version: " + id.toStdString());
        return result;
    };
    const auto floating = [&] (const juce::String& id, float low, float high, float interval, float skew, float defaultValue)
    {
        auto* result = parameter (id);
        const auto& range = result->getNormalisableRange();
        require (range.start == low && range.end == high && range.interval == interval && range.skew == skew
                     && std::abs (result->convertFrom0to1 (result->getDefaultValue()) - range.snapToLegalValue (defaultValue)) < 2.0e-6f,
                 "Expanded source range/default changed: " + id.toStdString());
    };
    const auto choice = [&] (const juce::String& id, int count, int defaultValue)
    {
        auto* result = parameter (id);
        require (result->getNumSteps() == count
                     && result->convertFrom0to1 (result->getDefaultValue()) == defaultValue,
                 "Expanded source choice/default changed: " + id.toStdString());
        for (int value = 0; value < count; ++value)
            require (result->convertFrom0to1 (static_cast<float> (value) / (count - 1)) == value,
                     "Expanded source normalized choice mapping changed: " + id.toStdString());
    };
    for (int lfo = 3; lfo <= 10; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo);
        floating (prefix + "Rate", .05f, 20, 0, .4f, static_cast<float> (lfo));
        choice (prefix + "Shape", 4, 0); choice (prefix + "Kind", 3, 0); choice (prefix + "Sync", 2, 0);
        choice (prefix + "Division", 12, 4); choice (prefix + "Mode", 3, 0); choice (prefix + "Interpolation", 2, 0);
        for (int point = 0; point <= 32; ++point)
            floating (prefix + "Point" + juce::String (point), -1, 1, .01f, 1,
                      std::sin (juce::MathConstants<float>::twoPi * point / 32.0f));
    }
    for (int env = 3; env <= 4; ++env)
    {
        const auto prefix = "env" + juce::String (env);
        floating (prefix + "Attack", .001f, 8, 0, .3f, .01f);
        floating (prefix + "Decay", .001f, 8, 0, .3f, .2f);
        floating (prefix + "Sustain", 0, 1, 0, 1, .3f);
        floating (prefix + "Release", .001f, 8, 0, .3f, .3f);
    }
    for (int macro = 5; macro <= 8; ++macro) floating ("macro" + juce::String (macro), 0, 1, 0, 1, 0);
    for (int route = 1; route <= 32; ++route)
    {
        choice ("mod" + juce::String (route) + "SourceExtra", 16, 0);
        choice ("mod" + juce::String (route) + "AuxSourceExtra", 16, 0);
        // Both original selectors must retain their ten-choice automation scale.
        for (const auto* suffix : { "Source", "AuxSource" })
        {
            auto* old = processor.apvts.getParameter ("mod" + juce::String (route) + suffix);
            require (old->getNumSteps() == 10 && old->getVersionHint() < 10,
                     "Extending sources reinterpreted old source automation");
            for (int value = 0; value < 10; ++value)
                require (old->convertFrom0to1 (value / 9.0f) == value, "A legacy source index was renumbered");
        }
    }
    require (next == all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount - advancedEnvelopeParameterCount, "The v0.14 source controls moved from their frozen 396-parameter block");
}

void testExpandedSourceStateValidation()
{
    AnyvocAudioProcessor source;
    int visited = 0;
    for (auto* base : source.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
            parameter != nullptr && isExpandedSourceParameter (parameter->getParameterID()))
        {
            // AudioParameterBool stores arbitrary normalized floats verbatim,
            // whereas APVTS serializes its snapped legal boolean. Use valid
            // normalized choices so the fixture tests real state roundtrips.
            const auto normalized = .12f + (visited++ % 7) * .12f;
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (parameter->convertFrom0to1 (normalized)));
        }
    require (visited == expandedSourceParameterCount, "Expanded state fixture missed a host control");
    const auto expected = parameters (source);
    const auto current = stateTree (source);
    AnyvocAudioProcessor target;
    restore (target, current); expectParameters (target, expected);
    juce::TemporaryFile file (".anyvoc");
    require (source.savePreset (file.getFile()) && target.loadPreset (file.getFile()), "Expanded-source preset roundtrip failed");
    expectParameters (target, expected);
    target.setCurrentProgram (0);
    for (auto* base : target.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (base);
            parameter != nullptr && isExpandedSourceParameter (parameter->getParameterID()))
        {
            // AudioParameterFloat writes a normalized value through its range,
            // then reads it back through the inverse. Skewed float ranges need
            // not return the original normalized bit pattern after that roundtrip.
            // Assert the exact canonical result, with no numerical tolerance.
            const auto definedDefault = parameter->getDefaultValue();
            const auto canonicalDefault = parameter->convertTo0to1 (parameter->convertFrom0to1 (definedDefault));
            const auto diagnostic = "id=" + parameter->getParameterID()
                + " actual=" + juce::String (parameter->getValue(), 12)
                + " default=" + juce::String (definedDefault, 12) + " canonical=" + juce::String (canonicalDefault, 12)
                + " raw=" + juce::String (plainParameter (target, parameter->getParameterID()), 12);
            require (parameter->getValue() == canonicalDefault, "Init retained an expanded source parameter; " + diagnostic.toStdString());
            if (parameter->getValue() != definedDefault)
                std::cout << "INFO Canonical Init default: " << diagnostic << '\n';
        }

    AnyvocAudioProcessor legacy;
    legacy.setCurrentProgram (6); setParameter (legacy, "mod32Source", 9); setParameter (legacy, "mod32AuxSource", 2);
    const auto legacyExpected = parameters (legacy);
    auto v9 = stateTree (legacy); v9.setProperty ("schemaVersion", 9, nullptr);
    for (int child = v9.getNumChildren(); --child >= 0;)
        if (isExpandedSourceParameter (v9.getChild (child).getProperty ("id").toString())
            || isAdvancedEnvelopeParameter (v9.getChild (child).getProperty ("id").toString())
            || (isLfoMotionParameter (v9.getChild (child).getProperty ("id").toString()) || (isChaosParameter (v9.getChild (child).getProperty ("id").toString()) || isMacroDestinationParameter (v9.getChild (child).getProperty ("id").toString())))) v9.removeChild (child, nullptr);
    restore (target, current); restore (target, v9); expectParameters (target, legacyExpected);
    require (target.getModulationSource (31) == 9 && target.getModulationSource (31, true) == 2,
             "Schema 9 migration shadowed its old primary/auxiliary source");
    const auto retained = save (target);
    for (const auto* id : { "lfo3Rate", "lfo7Kind", "lfo10Point32", "env3Attack", "env4Release", "macro8",
                           "mod1SourceExtra", "mod32AuxSourceExtra" })
    {
        auto missing = current.createCopy(); missing.removeChild (missing.getChildWithProperty ("id", id), nullptr);
        restore (target, missing); require (save (target) == retained, std::string ("Schema 10 accepted missing ") + id);
        auto duplicate = current.createCopy(); duplicate.addChild (duplicate.getChildWithProperty ("id", id).createCopy(), -1, nullptr);
        restore (target, duplicate); require (save (target) == retained, std::string ("Schema 10 accepted duplicate ") + id);
        for (const auto* bad : { "NaN", "Infinity", "not a number" })
        {
            auto invalid = current.createCopy(); invalid.getChildWithProperty ("id", id).setProperty ("value", bad, nullptr);
            restore (target, invalid); require (save (target) == retained, std::string ("Invalid source state partially loaded: ") + id);
        }
    }
}

void testExpandedSourceSelectionAndAssignment()
{
    AnyvocAudioProcessor processor;
    for (const bool auxiliary : { false, true })
    {
        const juce::String id = auxiliary ? "mod32AuxSource" : "mod32Source";
        setParameter (processor, id.toRawUTF8(), 7);
        for (int source = 10; source <= 24; ++source)
        {
            clearMatrixHistory (processor);
            const auto before = matrixPayload (processor, 31);
            processor.setModulationSource (31, source, auxiliary);
            require (processor.getModulationSource (31, auxiliary) == source
                         && plainParameter (processor, id) == 7 && plainParameter (processor, id + "Extra") == source - 9
                         && processor.getUndoCount() == 1,
                     "An expanded source overwrote its inactive legacy value or fragmented history");
            const auto after = matrixPayload (processor, 31);
            expectHistoryResult (processor.undoUserEdit(), "Undo expanded source selection");
            expectMatrixPayload (processor, 31, before);
            expectHistoryResult (processor.redoUserEdit(), "Redo expanded source selection");
            expectMatrixPayload (processor, 31, after);
            const auto unchanged = save (processor); const auto count = processor.getUndoCount();
            processor.setModulationSource (31, source, auxiliary);
            require (save (processor) == unchanged && processor.getUndoCount() == count, "Repeated source selection created an edit");
        }
        for (int source = 0; source <= 9; ++source)
        {
            processor.setModulationSource (31, 24, auxiliary);
            processor.setModulationSource (31, source, auxiliary);
            require (processor.getModulationSource (31, auxiliary) == source && plainParameter (processor, id) == source
                         && plainParameter (processor, id + "Extra") == 0, "Selecting an old source failed to clear its extension bank");
        }
    }
    const auto unchanged = save (processor); const auto revision = processor.getHistoryRevision();
    for (const bool auxiliary : { false, true })
    {
        processor.setModulationSource (-1, 1, auxiliary); processor.setModulationSource (32, 24, auxiliary);
        processor.setModulationSource (0, -1, auxiliary); processor.setModulationSource (0, 25, auxiliary);
    }
    require (save (processor) == unchanged && processor.getHistoryRevision() == revision
                 && processor.getModulationSource (-1) == 0 && processor.getModulationSource (32, true) == 0,
             "Invalid source selection changed state/history or escaped its range");
    processor.setCurrentProgram (0);
    require (processor.assignModulation (24, 17, -.23457f), "Macro 8 could not be assigned");
    setParameter (processor, "mod1Source", 2); // This remembered source must not affect occupied/duplicate matching.
    setParameter (processor, "mod1Enabled", 0); setParameter (processor, "mod1AuxSourceExtra", 8);
    clearMatrixHistory (processor);
    auto expected = matrixPayload (processor, 0); expected[7] = 1;
    require (processor.assignModulation (24, 17, .9f), "An expanded bypassed duplicate could not be re-enabled");
    expectMatrixPayload (processor, 0, expected);
    require (processor.getUndoCount() == 1 && processor.getModulationSource (1) == 0,
             "Duplicate matching used the inactive legacy selector");
    require (processor.assignModulation (2, 17, .1f) && processor.getModulationSource (1) == 2,
             "An inactive legacy value falsely matched an existing expanded route");
    setParameter (processor, "mod1WarpEnabled", 0); setParameter (processor, "mod1Dest", 0);
    require (processor.assignModulation (10, 12, .33337f), "A vacant expanded-source row could not be reused");
    const auto reused = processor.getModulationRoute (0);
    require (reused.source == 10 && reused.destination == 12 && reused.auxSource == 0 && reused.enabled
                 && plainParameter (processor, "mod1Source") == 0 && plainParameter (processor, "mod1SourceExtra") == 1
                 && plainParameter (processor, "mod1AuxSourceExtra") == 0 && std::abs (reused.amount - .33337f) < 1.0e-6f,
             "An expanded assignment inherited hidden legacy/auxiliary state or quantised its depth");
}

void testExpandedSourceHostBoundaries()
{
    // Switching back to an old source writes two selectors. A host callback at
    // any boundary of either write must cancel every remaining stale write.
    for (const bool auxiliary : { false, true }) for (int triggerField = 0; triggerField < 2; ++triggerField)
        for (int boundary = 0; boundary < 3; ++boundary) for (const bool replace : { false, true })
        {
            AnyvocAudioProcessor processor;
            const juce::String id = auxiliary ? "mod32AuxSource" : "mod32Source";
            setParameter (processor, id.toRawUTF8(), 2); processor.setModulationSource (31, 24, auxiliary);
            clearMatrixHistory (processor);
            const auto replacement = save (processor);
            ModulationGestureProbe legacy (*processor.apvts.getParameter (id)), extra (*processor.apvts.getParameter (id + "Extra"));
            auto& trigger = triggerField == 0 ? legacy : extra;
            std::vector<float> expected;
            const auto callback = [&]
            {
                if (replace) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else setParameter (processor, (id + (triggerField == 0 ? "Extra" : "")).toRawUTF8(), triggerField == 0 ? 7 : 8);
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback;
            else if (boundary == 1) trigger.onValue = callback;
            else trigger.onEnd = callback;
            processor.setModulationSource (31, 5, auxiliary);
            require (! expected.empty(), "Composite source host callback never ran");
            expectParameters (processor, expected, "source auxiliary=" + std::to_string (auxiliary) + " field="
                              + std::to_string (triggerField) + " boundary=" + std::to_string (boundary) + " replace=" + std::to_string (replace));
            require (legacy.starts == legacy.ends && extra.starts == extra.ends && ! processor.isUserEditActive(),
                     "A composite source selection left an unbalanced host gesture");
            if (replace) require (! processor.canUndo() && ! processor.canRedo(), "Host replacement retained stale source history");
        }
    for (int triggerField = 0; triggerField < 2; ++triggerField) for (int boundary = 0; boundary < 3; ++boundary)
    {
        AnyvocAudioProcessor processor;
        setParameter (processor, "lfo10Shape", 1); processor.setLfoShape (9, 5); clearMatrixHistory (processor);
        ModulationGestureProbe shape (*processor.apvts.getParameter ("lfo10Shape")), kind (*processor.apvts.getParameter ("lfo10Kind"));
        auto& trigger = triggerField == 0 ? shape : kind;
        std::vector<float> expected;
        const auto host = [&]
        {
            setParameter (processor, triggerField == 0 ? "lfo10Kind" : "lfo10Shape", triggerField == 0 ? 1 : 3);
            expected = parameters (processor);
        };
        if (boundary == 0) trigger.onStart = host;
        else if (boundary == 1) trigger.onValue = host;
        else trigger.onEnd = host;
        processor.setLfoShape (9, 2);
        require (! expected.empty(), "LFO composite selector host callback never ran"); expectParameters (processor, expected);
        require (shape.starts == shape.ends && kind.starts == kind.ends && ! processor.isUserEditActive(),
                 "LFO shape helper overwrote a related host edit or left a gesture open");
    }
}

void prepareExpandedSourceAudio (AnyvocAudioProcessor& processor, int source, bool auxiliary, int lfoStyle = 0)
{
    prepareCrossModulationFixture (processor);
    processor.setWarpMode (0, 0, 6); setParameter (processor, "osc1Warp1Amount", .2f);
    setParameter (processor, "tempo", 117);
    for (int lfo = 1; lfo <= 10; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo);
        setParameter (processor, (prefix + "Rate").toRawUTF8(), 3.17f);
        setParameter (processor, (prefix + "Mode").toRawUTF8(), static_cast<float> (lfoStyle % 3));
        setParameter (processor, (prefix + "Sync").toRawUTF8(), lfoStyle >= 2 ? 1 : 0);
        setParameter (processor, (prefix + "Division").toRawUTF8(), 6);
        processor.setLfoShape (lfo - 1, lfoStyle == 0 ? 1 : 5);
        setParameter (processor, (prefix + "Interpolation").toRawUTF8(), lfoStyle == 3 ? 1 : 0);
        for (int point = 0; point <= 32; ++point)
            setParameter (processor, (prefix + "Point" + juce::String (point)).toRawUTF8(),
                          point < 8 ? -.7f : point < 16 ? .6f : point < 24 ? -.2f : .8f);
    }
    for (const auto* prefix : { "amp", "env", "env3", "env4" })
    {
        setParameter (processor, (juce::String (prefix) + "Attack").toRawUTF8(), .025f);
        setParameter (processor, (juce::String (prefix) + "Decay").toRawUTF8(), .09f);
        setParameter (processor, (juce::String (prefix) + "Sustain").toRawUTF8(), .4f);
        setParameter (processor, (juce::String (prefix) + "Release").toRawUTF8(), .08f);
    }
    for (int macro = 1; macro <= 8; ++macro) setParameter (processor, ("macro" + juce::String (macro)).toRawUTF8(), .6f);
    processor.setModulationDestination (31, 12);
    processor.setModulationSource (31, auxiliary ? 6 : source);
    processor.setModulationSource (31, auxiliary ? source : 0, true);
    setParameter (processor, "mod32Amount", .25f);
    setParameter (processor, auxiliary ? "mod32AuxCurve" : "mod32Curve", .25f);
    processor.prepareToPlay (48000, 512);
}

void testExpandedSourcesReachAudio()
{
    const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 60, .85f) },
                                   { 9000, juce::MidiMessage::noteOff (1, 60) } };
    for (int source = 10; source <= 24; ++source) for (const bool auxiliary : { false, true })
    {
        const int referenceSource = source <= 17 ? 1 : source <= 20 ? 3 : 6;
        const int style = source <= 17 ? (source - 10) % 4 : 0;
        AnyvocAudioProcessor actual, reference, silentRoute;
        prepareExpandedSourceAudio (actual, source, auxiliary, style);
        prepareExpandedSourceAudio (reference, referenceSource, auxiliary, style);
        prepareExpandedSourceAudio (silentRoute, referenceSource, auxiliary, style);
        setParameter (silentRoute, "mod32Amount", 0);
        const auto audio = render (actual, 16000, notes, 127);
        const auto expected = render (reference, 16000, notes, 127);
        const auto baseline = render (silentRoute, 16000, notes, 127);
        require (rms (audio) > .005 && crossAudioDifference (audio, expected) < 2.0e-6
                     && crossAudioDifference (audio, baseline) > .0001,
                 "Expanded source " + std::to_string (source) + (auxiliary ? " AUX" : " primary")
                     + " disagreed with its legacy-source audio reference; difference="
                     + std::to_string (crossAudioDifference (audio, expected)));
    }
    // The last LFO's independent phase, random seed and custom buffer must be
    // repeatable after reset, including when the host changes block size.
    for (int shape : { 4, 5 })
    {
        AnyvocAudioProcessor first, second;
        prepareExpandedSourceAudio (first, 17, false, 0); prepareExpandedSourceAudio (second, 17, false, 0);
        first.setLfoShape (9, shape); second.setLfoShape (9, shape);
        const auto a = render (first, 16000, notes, 64), b = render (second, 16000, notes, 257);
        require (crossAudioDifference (a, b) < 2.0e-6, "LFO 10 random/custom playback depended on host block size");
    }
}

void testExpandedFactoryPresets()
{
    AnyvocAudioProcessor processor;
    require (processor.getNumPrograms() == 28 && processor.getProgramName (18) == "Motion / Prism Weave"
                 && processor.getProgramName (19) == "Pad / Four Horizons", "Expanded source presets displaced the old factory bank");
    for (int program = 0; program < processor.getNumPrograms(); ++program)
    {
        processor.setCurrentProgram (program); processor.prepareToPlay (48000, 512);
        const auto audio = render (processor, 24000, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }, 257);
        require (rms (audio) > .0001, "Factory program " + std::to_string (program) + " was silent after the source expansion");
    }
    processor.setCurrentProgram (18);
    require (processor.getModulationSource (0) == 17 && processor.getModulationSource (1) == 10
                 && processor.getModulationSource (2) == 11 && processor.getModulationSource (3) == 19
                 && processor.getModulationSource (3, true) == 21, "Prism Weave did not demonstrate its documented LFO/envelope/macro sources");
    processor.setCurrentProgram (19);
    require (processor.getModulationSource (0) == 19 && processor.getModulationSource (1) == 20
                 && processor.getModulationSource (2) == 16 && processor.getModulationSource (2, true) == 24
                 && processor.getModulationSource (3) == 18, "Four Horizons did not demonstrate all four envelope/source families");
    processor.setCurrentProgram (20);
    require (processor.getProgramName (20) == "Motion / Clockwork Bloom" && plainParameter (processor, "ampTempoSync") == 1
                 && plainParameter (processor, "envTempoSync") == 1 && plainParameter (processor, "envDelayDivision") > 0
                 && plainParameter (processor, "ampAttackCurve") != 0, "Clockwork Bloom did not demonstrate synchronized curved stages");
    processor.setCurrentProgram (21);
    require (processor.getProgramName (21) == "Lead / Second Breath" && plainParameter (processor, "voiceMode") == 2
                 && plainParameter (processor, "envRetriggerLegato") == 1 && plainParameter (processor, "env3RetriggerLegato") == 1
                 && plainParameter (processor, "ampRetriggerLegato") == 0, "Second Breath did not demonstrate independent envelope legato retrigger");
    processor.setCurrentProgram (22);
    require (processor.getProgramName (22) == "Motion / Swing Carousel" && processor.getLfoRateMode (0) == 1
                 && plainParameter (processor, "lfo1FollowSwing") == 1 && plainParameter (processor, "lfo1Direction") == 2
                 && std::abs (plainParameter (processor, "swingAmount") - .6f) < 1.0e-6f, "Swing Carousel did not expose its clocked swing/ping-pong example");
    processor.setCurrentProgram (23);
    require (processor.getProgramName (23) == "Keys / Phase Lantern" && processor.getLfoRateMode (0) == 2
                 && std::abs (plainParameter (processor, "lfo1AudioRateHz") - 110) < .001f
                 && processor.getModulationDestination (1) == 18 && processor.getModulationSource (1) == 2,
             "Phase Lantern did not expose its audio-rate LFO and second-LFO phase routing");
    processor.setCurrentProgram (24);
    require (processor.getLfoShape (0) == 6 && processor.getModulationSource (0) == 1
                 && processor.getModulationDestination (0) == 1 && processor.getModulationDestination (1) == 2,
             "Butterfly Current did not route its Lorenz source to cutoff and wavetable position");
    processor.setCurrentProgram (25);
    require (processor.getLfoShape (0) == 7 && processor.getModulationSource (0) == 1
                 && processor.getModulationDestination (0) == 1 && processor.getModulationDestination (1) == 7,
             "Spiral Drift did not route its Rossler source to cutoff and pan");
}

void testExpandedSourceEditorNavigation()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (
        new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1008, 684); findButton (*editor, "MOD")->onClick();
    auto* first = findComponent<juce::ComboBox> (*editor, "LfoCardSelect1");
    auto* second = findComponent<juce::ComboBox> (*editor, "LfoCardSelect2");
    auto* envelope = findComponent<juce::ComboBox> (*editor, "EnvelopeSelect");
    require (modulationControlVisible (first) && modulationControlVisible (second) && first->getNumItems() == 10
                 && second->getNumItems() == 10 && modulationControlVisible (envelope) && envelope->getNumItems() == 4,
             "Expanded LFO/envelope view selectors were unavailable");
    const auto unchanged = save (processor);
    const auto unchangedParameters = parameters (processor);
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        first->setSelectedItemIndex (lfo, juce::sendNotificationSync);
        auto* display = findComponent<juce::Component> (*editor, "lfo" + juce::String (lfo + 1) + "Display");
        require (first->getSelectedItemIndex() != second->getSelectedItemIndex() && modulationControlVisible (display)
                     && editor->getLocalBounds().contains (editor->getLocalArea (display, display->getLocalBounds())),
                 "An LFO card could not display every source without overlapping the other card");
    }
    auto* rate = findComponent<juce::Slider> (*editor, "lfo10Rate");
    require (modulationControlVisible (rate), "The tenth LFO rate knob was hidden");
    require (save (processor) == unchanged && ! processor.canUndo(),
             "LFO card navigation changed the sound; " + historyStateDiagnostic (processor, unchanged));
    const auto originalRate = plainParameter (processor, "lfo1Rate");
    rate->setValue (4.321, juce::sendNotificationSync);
    require (std::abs (plainParameter (processor, "lfo10Rate") - 4.321f) < 1.0e-5f
                 && plainParameter (processor, "lfo1Rate") == originalRate && processor.getUndoCount() == 1,
             "LFO 10's rate control edited another LFO or lost its undo action");
    expectHistoryResult (processor.undoUserEdit(), "Undo tenth LFO rate");
    expectParameters (processor, unchangedParameters, "Undo tenth LFO rate");
    clearMatrixHistory (processor);
    if (save (processor) != unchanged)
        std::cout << "INFO Navigation fixture after ordinary restore: " << historyStateDiagnostic (processor, unchanged) << '\n';
    const auto beforeLabNavigation = save (processor);
    clickMatrixControl (*editor, "lfo10Edit");
    int visible = 10;
    for (int lfo = 1; lfo <= 10; ++lfo)
    {
        auto* selector = findComponent<juce::ComboBox> (*editor, "LFOLabSelector" + juce::String (visible));
        require (modulationControlVisible (selector) && selector->getNumItems() == 10, "LFO Lab source navigator was unavailable");
        selector->setSelectedItemIndex (lfo - 1, juce::sendNotificationSync);
        visible = lfo;
        require (modulationControlVisible (findComponent<juce::Component> (*editor, "LfoDetail" + juce::String (lfo))),
                 "LFO Lab navigation did not open its selected independent curve");
    }
    auto* detail = findComponent<juce::Component> (*editor, "LfoDetail10");
    require (save (processor) == beforeLabNavigation && ! processor.canUndo(),
             "LFO Lab navigation changed the sound; " + historyStateDiagnostic (processor, beforeLabNavigation));
    auto* pulses = findButton (*detail, "PULSES");
    require (pulses != nullptr && static_cast<bool> (pulses->onClick), "LFO 10's editable curve actions were missing");
    const auto oldPoint = plainParameter (processor, "lfo1Point5");
    pulses->onClick();
    require (processor.getLfoShape (9) == 5 && processor.getLfoShape (0) == 0
                 && plainParameter (processor, "lfo1Point5") == oldPoint && processor.getUndoCount() == 1,
             "Editing the last custom LFO changed another source or split its curve history");
    expectHistoryResult (processor.undoUserEdit(), "Undo LFO 10 curve");
    expectParameters (processor, unchangedParameters, "Undo LFO 10 curve");
    clearMatrixHistory (processor);
    const auto beforeEnvelopeNavigation = save (processor);
    findButton (*detail, "DONE")->onClick();
    for (int env = 0; env < 4; ++env)
    {
        envelope->setSelectedItemIndex (env, juce::sendNotificationSync);
        const juce::String id = env == 0 ? "envAttack" : env == 1 ? "ampAttackMod" : "env" + juce::String (env + 1) + "Attack";
        auto* control = findComponent<juce::Slider> (*editor, id);
        require (modulationControlVisible (control) && editor->getLocalBounds().contains (editor->getLocalArea (control, control->getLocalBounds())),
                 "An envelope could not be selected at minimum editor size");
    }
    clickMatrixControl (*editor, "MacroBank2");
    for (int macro = 1; macro <= 8; ++macro)
        require (modulationControlVisible (findComponent<juce::Slider> (*editor, "macro" + juce::String (macro))) == (macro >= 5),
                 "Macro bank selection failed to expose exactly four controls");
    require (save (processor) == beforeEnvelopeNavigation && ! processor.canUndo(),
             "Envelope/macro navigation changed the sound; " + historyStateDiagnostic (processor, beforeEnvelopeNavigation));
    findComponent<juce::Slider> (*editor, "env4Decay")->setValue (.4567, juce::sendNotificationSync);
    findComponent<juce::Slider> (*editor, "macro8")->setValue (.76543, juce::sendNotificationSync);
    require (std::abs (plainParameter (processor, "env4Decay") - .4567f) < 1.0e-5f
                 && std::abs (plainParameter (processor, "macro8") - .76543f) < 1.0e-6f
                 && plainParameter (processor, "macro4") == 0, "Expanded envelope/macro controls did not reach their independent host values");
    setParameter (processor, "macro8", .23456f); refreshEditorTimers();
    require (std::abs (findComponent<juce::Slider> (*editor, "macro8")->getValue() - .23456) < 1.0e-5,
             "Host automation did not refresh Macro 8");
    clickMatrixControl (*editor, "openModulationSources");
    auto* palette = findComponent<juce::Component> (*editor, "ModulationSourcePalette");
    require (modulationControlVisible (palette), "The complete source palette did not open");
    const auto paletteState = save (processor); const auto history = processor.getUndoCount();
    for (int source = 1; source <= 24; ++source)
    {
        auto* chip = findComponent<juce::Component> (*editor, "paletteModSource" + juce::String (source));
        require (modulationControlVisible (chip) && palette->getLocalBounds().contains (palette->getLocalArea (chip, chip->getLocalBounds())),
                 "A source was missing or clipped in the complete palette");
        const auto point = chip->getLocalBounds().toFloat().getCentre();
        const auto event = modulationMouseEvent (*chip, point, point);
        chip->mouseDown (event); chip->mouseUp (event);
    }
    require (save (processor) == paletteState && processor.getUndoCount() == history, "Selecting a palette source edited sound/history");
    clickMatrixControl (*editor, "SourcePaletteClose");
    require (! modulationControlVisible (palette), "The source palette could not close");
}

void testExpandedSourceUiHostCallbacks()
{
    PresetTestDirectory files;
    for (const bool auxiliary : { false, true }) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 3; ++action)
        {
            AnyvocAudioProcessor processor;
            const juce::String id = auxiliary ? "mod1AuxSource" : "mod1Source";
            setParameter (processor, id.toRawUTF8(), 2); processor.setModulationSource (0, 24, auxiliary);
            clearMatrixHistory (processor);
            ModulationGestureProbe legacy (*processor.apvts.getParameter (id)), extra (*processor.apvts.getParameter (id + "Extra"));
            std::unique_ptr<juce::AudioProcessorEditor> editor (
                new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684); findButton (*editor, "MOD")->onClick();
            clickMatrixControl (*editor, "mod1Edit");
            const auto replacement = save (processor);
            std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, (id + "Extra").toRawUTF8(), 7);
                else editor.reset();
                expected = parameters (processor);
            };
            if (boundary == 0) legacy.onStart = callback;
            else if (boundary == 1) legacy.onValue = callback;
            else legacy.onEnd = callback;
            auto* box = findComponent<juce::ComboBox> (*editor, id + (auxiliary ? "" : "Detail"));
            require (modulationControlVisible (box) && box->getNumItems() == 25 && box->getSelectedItemIndex() == 24,
                     "Composite source selector failed to show its extension bank");
            box->setSelectedItemIndex (5, juce::sendNotificationSync);
            require (! expected.empty(), "Expanded source UI host callback did not run");
            expectParameters (processor, expected, "source UI auxiliary=" + std::to_string (auxiliary)
                              + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action));
            require (legacy.starts == legacy.ends && extra.starts == extra.ends && ! processor.isUserEditActive(),
                     "Expanded source UI retained an unfinished gesture across a host callback");
            if (editor != nullptr)
            {
                refreshEditorTimers();
                require (box->getSelectedItemIndex() == processor.getModulationSource (0, auxiliary),
                         "Composite source UI did not reconcile its host's final effective value");
            }
        }
}

void testExpandedLfoCurveHostCallbacks()
{
    PresetTestDirectory files;
    for (const bool preset : { false, true }) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 9; ++action)
        {
            AnyvocAudioProcessor processor;
            processor.setLfoShape (9, 5); setParameter (processor, "lfo10Phase", .25f); clearMatrixHistory (processor);
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (int point = 0; point <= 32; ++point)
                probes.push_back (std::make_unique<ModulationGestureProbe> (
                    *processor.apvts.getParameter ("lfo10Point" + juce::String (point))));
            for (const auto* suffix : { "Shape", "Kind", "Interpolation" })
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter ("lfo10" + juce::String (suffix))));
            std::unique_ptr<juce::AudioProcessorEditor> editor (
                new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684); findButton (*editor, "MOD")->onClick();
            findComponent<juce::ComboBox> (*editor, "LfoCardSelect1")->setSelectedItemIndex (9, juce::sendNotificationSync);
            clickMatrixControl (*editor, "lfo10Edit");
            const auto replacement = save (processor);
            std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, "lfo10Point32", -.84f);
                else if (action == 2) editor.reset();
                else if (action == 3)
                {
                    auto* phase = processor.apvts.getParameter ("lfo10Phase");
                    phase->setValueNotifyingHost (std::nextafter (phase->getValue(), 1.0f));
                }
                else if (action == 8) setParameter (processor, "lfo10Chaos", 1);
                else setParameter (processor, action == 4 ? "lfo10Direction" : action == 5 ? "lfo10FollowSwing"
                                               : action == 6 ? "lfo10AudioRateEnabled" : "lfo10AudioRateHz", action == 7 ? 500 : 1);
                expected = parameters (processor);
            };
            auto& trigger = *probes.front();
            if (boundary == 0) trigger.onStart = callback;
            else if (boundary == 1) trigger.onValue = callback;
            else trigger.onEnd = callback;
            auto* curve = findComponent<juce::Component> (*editor, "lfo10Curve");
            require (modulationControlVisible (curve), "The last drawable LFO was unavailable for host callback testing");
            const juce::Component::SafePointer<juce::Component> safe (curve);
            if (preset)
            {
                auto* detail = findComponent<juce::Component> (*editor, "LfoDetail10");
                auto* pulses = findButton (*detail, "PULSES");
                require (pulses != nullptr && static_cast<bool> (pulses->onClick), "LFO curve preset action was missing");
                pulses->onClick();
            }
            else
            {
                const juce::Point<float> start { 1, 1 }, end { 650, 170 };
                curve->mouseDown (modulationMouseEvent (*curve, start, start));
                if (safe != nullptr) safe->mouseDrag (modulationMouseEvent (*safe, end, start));
                if (safe != nullptr) safe->mouseUp (modulationMouseEvent (*safe, end, start));
            }
            const auto context = " preset=" + std::to_string (preset) + " boundary=" + std::to_string (boundary)
                                 + " action=" + std::to_string (action);
            require (! expected.empty(), "LFO curve callback did not run;" + context); expectParameters (processor, expected, context);
            require (parameters (processor) == expected, "LFO curve wrote after the exact host snapshot;" + context);
            require (! processor.isUserEditActive(), "LFO curve left an edit active;" + context);
            for (const auto& probe : probes)
                require (probe->starts == probe->ends, "LFO curve retained an unbalanced parameter gesture;" + context);
            if (action == 0) require (! processor.canUndo() && ! processor.canRedo(), "LFO curve retained history after project replacement;" + context);
        }
}

void testExpandedLfoShapeUiHostCallbacks()
{
    PresetTestDirectory files;
    for (int boundary = 0; boundary < 3; ++boundary) for (int action = 0; action < 3; ++action)
    {
        AnyvocAudioProcessor processor;
        setParameter (processor, "lfo10Shape", 1); processor.setLfoShape (9, 5); clearMatrixHistory (processor);
        ModulationGestureProbe basic (*processor.apvts.getParameter ("lfo10Shape")), kind (*processor.apvts.getParameter ("lfo10Kind"));
        std::unique_ptr<juce::AudioProcessorEditor> editor (
            new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
        editor->setVisible (true); editor->setSize (1008, 684); findButton (*editor, "MOD")->onClick();
        findComponent<juce::ComboBox> (*editor, "LfoCardSelect1")->setSelectedItemIndex (9, juce::sendNotificationSync);
        clickMatrixControl (*editor, "lfo10Edit");
        const auto replacement = save (processor);
        std::vector<float> expected;
        const auto callback = [&]
        {
            if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
            else if (action == 1) setParameter (processor, "lfo10Kind", 1);
            else editor.reset();
            expected = parameters (processor);
        };
        if (boundary == 0) basic.onStart = callback;
        else if (boundary == 1) basic.onValue = callback;
        else basic.onEnd = callback;
        auto* detail = findComponent<juce::Component> (*editor, "LfoDetail10");
        auto* box = findComponent<juce::ComboBox> (*detail, "lfo10ShapeChoice");
        require (modulationControlVisible (box) && box->getNumItems() == 8, "LFO 10 composite shape control was missing");
        box->setSelectedItemIndex (2, juce::sendNotificationSync);
        require (! expected.empty(), "LFO shape UI host callback did not run");
        expectParameters (processor, expected, "LFO shape UI boundary=" + std::to_string (boundary) + " action=" + std::to_string (action));
        require (basic.starts == basic.ends && kind.starts == kind.ends && ! processor.isUserEditActive(),
                 "LFO shape UI retained an unbalanced gesture across a host callback");
        if (editor != nullptr)
        {
            refreshEditorTimers();
            require (box->getSelectedItemIndex() == processor.getLfoShape (9), "LFO shape UI did not reconcile the host's final shape");
        }
    }
}

void testExpandedSourceDropHostCallbacks()
{
    PresetTestDirectory files;
    for (int scenario = 0; scenario < 3; ++scenario) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 3; ++action)
        {
            AnyvocAudioProcessor processor;
            // Occupy every earlier slot, so the real GUI drop must publish the
            // last route's extended selector rather than an easier first row.
            for (int route = 1; route < 32; ++route)
            {
                setParameter (processor, ("mod" + juce::String (route) + "Source").toRawUTF8(), 1);
                setParameter (processor, ("mod" + juce::String (route) + "Dest").toRawUTF8(), 1);
            }
            if (scenario == 2)
            {
                require (processor.assignModulation (17, 1, .43123f), "Drop fixture could not create its expanded duplicate");
                setParameter (processor, "mod32Enabled", 0); setParameter (processor, "mod32AuxSourceExtra", 15);
            }
            clearMatrixHistory (processor);
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* suffix : matrixRouteFields)
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter ("mod32" + juce::String (suffix))));
            auto& trigger = *probes[static_cast<size_t> (scenario == 0 ? 1 : scenario == 1 ? 13 : 7)];
            std::unique_ptr<juce::AudioProcessorEditor> editor (
                new AnyvocAudioProcessorEditor (processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684);
            clickMatrixControl (*editor, "openModulationSources");
            auto* chip = findComponent<juce::Component> (*editor, "paletteModSource17");
            auto* slider = findComponent<juce::Slider> (*editor, "cutoff");
            require (modulationControlVisible (chip) && slider != nullptr, "Expanded-source drag/drop controls were unavailable");
            auto* target = dynamic_cast<juce::DragAndDropTarget*> (slider->getParentComponent());
            require (target != nullptr, "Cutoff knob did not expose the actual drag/drop target interface");
            const juce::DragAndDropTarget::SourceDetails drop { juce::String ("anyvoc.mod:17"), chip, { 30, 30 } };
            require (target->isInterestedInDragSource (drop), "Cutoff rejected the LFO 10 source chip");
            const auto replacement = save (processor);
            std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, scenario == 1 ? "mod32AuxSourceExtra" : "mod32SourceExtra", 5);
                else editor.reset();
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback;
            else if (boundary == 1) trigger.onValue = callback;
            else trigger.onEnd = callback;
            target->itemDragEnter (drop);
            target->itemDropped (drop);
            const auto context = "LFO 10 drop scenario=" + std::to_string (scenario)
                                 + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty(), "Expanded-source drop callback never fired; " + context);
            expectParameters (processor, expected, context);
            require (! processor.isUserEditActive(), "Expanded-source drop retained a user edit; " + context);
            for (const auto& probe : probes)
                require (probe->starts == probe->ends, "Expanded-source drop retained a host gesture; " + context);
            if (action == 0) require (! processor.canUndo() && ! processor.canRedo(), "Drop retained stale history after host restore; " + context);
            if (action == 2) require (editor == nullptr, "Drop did not exercise editor destruction; " + context);
        }
}

void testAdvancedEnvelopeParameterCompatibility()
{
    AnyvocAudioProcessor processor;
    const auto& all = processor.getParameters();
    require (all.size() == 1018 + advancedEnvelopeParameterCount + lfoMotionParameterCount + chaosParameterCount + macroDestinationParameterCount, "Advanced envelopes did not append exactly 48 controls after v0.14");
    int next = 1018;
    const std::array<int, 5> divisionDefaults { 0, 3, 0, 5, 5 };
    const juce::StringArray divisions { "Instant", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars",
                                       "1/8 T", "1/16 T", "1/8 D", "1/16 D" };
    for (const auto* prefix : envelopePrefixes)
    {
        for (size_t field = 0; field < advancedEnvelopeFields.size(); ++field)
        {
            const auto id = juce::String (prefix) + advancedEnvelopeFields[field];
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (all[next++]);
            require (parameter != nullptr && parameter->getParameterID() == id && parameter->getVersionHint() == 11,
                     "Advanced envelope parameter order/version changed: " + id.toStdString());
            const auto& range = parameter->getNormalisableRange();
            if (field < 5)
            {
                require (range.start == (field < 2 ? 0.0f : -1.0f) && range.end == (field < 2 ? 8.0f : 1.0f)
                             && range.interval == 0 && range.skew == (field < 2 ? .3f : 1.0f)
                             && parameter->convertFrom0to1 (parameter->getDefaultValue()) == 0,
                         "Envelope duration/curve lost its continuous range or neutral default: " + id.toStdString());
            }
            else if (field == 5 || field == 11)
                require (parameter->getNumSteps() == 2 && parameter->getDefaultValue() == 0,
                         "Envelope tempo/retrigger flags must default to the legacy behavior");
            else
            {
                auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter);
                require (choice != nullptr && choice->choices == divisions && parameter->getNumSteps() == 14
                             && choice->convertFrom0to1 (parameter->getDefaultValue()) == divisionDefaults[field - 6],
                         "Envelope stage division order/default changed: " + id.toStdString());
                for (int item = 0; item < 14; ++item)
                    require (choice->convertFrom0to1 (item / 13.0f) == item, "Envelope tempo automation changed its normalized mapping");
            }
        }
        for (const auto* suffix : { "Attack", "Decay", "Sustain", "Release" })
        {
            const auto id = juce::String (prefix) + suffix;
            auto* parameter = processor.apvts.getParameter (id);
            const bool sustain = juce::String (suffix) == "Sustain";
            require (parameter->getVersionHint() == (juce::String (prefix).length() > 3 ? 10 : 1)
                         && parameter->getNormalisableRange().start == (sustain ? 0.0f : .001f)
                         && parameter->getNormalisableRange().end == (sustain ? 1.0f : 8.0f)
                         && parameter->getNormalisableRange().interval == 0
                         && parameter->getNormalisableRange().skew == (sustain ? 1.0f : .3f),
                     "An existing ADSR parameter changed its host range/version: " + id.toStdString());
        }
    }
    require (next == all.size() - macroDestinationParameterCount - chaosParameterCount - lfoMotionParameterCount, "Advanced envelope verification missed a tail parameter");
}

void testAdvancedEnvelopeStateAndMigration()
{
    AnyvocAudioProcessor source;
    int index = 0;
    for (const auto* prefix : envelopePrefixes) for (const auto* suffix : advancedEnvelopeFields)
    {
        auto* parameter = source.apvts.getParameter (juce::String (prefix) + suffix);
        const auto fraction = .15f + (index++ % 6) * .14f;
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (parameter->convertFrom0to1 (fraction)));
    }
    setParameter (source, "ampAttack", .12345f); setParameter (source, "env4Release", .45678f);
    const auto expected = parameters (source);
    const auto current = stateTree (source);
    require (static_cast<int> (current.getProperty ("schemaVersion")) == 14, "Envelope state did not advertise schema 14");
    AnyvocAudioProcessor restored;
    restore (restored, current); expectParameters (restored, expected, "Advanced envelope DAW roundtrip");
    juce::TemporaryFile file (".anyvoc");
    require (source.savePreset (file.getFile()) && restored.loadPreset (file.getFile()), "Advanced envelope preset roundtrip failed");
    expectParameters (restored, expected, "Advanced envelope preset roundtrip");
    restored.setCurrentProgram (0);
    for (const auto* prefix : envelopePrefixes) for (const auto* suffix : advancedEnvelopeFields)
    {
        auto* parameter = restored.apvts.getParameter (juce::String (prefix) + suffix);
        const auto canonical = parameter->convertTo0to1 (parameter->convertFrom0to1 (parameter->getDefaultValue()));
        require (parameter->getValue() == canonical, "Init retained an advanced envelope value: " + parameter->getParameterID().toStdString());
    }
    AnyvocAudioProcessor legacy;
    legacy.setCurrentProgram (19); setParameter (legacy, "env4Attack", .21345f);
    const auto legacyExpected = parameters (legacy);
    auto v10 = stateTree (legacy); v10.setProperty ("schemaVersion", 10, nullptr);
    for (int child = v10.getNumChildren(); --child >= 0;)
        if (isAdvancedEnvelopeParameter (v10.getChild (child).getProperty ("id").toString())
            || (isLfoMotionParameter (v10.getChild (child).getProperty ("id").toString()) || (isChaosParameter (v10.getChild (child).getProperty ("id").toString()) || isMacroDestinationParameter (v10.getChild (child).getProperty ("id").toString())))) v10.removeChild (child, nullptr);
    restore (restored, current); restore (restored, v10); expectParameters (restored, legacyExpected, "Authentic schema 10 envelope migration");
    const auto retained = save (restored);
    for (const auto* prefix : envelopePrefixes) for (const auto* suffix : advancedEnvelopeFields)
    {
        const auto id = juce::String (prefix) + suffix;
        auto missing = current.createCopy(); missing.removeChild (missing.getChildWithProperty ("id", id), nullptr);
        restore (restored, missing); require (save (restored) == retained, "Schema 14 accepted missing " + id.toStdString());
        auto duplicate = current.createCopy(); duplicate.addChild (duplicate.getChildWithProperty ("id", id).createCopy(), -1, nullptr);
        restore (restored, duplicate); require (save (restored) == retained, "Schema 14 accepted duplicate " + id.toStdString());
    }
    for (const auto* prefix : envelopePrefixes) for (const auto* suffix : { "Delay", "AttackCurve", "TempoSync", "ReleaseDivision", "RetriggerLegato" })
        for (const auto* bad : { "NaN", "Infinity", "invalid" })
        {
            const auto id = juce::String (prefix) + suffix;
            auto invalid = current.createCopy(); invalid.getChildWithProperty ("id", id).setProperty ("value", bad, nullptr);
            restore (restored, invalid); require (save (restored) == retained, "Malformed envelope field partially loaded: " + id.toStdString());
        }
}

void setEnvelopeValue (AnyvocAudioProcessor& processor, const juce::String& prefix, const char* suffix, float value)
{
    setParameter (processor, (prefix + suffix).toRawUTF8(), value);
}

void prepareAdvancedEnvelopeAudio (AnyvocAudioProcessor& processor, int source)
{
    prepareCrossModulationFixture (processor);
    processor.setWarpMode (0, 0, 6); setParameter (processor, "osc1Warp1Amount", .2f);
    for (const auto* prefix : envelopePrefixes)
    {
        setEnvelopeValue (processor, prefix, "Attack", .025f); setEnvelopeValue (processor, prefix, "Decay", .09f);
        setEnvelopeValue (processor, prefix, "Sustain", .4f); setEnvelopeValue (processor, prefix, "Release", .3f);
    }
    processor.setModulationSource (31, source); processor.setModulationDestination (31, 12);
    setParameter (processor, "mod32Amount", .25f);
}

void testAdvancedEnvelopesReachAudio()
{
    const std::array<int, 4> sources { 18, 3, 19, 20 };
    const std::array<const char*, 5> fields { "Delay", "Hold", "AttackCurve", "DecayCurve", "ReleaseCurve" };
    const std::array<float, 5> values { .025f, .06f, .5f, -.5f, .5f };
    const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 60, .85f) }, { 14400, juce::MidiMessage::noteOff (1, 60) } };
    for (size_t feature = 0; feature < fields.size(); ++feature) for (size_t env = 0; env < sources.size(); ++env)
    {
        AnyvocAudioProcessor actual, reference, baseline;
        prepareAdvancedEnvelopeAudio (actual, sources[env]);
        prepareAdvancedEnvelopeAudio (reference, sources[(env + 1) % sources.size()]);
        prepareAdvancedEnvelopeAudio (baseline, sources[env]);
        for (auto* processor : { &actual, &reference })
            for (const auto* prefix : envelopePrefixes) setEnvelopeValue (*processor, prefix, fields[feature], values[feature]);
        for (auto* processor : { &actual, &reference, &baseline }) processor->prepareToPlay (48000, 512);
        const auto a = render (actual, 30000, notes, 127), r = render (reference, 30000, notes, 127), b = render (baseline, 30000, notes, 127);
        require (rms (a) > .001 && crossAudioDifference (a, r) < 2.0e-6 && crossAudioDifference (a, b) > .0001,
                 juce::String (envelopePrefixes[env]).toStdString() + fields[feature] + " did not reach independent envelope source audio; reference difference="
                     + std::to_string (crossAudioDifference (a, r)) + " baseline difference=" + std::to_string (crossAudioDifference (a, b)));
        if (feature == 0)
            require (a.getMagnitude (0, 1200) == 0 && a.getMagnitude (1200, 4096) > .001f,
                     "The amplifier delay either leaked audio before 25 ms or retired its silent voice before attack");
    }
}

void testAdvancedEnvelopeTempoAudio()
{
    const std::array<int, 4> sources { 18, 3, 19, 20 };
    const std::array<const char*, 5> stages { "Delay", "Attack", "Hold", "Decay", "Release" };
    // Fractions chosen independently from musical notation: 1/32, 1/16,
    // 1/64, 1/8 and 1/16 respectively, all exactly representable at 120/240 BPM.
    const std::array<int, 5> division { 2, 3, 1, 4, 3 };
    const std::array<float, 5> quarterNotes { .125f, .25f, .0625f, .5f, .25f };
    for (size_t env = 0; env < sources.size(); ++env) for (int mode = 0; mode < 3; ++mode)
    {
        TestPlayHead host;
        host.position.setBpm (mode == 1 ? 240.0 : std::numeric_limits<double>::quiet_NaN());
        host.position.setIsPlaying (true);
        AnyvocAudioProcessor synced, seconds;
        prepareAdvancedEnvelopeAudio (synced, sources[env]); prepareAdvancedEnvelopeAudio (seconds, sources[env]);
        const auto prefix = juce::String (envelopePrefixes[env]);
        setParameter (synced, "tempo", 120); setParameter (seconds, "tempo", 120);
        setEnvelopeValue (synced, prefix, "TempoSync", 1);
        for (size_t stage = 0; stage < stages.size(); ++stage)
        {
            setParameter (synced, (prefix + stages[stage] + "Division").toRawUTF8(), static_cast<float> (division[stage]));
            setEnvelopeValue (seconds, prefix, stages[stage], quarterNotes[stage] * (mode == 1 ? .25f : .5f));
        }
        if (mode != 0) synced.setPlayHead (&host);
        synced.prepareToPlay (48000, 512); seconds.prepareToPlay (48000, 512);
        const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 60, .85f) }, { 24000, juce::MidiMessage::noteOff (1, 60) } };
        const auto actual = render (synced, 36000, notes, 127), expected = render (seconds, 36000, notes, 127);
        require (crossAudioDifference (actual, expected) < 3.0e-6 && rms (actual) > .001
                     && synced.isTempoFromHost() == (mode == 1),
                 prefix.toStdString() + " synchronized stages differed from equal-duration seconds; tempo mode=" + std::to_string (mode)
                     + " difference=" + std::to_string (crossAudioDifference (actual, expected)));
        synced.setPlayHead (nullptr);
    }
    AnyvocAudioProcessor tail;
    setParameter (tail, "delayMix", 0); setParameter (tail, "reverbMix", 0);
    setParameter (tail, "ampTempoSync", 1); setParameter (tail, "ampReleaseDivision", 9); // Four bars = sixteen quarter notes.
    tail.prepareToPlay (48000, 512);
    for (const float bpm : { 30.0f, 300.0f, 60.0f })
    {
        setParameter (tail, "tempo", bpm);
        require (std::abs (tail.getTailLengthSeconds() - (16.0 * 60.0 / bpm + .1)) < 1.0e-8,
                 "Synchronized release tail ignored a local-tempo change before the first audio block");
    }
    TestPlayHead tailHost; tailHost.position.setBpm (90.0); tailHost.position.setIsPlaying (true);
    tail.setPlayHead (&tailHost); render (tail, 1);
    setParameter (tail, "tempo", 300);
    require (tail.isTempoFromHost() && std::abs (tail.getTailLengthSeconds() - (16.0 * 60.0 / 90.0 + .1)) < 1.0e-8,
             "An unprocessed local-tempo edit overrode the active host clock's release tail");
    tail.setPlayHead (nullptr);
}

void testAdvancedEnvelopeLegatoIsolation()
{
    const std::array<int, 4> sources { 18, 3, 19, 20 };
    const std::array<int, 4> unrelated { 1, 2, 3, 1 }; // Never retrigger the amp as an unrelated modulation source.
    const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 60, .85f) },
        { 7000, juce::MidiMessage::noteOn (1, 72, .85f) }, { 12000, juce::MidiMessage::noteOff (1, 72) }, { 17000, juce::MidiMessage::noteOff (1, 60) } };
    for (size_t env = 0; env < sources.size(); ++env)
    {
        AnyvocAudioProcessor baseline, retriggered, independent;
        for (auto* processor : { &baseline, &retriggered, &independent })
        {
            prepareAdvancedEnvelopeAudio (*processor, sources[env]); setParameter (*processor, "voiceMode", 2);
            for (const auto* prefix : envelopePrefixes) setEnvelopeValue (*processor, prefix, "Attack", .08f);
        }
        setEnvelopeValue (retriggered, envelopePrefixes[env], "RetriggerLegato", 1);
        setEnvelopeValue (independent, envelopePrefixes[static_cast<size_t> (unrelated[env])], "RetriggerLegato", 1);
        for (auto* processor : { &baseline, &retriggered, &independent }) processor->prepareToPlay (48000, 512);
        const auto base = render (baseline, 27000, notes, 127), changed = render (retriggered, 27000, notes, 127), other = render (independent, 27000, notes, 127);
        require (crossAudioDifference (base, changed) > .0001 && crossAudioDifference (base, other) < 2.0e-6,
                 "The per-envelope legato flag did not independently control " + std::string (envelopePrefixes[env]));
        for (int channel = 0; channel < 2; ++channel) for (int sample = 0; sample < 7000; ++sample)
            require (base.getSample (channel, sample) == changed.getSample (channel, sample), "Legato retrigger changed the initial non-overlapping note");
    }
}

void openEnvelopeLab (juce::Component& editor, int envelope)
{
    clickMatrixControl (editor, "openAmpEnvelopeLab");
    auto* selector = findComponent<juce::ComboBox> (editor, "EnvelopeLabSelect");
    require (modulationControlVisible (selector) && selector->getNumItems() == 4, "Envelope Lab navigator was unavailable");
    selector->setSelectedItemIndex (envelope, juce::sendNotificationSync);
    dispatchEditorActions();
}

void testAdvancedEnvelopeEditorNavigationAndGestures()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    for (const auto* prefix : envelopePrefixes)
    {
        setEnvelopeValue (processor, prefix, "Delay", .05f); setEnvelopeValue (processor, prefix, "Attack", .1f);
        setEnvelopeValue (processor, prefix, "Hold", .05f); setEnvelopeValue (processor, prefix, "Decay", .2f);
        setEnvelopeValue (processor, prefix, "Sustain", .5f); setEnvelopeValue (processor, prefix, "Release", .25f);
    }
    clearMatrixHistory (processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
        processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1008, 684);
    const auto navigationState = save (processor);
    openEnvelopeLab (*editor, 0);
    for (int env = 0; env < 4; ++env)
    {
        const auto prefix = juce::String (envelopePrefixes[static_cast<size_t> (env)]);
        findComponent<juce::ComboBox> (*editor, "EnvelopeLabSelect")->setSelectedItemIndex (env, juce::sendNotificationSync);
        dispatchEditorActions();
        auto* page = findComponent<juce::Component> (*editor, prefix + "EnvelopeLabPage");
        require (modulationControlVisible (page), "Envelope Lab did not reveal " + prefix.toStdString());
        for (const auto* field : { "Delay", "Attack", "Hold", "Decay", "Sustain", "Release", "AttackCurve", "DecayCurve", "ReleaseCurve" })
        {
            auto* slider = findComponent<juce::Slider> (*page, prefix + field + "Lab");
            require (modulationControlVisible (slider) && editor->getLocalBounds().contains (editor->getLocalArea (slider, slider->getLocalBounds())),
                     "Envelope Lab slider was missing/clipped: " + (prefix + field).toStdString());
        }
        require (modulationControlVisible (findComponent<juce::Component> (*page, prefix + "EnvelopeGraph"))
                     && modulationControlVisible (findComponent<juce::Button> (*page, prefix + "TempoSyncLab"))
                     && modulationControlVisible (findComponent<juce::Button> (*page, prefix + "RetriggerLegatoLab")),
                 "Envelope Lab omitted its graph or independent flags");
    }
    clickMatrixControl (*editor, "EnvelopeLabClose");
    findButton (*editor, "MOD")->onClick();
    findComponent<juce::ComboBox> (*editor, "EnvelopeSelect")->setSelectedItemIndex (3, juce::sendNotificationSync);
    clickMatrixControl (*editor, "openModEnvelopeLab");
    require (findComponent<juce::ComboBox> (*editor, "EnvelopeLabSelect")->getSelectedItemIndex() == 3,
             "The MOD envelope action did not open the selected fourth envelope");
    require (save (processor) == navigationState && ! processor.canUndo(), "Envelope Lab navigation changed sound/history");

    const std::array<const char*, 9> handles { "NodeDelay", "NodeAttack", "NodeHold", "NodeDecay", "NodeSustain", "NodeRelease",
                                               "CurveAttack", "CurveDecay", "CurveRelease" };
    const std::array<const char*, 9> editedFields { "Delay", "Attack", "Hold", "Decay", "Sustain", "Release", "AttackCurve", "DecayCurve", "ReleaseCurve" };
    for (int env = 0; env < 4; ++env) for (size_t control = 0; control < handles.size(); ++control)
    {
        const auto prefix = juce::String (envelopePrefixes[static_cast<size_t> (env)]);
        findComponent<juce::ComboBox> (*editor, "EnvelopeLabSelect")->setSelectedItemIndex (env, juce::sendNotificationSync);
        dispatchEditorActions(); clearMatrixHistory (processor); refreshEditorTimers();
        const auto before = parameters (processor);
        auto* handle = findComponent<juce::Component> (*editor, prefix + "Envelope" + handles[control]);
        require (modulationControlVisible (handle), "Envelope graph handle missing: " + (prefix + handles[control]).toStdString());
        const auto start = handle->getLocalBounds().toFloat().getCentre();
        const auto end = start + juce::Point<float> (60, -30);
        handle->mouseDown (modulationMouseEvent (*handle, start, start));
        handle->mouseDrag (modulationMouseEvent (*handle, end, start));
        handle->mouseUp (modulationMouseEvent (*handle, end, start));
        const auto after = parameters (processor);
        const auto field = prefix + editedFields[control];
        require (after[static_cast<size_t> (processor.apvts.getParameter (field)->getParameterIndex())]
                     != before[static_cast<size_t> (processor.apvts.getParameter (field)->getParameterIndex())],
                 "Envelope graph drag did not edit " + field.toStdString());
        for (int i = 0; i < processor.getParameters().size(); ++i)
        {
            const auto id = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters()[i])->getParameterID();
            if (id != field && ! (control == 3 && id == prefix + "Sustain"))
                require (after[static_cast<size_t> (i)] == before[static_cast<size_t> (i)], "Graph drag changed unrelated " + id.toStdString());
        }
        if (control == 3)
            require (plainParameter (processor, (prefix + "Sustain").toRawUTF8()) != .5f, "Decay node did not also move sustain vertically");
        require (processor.getUndoCount() == 1 && ! processor.isUserEditActive(), "Envelope graph gesture split/retained its user edit");
        expectHistoryResult (processor.undoUserEdit(), "Undo envelope graph"); expectParameters (processor, before, field.toStdString());
        expectHistoryResult (processor.redoUserEdit(), "Redo envelope graph"); expectParameters (processor, after, field.toStdString());
        expectHistoryResult (processor.undoUserEdit(), "Restore envelope graph fixture");
    }
    for (int env = 0; env < 4; ++env)
    {
        const auto prefix = juce::String (envelopePrefixes[static_cast<size_t> (env)]);
        findComponent<juce::ComboBox> (*editor, "EnvelopeLabSelect")->setSelectedItemIndex (env, juce::sendNotificationSync);
        dispatchEditorActions();
        auto* secondsSlider = findComponent<juce::Slider> (*editor, prefix + "AttackLab");
        require (secondsSlider != nullptr, "Envelope seconds control was unavailable before synchronization");
        auto* toggle = findComponent<juce::Button> (*editor, prefix + "TempoSyncLab");
        toggle->setToggleState (true, juce::sendNotificationSync); refreshEditorTimers();
        auto* division = findComponent<juce::ComboBox> (*editor, prefix + "AttackDivisionLab");
        require (modulationControlVisible (division) && division->isEnabled() && division->getNumItems() == 14,
                 "Envelope sync failed to expose its 14 musical divisions");
        const auto seconds = plainParameter (processor, (prefix + "Attack").toRawUTF8());
        clearMatrixHistory (processor); const auto before = parameters (processor);
        secondsSlider->setValue (.731, juce::sendNotificationSync);
        require (parameters (processor) == before && ! processor.canUndo(), "A stale seconds control changed a synchronized envelope");
        auto* handle = findComponent<juce::Component> (*editor, prefix + "EnvelopeNodeAttack");
        const auto start = handle->getLocalBounds().toFloat().getCentre(), end = start + juce::Point<float> (40, 0);
        handle->mouseDown (modulationMouseEvent (*handle, start, start));
        handle->mouseDrag (modulationMouseEvent (*handle, end, start));
        handle->mouseUp (modulationMouseEvent (*handle, end, start));
        require (plainParameter (processor, (prefix + "AttackDivision").toRawUTF8()) != 3
                     && plainParameter (processor, (prefix + "Attack").toRawUTF8()) == seconds && processor.getUndoCount() == 1,
                 "A synchronized graph drag changed saved seconds or failed to edit its division");
        expectHistoryResult (processor.undoUserEdit(), "Undo synchronized envelope node"); expectParameters (processor, before);
        setEnvelopeValue (processor, prefix, "AttackCurve", -.37f); refreshEditorTimers();
        auto* curve = findComponent<juce::Slider> (*editor, prefix + "AttackCurveLab");
        require (std::abs (curve->getValue() + .37) < 1.0e-6, "Host automation failed to refresh an envelope curve");
    }
}

void testAdvancedEnvelopeGraphHostCallbacks()
{
    PresetTestDirectory files;
    for (int control = 0; control < 3; ++control) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 4; ++action)
        {
            AnyvocAudioProcessor processor;
            setParameter (processor, "env4Decay", .2f); setParameter (processor, "env4Sustain", .5f);
            setParameter (processor, "env4TempoSync", control == 1 ? 1.0f : 0.0f); clearMatrixHistory (processor);
            const std::array<const char*, 3> fields { "Decay", "AttackDivision", "ReleaseCurve" };
            const std::array<const char*, 3> handles { "NodeDecay", "NodeAttack", "CurveRelease" };
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* field : { "Delay", "Attack", "Hold", "Decay", "Sustain", "Release", "AttackCurve", "DecayCurve", "ReleaseCurve",
                                       "TempoSync", "DelayDivision", "AttackDivision", "HoldDivision", "DecayDivision", "ReleaseDivision", "RetriggerLegato" })
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter ("env4" + juce::String (field))));
            auto& trigger = *probes[static_cast<size_t> (control == 0 ? 3 : control == 1 ? 11 : 8)];
            std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
                processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684); openEnvelopeLab (*editor, 3);
            const auto replacement = save (processor); std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, "env4HoldDivision", 9); // Related even when inactive in seconds mode.
                else if (action == 2) editor.reset();
                else
                {
                    auto* curve = processor.apvts.getParameter ("env4AttackCurve");
                    curve->setValueNotifyingHost (std::nextafter (curve->getValue(), 1.0f));
                }
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            auto* handle = findComponent<juce::Component> (*editor, "env4Envelope" + juce::String (handles[static_cast<size_t> (control)]));
            require (modulationControlVisible (handle), "Envelope callback fixture handle missing");
            const juce::Component::SafePointer<juce::Component> safe (handle);
            const auto start = handle->getLocalBounds().toFloat().getCentre(), end = start + juce::Point<float> (60, -30);
            handle->mouseDown (modulationMouseEvent (*handle, start, start));
            if (safe != nullptr) safe->mouseDrag (modulationMouseEvent (*safe, end, start));
            if (safe != nullptr) safe->mouseUp (modulationMouseEvent (*safe, end, start));
            const auto context = std::string (fields[static_cast<size_t> (control)]) + " graph boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty(), "Envelope graph host callback did not fire: " + context); expectParameters (processor, expected, context);
            require (parameters (processor) == expected, "Envelope graph changed parameters after the host callback snapshot: " + context);
            require (! processor.isUserEditActive(), "Envelope graph retained its user edit: " + context);
            for (const auto& probe : probes) require (probe->starts == probe->ends, "Envelope graph retained a host gesture: " + context);
            if (action == 0) require (! processor.canUndo() && ! processor.canRedo(), "Envelope graph kept stale history after restore");
        }
}

void testAdvancedEnvelopeControlHostCallbacks()
{
    PresetTestDirectory files;
    const std::array<const char*, 4> fields { "AttackCurve", "AttackDivision", "TempoSync", "RetriggerLegato" };
    for (int control = 0; control < 4; ++control) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 3; ++action)
        {
            AnyvocAudioProcessor processor;
            if (control == 1) setParameter (processor, "env3TempoSync", 1);
            clearMatrixHistory (processor);
            const auto id = "env3" + juce::String (fields[static_cast<size_t> (control)]);
            ModulationGestureProbe probe (*processor.apvts.getParameter (id));
            std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
                processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684); openEnvelopeLab (*editor, 2);
            const auto replacement = save (processor); std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, id.toRawUTF8(), control == 0 ? -.7f : control == 1 ? 9.0f : boundary == 0 ? 1.0f : 0.0f);
                else editor.reset();
                expected = parameters (processor);
            };
            if (boundary == 0) probe.onStart = callback; else if (boundary == 1) probe.onValue = callback; else probe.onEnd = callback;
            if (control == 0) findComponent<juce::Slider> (*editor, id + "Lab")->setValue (.45, juce::sendNotificationSync);
            else if (control == 1) findComponent<juce::ComboBox> (*editor, id + "Lab")->setSelectedItemIndex (7, juce::sendNotificationSync);
            else findComponent<juce::Button> (*editor, id + "Lab")->setToggleState (true, juce::sendNotificationSync);
            const auto context = id.toStdString() + " UI boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty(), "Envelope control host callback did not fire: " + context); expectParameters (processor, expected, context);
            require (probe.starts == probe.ends && ! processor.isUserEditActive(), "Envelope control retained a gesture: " + context);
            if (action == 0) require (! processor.canUndo() && ! processor.canRedo(), "Envelope control kept stale history after restore");
        }
}

void testLfoMotionParameterCompatibility()
{
    AnyvocAudioProcessor processor;
    const auto& all = processor.getParameters();
    require (all.size() == 1066 + lfoMotionParameterCount + chaosParameterCount + macroDestinationParameterCount, "LFO motion must append 116 parameters after the frozen v0.15 prefix");
    int next = 1066;
    const auto parameter = [&] (const juce::String& id)
    {
        auto* result = dynamic_cast<juce::RangedAudioParameter*> (all[next++]);
        require (result != nullptr && result->getParameterID() == id && result->getVersionHint() == 12,
                 "LFO motion append order/version changed: " + id.toStdString());
        return result;
    };
    const auto floating = [&] (const juce::String& id, float low, float high, float def, float skew)
    {
        auto* p = parameter (id); const auto& range = p->getNormalisableRange();
        require (range.start == low && range.end == high && range.interval == 0 && range.skew == skew
                     && std::abs (p->convertFrom0to1 (p->getDefaultValue()) - def) < 2.0e-5f,
                 "LFO motion continuous range/default changed: " + id.toStdString());
    };
    const auto choice = [&] (const juce::String& id, const juce::StringArray& names)
    {
        auto* p = parameter (id); const auto* c = dynamic_cast<juce::AudioParameterChoice*> (p);
        require (c != nullptr && c->choices == names && p->getDefaultValue() == 0 && p->getNumSteps() == names.size(),
                 "LFO motion choice mapping/default changed: " + id.toStdString());
        for (int item = 0; item < names.size(); ++item)
            require (p->convertFrom0to1 (static_cast<float> (item) / (names.size() - 1)) == item, "LFO motion normalized choice changed");
    };
    const auto toggle = [&] (const juce::String& id)
    {
        auto* p = parameter (id); require (p->getNumSteps() == 2 && p->getDefaultValue() == 0, "New LFO flag must preserve legacy behavior by default");
    };
    for (int lfo = 1; lfo <= 10; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo);
        floating (prefix + "Phase", 0, 1, 0, 1); choice (prefix + "Direction", { "Forward", "Reverse", "Ping Pong" });
        toggle (prefix + "FollowSwing"); toggle (prefix + "AudioRateEnabled"); floating (prefix + "AudioRateHz", 20, 1000, 100, .5f);
        auto* rate = processor.apvts.getParameter (prefix + "Rate");
        require (rate->getNormalisableRange().start == .05f && rate->getNormalisableRange().end == 20
                     && rate->getNormalisableRange().interval == 0 && rate->getNormalisableRange().skew == .4f
                     && rate->getVersionHint() == (lfo <= 2 ? 1 : 10), "Audio rate reinterpreted old LFO Hz automation");
        require (processor.apvts.getParameter (prefix + "Sync")->getNumSteps() == 2
                     && processor.apvts.getParameter (prefix + "Division")->getNumSteps() == 12,
                 "LFO motion reinterpreted existing clock automation");
    }
    floating ("swingAmount", 0, 1, 0, 1); choice ("swingDivision", { "1/8", "1/16" });
    juce::StringArray destinations; for (int lfo = 1; lfo <= 10; ++lfo) destinations.add ("LFO " + juce::String (lfo) + " Phase");
    for (int route = 1; route <= 32; ++route)
    {
        const auto prefix = "mod" + juce::String (route);
        toggle (prefix + "LfoPhaseEnabled"); choice (prefix + "LfoPhaseTarget", destinations);
    }
    require (next == all.size() - macroDestinationParameterCount - chaosParameterCount, "LFO motion parameter verification missed appended controls");
}

void testLfoMotionStateAndMigration()
{
    AnyvocAudioProcessor source;
    int count = 0;
    for (auto* base : source.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base); p != nullptr && isLfoMotionParameter (p->getParameterID()))
        {
            const float normalized = .13f + (count++ % 7) * .12f;
            p->setValueNotifyingHost (p->convertTo0to1 (p->convertFrom0to1 (normalized)));
        }
    require (count == lfoMotionParameterCount, "LFO motion state fixture missed appended controls");
    const auto expected = parameters (source);
    const auto current = stateTree (source);
    require (static_cast<int> (current.getProperty ("schemaVersion")) == 14, "LFO motion state must use schema 14");
    AnyvocAudioProcessor restored; restore (restored, current); expectParameters (restored, expected, "LFO motion DAW roundtrip");
    juce::TemporaryFile preset (".anyvoc");
    require (source.savePreset (preset.getFile()) && restored.loadPreset (preset.getFile()), "LFO motion preset file failed to roundtrip");
    expectParameters (restored, expected, "LFO motion preset roundtrip");
    restored.setCurrentProgram (0);
    for (auto* base : restored.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base); p != nullptr && isLfoMotionParameter (p->getParameterID()))
            require (p->getValue() == p->convertTo0to1 (p->convertFrom0to1 (p->getDefaultValue())),
                     "Init retained LFO motion parameter " + p->getParameterID().toStdString());
    AnyvocAudioProcessor legacy; legacy.setCurrentProgram (20); setParameter (legacy, "lfo10Rate", 7.4321f);
    const auto oldValues = parameters (legacy); auto v11 = stateTree (legacy); v11.setProperty ("schemaVersion", 11, nullptr);
    for (int child = v11.getNumChildren(); --child >= 0;)
        if ((isLfoMotionParameter (v11.getChild (child).getProperty ("id").toString()) || (isChaosParameter (v11.getChild (child).getProperty ("id").toString()) || isMacroDestinationParameter (v11.getChild (child).getProperty ("id").toString())))) v11.removeChild (child, nullptr);
    restore (restored, current); restore (restored, v11); expectParameters (restored, oldValues, "Authentic schema 11 migration");
    const auto retained = save (restored);
    for (auto* base : source.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base); p != nullptr && isLfoMotionParameter (p->getParameterID()))
        {
            const auto id = p->getParameterID();
            auto missing = current.createCopy(); missing.removeChild (missing.getChildWithProperty ("id", id), nullptr);
            restore (restored, missing); require (save (restored) == retained, "Schema 14 accepted missing " + id.toStdString());
            auto duplicate = current.createCopy(); duplicate.addChild (duplicate.getChildWithProperty ("id", id).createCopy(), -1, nullptr);
            restore (restored, duplicate); require (save (restored) == retained, "Schema 14 accepted duplicate " + id.toStdString());
            for (const auto* bad : { "NaN", "Infinity", "not numeric" })
            {
                auto invalid = current.createCopy(); invalid.getChildWithProperty ("id", id).setProperty ("value", bad, nullptr);
                restore (restored, invalid); require (save (restored) == retained, "Malformed LFO motion partially loaded: " + id.toStdString());
            }
        }
}

void testLfoRateModesAndPhaseDestinationHistory()
{
    AnyvocAudioProcessor processor;
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        const auto prefix = "lfo" + juce::String (lfo + 1);
        setParameter (processor, (prefix + "Rate").toRawUTF8(), 7.4321f);
        setParameter (processor, (prefix + "AudioRateHz").toRawUTF8(), 731.25f);
        setParameter (processor, (prefix + "Division").toRawUTF8(), 9);
        setParameter (processor, (prefix + "Sync").toRawUTF8(), 1); setParameter (processor, (prefix + "AudioRateEnabled").toRawUTF8(), 1);
        require (processor.getLfoRateMode (lfo) == 1, "Legacy Sync did not take precedence over audio-rate mode");
        for (const int mode : { 0, 2, 1, 0 })
        {
            clearMatrixHistory (processor); const auto before = parameters (processor);
            const auto slow = plainParameter (processor, prefix + "Rate"), fast = plainParameter (processor, prefix + "AudioRateHz");
            processor.setLfoRateMode (lfo, mode); const auto after = parameters (processor);
            require (processor.getLfoRateMode (lfo) == mode && plainParameter (processor, prefix + "Rate") == slow
                         && plainParameter (processor, prefix + "AudioRateHz") == fast && plainParameter (processor, prefix + "Division") == 9
                         && processor.getUndoCount() == 1, "A rate-mode switch changed remembered rates/division or split history");
            expectHistoryResult (processor.undoUserEdit(), "Undo LFO rate mode"); expectParameters (processor, before);
            expectHistoryResult (processor.redoUserEdit(), "Redo LFO rate mode"); expectParameters (processor, after);
            const auto unchanged = save (processor); processor.setLfoRateMode (lfo, mode);
            require (save (processor) == unchanged && processor.getUndoCount() == 1, "Repeated rate-mode selection created an edit");
        }
    }
    const auto unchanged = save (processor);
    processor.setLfoRateMode (-1, 0); processor.setLfoRateMode (10, 2); processor.setLfoRateMode (0, -1); processor.setLfoRateMode (0, 3);
    require (save (processor) == unchanged, "An invalid rate-mode request changed state");
    for (const int route : { 0, 31 })
    {
        const auto prefix = "mod" + juce::String (route + 1);
        configureMatrixPayload (processor, route); processor.setModulationDestination (route, 17);
        for (int destination = 18; destination <= 27; ++destination)
        {
            const auto oldLegacy = plainParameter (processor, prefix + "Dest"), oldFilter = plainParameter (processor, prefix + "FilterTarget");
            const auto oldWarp = plainParameter (processor, prefix + "WarpTarget");
            clearMatrixHistory (processor); const auto before = matrixPayload (processor, route);
            processor.setModulationDestination (route, destination); const auto after = matrixPayload (processor, route);
            require (processor.getModulationDestination (route) == destination && plainParameter (processor, prefix + "LfoPhaseTarget") == destination - 18
                         && plainParameter (processor, prefix + "LfoPhaseEnabled") == 1 && plainParameter (processor, prefix + "Dest") == oldLegacy
                         && plainParameter (processor, prefix + "FilterTarget") == oldFilter && plainParameter (processor, prefix + "WarpTarget") == oldWarp
                         && processor.getUndoCount() == 1, "Selecting an LFO phase target lost inactive selectors or compound history");
            expectHistoryResult (processor.undoUserEdit(), "Undo phase destination"); expectMatrixPayload (processor, route, before);
            expectHistoryResult (processor.redoUserEdit(), "Redo phase destination"); expectMatrixPayload (processor, route, after);
        }
        for (const int legacy : { 0, 7, 11, 17 })
        {
            processor.setModulationDestination (route, 27); processor.setModulationDestination (route, legacy);
            require (processor.getModulationDestination (route) == legacy && plainParameter (processor, prefix + "LfoPhaseEnabled") == 0
                         && plainParameter (processor, prefix + "LfoPhaseTarget") == 9, "Returning to a legacy destination lost the remembered phase target");
        }
    }
    processor.setCurrentProgram (0);
    for (int row = 0; row < 32; ++row)
        require (processor.assignModulation (1 + row / 10, 18 + row % 10, .2f), "Phase destinations did not fill all 32 matrix rows");
    setParameter (processor, "mod32Enabled", 0); setParameter (processor, "mod32Curve", -.37f); clearMatrixHistory (processor);
    auto duplicate = matrixPayload (processor, 31); duplicate[7] = 1;
    require (processor.assignModulation (4, 19, .8f), "A bypassed phase duplicate could not be re-enabled");
    expectMatrixPayload (processor, 31, duplicate);
    const auto full = save (processor); require (! processor.assignModulation (24, 27) && save (processor) == full, "Phase assignment overwrote a full matrix");
    setParameter (processor, "mod32Source", 0); setParameter (processor, "mod32SourceExtra", 0);
    require (processor.assignModulation (24, 17, -.43127f) && plainParameter (processor, "mod32LfoPhaseEnabled") == 0
                 && plainParameter (processor, "mod32LfoPhaseTarget") == 0, "Reusing a phase route leaked its phase destination bank");
}

void testLfoMotionHelperHostBoundaries()
{
    for (int control = 0; control < 4; ++control) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 3; ++action)
        {
            AnyvocAudioProcessor processor;
            setParameter (processor, "lfo10Sync", 1); setParameter (processor, "lfo10AudioRateEnabled", 1);
            setParameter (processor, "mod32Dest", 1); setParameter (processor, "mod32DestBank", 1);
            setParameter (processor, "mod32FilterTarget", 2); setParameter (processor, "mod32WarpEnabled", 1);
            setParameter (processor, "mod32WarpTarget", 5); setParameter (processor, "mod32LfoPhaseTarget", 9);
            setParameter (processor, "mod32LfoPhaseEnabled", control == 2 ? 1.0f : 0.0f);
            clearMatrixHistory (processor);
            const auto replacement = save (processor);
            const std::array<const char*, 4> triggerIds { "lfo10Sync", "lfo10AudioRateEnabled", "mod32Dest", "mod32LfoPhaseTarget" };
            const std::array<const char*, 4> relatedIds { "lfo10AudioRateEnabled", "lfo10Sync", "mod32LfoPhaseTarget", "mod32FilterTarget" };
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* id : { "lfo10Sync", "lfo10AudioRateEnabled", "mod32Dest", "mod32DestBank", "mod32FilterTarget",
                                   "mod32WarpEnabled", "mod32WarpTarget", "mod32LfoPhaseEnabled", "mod32LfoPhaseTarget" })
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter (id)));
            auto& trigger = *probes[static_cast<size_t> (control < 3 ? control : 8)];
            bool alive = true; std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, relatedIds[static_cast<size_t> (control)], control == 1 ? 0 : 1);
                else alive = false;
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            if (control < 2) processor.setLfoRateMode (9, 0, [&] { return alive; });
            else processor.setModulationDestination (31, control == 2 ? 7 : 18, [&] { return alive; });
            const auto context = std::string (triggerIds[static_cast<size_t> (control)]) + " helper boundary=" + std::to_string (boundary)
                                 + " action=" + std::to_string (action);
            require (! expected.empty(), "LFO motion helper callback did not run: " + context);
            require (parameters (processor) == expected && ! processor.isUserEditActive(), "LFO motion helper wrote after host interruption: " + context);
            for (const auto& probe : probes) require (probe->starts == probe->ends, "LFO motion helper retained a host gesture: " + context);
            if (action == 0) require (! processor.canUndo() && ! processor.canRedo(), "LFO motion retained stale history after project restore");
        }
}

int lfoSourceIndex (int lfo) { return lfo < 2 ? lfo + 1 : lfo + 8; }

void prepareLfoMotionAudio (AnyvocAudioProcessor& processor, int lfo)
{
    prepareCrossModulationFixture (processor);
    setParameter (processor, "osc1Position", .45f); setParameter (processor, "ampAttack", .001f); setParameter (processor, "ampSustain", 1);
    setParameter (processor, "tempo", 120);
    for (int index = 1; index <= 10; ++index)
    {
        const auto prefix = "lfo" + juce::String (index);
        setParameter (processor, (prefix + "Rate").toRawUTF8(), 3.17f); setParameter (processor, (prefix + "Mode").toRawUTF8(), 1);
        setParameter (processor, (prefix + "Division").toRawUTF8(), 6); processor.setLfoShape (index - 1, 2);
    }
    processor.setModulationSource (31, lfoSourceIndex (lfo)); processor.setModulationDestination (31, 2);
    setParameter (processor, "mod32Amount", .35f);
}

void testLfoMotionControlsReachAudio()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int lfo = 0; lfo < 10; ++lfo) for (int feature = 0; feature < 5; ++feature)
    {
        AnyvocAudioProcessor actual, reference, baseline;
        const int other = lfo == 0 ? 1 : 0;
        prepareLfoMotionAudio (actual, lfo); prepareLfoMotionAudio (reference, other); prepareLfoMotionAudio (baseline, lfo);
        for (auto* processor : { &actual, &reference, &baseline })
        {
            setParameter (*processor, "swingAmount", .6f); setParameter (*processor, "swingDivision", 1);
            if (feature == 3)
                for (int index = 1; index <= 10; ++index) setParameter (*processor, ("lfo" + juce::String (index) + "Sync").toRawUTF8(), 1);
        }
        for (auto* processor : { &actual, &reference })
        {
            const auto prefix = "lfo" + juce::String ((processor == &actual ? lfo : other) + 1);
            if (feature == 0) setParameter (*processor, (prefix + "Phase").toRawUTF8(), .25f);
            else if (feature < 3) setParameter (*processor, (prefix + "Direction").toRawUTF8(), static_cast<float> (feature));
            else if (feature == 3) setParameter (*processor, (prefix + "FollowSwing").toRawUTF8(), 1);
            else { processor->setLfoRateMode (processor == &actual ? lfo : other, 2); setParameter (*processor, (prefix + "AudioRateHz").toRawUTF8(), 997); }
        }
        for (auto* processor : { &actual, &reference, &baseline }) processor->prepareToPlay (48000, 512);
        const auto a = render (actual, 24000, note, 127), r = render (reference, 24000, note, 127), b = render (baseline, 24000, note, 127);
        const auto difference = crossAudioDifference (a, r), baselineDifference = crossAudioDifference (a, b);
        require (rms (a) > .005 && difference < 2.0e-6 && baselineDifference > .0001,
                 "LFO " + std::to_string (lfo + 1) + " motion feature=" + std::to_string (feature)
                     + " reference difference=" + std::to_string (difference) + " baseline difference=" + std::to_string (baselineDifference));
    }
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        AnyvocAudioProcessor fast, legacy;
        prepareLfoMotionAudio (fast, lfo); prepareLfoMotionAudio (legacy, lfo);
        const auto prefix = "lfo" + juce::String (lfo + 1);
        fast.setLfoRateMode (lfo, 2); setParameter (fast, (prefix + "AudioRateHz").toRawUTF8(), 20);
        setParameter (legacy, (prefix + "Rate").toRawUTF8(), 20);
        fast.prepareToPlay (48000, 512); legacy.prepareToPlay (48000, 512);
        const auto a = render (fast, 12000, note, 127), b = render (legacy, 12000, note, 127);
        require (crossAudioDifference (a, b) < 2.0e-6, "20 Hz audio mode did not match the old 20 Hz wavetable-position path");
        for (auto* processor : { &fast, &legacy }) setParameter (*processor, (prefix + "Sync").toRawUTF8(), 1);
        setParameter (fast, (prefix + "AudioRateHz").toRawUTF8(), 997);
        fast.prepareToPlay (48000, 512); legacy.prepareToPlay (48000, 512);
        const auto syncA = render (fast, 12000, note, 127), syncB = render (legacy, 12000, note, 127);
        require (crossAudioDifference (syncA, syncB) == 0, "Remembered audio-rate controls changed a synchronized LFO");
    }
}

void testLfoPhaseDestinationsReachAudio()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        AnyvocAudioProcessor routed, direct, baseline;
        for (auto* processor : { &routed, &direct, &baseline }) prepareLfoMotionAudio (*processor, lfo);
        setParameter (direct, ("lfo" + juce::String (lfo + 1) + "Phase").toRawUTF8(), .2f);
        setParameter (routed, "macro1", .5f);
        routed.setModulationSource (0, 6); routed.setModulationDestination (0, 18 + lfo); setParameter (routed, "mod1Amount", .4f);
        for (auto* processor : { &routed, &direct, &baseline }) { processor->prepareToPlay (48000, 512); render (*processor, 48000, note, 127); }
        const auto actual = render (routed, 12000, {}, 127), expected = render (direct, 12000, {}, 127), unchanged = render (baseline, 12000, {}, 127);
        require (crossAudioDifference (actual, expected) < 2.0e-6 && crossAudioDifference (actual, unchanged) > .0001,
                 "Phase destination " + std::to_string (18 + lfo) + " missed its LFO; scalar difference=" + std::to_string (crossAudioDifference (actual, expected)));
    }
}

void openLfoMotionLab (juce::Component& editor, int index)
{
    findButton (editor, "MOD")->onClick();
    findComponent<juce::ComboBox> (editor, "LfoCardSelect1")->setSelectedItemIndex (index, juce::sendNotificationSync);
    clickMatrixControl (editor, "lfo" + juce::String (index + 1) + "Edit");
}

void testLfoMotionEditorControls()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
        processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1008, 684);
    const auto beforeNavigation = save (processor); openLfoMotionLab (*editor, 0);
    int previous = 0;
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        findComponent<juce::ComboBox> (*editor, "LFOLabSelector" + juce::String (previous + 1))->setSelectedItemIndex (lfo, juce::sendNotificationSync);
        dispatchEditorActions(); previous = lfo;
        auto* detail = findComponent<juce::Component> (*editor, "LfoDetail" + juce::String (lfo + 1));
        const auto prefix = "lfo" + juce::String (lfo + 1);
        for (const auto& id : { prefix + "Phase", prefix + "Direction", prefix + "FollowSwing", prefix + "RateMode", juce::String ("swingAmount"), juce::String ("swingDivision") })
        {
            auto* control = findComponent<juce::Component> (*detail, id);
            require (modulationControlVisible (control) && editor->getLocalBounds().contains (editor->getLocalArea (control, control->getLocalBounds())),
                     "LFO motion control was missing/clipped at minimum size: " + id.toStdString());
        }
        require (findComponent<juce::ComboBox> (*detail, prefix + "Direction")->getNumItems() == 3
                     && findComponent<juce::ComboBox> (*detail, prefix + "RateMode")->getNumItems() == 3
                     && findComponent<juce::ComboBox> (*detail, "swingDivision")->getNumItems() == 2,
                 "LFO motion choices omitted a supported mode");
    }
    require (save (processor) == beforeNavigation && ! processor.canUndo(), "LFO motion navigation changed sound/history");
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        findComponent<juce::ComboBox> (*editor, "LFOLabSelector" + juce::String (previous + 1))->setSelectedItemIndex (lfo, juce::sendNotificationSync);
        dispatchEditorActions(); previous = lfo;
        const auto prefix = "lfo" + juce::String (lfo + 1);
        auto* detail = findComponent<juce::Component> (*editor, "LfoDetail" + juce::String (lfo + 1));
        auto* mode = findComponent<juce::ComboBox> (*detail, prefix + "RateMode");
        auto* oldRate = findComponent<juce::Slider> (*detail, prefix + "Rate");
        require (oldRate != nullptr, "The original Hz control was missing before switching rate mode");
        clearMatrixHistory (processor); const auto before = parameters (processor);
        mode->setSelectedItemIndex (2, juce::sendNotificationSync); refreshEditorTimers();
        require (processor.getLfoRateMode (lfo) == 2 && processor.getUndoCount() == 1, "Audio-rate UI did not form one mode edit");
        expectHistoryResult (processor.undoUserEdit(), "Undo audio-rate UI"); expectParameters (processor, before);
        expectHistoryResult (processor.redoUserEdit(), "Redo audio-rate UI"); refreshEditorTimers();
        const auto audioModeState = save (processor);
        oldRate->setValue (9.3, juce::sendNotificationSync);
        findComponent<juce::Button> (*detail, prefix + "FollowSwing")->setToggleState (true, juce::sendNotificationSync);
        require (save (processor) == audioModeState && processor.getUndoCount() == 1, "An inactive slow-rate or swing control wrote while audio-rate mode was active");
        auto* fast = findComponent<juce::Slider> (*detail, prefix + "AudioRateHz");
        auto* phase = findComponent<juce::Slider> (*detail, prefix + "Phase");
        auto* direction = findComponent<juce::ComboBox> (*detail, prefix + "Direction");
        require (modulationControlVisible (fast) && fast->isEnabled() && fast->getMinimum() == 20 && fast->getMaximum() == 1000,
                 "Audio-rate mode did not expose its 20-1000 Hz control");
        require (phase->getTextFromValue (.5).contains ("180"), "LFO phase UI did not show degrees");
        phase->setValue (.213, juce::sendNotificationSync); direction->setSelectedItemIndex (2, juce::sendNotificationSync);
        fast->setValue (731.25, juce::sendNotificationSync);
        require (std::abs (plainParameter (processor, prefix + "Phase") - .213f) < 1.0e-6f
                     && plainParameter (processor, prefix + "Direction") == 2
                     && std::abs (plainParameter (processor, prefix + "AudioRateHz") - 731.25f) < .001f,
                 "An LFO motion UI control edited the wrong independent modulator");
        mode->setSelectedItemIndex (1, juce::sendNotificationSync); refreshEditorTimers();
        auto* swing = findComponent<juce::Button> (*detail, prefix + "FollowSwing");
        require (swing->isEnabled(), "Tempo mode did not enable Follow Swing");
        swing->setToggleState (true, juce::sendNotificationSync);
        findComponent<juce::Slider> (*detail, "swingAmount")->setValue (.6, juce::sendNotificationSync);
        findComponent<juce::ComboBox> (*detail, "swingDivision")->setSelectedItemIndex (1, juce::sendNotificationSync);
        require (plainParameter (processor, prefix + "FollowSwing") == 1 && std::abs (plainParameter (processor, "swingAmount") - .6f) < 1.0e-6f
                     && plainParameter (processor, "swingDivision") == 1, "LFO Swing UI did not publish its local flag and global controls");
        processor.setLfoShape (lfo, 4); refreshEditorTimers();
        require (! phase->isEnabled() && ! direction->isEnabled() && plainParameter (processor, prefix + "Direction") == 2
                     && std::abs (plainParameter (processor, prefix + "Phase") - .213f) < 1.0e-6f,
                 "Random LFO did not disable ineffective phase/direction while preserving their values");
        const auto randomState = save (processor);
        phase->setValue (.82, juce::sendNotificationSync); direction->setSelectedItemIndex (0, juce::sendNotificationSync);
        require (save (processor) == randomState, "Stale disabled phase/direction UI wrote to a Random LFO");
        processor.setLfoShape (lfo, 2); setParameter (processor, (prefix + "Phase").toRawUTF8(), .37f); refreshEditorTimers();
        require (phase->isEnabled() && direction->isEnabled() && std::abs (phase->getValue() - .37) < 1.0e-6,
                 "Returning to a periodic shape did not reconcile automated phase");
    }
    auto* hiddenRateMode = findComponent<juce::ComboBox> (*editor, "lfo10RateMode");
    clickMatrixControl (*editor, "LfoDetailClose10");
    const auto hiddenState = save (processor); hiddenRateMode->setSelectedItemIndex (0, juce::sendNotificationSync);
    require (save (processor) == hiddenState, "A delayed rate-mode event from a hidden LFO Lab changed sound");
    for (int page = 1; page <= 3; ++page) clickMatrixControl (*editor, "MatrixNextPage");
    clickMatrixControl (*editor, "mod32Edit");
    auto* detail = findComponent<juce::Component> (*editor, "MatrixRouteDetail");
    require (modulationControlVisible (detail), "Matrix route 32 detail was unavailable");
    auto* destination = findComponent<juce::ComboBox> (*detail, "mod32DestinationDetail");
    require (modulationControlVisible (destination) && destination->getNumItems() == 36, "Matrix detail omitted LFO phase destinations");
    for (int target = 18; target <= 27; ++target)
    {
        destination->setSelectedItemIndex (target, juce::sendNotificationSync);
        require (processor.getModulationDestination (31) == target, "Matrix UI could not choose phase destination " + std::to_string (target));
    }
    setParameter (processor, "mod32LfoPhaseTarget", 2);
    require (destination->getSelectedItemIndex() == 20, "Destination attachment read stale APVTS values during phase-bank publication");
    refreshEditorTimers();
    require (destination->getSelectedItemIndex() == 20, "Host phase-bank automation did not refresh the matrix destination");
}

void testLfoMotionUiHostCallbacks()
{
    PresetTestDirectory files;
    const std::array<const char*, 8> ids { "lfo10Phase", "lfo10Direction", "lfo10FollowSwing", "lfo10AudioRateHz", "swingAmount", "swingDivision", "lfo10Sync", "mod1Dest" };
    for (int control = 0; control < 8; ++control) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 4; ++action)
        {
            if (action == 3 && control != 6) continue;
            const auto context = std::string (ids[static_cast<size_t> (control)]) + " UI boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            AnyvocAudioProcessor processor;
            setParameter (processor, "lfo10Phase", .2f); processor.setLfoRateMode (9, control == 3 ? 2 : 1);
            if (control == 7) processor.setModulationDestination (0, 27);
            clearMatrixHistory (processor);
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* id : ids) probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter (id)));
            for (const auto* id : { "lfo10AudioRateEnabled", "mod1DestBank", "mod1FilterTarget", "mod1WarpEnabled", "mod1WarpTarget", "mod1LfoPhaseEnabled", "mod1LfoPhaseTarget" })
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter (id)));
            std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
                processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684);
            if (control == 7) { findButton (*editor, "MOD")->onClick(); clickMatrixControl (*editor, "mod1Edit"); }
            else openLfoMotionLab (*editor, 9);
            const auto replacement = save (processor); std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1)
                {
                    if (control == 6) setParameter (processor, "lfo10AudioRateEnabled", 0);
                    else if (control == 7) setParameter (processor, "mod1LfoPhaseTarget", 4);
                    else setParameter (processor, ids[static_cast<size_t> (control)], control == 1 ? 1 : control == 2 ? (boundary == 0 ? 1 : 0)
                                        : control == 3 ? 243.75f : control == 5 ? (boundary == 0 ? 1 : 0) : .33f);
                }
                else if (action == 2) editor.reset();
                else findComponent<juce::Component> (*editor, "LfoDetail10")->setVisible (false);
                expected = parameters (processor);
            };
            auto& trigger = *probes[static_cast<size_t> (control)];
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            auto* scope = findComponent<juce::Component> (*editor, control == 7 ? "MatrixRouteDetail" : "LfoDetail10");
            require (modulationControlVisible (scope), "LFO callback scope was unavailable: " + context);
            if (control == 0 || control == 3 || control == 4)
            {
                auto* slider = findComponent<juce::Slider> (*scope, ids[static_cast<size_t> (control)]);
                require (modulationControlVisible (slider), "LFO callback slider was unavailable: " + context);
                slider->setValue (control == 3 ? 731.25 : .45, juce::sendNotificationSync);
            }
            else if (control == 2) findComponent<juce::Button> (*scope, "lfo10FollowSwing")->setToggleState (true, juce::sendNotificationSync);
            else
            {
                const auto id = control == 6 ? "lfo10RateMode" : control == 7 ? "mod1DestinationDetail" : ids[static_cast<size_t> (control)];
                auto* choice = findComponent<juce::ComboBox> (*scope, id);
                require (modulationControlVisible (choice), "LFO callback choice was unavailable: " + context + " component=" + id);
                choice->setSelectedItemIndex (control == 5 ? 1 : control == 7 ? 7 : 2, juce::sendNotificationSync);
            }
            require (! expected.empty(), "LFO UI host callback did not fire: " + context);
            require (parameters (processor) == expected && ! processor.isUserEditActive(), "LFO UI overwrote host state after callback: " + context);
            for (const auto& probe : probes) require (probe->starts == probe->ends, "LFO UI retained a host gesture: " + context);
        }
}

void testLfoPhaseDropHostCallbacks()
{
    PresetTestDirectory files;
    for (int scenario = 0; scenario < 3; ++scenario) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 4; ++action)
        {
            AnyvocAudioProcessor processor;
            for (int row = 1; row < 32; ++row) { setParameter (processor, ("mod" + juce::String (row) + "Source").toRawUTF8(), 1); setParameter (processor, ("mod" + juce::String (row) + "Dest").toRawUTF8(), 1); }
            if (scenario == 2)
            {
                require (processor.assignModulation (17, 27, .43127f), "Phase drop fixture could not create its duplicate");
                setParameter (processor, "mod32Enabled", 0); setParameter (processor, "mod32AuxCurve", -.35f);
            }
            clearMatrixHistory (processor);
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* field : matrixRouteFields) probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter ("mod32" + juce::String (field))));
            auto& trigger = *probes[static_cast<size_t> (scenario == 0 ? 15 : scenario == 1 ? 16 : 7)];
            std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
                processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684); openLfoMotionLab (*editor, 9);
            auto* phase = findComponent<juce::Slider> (*editor, "lfo10Phase");
            require (modulationControlVisible (phase), "LFO phase drop target was missing");
            auto* target = dynamic_cast<juce::DragAndDropTarget*> (phase->getParentComponent());
            require (target != nullptr, "LFO phase knob did not expose the real drag/drop interface");
            clickMatrixControl (*editor, "openModulationSources");
            auto* chip = findComponent<juce::Component> (*editor, "paletteModSource17");
            require (modulationControlVisible (chip), "LFO phase drop source chip was missing");
            const juce::DragAndDropTarget::SourceDetails drop { juce::String ("anyvoc.mod:17"), chip, { 20, 20 } };
            require (target->isInterestedInDragSource (drop), "LFO phase knob rejected a valid source");
            const auto replacement = save (processor); std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, "mod32WarpTarget", 5);
                else if (action == 2) editor.reset();
                else findComponent<juce::Component> (*editor, "LfoDetail10")->setVisible (false);
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            target->itemDragEnter (drop); target->itemDropped (drop);
            const auto context = "phase drop scenario=" + std::to_string (scenario) + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty(), "Phase drop callback did not fire: " + context);
            require (parameters (processor) == expected && ! processor.isUserEditActive(), "Phase drop continued after host interruption: " + context);
            for (const auto& probe : probes) require (probe->starts == probe->ends, "Phase drop retained a host gesture: " + context);
        }
}

void testChaosParameterCompatibilityAndState()
{
    AnyvocAudioProcessor source;
    const auto& all = source.getParameters();
    require (all.size() == 1192 + macroDestinationParameterCount, "Chaos must append ten parameters after the frozen 1182-parameter prefix");
    for (int index = 0; index < 10; ++index)
    {
        const auto prefix = "lfo" + juce::String (index + 1);
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (all[1182 + index]);
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p);
        require (p != nullptr && p->getParameterID() == prefix + "Chaos" && p->getVersionHint() == 13
                     && choice != nullptr && choice->choices == juce::StringArray { "Off", "Lorenz", "Rossler" }
                     && p->getNumSteps() == 3 && p->getDefaultValue() == 0,
                 "Chaos tail order, mapping or default changed at " + prefix.toStdString());
        auto* shape = dynamic_cast<juce::AudioParameterChoice*> (source.apvts.getParameter (prefix + "Shape"));
        auto* kind = dynamic_cast<juce::AudioParameterChoice*> (source.apvts.getParameter (prefix + "Kind"));
        require (shape != nullptr && shape->choices == juce::StringArray { "Sine", "Triangle", "Saw", "Square" }
                     && kind != nullptr && kind->choices == juce::StringArray { "Basic", "Random", "Custom" },
                 "Chaos reinterpreted existing normalized shape/kind automation");
        for (int value = 0; value < 3; ++value)
            require (p->convertFrom0to1 (value * .5f) == value, "Chaos choice changed its host mapping");
        setParameter (source, (prefix + "Shape").toRawUTF8(), static_cast<float> (index % 4));
        setParameter (source, (prefix + "Kind").toRawUTF8(), static_cast<float> (index % 3));
        setParameter (source, (prefix + "Chaos").toRawUTF8(), static_cast<float> (1 + index % 2));
    }
    const auto current = stateTree (source);
    const auto expected = parameters (source);
    require (static_cast<int> (current.getProperty ("schemaVersion")) == 14, "Chaos state must use schema 14");
    AnyvocAudioProcessor restored; restore (restored, current); expectParameters (restored, expected, "Chaos DAW roundtrip");
    juce::TemporaryFile file (".anyvoc");
    require (source.savePreset (file.getFile()) && restored.loadPreset (file.getFile()), "Chaos preset file roundtrip failed");
    expectParameters (restored, expected, "Chaos file roundtrip");
    restored.setCurrentProgram (0);
    for (int index = 0; index < 10; ++index)
        require (plainParameter (restored, "lfo" + juce::String (index + 1) + "Chaos") == 0, "Init retained Chaos");
    AnyvocAudioProcessor legacy; legacy.setCurrentProgram (23);
    const auto legacyExpected = parameters (legacy); auto v12 = stateTree (legacy); v12.setProperty ("schemaVersion", 12, nullptr);
    for (int child = v12.getNumChildren(); --child >= 0;)
        if ((isChaosParameter (v12.getChild (child).getProperty ("id").toString()) || isMacroDestinationParameter (v12.getChild (child).getProperty ("id").toString()))) v12.removeChild (child, nullptr);
    restore (restored, current); restore (restored, v12); expectParameters (restored, legacyExpected, "Authentic schema 12 migration over active Chaos");
    const auto retained = save (restored);
    for (int index = 1; index <= 10; ++index)
    {
        const auto id = "lfo" + juce::String (index) + "Chaos";
        auto missing = current.createCopy(); missing.removeChild (missing.getChildWithProperty ("id", id), nullptr);
        restore (restored, missing); require (save (restored) == retained, "Schema 14 accepted missing " + id.toStdString());
        auto duplicate = current.createCopy(); duplicate.addChild (duplicate.getChildWithProperty ("id", id).createCopy(), -1, nullptr);
        restore (restored, duplicate); require (save (restored) == retained, "Schema 14 accepted duplicate " + id.toStdString());
        for (const auto* invalid : { "NaN", "Infinity", "invalid" })
        {
            auto bad = current.createCopy(); bad.getChildWithProperty ("id", id).setProperty ("value", invalid, nullptr);
            restore (restored, bad); require (save (restored) == retained, "Malformed Chaos partially replaced the live state");
        }
    }
    auto future = current.createCopy(); future.setProperty ("schemaVersion", 15, nullptr);
    restore (restored, future); require (save (restored) == retained, "Unknown future schema was accepted");
    require (source.getProgramName (24) == "Motion / Butterfly Current" && source.getProgramName (25) == "Texture / Spiral Drift",
             "Chaos presets displaced existing factory indices");
}

void testChaosShapeHistoryAndHostBoundaries()
{
    AnyvocAudioProcessor processor;
    for (int index = 0; index < 10; ++index)
    {
        const auto prefix = "lfo" + juce::String (index + 1);
        setParameter (processor, (prefix + "Shape").toRawUTF8(), 2); processor.setLfoShape (index, 5);
        for (const int selected : { 6, 7, 0, 1, 2, 3, 4, 5, 6 })
        {
            clearMatrixHistory (processor); const auto before = parameters (processor);
            const auto oldShape = plainParameter (processor, prefix + "Shape"), oldKind = plainParameter (processor, prefix + "Kind");
            processor.setLfoShape (index, selected); const auto after = parameters (processor);
            require (processor.getLfoShape (index) == selected && processor.getUndoCount() == 1, "Chaos selection split its history or chose the wrong LFO");
            if (selected >= 6)
                require (plainParameter (processor, prefix + "Shape") == oldShape && plainParameter (processor, prefix + "Kind") == oldKind,
                         "Entering Chaos discarded remembered legacy selectors");
            else require (plainParameter (processor, prefix + "Chaos") == 0, "Leaving Chaos retained its override");
            expectHistoryResult (processor.undoUserEdit(), "Undo Chaos shape"); expectParameters (processor, before);
            expectHistoryResult (processor.redoUserEdit(), "Redo Chaos shape"); expectParameters (processor, after);
            const auto unchanged = save (processor); processor.setLfoShape (index, selected);
            require (save (processor) == unchanged && processor.getUndoCount() == 1, "Repeated Chaos shape created history");
        }
    }
    const auto unchanged = save (processor);
    processor.setLfoShape (-1, 6); processor.setLfoShape (10, 7); processor.setLfoShape (0, -1); processor.setLfoShape (0, 8);
    require (save (processor) == unchanged, "Invalid Chaos selection changed state");
    for (int scenario = 0; scenario < 4; ++scenario) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 3; ++action)
        {
            AnyvocAudioProcessor p; setParameter (p, "lfo10Shape", 2); p.setLfoShape (9, 5);
            if (scenario != 0) p.setLfoShape (9, 6);
            clearMatrixHistory (p); const auto replacement = save (p); bool alive = true;
            ModulationGestureProbe basic (*p.apvts.getParameter ("lfo10Shape")), kind (*p.apvts.getParameter ("lfo10Kind")), chaos (*p.apvts.getParameter ("lfo10Chaos"));
            auto& trigger = scenario == 1 ? basic : scenario == 2 ? kind : chaos;
            std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) p.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (p, scenario == 1 ? "lfo10Chaos" : scenario == 3 ? "lfo10Kind" : "lfo10Shape", scenario == 1 ? 2 : scenario == 3 ? 1 : 3);
                else alive = false;
                expected = parameters (p);
            };
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            p.setLfoShape (9, scenario == 0 ? 6 : scenario == 1 ? 3 : scenario == 2 ? 4 : 7, [&] { return alive; });
            const auto context = "Chaos helper scenario=" + std::to_string (scenario) + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty() && parameters (p) == expected && ! p.isUserEditActive(), "Stale compound write after " + context);
            require (basic.starts == basic.ends && kind.starts == kind.ends && chaos.starts == chaos.ends, "Unbalanced gesture after " + context);
        }
}

void testChaosAllSourcesReachEvolvingAudio()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int index = 0; index < 10; ++index) for (int shape : { 6, 7 })
    {
        AnyvocAudioProcessor actual, partitioned, dry;
        for (auto* p : { &actual, &partitioned, &dry })
        {
            prepareLfoMotionAudio (*p, index); p->setLfoShape (index, shape);
        }
        setParameter (dry, "mod32Amount", 0);
        const auto resetAfterEffectWarmup = [] (AnyvocAudioProcessor& p)
        {
            // JUCE's reverb dry/wet gains begin at their constructor targets and
            // retain settled targets across prepare/reset. Settle them without
            // a voice, then reset the synth so both recordings start alike.
            p.prepareToPlay (48000, 512);
            render (p, 4096, {}, 127);
            p.prepareToPlay (48000, 512);
        };
        for (auto* p : { &actual, &partitioned, &dry }) resetAfterEffectWarmup (*p);
        const auto a = render (actual, 24000, note, 64), b = render (partitioned, 24000, note, 257), d = render (dry, 24000, note, 127);
        const auto initialPoint = actual.getLfoChaosPoint (index);
        const auto context = "LFO " + std::to_string (index + 1) + " shape=" + std::to_string (shape);
        require (rms (a) > .005 && crossAudioDifference (a, d) > .0001, "Chaos source did not reach audio: " + context);
        require (crossAudioDifference (a, b) < 2.0e-6, "Chaos audio depended on block partition: " + context);
        auto previous = actual.getLfoChaosPoint (index); int changed = 0;
        for (int step = 0; step < 12; ++step)
        {
            render (actual, 2048, {}, 127); const auto point = actual.getLfoChaosPoint (index);
            for (float value : point) require (std::isfinite (value) && std::abs (value) <= 1.00001f, "Chaos telemetry was nonfinite or unbounded: " + context);
            if (point != previous) ++changed;
            previous = point;
        }
        require (changed >= 10, "Chaos telemetry froze instead of continuing its trajectory: " + context);
        resetAfterEffectWarmup (actual);
        const auto repeated = render (actual, 24000, note, 257);
        const auto repeatedDifference = crossAudioDifference (a, repeated);
        require (repeatedDifference == 0 && actual.getLfoChaosPoint (index) == initialPoint,
                 "Preparing the processor did not reproduce the same Chaos trajectory: " + context
                     + " audio difference=" + juce::String (repeatedDifference, 12).toStdString());
    }
    AnyvocAudioProcessor lorenz, rossler;
    for (auto* p : { &lorenz, &rossler }) { prepareLfoMotionAudio (*p, 9); p->setLfoShape (9, p == &lorenz ? 6 : 7); p->prepareToPlay (48000, 512); }
    require (crossAudioDifference (render (lorenz, 48000, note, 127), render (rossler, 48000, note, 127)) > .0001,
             "Lorenz and Rossler produced the same modulation audio");
}

void testChaosRateBindingsAndInactivePhase()
{
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    for (int shape : { 6, 7 }) for (int feature = 0; feature < 5; ++feature)
    {
        AnyvocAudioProcessor actual, reference;
        for (auto* p : { &actual, &reference }) { prepareLfoMotionAudio (*p, 9); p->setLfoShape (9, shape); }
        if (feature == 0)
        {
            setParameter (actual, "lfo10Phase", .713f); setParameter (actual, "lfo10Direction", 2);
            setParameter (actual, "macro1", .9f); actual.setModulationSource (0, 6); actual.setModulationDestination (0, 27); setParameter (actual, "mod1Amount", .7f);
        }
        else if (feature == 1) { actual.setLfoRateMode (9, 2); setParameter (actual, "lfo10AudioRateHz", 137); }
        else
        {
            for (auto* p : { &actual, &reference }) p->setLfoRateMode (9, 1);
            if (feature == 2) setParameter (actual, "tempo", 83);
            if (feature == 3) { setParameter (actual, "lfo10FollowSwing", 1); setParameter (actual, "swingAmount", .8f); }
            if (feature == 4) { setParameter (actual, "lfo10AudioRateEnabled", 1); setParameter (actual, "lfo10AudioRateHz", 997); }
        }
        for (auto* p : { &actual, &reference }) p->prepareToPlay (48000, 512);
        const auto a = render (actual, 48000, note, 127), b = render (reference, 48000, note, 127);
        const auto difference = crossAudioDifference (a, b);
        require ((feature == 0 || feature == 4) ? difference == 0 : difference > .0001,
                 "Chaos rate/phase binding mismatch shape=" + std::to_string (shape) + " feature=" + std::to_string (feature) + " difference=" + std::to_string (difference));
    }
    for (int shape = 0; shape < 6; ++shape)
    {
        AnyvocAudioProcessor visited, legacy;
        for (auto* p : { &visited, &legacy }) { prepareLfoMotionAudio (*p, 9); p->setLfoShape (9, shape); }
        visited.setLfoShape (9, 6); visited.setLfoShape (9, 7); visited.setLfoShape (9, shape);
        for (auto* p : { &visited, &legacy }) p->prepareToPlay (48000, 512);
        require (crossAudioDifference (render (visited, 12000, note, 127), render (legacy, 12000, note, 127)) == 0,
                 "Returning from Chaos changed legacy shape " + std::to_string (shape));
    }
}

void testChaosEditorGatingAndRealTrajectory()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor;
    prepareLfoMotionAudio (processor, 9);
    std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
        processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1008, 684);
    for (int index = 0; index < 10; ++index)
    {
        const auto prefix = "lfo" + juce::String (index + 1);
        setParameter (processor, (prefix + "Phase").toRawUTF8(), .213f); setParameter (processor, (prefix + "Direction").toRawUTF8(), 2);
        const auto beforeNavigation = save (processor); openLfoMotionLab (*editor, index);
        require (save (processor) == beforeNavigation, "Opening a Chaos Lab changed sound");
        auto* detail = findComponent<juce::Component> (*editor, "LfoDetail" + juce::String (index + 1));
        auto* choice = findComponent<juce::ComboBox> (*detail, prefix + "ShapeChoice");
        auto* curve = findComponent<juce::Component> (*detail, prefix + "Curve");
        auto* phase = findComponent<juce::Slider> (*detail, prefix + "Phase");
        auto* direction = findComponent<juce::ComboBox> (*detail, prefix + "Direction");
        require (modulationControlVisible (choice) && choice->getNumItems() == 8 && modulationControlVisible (curve)
                     && modulationControlVisible (phase) && modulationControlVisible (direction), "Chaos Lab omitted an expected control");
        for (int shape : { 6, 7 })
        {
            const auto context = "LFO " + std::to_string (index + 1) + " shape=" + std::to_string (shape);
            choice->setSelectedItemIndex (shape, juce::sendNotificationSync); refreshEditorTimers();
            require (processor.getLfoShape (index) == shape && ! phase->isEnabled() && ! direction->isEnabled()
                         && std::abs (plainParameter (processor, prefix + "Phase") - .213f) < 1.0e-6f
                         && plainParameter (processor, prefix + "Direction") == 2, "Chaos UI did not preserve and disable ineffective phase/direction");
            const auto unchanged = save (processor);
            phase->setValue (.8, juce::sendNotificationSync); direction->setSelectedItemIndex (0, juce::sendNotificationSync);
            require (save (processor) == unchanged, "Inactive Chaos phase/direction callback changed sound: " + context);
            for (int preset = 0; preset < 5; ++preset)
            {
                auto* button = findComponent<juce::Button> (*detail, prefix + "CurvePreset" + juce::String (preset));
                require (modulationControlVisible (button) && ! button->isEnabled(), "Chaos retained an enabled curve preset");
                if (button->onClick) button->onClick();
                require (save (processor) == unchanged,
                         "Inactive Chaos curve preset changed sound: " + context + " preset=" + std::to_string (preset));
            }
            const juce::Point<float> start { 1, 1 }, end { 600, 100 };
            curve->mouseDown (modulationMouseEvent (*curve, start, start));
            curve->mouseDrag (modulationMouseEvent (*curve, end, start)); curve->mouseUp (modulationMouseEvent (*curve, end, start));
            require (save (processor) == unchanged, "Inactive Chaos drawing callback changed sound: " + context);
            auto* drop = dynamic_cast<juce::DragAndDropTarget*> (phase->getParentComponent());
            require (drop != nullptr, "Phase lost its real drop interface");
            const juce::DragAndDropTarget::SourceDetails source { juce::String ("anyvoc.mod:6"), curve, { 20, 20 } };
            require (! drop->isInterestedInDragSource (source), "Chaos Phase accepted a modulation drag: " + context);
            drop->itemDropped (source);
            require (save (processor) == unchanged && ! processor.isUserEditActive(), "A disabled Chaos drawing, preset, phase or drop callback changed sound: " + context);
            setParameter (processor, (prefix + "Chaos").toRawUTF8(), 0);
            require (choice->getSelectedItemIndex() == processor.getLfoShape (index), "Shape UI read stale legacy values while host disabled Chaos");
            setParameter (processor, (prefix + "Chaos").toRawUTF8(), static_cast<float> (shape - 5));
            require (choice->getSelectedItemIndex() == shape, "Shape UI read stale APVTS cache during Chaos host publication");
        }
    }
    auto* detail = findComponent<juce::Component> (*editor, "LfoDetail10");
    auto* curve = findComponent<juce::Component> (*detail, "lfo10Curve");
    for (int shape : { 6, 7 })
    {
        processor.setLfoShape (9, shape); processor.prepareToPlay (48000, 512); refreshEditorTimers();
        const auto saved = save (processor);
        for (int tick = 0; tick < 12; ++tick)
        {
            render (processor, 2048, tick == 0 ? std::vector<Event> { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } } : std::vector<Event> {}, 127);
            refreshEditorTimers();
            const auto point = processor.getLfoChaosPoint (9); const auto& props = curve->getProperties();
            require (static_cast<int> (props["chaosTraceShape"]) == shape
                         && std::abs (static_cast<double> (props["chaosTraceX"]) - point[0]) < 1.0e-7
                         && std::abs (static_cast<double> (props["chaosTraceY"]) - point[1]) < 1.0e-7
                         && std::abs (static_cast<double> (props["chaosTraceZ"]) - point[2]) < 1.0e-7,
                     "Chaos graph did not display actual processor XYZ telemetry");
        }
        const int samples = static_cast<int> (curve->getProperties()["chaosTraceSamples"]);
        require (samples >= 10, "Chaos display did not retain an evolving trajectory");
        refreshEditorTimers(); refreshEditorTimers();
        require (static_cast<int> (curve->getProperties()["chaosTraceSamples"]) == samples && save (processor) == saved,
                 "Idle Chaos display fabricated motion or modified the preset");
    }
    setParameter (processor, "lfo10Mode", 2); processor.prepareToPlay (48000, 512);
    render (processor, 96000, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }, 127); refreshEditorTimers();
    const auto held = processor.getLfoChaosPoint (9);
    const int heldCount = static_cast<int> (curve->getProperties()["chaosTraceTotalSamples"]);
    render (processor, 12000, {}, 127); refreshEditorTimers();
    require (processor.getLfoChaosPoint (9) == held && static_cast<int> (curve->getProperties()["chaosTraceTotalSamples"]) == heldCount,
             "A held Chaos envelope acquired artificial UI motion");
    auto* choice = findComponent<juce::ComboBox> (*detail, "lfo10ShapeChoice");
    clickMatrixControl (*editor, "LfoDetailClose10"); const auto hidden = save (processor);
    choice->setSelectedItemIndex (0, juce::sendNotificationSync);
    require (save (processor) == hidden, "A delayed hidden Chaos selector changed sound");

    // Exercise the interval between a message-thread parameter change and the
    // next audio block. Waiting for a timer or processing audio before the first
    // read would hide a stale snapshot from the previous algorithm/play mode.
    const std::array<float, 3> noPoint {};
    const auto refreshLabImmediately = [&] (int index)
    {
        const auto number = juce::String (index + 1);
        if (auto* close = findComponent<juce::Button> (*editor, "LfoDetailClose" + number);
            modulationControlVisible (close))
            close->onClick();
        findButton (*editor, "MOD")->onClick();
        findComponent<juce::ComboBox> (*editor, "LfoCardSelect1")->setSelectedItemIndex (index, juce::sendNotificationSync);
        auto* open = findComponent<juce::Button> (*editor, "lfo" + number + "Edit");
        require (modulationControlVisible (open) && static_cast<bool> (open->onClick), "Missing synchronous Chaos Lab open action");
        open->onClick(); // openLfoDetail synchronously refreshes the real canvas.
        auto* page = findComponent<juce::Component> (*editor, "LfoDetail" + number);
        auto* canvas = findComponent<juce::Component> (*page, "lfo" + number + "Curve");
        require (modulationControlVisible (canvas), "Chaos snapshot canvas was not visible");
        return canvas;
    };
    const auto expectSnapshot = [&] (juce::Component& canvas, const std::array<float, 3>& expected,
                                     int shape, int mode, int samples, const std::string& context)
    {
        const auto& properties = canvas.getProperties();
        require (static_cast<int> (properties["chaosTraceShape"]) == shape
                     && static_cast<int> (properties["chaosTraceMode"]) == mode
                     && static_cast<int> (properties["chaosTraceSamples"]) == samples
                     && static_cast<int> (properties["chaosTraceTotalSamples"]) == samples
                     && static_cast<double> (properties["chaosTraceX"]) == expected[0]
                     && static_cast<double> (properties["chaosTraceY"]) == expected[1]
                     && static_cast<double> (properties["chaosTraceZ"]) == expected[2],
                 "Chaos UI retained a mismatched snapshot or fabricated a zero-point trace: " + context);
    };
    for (int index : { 0, 9 })
    {
        const auto prefix = "lfo" + juce::String (index + 1);
        for (int action = 0; action < 4; ++action)
        {
            const auto context = "LFO " + std::to_string (index + 1) + " publication action=" + std::to_string (action);
            prepareLfoMotionAudio (processor, index);
            processor.setLfoShape (index, 6); clearMatrixHistory (processor);
            const int oldShape = action == 1 ? 6 : 7;
            processor.setLfoShape (index, oldShape);
            processor.prepareToPlay (48000, 512);
            for (int tick = 0; tick < 3; ++tick)
            {
                render (processor, 2048, tick == 0 ? std::vector<Event> { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } } : std::vector<Event> {}, 127);
                refreshLabImmediately (index);
            }
            const auto oldPoint = processor.getLfoChaosPoint (index);
            require (processor.getLfoChaosPoint (index, oldShape, 1) == oldPoint,
                     "Explicit Chaos labels did not select the current snapshot: " + context);
            auto* canvas = refreshLabImmediately (index);
            require (oldPoint != noPoint && static_cast<int> (canvas->getProperties()["chaosTraceSamples"]) >= 3,
                     "Chaos publication fixture did not establish a real old trajectory: " + context);

            const int nextShape = action == 1 ? 7 : action == 2 ? 7 : 6;
            const int nextMode = action == 2 ? 2 : 1;
            if (action == 0) processor.setLfoShape (index, nextShape);
            else if (action == 1) setParameter (processor, (prefix + "Chaos").toRawUTF8(), 2);
            else if (action == 2) setParameter (processor, (prefix + "Mode").toRawUTF8(), 2);
            else expectHistoryResult (processor.undoUserEdit(), "Undo Rossler before the next audio block");

            require (processor.getLfoShape (index) == nextShape && plainParameter (processor, prefix + "Mode") == nextMode,
                     "Chaos publication fixture did not change the intended model: " + context);
            require (processor.getLfoChaosPoint (index) == noPoint,
                     "Getter returned the previous model before its next audio block: " + context);
            require (processor.getLfoChaosPoint (index, nextShape, nextMode) == noPoint
                         && processor.getLfoChaosPoint (index, oldShape, 1) == oldPoint,
                     "Explicit Chaos snapshot filtering reread the newer APVTS labels: " + context);
            canvas = refreshLabImmediately (index);
            expectSnapshot (*canvas, noPoint, nextShape, nextMode, 0, context + " before audio");
            expectSnapshot (*refreshLabImmediately (index), noPoint, nextShape, nextMode, 0, context + " repeated UI refresh");

            // A freshly prepared instance provides the new model's same-index
            // read position, independently of the old active voice's history.
            AnyvocAudioProcessor reference;
            prepareLfoMotionAudio (reference, index); reference.setLfoShape (index, nextShape);
            setParameter (reference, (prefix + "Mode").toRawUTF8(), static_cast<float> (nextMode));
            reference.prepareToPlay (48000, 512);
            render (reference, 4096, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }, 127);
            render (processor, 4096, {}, 127);
            const auto newPoint = processor.getLfoChaosPoint (index);
            require (newPoint != noPoint && newPoint == reference.getLfoChaosPoint (index),
                     "Newly processed Chaos snapshot did not match its algorithm and play mode: " + context);
            // The UI may have captured its labels just before the audio thread
            // publishes a different model. Filter against those captured labels,
            // rather than silently substituting a second APVTS read here.
            require (processor.getLfoChaosPoint (index, oldShape, 1) == noPoint
                         && processor.getLfoChaosPoint (index, nextShape, nextMode) == newPoint,
                     "Captured old Chaos labels accepted a newly published model: " + context);
            expectSnapshot (*refreshLabImmediately (index), newPoint, nextShape, nextMode, 1, context + " after audio");
        }

        processor.setLfoShape (index, 6); setParameter (processor, (prefix + "Mode").toRawUTF8(), 1);
        processor.prepareToPlay (48000, 512); render (processor, 4096, {}, 127);
        require (processor.getLfoChaosPoint (index) == noPoint, "Idle Trigger Chaos fabricated a voice snapshot");
        expectSnapshot (*refreshLabImmediately (index), noPoint, 6, 1, 0, "idle Trigger");
        expectSnapshot (*refreshLabImmediately (index), noPoint, 6, 1, 0, "idle Trigger repeated refresh");

        setParameter (processor, (prefix + "Mode").toRawUTF8(), 2);
        require (processor.getLfoChaosPoint (index) == noPoint, "An idle mode change exposed an old snapshot");
        expectSnapshot (*refreshLabImmediately (index), noPoint, 6, 2, 0, "Envelope before its first voice");
        render (processor, 96000, { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } }, 127);
        const auto heldPoint = processor.getLfoChaosPoint (index);
        require (heldPoint != noPoint, "Completed Chaos Envelope lost its actual held point");
        expectSnapshot (*refreshLabImmediately (index), heldPoint, 6, 2, 1, "Envelope held point");
        render (processor, 4096, {}, 127);
        require (processor.getLfoChaosPoint (index) == heldPoint, "Completed Chaos Envelope changed its held point");
        expectSnapshot (*refreshLabImmediately (index), heldPoint, 6, 2, 1, "Envelope held repeated refresh");
    }
}

void testChaosShapeUiHostCallbacks()
{
    PresetTestDirectory files;
    for (int scenario = 0; scenario < 3; ++scenario) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 4; ++action)
        {
            AnyvocAudioProcessor p; setParameter (p, "lfo10Shape", 2); p.setLfoShape (9, 5);
            if (scenario != 0) p.setLfoShape (9, 6);
            clearMatrixHistory (p);
            ModulationGestureProbe basic (*p.apvts.getParameter ("lfo10Shape")), kind (*p.apvts.getParameter ("lfo10Kind")), chaos (*p.apvts.getParameter ("lfo10Chaos"));
            std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (p, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684); openLfoMotionLab (*editor, 9);
            const auto replacement = save (p); std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) p.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (p, scenario == 1 ? "lfo10Chaos" : "lfo10Shape", scenario == 1 ? 2 : 3);
                else if (action == 2) editor.reset();
                else findComponent<juce::Component> (*editor, "LfoDetail10")->setVisible (false);
                expected = parameters (p);
            };
            auto& trigger = scenario == 0 ? chaos : scenario == 1 ? basic : kind;
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            auto* detail = findComponent<juce::Component> (*editor, "LfoDetail10");
            auto* box = findComponent<juce::ComboBox> (*detail, "lfo10ShapeChoice");
            require (modulationControlVisible (box) && box->getNumItems() == 8, "Chaos unified shape control was unavailable");
            box->setSelectedItemIndex (scenario == 0 ? 6 : scenario == 1 ? 3 : 4, juce::sendNotificationSync);
            const auto context = "Chaos UI scenario=" + std::to_string (scenario) + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty() && parameters (p) == expected && ! p.isUserEditActive(), "Stale shape UI write after " + context);
            require (basic.starts == basic.ends && kind.starts == kind.ends && chaos.starts == chaos.ends, "Unbalanced UI gesture after " + context);
        }
}

// The platform boundary is simulated; the actual buttons, snapshot guards,
// codec, file I/O and sound history remain production code. No test reads or
// overwrites the user's OS clipboard or opens a native chooser.
void testMacroDestinationCompatibilityAndState()
{
    AnyvocAudioProcessor source;
    const auto& all = source.getParameters();
    require (all.size() == 1256 && anyvoc::modulationDestinationCount == 36,
             "Macro destinations must append exactly 64 controls after the complete 1192-parameter prefix");
    for (int index = 0; index < 1192; ++index)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (all[index]);
        require (parameter != nullptr && parameter->getVersionHint() < 14
                     && ! isMacroDestinationParameter (parameter->getParameterID()),
                 "A macro destination control was inserted into the legacy host prefix");
    }
    const juce::StringArray names { "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    for (int row = 0; row < 32; ++row)
    {
        const auto prefix = "mod" + juce::String (row + 1);
        for (int field = 0; field < 2; ++field)
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (all[1192 + row * 2 + field]);
            require (parameter != nullptr && parameter->getParameterID() == prefix + (field == 0 ? "MacroEnabled" : "MacroTarget")
                         && parameter->getVersionHint() == 14 && parameter->getDefaultValue() == 0
                         && parameter->getNumSteps() == (field == 0 ? 2 : 8),
                     "Macro target tail order, version, neutral default or host mapping changed at " + prefix.toStdString());
            if (field == 0) require (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr, "Macro bank enable is not a Boolean parameter");
            else
            {
                auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter);
                require (choice != nullptr && choice->choices == names, "Macro target labels or normalized choice count changed");
                for (int target = 0; target < 8; ++target)
                    require (parameter->convertFrom0to1 (target / 7.0f) == target, "Macro target normalized host mapping is incorrect");
            }
        }
        configureMatrixPayload (source, row);
        source.setModulationDestination (row, 28 + row % 8);
    }
    for (const int destination : { -1, 0, 17, 28, 35, 36 })
        require (anyvoc::lfoIndexForPhaseDestination (destination) == -1,
                 "A macro or invalid destination was misclassified as an LFO phase index");
    require (anyvoc::lfoIndexForPhaseDestination (18) == 0 && anyvoc::lfoIndexForPhaseDestination (27) == 9,
             "The original phase destination boundaries changed");
    require (anyvoc::macroIndexForDestination (27) == -1 && anyvoc::macroIndexForDestination (28) == 0
                 && anyvoc::macroIndexForDestination (35) == 7 && anyvoc::macroIndexForDestination (36) == -1
                 && anyvoc::macroDestinationId (-1) == 0 && anyvoc::macroDestinationId (0) == 28
                 && anyvoc::macroDestinationId (7) == 35 && anyvoc::macroDestinationId (8) == 0,
             "Macro destination helper boundaries overlap legacy destinations or accept invalid indices");
    clearMatrixHistory (source);
    const auto current = stateTree (source);
    require (static_cast<int> (current.getProperty ("schemaVersion")) == 14, "Macro destinations require schema 14");
    AnyvocAudioProcessor restored;
    restore (restored, current);
    for (int row = 0; row < 32; ++row) expectMatrixPayload (restored, row, matrixPayload (source, row));
    juce::TemporaryFile file (".anyvoc");
    require (source.savePreset (file.getFile()) && restored.loadPreset (file.getFile()), "Macro destination preset roundtrip failed");
    expectParameters (restored, parameters (source), "Macro destination file roundtrip");
    AnyvocAudioProcessor legacy;
    legacy.setCurrentProgram (24); clearMatrixHistory (legacy);
    auto v13 = stateTree (legacy); v13.setProperty ("schemaVersion", 13, nullptr);
    for (int child = v13.getNumChildren(); --child >= 0;)
        if (isMacroDestinationParameter (v13.getChild (child).getProperty ("id").toString())) v13.removeChild (child, nullptr);
    restore (restored, v13);
    expectParameters (restored, parameters (legacy), "Authentic schema 13 over active macro destinations");
    require (restored.getLfoShape (0) == legacy.getLfoShape (0), "Macro migration discarded an existing Chaos selection");
    const auto retained = save (restored);
    for (int row = 1; row <= 32; ++row) for (const auto* suffix : { "MacroEnabled", "MacroTarget" })
    {
        const auto id = "mod" + juce::String (row) + suffix;
        auto missing = current.createCopy(); missing.removeChild (missing.getChildWithProperty ("id", id), nullptr);
        restore (restored, missing); require (save (restored) == retained, "Schema 14 accepted missing " + id.toStdString());
        auto duplicate = current.createCopy(); duplicate.addChild (duplicate.getChildWithProperty ("id", id).createCopy(), -1, nullptr);
        restore (restored, duplicate); require (save (restored) == retained, "Duplicate macro parameter partially replaced sound: " + id.toStdString());
        for (const auto* value : { "NaN", "Infinity", "0 junk" })
        {
            auto bad = current.createCopy(); bad.getChildWithProperty ("id", id).setProperty ("value", value, nullptr);
            restore (restored, bad); require (save (restored) == retained, "Malformed macro target partially replaced sound: " + id.toStdString());
        }
    }
    restored.setCurrentProgram (0);
    for (int row = 1; row <= 32; ++row)
        require (plainParameter (restored, "mod" + juce::String (row) + "MacroEnabled") == 0
                     && plainParameter (restored, "mod" + juce::String (row) + "MacroTarget") == 0,
                 "Init retained a macro destination bank");
    require (source.getProgramName (26) == "Performance / Prism Handle" && source.getProgramName (27) == "Motion / Breathing Prism",
             "Macro examples did not append after the unchanged 26 factory programs");
}

void testMacroDestinationBanksCapacityAndHistory()
{
    AnyvocAudioProcessor processor;
    for (const int row : { 0, 31 })
    {
        const auto prefix = "mod" + juce::String (row + 1);
        configureMatrixPayload (processor, row);
        for (int target = 0; target < 8; ++target)
        {
            processor.setModulationDestination (row, 27);
            clearMatrixHistory (processor);
            const auto before = matrixPayload (processor, row);
            processor.setModulationDestination (row, 28 + target);
            auto expected = before; expected[17] = 1; expected[18] = target / 7.0f;
            expectMatrixPayload (processor, row, expected);
            require (processor.getModulationDestination (row) == 28 + target && processor.getUndoCount() == 1,
                     "Macro selection changed an inactive bank or split compound history");
            expectHistoryResult (processor.undoUserEdit(), "Undo macro destination"); expectMatrixPayload (processor, row, before);
            expectHistoryResult (processor.redoUserEdit(), "Redo macro destination"); expectMatrixPayload (processor, row, expected);
            const auto unchanged = save (processor); processor.setModulationDestination (row, 28 + target);
            require (save (processor) == unchanged && processor.getUndoCount() == 1, "Repeated macro destination selection created an edit");
        }
        // All flags may be simultaneously remembered by host automation. The
        // highest active bank wins; turning it off reveals the next bank.
        setParameter (processor, (prefix + "Dest").toRawUTF8(), 7); setParameter (processor, (prefix + "FilterTarget").toRawUTF8(), 3);
        setParameter (processor, (prefix + "DestBank").toRawUTF8(), 1); setParameter (processor, (prefix + "WarpTarget").toRawUTF8(), 5);
        setParameter (processor, (prefix + "WarpEnabled").toRawUTF8(), 1); setParameter (processor, (prefix + "LfoPhaseTarget").toRawUTF8(), 9);
        setParameter (processor, (prefix + "LfoPhaseEnabled").toRawUTF8(), 1);
        require (processor.getModulationDestination (row) == 35, "Macro bank did not have the documented highest priority");
        for (const auto& entry : std::array<std::pair<const char*, int>, 4> {{{ "MacroEnabled", 27 }, { "LfoPhaseEnabled", 17 }, { "WarpEnabled", 11 }, { "DestBank", 7 }}})
        {
            setParameter (processor, (prefix + entry.first).toRawUTF8(), 0);
            require (processor.getModulationDestination (row) == entry.second, "Hidden destination bank priority was changed by macro targets");
        }
        for (const int destination : { 0, 7, 11, 17, 18, 27 })
        {
            processor.setModulationDestination (row, 35); processor.setModulationDestination (row, destination);
            require (processor.getModulationDestination (row) == destination && plainParameter (processor, prefix + "MacroEnabled") == 0
                         && plainParameter (processor, prefix + "MacroTarget") == 7, "Leaving a macro destination discarded its remembered target");
        }
    }
    processor.setCurrentProgram (0);
    for (int row = 0; row < 32; ++row)
        require (processor.assignModulation (1 + row / 8, 28 + row % 8, .25f), "Macro destinations could not fill all 32 rows");
    setParameter (processor, "mod1Enabled", 0); setParameter (processor, "mod32Enabled", 0);
    const auto full = save (processor);
    require (! processor.assignModulation (24, 35) && save (processor) == full, "Macro assignment reused an occupied bypassed row");
    setParameter (processor, "mod32AuxCurve", -.43127f); setParameter (processor, "mod32LfoPhaseTarget", 9);
    clearMatrixHistory (processor);
    auto expected = matrixPayload (processor, 31); expected[7] = 1;
    require (processor.assignModulation (4, 35, -.9f), "A bypassed macro duplicate could not be re-enabled");
    expectMatrixPayload (processor, 31, expected);
    require (processor.getUndoCount() == 1, "Macro duplicate re-enable fragmented history");
    expectHistoryResult (processor.undoUserEdit(), "Undo macro duplicate"); expected[7] = 0; expectMatrixPayload (processor, 31, expected);
    expectHistoryResult (processor.redoUserEdit(), "Redo macro duplicate"); expected[7] = 1; expectMatrixPayload (processor, 31, expected);
    const auto unchanged = save (processor);
    require (processor.assignModulation (4, 35, -.9f) && save (processor) == unchanged, "An active macro duplicate changed depth or remembered controls");
    setParameter (processor, "mod32Source", 0); setParameter (processor, "mod32SourceExtra", 0);
    require (processor.assignModulation (24, 27, .3125f) && processor.getModulationDestination (31) == 27
                 && plainParameter (processor, "mod32MacroEnabled") == 0 && plainParameter (processor, "mod32MacroTarget") == 0,
             "Reusing a macro route leaked its inactive target bank");
    const auto invalidBefore = save (processor); const auto revision = processor.getHistoryRevision();
    for (const int invalid : { -1, 36, 99 })
    {
        processor.setModulationDestination (31, invalid);
        require (! processor.assignModulation (1, invalid), "An invalid macro destination was accepted");
    }
    require (save (processor) == invalidBefore && processor.getHistoryRevision() == revision, "Invalid destination changed sound or history");
}

void testMacroDestinationHelperHostBoundaries()
{
    const std::array<const char*, 9> selectors { "Dest", "FilterTarget", "DestBank", "WarpTarget", "WarpEnabled",
                                               "LfoPhaseTarget", "LfoPhaseEnabled", "MacroTarget", "MacroEnabled" };
    for (int operation = 0; operation < 4; ++operation) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = -2; action < static_cast<int> (selectors.size()); ++action)
        {
            AnyvocAudioProcessor processor;
            if (operation == 2) { setParameter (processor, "mod32AuxCurve", -.3125f); }
            else
            {
                configureMatrixPayload (processor, 31);
                processor.setModulationSource (31, 17);
                processor.setModulationDestination (31, operation == 0 ? 27 : 35);
                if (operation == 3) setParameter (processor, "mod32Enabled", 0);
            }
            // Force the final row to be the only vacancy when assigning.
            for (int row = 1; row < 32; ++row)
            {
                setParameter (processor, ("mod" + juce::String (row) + "Source").toRawUTF8(), 1);
                setParameter (processor, ("mod" + juce::String (row) + "Dest").toRawUTF8(), 1);
            }
            clearMatrixHistory (processor); const auto replacement = save (processor);
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* field : matrixRouteFields)
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter ("mod32" + juce::String (field))));
            auto& trigger = *probes[static_cast<size_t> (operation == 0 ? 18 : operation == 1 ? 1 : operation == 2 ? 2 : 7)];
            bool alive = true; std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == -2) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == -1) alive = false;
                else
                {
                    auto* parameter = processor.apvts.getParameter ("mod32" + juce::String (selectors[static_cast<size_t> (action)]));
                    parameter->setValueNotifyingHost (parameter->getValue() > .5f ? 0.0f : 1.0f);
                }
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            if (operation < 2) processor.setModulationDestination (31, operation == 0 ? 28 : 7, [&] { return alive; });
            else processor.assignModulation (17, 35, .5f, [&] { return alive; });
            const auto context = "macro helper operation=" + std::to_string (operation) + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty(), "Requested host callback did not run: " + context);
            require (parameters (processor) == expected && ! processor.isUserEditActive(), "Macro helper continued after host interruption: " + context);
            for (const auto& probe : probes) require (probe->starts == probe->ends, "Macro helper retained a host gesture: " + context);
            if (action == -2) require (! processor.canUndo() && ! processor.canRedo(), "Project replacement retained stale macro history");
        }
}

void testMacroDestinationsReachAudioAndSnapshots()
{
    // These constants are independent of the modulation implementation. Each
    // active incoming route produces .25, zero, or a saturated endpoint.
    // References use an ordinary stored macro, with no incoming macro route.
    struct Case { float amount; int input; bool auxiliary; int auxPolarity; float curve; bool enabled; float expected; };
    const std::array<Case, 7> cases {{
        { .25f, 0, false, 0, 0, true, .5f },
        { .5f, 0, true, 0, 0, true, .5f },
        { .5f, 0, true, 2, 0, true, .25f },
        { 1, 1, false, 0, .25f, true, .5f },
        { -1, 0, false, 0, 0, true, 0 },
        { 1, 0, false, 0, 0, true, 1 },
        { .25f, 0, false, 0, 0, false, .25f }
    }};
    for (int macro = 0; macro < 8; ++macro) for (size_t scenario = 0; scenario < cases.size(); ++scenario)
    {
        AnyvocAudioProcessor direct, routed;
        const auto item = cases[scenario];
        const int other = (macro + 1) % 8;
        const int source = macro < 4 ? 6 + macro : 17 + macro;
        const int otherSource = other < 4 ? 6 + other : 17 + other;
        const auto macroId = "macro" + juce::String (macro + 1);
        for (auto* processor : { &direct, &routed })
        {
            prepareCrossModulationFixture (*processor);
            setParameter (*processor, macroId.toRawUTF8(), .25f);
            setParameter (*processor, ("macro" + juce::String (other + 1)).toRawUTF8(), .5f);
            processor->setModulationSource (31, source); processor->setModulationDestination (31, 2);
            setParameter (*processor, "mod32Amount", .5f);
        }
        setParameter (direct, macroId.toRawUTF8(), item.expected);
        require (plainParameter (direct, macroId) == item.expected, "Macro oracle was changed by legacy host quantization");
        routed.setModulationSource (0, item.input == 0 ? 4 : otherSource);
        routed.setModulationDestination (0, 28 + macro);
        setParameter (routed, "mod1Amount", item.amount); setParameter (routed, "mod1Curve", item.curve);
        setParameter (routed, "mod1Enabled", item.enabled ? 1 : 0);
        if (item.auxiliary) routed.setModulationSource (0, otherSource, true);
        setParameter (routed, "mod1AuxPolarity", static_cast<float> (item.auxPolarity));
        clearMatrixHistory (routed); const auto stored = save (routed);
        direct.prepareToPlay (48000, 257); routed.prepareToPlay (48000, 257);
        const std::vector<Event> notes { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } };
        render (direct, 24000, notes, 257); render (routed, 24000, notes, 257);
        const auto expectedAudio = render (direct, 4096, {}, 257), audio = render (routed, 4096, {}, 257);
        const auto context = "macro=" + std::to_string (macro + 1) + " case=" + std::to_string (scenario);
        require (rms (audio) > .005 && crossAudioDifference (audio, expectedAudio) < 2.0e-6,
                 "Macro target did not match the independent constant audio reference: " + context);
        const auto snapshot = routed.getMacroModulationSnapshot();
        require (snapshot.hasActiveVoice && snapshot.voiceId != 0 && std::abs (snapshot.values[static_cast<size_t> (macro)] - item.expected) < 2.0e-6f,
                 "Live macro snapshot does not describe the value used by the rendered voice: " + context);
        for (size_t index = 0; index < snapshot.values.size(); ++index)
            require (std::isfinite (snapshot.values[index]) && snapshot.values[index] >= 0 && snapshot.values[index] <= 1
                         && (index == static_cast<size_t> (macro)
                             || std::abs (snapshot.values[index] - plainParameter (routed, "macro" + juce::String (static_cast<int> (index) + 1))) < 2.0e-6f),
                     "A macro destination changed an unrelated macro or published invalid telemetry: " + context);
        require (save (routed) == stored && ! routed.canUndo() && ! routed.isUserEditActive(),
                 "Audio-rate macro evaluation wrote base parameters, project state or sound history");
        routed.panic();
        const auto panic = routed.getMacroModulationSnapshot();
        require (! panic.hasActiveVoice && panic.voiceId == 0 && panic.values[static_cast<size_t> (macro)] == .25f,
                 "Pending panic exposed a stale effective macro before the next audio block");
        render (routed, 1);
        const auto processedPanic = routed.getMacroModulationSnapshot();
        require (! processedPanic.hasActiveVoice && processedPanic.voiceId == 0
                     && std::abs (processedPanic.values[static_cast<size_t> (macro)] - .25f) < 2.0e-6f,
                 "Consuming panic republished the old active macro voice");
        routed.setCurrentProgram (0);
        const auto reset = routed.getMacroModulationSnapshot();
        require (! reset.hasActiveVoice && reset.voiceId == 0, "Pending state replacement retained a previous macro voice");
        for (int index = 0; index < 8; ++index)
            require (reset.values[static_cast<size_t> (index)] == plainParameter (routed, "macro" + juce::String (index + 1)),
                     "State replacement did not immediately publish the new stored macro bases");
        render (routed, 1); routed.releaseResources();
        const auto released = routed.getMacroModulationSnapshot();
        require (! released.hasActiveVoice && released.voiceId == 0, "releaseResources retained an active macro voice");
        routed.prepareToPlay (48000, 128);
        const auto prepared = routed.getMacroModulationSnapshot();
        require (! prepared.hasActiveVoice && prepared.voiceId == 0, "prepareToPlay exposed stale macro voice telemetry");
    }
    // A UI reorder moves the complete payload while amount smoothing is in
    // flight. Its multiset is identical, so it must not restart modulation.
    AnyvocAudioProcessor moved, reference;
    for (auto* processor : { &moved, &reference })
    {
        prepareCrossModulationFixture (*processor);
        setParameter (*processor, "macro1", .25f);
        require (processor->assignModulation (1, 28, .25f) && processor->assignModulation (6, 2, .5f), "Macro reorder audio fixture failed");
        processor->prepareToPlay (48000, 128);
        render (*processor, 4096, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
        setParameter (*processor, "mod1Amount", .75f); render (*processor, 37);
    }
    moved.moveModulationRoute (0, 31);
    const auto actual = render (moved, 4096), expected = render (reference, 4096);
    require (crossAudioDifference (actual, expected) == 0, "Moving a complete macro route restarted its smoothing or changed accumulation order");
}

void testMacroDestinationEditorRoutingAndLiveValues()
{
    PresetTestDirectory files;
    AnyvocAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
        processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
    editor->setVisible (true); editor->setSize (1008, 684);
    const auto dropOnMacro = [&] (int macro, int source)
    {
        clickMatrixControl (*editor, macro < 4 ? "MacroBank1" : "MacroBank2");
        auto* slider = findComponent<juce::Slider> (*editor, "macro" + juce::String (macro + 1));
        require (modulationControlVisible (slider), "A macro target knob was unavailable at minimum size");
        auto* target = dynamic_cast<juce::DragAndDropTarget*> (slider->getParentComponent());
        require (target != nullptr, "A macro knob did not expose the actual drag/drop path");
        clickMatrixControl (*editor, "openModulationSources");
        auto* chip = findComponent<juce::Component> (*editor, "paletteModSource" + juce::String (source));
        require (modulationControlVisible (chip), "Macro drop palette source was unavailable");
        const juce::DragAndDropTarget::SourceDetails drop { "anyvoc.mod:" + juce::String (source), chip, { 20, 20 } };
        require (target->isInterestedInDragSource (drop), "A visible macro knob rejected a valid source");
        target->itemDragEnter (drop); target->itemDropped (drop);
        clickMatrixControl (*editor, "SourcePaletteClose");
        return target;
    };
    for (int macro = 0; macro < 8; ++macro)
    {
        processor.setCurrentProgram (0); clearMatrixHistory (processor); refreshEditorTimers();
        const auto before = parameters (processor);
        auto* target = dropOnMacro (macro, 17);
        require (processor.getModulationSource (0) == 17 && processor.getModulationDestination (0) == 28 + macro
                     && processor.getUndoCount() == 1, "Macro drop used a wrong destination, source or history group");
        for (int index = 0; index < 8; ++index)
            require (plainParameter (processor, "macro" + juce::String (index + 1)) == 0, "A macro drop overwrote a saved base knob");
        const auto after = parameters (processor);
        expectHistoryResult (processor.undoUserEdit(), "Undo macro knob drop"); expectParameters (processor, before);
        expectHistoryResult (processor.redoUserEdit(), "Redo macro knob drop"); expectParameters (processor, after);
        refreshEditorTimers();
        auto* ring = findComponent<juce::Component> (*editor, "macro" + juce::String (macro + 1) + "ModulationRing");
        require (modulationControlVisible (ring), "Assigned macro did not expose its depth ring");
        const auto point = modulationRingPoint (*ring);
        if (macro == 0 || macro == 7)
            for (const auto* suffix : { "Source", "LfoPhaseTarget", "MacroTarget", "MacroEnabled" })
            {
                processor.setModulationSource (0, 17); processor.setModulationDestination (0, 28 + macro);
                setParameter (processor, "mod1Enabled", 1); setParameter (processor, "mod1Amount", .25f);
                clearMatrixHistory (processor); refreshEditorTimers();
                ModulationGestureProbe probe (*processor.apvts.getParameter ("mod1Amount"));
                ring->mouseDown (modulationMouseEvent (*ring, point, point));
                ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -10), point));
                require (probe.starts == 1, "Macro ring cancellation fixture did not start a real amount gesture");
                auto* parameter = processor.apvts.getParameter ("mod1" + juce::String (suffix));
                parameter->setValueNotifyingHost (parameter->getValue() > .5f ? 0.0f : 1.0f);
                const auto interrupted = parameters (processor);
                ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -50), point));
                ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -50), point));
                require (parameters (processor) == interrupted && probe.starts == probe.ends && ! processor.isUserEditActive(),
                         std::string ("Macro ring overwrote related host changes to ") + suffix);
            }
        auto* slider = findComponent<juce::Slider> (*editor, "macro" + juce::String (macro + 1));
        clickMatrixControl (*editor, macro < 4 ? "MacroBank2" : "MacroBank1");
        const auto hidden = parameters (processor); const auto history = processor.getHistoryRevision();
        const juce::DragAndDropTarget::SourceDetails late { "anyvoc.mod:17", nullptr, { 20, 20 } };
        require (! target->isInterestedInDragSource (late), "Hidden macro bank still advertised a drop target");
        target->itemDragEnter (late); target->itemDropped (late);
        slider->setValue (.75, juce::sendNotificationSync);
        ring->mouseDown (modulationMouseEvent (*ring, point, point));
        ring->mouseDrag (modulationMouseEvent (*ring, point.translated (0, -40), point));
        ring->mouseUp (modulationMouseEvent (*ring, point.translated (0, -40), point));
        require (parameters (processor) == hidden && processor.getHistoryRevision() == history && ! processor.isUserEditActive(),
                 "A late hidden-bank drop, knob or ring event changed sound/history");
    }
    processor.setCurrentProgram (0); clearMatrixHistory (processor);
    dropOnMacro (0, 24); dropOnMacro (7, 6); dropOnMacro (0, 6);
    require (processor.getModulationSource (0) == 24 && processor.getModulationDestination (0) == 28
                 && processor.getModulationSource (1) == 6 && processor.getModulationDestination (1) == 35
                 && processor.getModulationSource (2) == 6 && processor.getModulationDestination (2) == 28,
             "The actual macro drag path rejected self modulation or mutually routed macros");

    findButton (*editor, "MOD")->onClick();
    for (int page = 0; page < 3; ++page) clickMatrixControl (*editor, "MatrixNextPage");
    clickMatrixControl (*editor, "mod32Edit");
    auto* detail = findComponent<juce::Component> (*editor, "MatrixRouteDetail");
    auto* destination = findComponent<juce::ComboBox> (*detail, "mod32DestinationDetail");
    require (modulationControlVisible (destination) && destination->getNumItems() == 36, "Matrix detail omitted the eight appended macro targets");
    for (int macro = 0; macro < 8; ++macro)
    {
        destination->setSelectedItemIndex (28 + macro, juce::sendNotificationSync);
        require (processor.getModulationDestination (31) == 28 + macro, "Matrix macro target choice did not reach the processor");
        setParameter (processor, "mod32MacroTarget", static_cast<float> (7 - macro));
        require (destination->getSelectedItemIndex() == 35 - macro, "Macro destination UI lagged behind host target automation");
    }
    setParameter (processor, "mod32LfoPhaseTarget", 9); setParameter (processor, "mod32LfoPhaseEnabled", 1);
    setParameter (processor, "mod32MacroEnabled", 0);
    require (destination->getSelectedItemIndex() == 27, "Turning off the macro bank did not immediately reveal its remembered phase target");

    prepareCrossModulationFixture (processor); setParameter (processor, "macro8", .25f);
    processor.setModulationSource (0, 4); processor.setModulationDestination (0, 35); setParameter (processor, "mod1Amount", .25f);
    processor.setModulationSource (1, 24); processor.setModulationDestination (1, 2); setParameter (processor, "mod2Amount", .5f);
    clearMatrixHistory (processor); processor.prepareToPlay (48000, 128);
    render (processor, 24000, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
    const auto stored = save (processor);
    std::array<juce::Label*, 8> displays {};
    for (int bank = 0; bank < 2; ++bank)
    {
        clickMatrixControl (*editor, bank == 0 ? "MacroBank1" : "MacroBank2");
        for (int index = bank * 4; index < bank * 4 + 4; ++index)
        {
            displays[static_cast<size_t> (index)] = findComponent<juce::Label> (*editor, "macro" + juce::String (index + 1) + "LiveValue");
            require (modulationControlVisible (displays[static_cast<size_t> (index)]), "Macro live label was unavailable in its visible bank");
        }
    }
    const auto checkDisplay = [&]
    {
        const auto snapshot = processor.getMacroModulationSnapshot(); refreshEditorTimers();
        for (int macro = 0; macro < 8; ++macro)
        {
            auto* label = displays[static_cast<size_t> (macro)];
            require (label != nullptr && static_cast<double> (label->getProperties()["macroEffectiveValue"]) == static_cast<double> (snapshot.values[static_cast<size_t> (macro)])
                         && static_cast<bool> (label->getProperties()["macroHasActiveVoice"]) == snapshot.hasActiveVoice
                         && static_cast<juce::int64> (label->getProperties()["macroVoiceId"]) == static_cast<juce::int64> (snapshot.voiceId),
                     "Live macro labels did not use one real engine snapshot, including the hidden bank");
        }
    };
    checkDisplay();
    require (std::abs (processor.getMacroModulationSnapshot().values[7] - .5f) < 2.0e-6f, "Live macro fixture never received its expected modulation");
    clickMatrixControl (*editor, "MacroBank1"); checkDisplay(); clickMatrixControl (*editor, "MacroBank2"); checkDisplay();
    require (save (processor) == stored && ! processor.canUndo() && plainParameter (processor, "macro8") == .25f,
             "Live values or bank navigation changed the stored macro base or history");
    processor.panic(); checkDisplay();
    auto* context = findComponent<juce::Label> (*editor, "MacroLiveContext");
    require (context != nullptr && context->getText() == "EV: idle base", "Pending panic left a stale active-voice label");
}

void testMacroDestinationUiHostCallbacks()
{
    PresetTestDirectory files;
    // Real knob drop, bypassed duplicate and visible detail ComboBox paths.
    for (int scenario = 0; scenario < 5; ++scenario) for (int boundary = 0; boundary < 3; ++boundary)
        for (int action = 0; action < 4; ++action)
        {
            AnyvocAudioProcessor processor;
            for (int row = 1; row < 32; ++row)
            {
                setParameter (processor, ("mod" + juce::String (row) + "Source").toRawUTF8(), 1);
                setParameter (processor, ("mod" + juce::String (row) + "Dest").toRawUTF8(), 1);
            }
            if (scenario == 2 || scenario == 4)
            {
                require (processor.assignModulation (17, 35, .3125f), "Macro callback duplicate fixture failed");
                if (scenario == 2) setParameter (processor, "mod32Enabled", 0);
            }
            clearMatrixHistory (processor);
            std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
            for (const auto* field : matrixRouteFields)
                probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter ("mod32" + juce::String (field))));
            auto& trigger = *probes[static_cast<size_t> (scenario == 0 ? 17 : scenario == 1 || scenario == 3 ? 18 : scenario == 2 ? 7 : 1)];
            std::unique_ptr<juce::AudioProcessorEditor> editor (new AnyvocAudioProcessorEditor (
                processor, files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml")));
            editor->setVisible (true); editor->setSize (1008, 684);
            if (scenario >= 3)
            {
                findButton (*editor, "MOD")->onClick();
                for (int page = 0; page < 3; ++page) clickMatrixControl (*editor, "MatrixNextPage");
                clickMatrixControl (*editor, "mod32Edit");
            }
            else { clickMatrixControl (*editor, "MacroBank2"); clickMatrixControl (*editor, "openModulationSources"); }
            const auto replacement = save (processor); std::vector<float> expected;
            const auto callback = [&]
            {
                if (action == 0) processor.setStateInformation (replacement.getData(), static_cast<int> (replacement.getSize()));
                else if (action == 1) setParameter (processor, "mod32LfoPhaseTarget", 9);
                else if (action == 2) editor.reset();
                else if (scenario < 3) clickMatrixControl (*editor, "MacroBank1");
                else findComponent<juce::Component> (*editor, "MatrixRouteDetail")->setVisible (false);
                expected = parameters (processor);
            };
            if (boundary == 0) trigger.onStart = callback; else if (boundary == 1) trigger.onValue = callback; else trigger.onEnd = callback;
            if (scenario < 3)
            {
                auto* slider = findComponent<juce::Slider> (*editor, "macro8");
                require (modulationControlVisible (slider), "Macro callback target was hidden before the operation");
                auto* target = dynamic_cast<juce::DragAndDropTarget*> (slider->getParentComponent());
                auto* chip = findComponent<juce::Component> (*editor, "paletteModSource17");
                require (target != nullptr && modulationControlVisible (chip), "Macro callback drop did not use actual UI controls");
                const juce::DragAndDropTarget::SourceDetails drop { "anyvoc.mod:17", chip, { 20, 20 } };
                require (target->isInterestedInDragSource (drop), "Macro callback target rejected the source");
                target->itemDragEnter (drop); target->itemDropped (drop);
            }
            else
            {
                auto* detail = findComponent<juce::Component> (*editor, "MatrixRouteDetail");
                auto* choice = findComponent<juce::ComboBox> (*detail, "mod32DestinationDetail");
                require (modulationControlVisible (choice), "Macro destination callback used a hidden matrix choice");
                choice->setSelectedItemIndex (scenario == 3 ? 35 : 7, juce::sendNotificationSync);
            }
            const auto context = "macro UI scenario=" + std::to_string (scenario) + " boundary=" + std::to_string (boundary) + " action=" + std::to_string (action);
            require (! expected.empty(), "Requested UI host callback did not fire: " + context);
            require (parameters (processor) == expected && ! processor.isUserEditActive(), "Macro UI wrote after interruption: " + context);
            for (const auto& probe : probes) require (probe->starts == probe->ends, "Macro UI left an unbalanced host gesture: " + context);
            if (action == 3 && scenario >= 3)
            {
                // The panel was hidden inside begin/value/end. Its rejected
                // candidate must also be reconciled when it becomes visible.
                auto* detail = findComponent<juce::ComboBox> (*editor, "mod32DestinationDetail");
                require (detail == nullptr, "The callback fixture did not actually hide the destination control");
                // Keep the ordinary view reopening path; it must not commit
                // the previously rejected candidate to the sound.
                findButton (*editor, "SYNTH")->onClick(); findButton (*editor, "MOD")->onClick();
                clickMatrixControl (*editor, "mod32Edit");
                auto* reopened = findComponent<juce::ComboBox> (*editor, "mod32DestinationDetail");
                require (modulationControlVisible (reopened) && reopened->getSelectedItemIndex() == processor.getModulationDestination (31)
                             && parameters (processor) == expected, "Reopening a cancelled macro target choice retained its stale candidate");
            }
        }
}

struct MemoryLfoCurveServices final : LfoCurveServices
{
    juce::String clipboard;
    int reads = 0, writes = 0;
    bool replyImmediately = false;
    juce::File immediateFile;
    std::function<void()> onRead, onWrite;
    struct Request { bool saving; std::function<void (juce::File)> completion; };
    std::vector<Request> requests;
    juce::String readClipboard() override
    {
        ++reads; const auto result = clipboard; const auto callback = onRead;
        if (callback) callback();
        return result;
    }
    void writeClipboard (const juce::String& text) override
    {
        ++writes; clipboard = text; const auto callback = onWrite;
        if (callback) callback();
    }
    void chooseCurveFile (bool saving, std::function<void (juce::File)> completion) override
    {
        requests.push_back ({ saving, std::move (completion) });
        if (replyImmediately) reply (requests.size() - 1, immediateFile);
    }
    void reply (size_t index, const juce::File& file)
    {
        require (index < requests.size() && static_cast<bool> (requests[index].completion), "Curve chooser reply had no live request");
        auto completion = std::move (requests[index].completion); completion (file);
    }
};

std::vector<juce::String> curveParameterIds (int lfo)
{
    const auto prefix = "lfo" + juce::String (lfo + 1);
    std::vector<juce::String> result;
    for (int point = 0; point < 33; ++point) result.push_back (prefix + "Point" + juce::String (point));
    for (const auto* suffix : { "Interpolation", "Shape", "Kind", "Rate", "Sync", "Division", "Mode", "Phase",
                               "Direction", "FollowSwing", "AudioRateEnabled", "AudioRateHz", "Chaos" })
        result.push_back (prefix + suffix);
    require (result.size() == 46, "Curve snapshot contract no longer contains 46 parameters");
    return result;
}

std::unique_ptr<AnyvocAudioProcessorEditor> createCurveToolsEditor (AnyvocAudioProcessor& processor,
    PresetTestDirectory& files, const std::shared_ptr<MemoryLfoCurveServices>& services, int lfo = 0)
{
    auto editor = std::make_unique<AnyvocAudioProcessorEditor> (processor,
        files.directory.getChildFile ("Library"), files.directory.getChildFile ("Settings.xml"), services);
    editor->setVisible (true); editor->setSize (1008, 684); openLfoMotionLab (*editor, lfo);
    clickMatrixControl (*editor, "lfo" + juce::String (lfo + 1) + "Tools");
    return editor;
}

template <typename T>
T& curveTool (juce::Component& editor, int lfo, const char* suffix)
{
    const auto id = "lfo" + juce::String (lfo + 1) + suffix;
    auto* control = findComponent<T> (editor, id);
    require (modulationControlVisible (control), "Curve control unavailable: " + id.toStdString());
    return *control;
}

void setCurveToolSlider (juce::Component& editor, int lfo, const char* suffix, double value)
    { curveTool<juce::Slider> (editor, lfo, suffix).setValue (value, juce::sendNotificationSync); }

void setCurveToolToggle (juce::Component& editor, int lfo, const char* suffix, bool value)
{
    auto& control = curveTool<juce::Button> (editor, lfo, suffix);
    control.setToggleState (value, juce::dontSendNotification);
    require (static_cast<bool> (control.onClick), "Curve toggle lacked its real click handler"); control.onClick();
}

juce::Point<float> curveCanvasPosition (juce::Component& canvas, float point, float value)
{
    // The documented graph inset is 31px left / 15px right / 18px top/bottom.
    // Events are in this component's design coordinates, even at minimum size.
    return { 31.0f + (canvas.getWidth() - 46.0f) * point / 32.0f,
             18.0f + (canvas.getHeight() - 36.0f) * (1.0f - value) * .5f };
}

void selectCurveRange (juce::Component& editor, int lfo, int first, int last)
{
    curveTool<juce::ComboBox> (editor, lfo, "EditMode").setSelectedId (2, juce::sendNotificationSync);
    auto& canvas = curveTool<juce::Component> (editor, lfo, "Curve");
    const auto start = curveCanvasPosition (canvas, (float) first, 0), end = curveCanvasPosition (canvas, (float) last, 0);
    canvas.mouseDown (modulationMouseEvent (canvas, start, start));
    canvas.mouseDrag (modulationMouseEvent (canvas, end, start)); canvas.mouseUp (modulationMouseEvent (canvas, end, start));
    require ((int) canvas.getProperties()["curveSelectionFirst"] == std::min (first, last)
                 && (int) canvas.getProperties()["curveSelectionLast"] == std::max (first, last),
             "Select gesture did not retain its closed point interval");
}

void installCurveToolsFixture (AnyvocAudioProcessor& processor, int lfo)
{
    processor.setLfoShape (lfo, 5);
    const auto prefix = "lfo" + juce::String (lfo + 1);
    for (int point = 0; point < 33; ++point)
        setParameter (processor, (prefix + "Point" + juce::String (point)).toRawUTF8(), (point - 16) * .05f);
    setParameter (processor, (prefix + "Interpolation").toRawUTF8(), 0);
    setParameter (processor, (prefix + "Phase").toRawUTF8(), .25f);
    clearMatrixHistory (processor);
}

float canonicalCurveParameter (AnyvocAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.apvts.getParameter (id);
    require (parameter != nullptr, "Curve expected-value parameter was missing");
    return parameter->convertTo0to1 (parameter->convertFrom0to1 (parameter->convertTo0to1 (plain)));
}

void expectCurveOnly (AnyvocAudioProcessor& processor, const std::vector<float>& before, int lfo,
                      const std::array<float, 33>& points, int interpolation, const std::string& context)
{
    const auto prefix = "lfo" + juce::String (lfo + 1);
    auto expected = before;
    for (int point = 0; point < 33; ++point)
    {
        auto* parameter = processor.apvts.getParameter (prefix + "Point" + juce::String (point));
        const auto slot = (size_t) parameter->getParameterIndex();
        if (points[(size_t) point] != parameter->convertFrom0to1 (before[slot]))
            expected[slot] = canonicalCurveParameter (processor, parameter->getParameterID(), points[(size_t) point]);
    }
    for (const auto& pair : { std::pair<const char*, float> { "Kind", 2 }, { "Interpolation", (float) interpolation } })
    {
        auto* parameter = processor.apvts.getParameter (prefix + pair.first);
        expected[(size_t) parameter->getParameterIndex()] = canonicalCurveParameter (processor, parameter->getParameterID(), pair.second);
    }
    const auto actual = parameters (processor);
    for (size_t index = 0; index < actual.size(); ++index)
        if (actual[index] != expected[index])
        {
            const auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters()[(int) index]);
            require (false, context + " exact mismatch " + parameter->getParameterID().toStdString()
                     + " actual=" + juce::String (actual[index], 10).toStdString() + " expected=" + juce::String (expected[index], 10).toStdString());
        }
}

std::array<float, 33> curvePoints (AnyvocAudioProcessor& processor, int lfo)
{
    std::array<float, 33> result {};
    for (int point = 0; point < 33; ++point)
        result[(size_t) point] = plainParameter (processor, ("lfo" + juce::String (lfo + 1) + "Point" + juce::String (point)).toRawUTF8());
    return result;
}

juce::String independentCurveXml (float value = .6f, int interpolation = 1)
{
    juce::String text = "<ANYVOC_LFO_CURVE schema='1' interpolation='" + juce::String (interpolation) + "'>";
    for (int point = 0; point < 33; ++point) text += "<POINT value='" + juce::String (value, 8) + "'/>";
    return text + "</ANYVOC_LFO_CURVE>";
}

void expectOneCurveUndo (AnyvocAudioProcessor& processor, const std::vector<float>& before, const std::string& context)
{
    const auto after = parameters (processor);
    require (after != before && processor.canUndo() && ! processor.isUserEditActive(), context + " did not commit one sound edit");
    expectHistoryResult (processor.undoUserEdit(), ("Undo " + context).c_str());
    require (parameters (processor) == before && ! processor.canUndo() && processor.canRedo(), context + " undo was incomplete or required extra steps");
    expectHistoryResult (processor.redoUserEdit(), ("Redo " + context).c_str());
    require (parameters (processor) == after && processor.canUndo() && ! processor.canRedo(), context + " redo did not restore exact normalized values");
}

void testCurveToolsViewStateAndTenLfoIsolation()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor;
    for (int lfo = 0; lfo < 10; ++lfo) installCurveToolsFixture (processor, lfo);
    auto services = std::make_shared<MemoryLfoCurveServices>();
    auto editor = createCurveToolsEditor (processor, files, services);
    const auto before = parameters (processor); const auto state = save (processor);
    int previous = 0;
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        if (lfo != 0)
        {
            findComponent<juce::ComboBox> (*editor, "LFOLabSelector" + juce::String (previous + 1))->setSelectedItemIndex (lfo, juce::sendNotificationSync);
            dispatchEditorActions(); clickMatrixControl (*editor, "lfo" + juce::String (lfo + 1) + "Tools");
        }
        previous = lfo;
        auto& canvas = curveTool<juce::Component> (*editor, lfo, "Curve");
        require ((int) canvas.getProperties()["curveGridX"] == 32 && (int) canvas.getProperties()["curveGridY"] == 4
                     && !(bool) canvas.getProperties()["curveSnapX"] && !(bool) canvas.getProperties()["curveSnapY"]
                     && (int) canvas.getProperties()["curveEditMode"] == 0,
                 "Another LFO leaked its drawing-tool view settings");
        curveTool<juce::ComboBox> (*editor, lfo, "GridX").setSelectedId (lfo % 2 == 0 ? 8 : 16, juce::sendNotificationSync);
        curveTool<juce::ComboBox> (*editor, lfo, "GridY").setSelectedId (2, juce::sendNotificationSync);
        setCurveToolToggle (*editor, lfo, "SnapX", true); setCurveToolToggle (*editor, lfo, "SnapY", true);
        selectCurveRange (*editor, lfo, lfo, 32 - lfo);
        if (lfo == 0)
        {
            clickMatrixControl (*editor, "lfo1Tools"); clickMatrixControl (*editor, "lfo1Tools");
            require ((int) canvas.getProperties()["curveGridX"] == 8 && (bool) canvas.getProperties()["curveSnapX"],
                     "Closing and reopening tools discarded their view settings");
        }
        setCurveToolSlider (*editor, lfo, "OffsetX", 2); setCurveToolSlider (*editor, lfo, "OffsetY", .25);
        setCurveToolSlider (*editor, lfo, "ScaleX", 2); setCurveToolSlider (*editor, lfo, "ScaleY", .5);
        curveTool<juce::ComboBox> (*editor, lfo, "PointIndex").setSelectedId (lfo + 2, juce::sendNotificationSync);
        setCurveToolSlider (*editor, lfo, "PointValue", -.37);
        clickMatrixControl (*editor, "lfo" + juce::String (lfo + 1) + "CopyCurve");
        require (services->writes == lfo + 1 && services->clipboard.contains ("ANYVOC_LFO_CURVE"), "Copy did not use the supplied clipboard service");
        clickMatrixControl (*editor, "lfo" + juce::String (lfo + 1) + "SaveCurve");
        const auto request = services->requests.size() - 1;
        require (services->requests[request].saving, "SAVE opened a load request"); services->reply (request, {});
        require (parameters (processor) == before && save (processor) == state && ! processor.canUndo() && ! processor.canRedo(),
                 "Tool navigation, selection, staging, copy or cancelled save changed the sound");
    }
    for (int lfo = 0; lfo < 10; ++lfo)
    {
        findComponent<juce::ComboBox> (*editor, "LFOLabSelector" + juce::String (previous + 1))->setSelectedItemIndex (lfo, juce::sendNotificationSync);
        dispatchEditorActions(); previous = lfo;
        auto& canvas = curveTool<juce::Component> (*editor, lfo, "Curve");
        require ((int) canvas.getProperties()["curveSelectionFirst"] == lfo && (int) canvas.getProperties()["curveSelectionLast"] == 32 - lfo
                     && (int) canvas.getProperties()["curveGridX"] == (lfo % 2 == 0 ? 8 : 16), "LFO view state was not independently remembered");
        const auto editBefore = parameters (processor); auto expected = curvePoints (processor, lfo);
        setCurveToolSlider (*editor, lfo, "PointValue", -.37); expected[(size_t) lfo + 1] = -.37f;
        clickMatrixControl (*editor, "lfo" + juce::String (lfo + 1) + "SetPoint");
        expectCurveOnly (processor, editBefore, lfo, expected, 0, "LFO " + std::to_string (lfo + 1) + " numeric edit");
        expectOneCurveUndo (processor, editBefore, "independent numeric point");
        clearMatrixHistory (processor);
    }
}

void testCurveToolsDrawSnapping()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor;
    installCurveToolsFixture (processor, 0);
    for (int point = 0; point < 33; ++point) setParameter (processor, ("lfo1Point" + juce::String (point)).toRawUTF8(), 0);
    clearMatrixHistory (processor); const auto clean = save (processor);
    auto services = std::make_shared<MemoryLfoCurveServices>(); auto editor = createCurveToolsEditor (processor, files, services);
    auto& canvas = curveTool<juce::Component> (*editor, 0, "Curve");
    for (int variant = 0; variant < 8; ++variant)
    {
        processor.setStateInformation (clean.getData(), (int) clean.getSize());
        curveTool<juce::ComboBox> (*editor, 0, "GridX").setSelectedId (4, juce::sendNotificationSync);
        curveTool<juce::ComboBox> (*editor, 0, "GridY").setSelectedId (variant == 3 ? 1 : variant == 6 ? 16 : variant == 7 ? 32 : 4, juce::sendNotificationSync);
        setCurveToolToggle (*editor, 0, "SnapX", variant != 0);
        setCurveToolToggle (*editor, 0, "SnapY", variant != 0);
        const auto before = parameters (processor); std::array<float, 33> expected {};
        const auto start = curveCanvasPosition (canvas, variant == 2 ? 4.0f : variant == 5 ? 3.6f : 5.3f,
                                                variant == 3 ? 0.0f : variant == 6 ? .125f : variant == 7 ? .0625f : .26f);
        canvas.mouseDown (modulationMouseEvent (canvas, start, start));
        auto end = start;
        if (variant == 4)
        {
            end = curveCanvasPosition (canvas, 21.0f, -.76f);
            canvas.mouseDrag (modulationMouseEvent (canvas, end, start));
            // X: 5 -> 8, 21 -> 24. Endpoint values .5 and -1; interpolated
            // points snap to .5 increments, half-grid ties toward +1.
            const std::array<float, 17> hand { .5f,.5f,.5f,0,0,0,0,0,0,-.5f,-.5f,-.5f,-.5f,-.5f,-1,-1,-1 };
            std::copy (hand.begin(), hand.end(), expected.begin() + 8);
        }
        // The grid may be finer than the unchanged host parameter's .01 step:
        // Y16's .125 becomes .13; Y32's .0625 becomes .06 in the actual sound.
        else expected[(size_t) (variant == 0 ? 5 : variant == 5 ? 0 : 8)]
            = variant == 0 ? .26f : variant == 3 ? 1.0f : variant == 6 ? .13f : variant == 7 ? .06f : .5f;
        canvas.mouseUp (modulationMouseEvent (canvas, end, start));
        expectCurveOnly (processor, before, 0, expected, 0, "drawing variant " + std::to_string (variant));
        expectOneCurveUndo (processor, before, "snapped or legacy stroke");
    }
    installCurveToolsFixture (processor, 0); processor.setLfoShape (0, 4); clearMatrixHistory (processor);
    setCurveToolToggle (*editor, 0, "SnapX", false); setCurveToolToggle (*editor, 0, "SnapY", false);
    const auto randomBefore = parameters (processor); auto randomExpected = curvePoints (processor, 0); randomExpected[5] = -.37f;
    const auto randomPoint = curveCanvasPosition (canvas, 5, -.37f);
    canvas.mouseDown (modulationMouseEvent (canvas, randomPoint, randomPoint)); canvas.mouseUp (modulationMouseEvent (canvas, randomPoint, randomPoint));
    expectCurveOnly (processor, randomBefore, 0, randomExpected, 0, "Random edits its saved Custom curve");
    expectOneCurveUndo (processor, randomBefore, "drawing over the saved Random curve");
    // Switching to Chaos or hiding the Lab during a real stroke cancels all
    // subsequent drag events, including direct late deliveries to the canvas.
    for (int interrupt = 0; interrupt < 3; ++interrupt)
    {
        processor.setStateInformation (clean.getData(), (int) clean.getSize()); openLfoMotionLab (*editor, 0);
        const auto start = curveCanvasPosition (canvas, 0, .7f), end = curveCanvasPosition (canvas, 32, -.7f);
        canvas.mouseDown (modulationMouseEvent (canvas, start, start));
        if (interrupt == 0) setParameter (processor, "lfo1Chaos", 1);
        else if (interrupt == 1) clickMatrixControl (*editor, "LfoDetailClose1");
        else
        {
            findComponent<juce::ComboBox> (*editor, "LFOLabSelector1")->setSelectedItemIndex (9, juce::sendNotificationSync);
            dispatchEditorActions();
        }
        const auto afterInterrupt = parameters (processor);
        canvas.mouseDrag (modulationMouseEvent (canvas, end, start)); canvas.mouseUp (modulationMouseEvent (canvas, end, start));
        require (parameters (processor) == afterInterrupt && ! processor.isUserEditActive(), "Late canvas input changed a hidden, different or chaotic LFO");
    }
}

void testCurveToolsSelectedTransformsAndAudio()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor;
    installCurveToolsFixture (processor, 9);
    for (int point = 8; point <= 16; ++point) setParameter (processor, ("lfo10Point" + juce::String (point)).toRawUTF8(), (point - 12) * .2f);
    clearMatrixHistory (processor); const auto clean = save (processor);
    auto services = std::make_shared<MemoryLfoCurveServices>(); auto editor = createCurveToolsEditor (processor, files, services, 9);
    selectCurveRange (*editor, 9, 16, 8);
    const std::array<std::array<float, 9>, 6> hand {{
        {{ -.1f,.5f,.9f,-.4f,.2f,.7f,.1f,-.3f,-.8f }},
        {{ .8f,.3f,-.1f,-.7f,-.2f,.4f,-.9f,-.5f,.1f }},
        {{ -.8f,-.8f,-.8f,-.6f,-.4f,-.2f,0,.2f,.4f }},
        {{ -.4f,-.3f,-.2f,-.1f,0,.1f,.2f,.3f,.4f }},
        {{ .25f,.25f,.25f,.25f,.25f,.25f,.25f,.25f,.25f }},
        {{ -.4f,-.4f,-.2f,-.2f,0,0,.2f,.2f,.4f }}
    }};
    for (int operation = 0; operation < 6; ++operation)
    {
        processor.setStateInformation (clean.getData(), (int) clean.getSize());
        if (operation < 2)
        {
            // Asymmetric data distinguishes a horizontal reversal from a
            // vertical inversion, catching swapped button callbacks too.
            const std::array<float, 9> asymmetric { -.8f,-.3f,.1f,.7f,.2f,-.4f,.9f,.5f,-.1f };
            for (int point = 8; point <= 16; ++point)
                setParameter (processor, ("lfo10Point" + juce::String (point)).toRawUTF8(), asymmetric[(size_t) point - 8]);
        }
        if (operation == 5) setParameter (processor, "lfo10Interpolation", 1);
        clearMatrixHistory (processor);
        const auto before = parameters (processor); auto expected = curvePoints (processor, 9);
        std::copy (hand[(size_t) operation].begin(), hand[(size_t) operation].end(), expected.begin() + 8);
        setCurveToolSlider (*editor, 9, "OffsetX", operation == 2 ? 2 : 0);
        setCurveToolSlider (*editor, 9, "OffsetY", operation == 4 ? .25 : 0);
        setCurveToolSlider (*editor, 9, "ScaleX", operation == 3 || operation == 5 ? 2 : 1);
        setCurveToolSlider (*editor, 9, "ScaleY", operation == 4 ? 0 : 1);
        require (parameters (processor) == before && ! processor.canUndo(), "Staging a transform changed the sound");
        clickMatrixControl (*editor, "lfo10" + juce::String (operation == 0 ? "FlipX" : operation == 1 ? "FlipY" : "ApplyTransform"));
        expectCurveOnly (processor, before, 9, expected, operation == 5 ? 1 : 0, "selected transform " + std::to_string (operation));
        for (int point = 0; point < 33; ++point) if (point < 8 || point > 16)
        {
            auto* parameter = processor.apvts.getParameter ("lfo10Point" + juce::String (point));
            require (parameter->getValue() == before[(size_t) parameter->getParameterIndex()], "A transform changed a point outside its selected interval");
        }
        expectOneCurveUndo (processor, before, "selected transform");
    }
    processor.setStateInformation (clean.getData(), (int) clean.getSize());
    setCurveToolSlider (*editor, 9, "OffsetX", 0); setCurveToolSlider (*editor, 9, "OffsetY", 0);
    setCurveToolSlider (*editor, 9, "ScaleX", 1); setCurveToolSlider (*editor, 9, "ScaleY", 1);
    const auto identity = save (processor); clickMatrixControl (*editor, "lfo10ApplyTransform");
    require (save (processor) == identity && ! processor.canUndo(), "Identity transform created a sound or history change");
    // The UI result must actually reach audio, with an independent parameter
    // reference and a nontrivial difference from the unchanged curve.
    AnyvocAudioProcessor actual, reference, baseline;
    for (auto* p : { &actual, &reference, &baseline }) { prepareLfoMotionAudio (*p, 9); installCurveToolsFixture (*p, 9); }
    auto audioEditor = createCurveToolsEditor (actual, files, services, 9);
    clickMatrixControl (*audioEditor, "lfo10FlipY");
    for (int point = 0; point < 33; ++point)
        setParameter (reference, ("lfo10Point" + juce::String (point)).toRawUTF8(), -(point - 16) * .05f);
    for (auto* p : { &actual, &reference, &baseline }) p->prepareToPlay (48000, 512);
    const std::vector<Event> note { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } };
    const auto edited = render (actual, 24000, note), direct = render (reference, 24000, note), original = render (baseline, 24000, note);
    require (crossAudioDifference (edited, direct) == 0 && crossAudioDifference (edited, original) > .001,
             "Curve tool audio did not match the independent inverted curve or remained unchanged");
}

void testCurveToolsClipboardTransactions()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor;
    installCurveToolsFixture (processor, 0); installCurveToolsFixture (processor, 9);
    setParameter (processor, "lfo1Interpolation", 1); clearMatrixHistory (processor);
    auto services = std::make_shared<MemoryLfoCurveServices>(); auto editor = createCurveToolsEditor (processor, files, services);
    selectCurveRange (*editor, 0, 8, 16);
    const auto beforeCopy = save (processor); clickMatrixControl (*editor, "lfo1CopyCurve");
    anyvoc::LfoCurveData copied;
    require (anyvoc::LfoCurveDocument::parse (services->clipboard, copied).wasOk() && copied.points == curvePoints (processor, 0)
                 && copied.interpolation == 1 && save (processor) == beforeCopy && ! processor.canUndo(),
             "COPY omitted unselected points, interpolation, or changed history");
    findComponent<juce::ComboBox> (*editor, "LFOLabSelector1")->setSelectedItemIndex (9, juce::sendNotificationSync);
    dispatchEditorActions(); clickMatrixControl (*editor, "lfo10Tools");
    const auto beforePaste = parameters (processor); clickMatrixControl (*editor, "lfo10PasteCurve");
    expectCurveOnly (processor, beforePaste, 9, copied.points, 1, "cross-LFO paste");
    expectOneCurveUndo (processor, beforePaste, "complete curve paste"); clearMatrixHistory (processor);
    const auto noOp = save (processor); clickMatrixControl (*editor, "lfo10PasteCurve");
    require (save (processor) == noOp && ! processor.canUndo(), "Pasting the same curve created a spurious edit");
    const auto valid = independentCurveXml();
    const auto replaceFirst = [&valid] (const juce::String& needle, const juce::String& replacement)
    {
        const int position = valid.indexOf (needle);
        require (position >= 0, "Malformed-curve fixture could not find its target field");
        return valid.replaceSection (position, needle.length(), replacement);
    };
    const std::vector<juce::String> invalid {
        {}, "not a curve", valid.replace ("schema='1'", "schema='2'"), replaceFirst ("value='", "value='nan"),
        replaceFirst ("<POINT", "<UNKNOWN"), valid + "trailing",
        "<!DOCTYPE ANYVOC_LFO_CURVE>" + valid, replaceFirst ("schema='1'", "schema='1' unknown='1'"),
        valid + juce::String::repeatedString (" ", 16385)
    };
    for (size_t index = 0; index < invalid.size(); ++index)
    {
        services->clipboard = invalid[index]; const auto before = save (processor);
        clickMatrixControl (*editor, "lfo10PasteCurve");
        require (save (processor) == before && ! processor.canUndo() && ! processor.isUserEditActive(), "Invalid clipboard curve changed sound, case " + std::to_string (index));
        require (curveTool<juce::Label> (*editor, 9, "ToolsStatus").getText().contains ("Cannot paste curve"), "Invalid clipboard input had no actionable error");
    }
    // A synchronous platform callback may reenter the host before returning
    // text. An exact related parameter change must invalidate that older read.
    services->clipboard = valid;
    services->onRead = [&] { setParameter (processor, "lfo10Point32", -.99f); };
    const auto unchanged = curvePoints (processor, 9);
    clickMatrixControl (*editor, "lfo10PasteCurve");
    auto expected = unchanged; expected[32] = plainParameter (processor, "lfo10Point32");
    require (curvePoints (processor, 9) == expected && ! processor.canUndo(), "Reentrant clipboard read applied stale data");
    services->onRead = {};
    // Basic shapes copy what is visibly drawn, not the inactive saved points.
    // An identity operation leaves the Basic selector and history alone.
    processor.setLfoShape (9, 2); clearMatrixHistory (processor);
    const auto basicState = save (processor); clickMatrixControl (*editor, "lfo10CopyCurve");
    anyvoc::LfoCurveData visible;
    require (anyvoc::LfoCurveDocument::parse (services->clipboard, visible).wasOk(), "Basic shape COPY did not produce a valid asset");
    for (int point = 0; point < 33; ++point)
        require (visible.points[(size_t) point] == (float) point / 16.0f - 1.0f, "Basic COPY used inactive Custom points instead of the visible saw");
    clickMatrixControl (*editor, "lfo10ApplyTransform");
    require (save (processor) == basicState && ! processor.canUndo(), "An identity transform converted a Basic shape to Custom");
    processor.setLfoShape (9, 4); clearMatrixHistory (processor); clickMatrixControl (*editor, "lfo10CopyCurve");
    require (anyvoc::LfoCurveDocument::parse (services->clipboard, visible).wasOk() && visible.points == curvePoints (processor, 9),
             "Random COPY did not preserve the inactive saved Custom curve");
    // PASTE is an explicit asset replacement. Matching the currently visible
    // Basic samples or the saved Random points still means selecting Custom.
    for (const int shape : { 0, 4 })
    {
        installCurveToolsFixture (processor, 9); processor.setLfoShape (9, shape); clearMatrixHistory (processor);
        const auto hidden = curvePoints (processor, 9); const auto beforeAsset = parameters (processor);
        clickMatrixControl (*editor, "lfo10CopyCurve"); anyvoc::LfoCurveData asset;
        require (anyvoc::LfoCurveDocument::parse (services->clipboard, asset).wasOk(), "Same-shape clipboard asset was invalid");
        require (shape == 4 ? asset.points == hidden : asset.points != hidden, "Same-shape clipboard fixture did not distinguish saved and visible data");
        require (parameters (processor) == beforeAsset && ! processor.canUndo(), "Preparing a clipboard asset altered its source sound");
        clickMatrixControl (*editor, "lfo10PasteCurve");
        expectCurveOnly (processor, beforeAsset, 9, asset.points, asset.interpolation, "explicit matching Basic/Random paste");
        require (processor.getLfoShape (9) == 5, "Pasting a matching asset failed to select Custom");
        expectOneCurveUndo (processor, beforeAsset, "matching Basic/Random asset paste");
        expectHistoryResult (processor.undoUserEdit(), "Restore original Basic/Random shape after paste");
        require (processor.getLfoShape (9) == shape && curvePoints (processor, 9) == hidden && parameters (processor) == beforeAsset,
                 "Paste undo failed to restore original mode and all hidden Custom points");
    }
    // Direct late deliveries must be denied before the UI timer can disable
    // the controls, and again while the tools panel is hidden.
    for (int inactive = 0; inactive < 3; ++inactive)
    {
        processor.setLfoShape (9, 5); openLfoMotionLab (*editor, 9);
        if (! modulationControlVisible (findComponent<juce::Component> (*editor, "lfo10ToolsPanel"))) clickMatrixControl (*editor, "lfo10Tools");
        std::vector<std::function<void()>> lateClicks;
        for (const auto* suffix : { "SelectAll", "FlipX", "FlipY", "ApplyTransform", "SetPoint", "CopyCurve", "PasteCurve", "LoadCurve", "SaveCurve" })
            lateClicks.push_back (curveTool<juce::Button> (*editor, 9, suffix).onClick);
        if (inactive < 2) setParameter (processor, "lfo10Chaos", (float) inactive + 1);
        else clickMatrixControl (*editor, "lfo10Tools");
        clearMatrixHistory (processor); const auto before = parameters (processor);
        const auto reads = services->reads, writes = services->writes; const auto requests = services->requests.size();
        for (const auto& callback : lateClicks) callback();
        require (parameters (processor) == before && ! processor.canUndo() && ! processor.isUserEditActive()
                     && services->reads == reads && services->writes == writes && services->requests.size() == requests,
                 "Late Chaos or hidden tool commands reached sound or platform services");
    }
    editor.reset();
    for (int interrupt = 0; interrupt < 3; ++interrupt)
    {
        installCurveToolsFixture (processor, 9); services = std::make_shared<MemoryLfoCurveServices>();
        services->clipboard = valid; editor = createCurveToolsEditor (processor, files, services, 9);
        const auto replacement = save (processor); std::vector<float> readCallbackSnapshot;
        services->onRead = [&]
        {
            if (interrupt == 0) processor.setStateInformation (replacement.getData(), (int) replacement.getSize());
            else if (interrupt == 1) clickMatrixControl (*editor, "LfoDetailClose10");
            else editor.reset();
            readCallbackSnapshot = parameters (processor);
        };
        clickMatrixControl (*editor, "lfo10PasteCurve");
        require (! readCallbackSnapshot.empty() && parameters (processor) == readCallbackSnapshot && ! processor.canUndo() && ! processor.isUserEditActive(),
                 "Reentrant clipboard provider crossed project, view or editor lifetime");
        editor.reset();
    }
    installCurveToolsFixture (processor, 9); services = std::make_shared<MemoryLfoCurveServices>();
    editor = createCurveToolsEditor (processor, files, services, 9);
    const auto copyLifetime = parameters (processor); services->onWrite = [&] { editor.reset(); };
    clickMatrixControl (*editor, "lfo10CopyCurve");
    require (editor == nullptr && services->writes == 1 && parameters (processor) == copyLifetime && ! processor.canUndo(),
             "Clipboard write callback did not safely tolerate editor destruction");
}

void testCurveToolsHostCallbackBoundaries()
{
    PresetTestDirectory files;
    const std::array<const char*, 6> actions { "FlipX", "FlipY", "ApplyTransform", "SetPoint", "PasteCurve", "LoadCurve" };
    const auto sourceFile = files.directory.getChildFile ("Incoming.anycurve");
    require (sourceFile.replaceWithText (independentCurveXml()), "Could not prepare an independent incoming curve file");
    for (const auto* action : actions) for (int boundary = 0; boundary < 3; ++boundary) for (int interrupt = 0; interrupt < 5; ++interrupt)
    {
        AnyvocAudioProcessor processor; installCurveToolsFixture (processor, 9);
        auto services = std::make_shared<MemoryLfoCurveServices>(); services->clipboard = independentCurveXml();
        auto editor = createCurveToolsEditor (processor, files, services, 9);
        setCurveToolSlider (*editor, 9, "OffsetY", .2); setCurveToolSlider (*editor, 9, "PointValue", .6);
        const auto replacement = save (processor); std::vector<float> expected;
        std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
        for (const auto& id : curveParameterIds (9)) probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter (id)));
        const auto callback = [&]
        {
            if (interrupt == 0) processor.setStateInformation (replacement.getData(), (int) replacement.getSize());
            else if (interrupt == 1)
            {
                auto* phase = processor.apvts.getParameter ("lfo10Phase"); const float old = phase->getValue();
                phase->setValueNotifyingHost (std::nextafter (old, 1.0f));
                require (phase->getValue() != old, "One-ULP curve boundary fixture was swallowed by parameter storage");
            }
            else if (interrupt == 2) setParameter (processor, "lfo10Chaos", 1);
            else if (interrupt == 3) clickMatrixControl (*editor, "LfoDetailClose10");
            else editor.reset();
            expected = parameters (processor);
        };
        if (boundary == 0) probes[0]->onStart = callback;
        else if (boundary == 1) probes[0]->onValue = callback;
        else probes[0]->onEnd = callback;
        clickMatrixControl (*editor, "lfo10" + juce::String (action));
        if (juce::String (action) == "LoadCurve") services->reply (0, sourceFile);
        const auto context = std::string (action) + " boundary=" + std::to_string (boundary) + " interrupt=" + std::to_string (interrupt);
        require (! expected.empty(), "Curve tool did not reach the requested host callback: " + context);
        require (parameters (processor) == expected && ! processor.isUserEditActive(), "Curve tool wrote beyond the host/lifetime boundary: " + context);
        for (const auto& probe : probes) require (probe->starts == probe->ends, "Unbalanced curve tool gesture: " + context);
        if (interrupt == 0) require (! processor.canUndo() && ! processor.canRedo(), "Curve tool kept history after project restore: " + context);
    }
    // All 46 related values participate, including inactive controls. The old
    // point/choice parameters quantize, so those cases use their next legal
    // value; only continuous parameters are asserted to retain a one-ULP edit.
    AnyvocAudioProcessor processor; installCurveToolsFixture (processor, 9);
    const auto initial = save (processor); auto services = std::make_shared<MemoryLfoCurveServices>();
    auto editor = createCurveToolsEditor (processor, files, services, 9);
    for (const auto& id : curveParameterIds (9)) for (int boundary = 0; boundary < 2; ++boundary)
    {
        processor.setStateInformation (initial.getData(), (int) initial.getSize());
        openLfoMotionLab (*editor, 9);
        auto* changed = processor.apvts.getParameter (id); std::vector<float> expected;
        ModulationGestureProbe probe (*processor.apvts.getParameter ("lfo10Point0"));
        const auto callback = [&]
        {
            const float old = changed->getValue();
            if (changed->getNormalisableRange().interval == 0 && dynamic_cast<juce::AudioParameterFloat*> (changed) != nullptr)
            {
                changed->setValueNotifyingHost (std::nextafter (old, old < .5f ? 1.0f : 0.0f));
                if (changed->getValue() == old) // nonlinear round trips can consume one normalized ULP
                    changed->setValueNotifyingHost (changed->convertTo0to1 (changed->convertFrom0to1 (old) + .013f));
            }
            else changed->setValueNotifyingHost (old < .5f ? .9f : .1f);
            require (changed->getValue() != old, "Related parameter fixture did not actually change " + id.toStdString());
            expected = parameters (processor);
        };
        if (boundary == 0) probe.onStart = callback; else probe.onValue = callback;
        clickMatrixControl (*editor, "lfo10FlipY");
        require (! expected.empty() && parameters (processor) == expected && probe.starts == probe.ends && ! processor.isUserEditActive(),
                 "Curve guard omitted related field " + id.toStdString() + " boundary=" + std::to_string (boundary));
    }
    for (const auto* action : { "FlipX", "ApplyTransform", "PasteCurve" }) for (const bool restoring : { false, true })
    {
        processor.setStateInformation (initial.getData(), (int) initial.getSize()); openLfoMotionLab (*editor, 9);
        services->clipboard = independentCurveXml(); setCurveToolSlider (*editor, 9, "OffsetY", .2);
        std::vector<std::unique_ptr<ModulationGestureProbe>> probes;
        for (const auto& id : curveParameterIds (9)) probes.push_back (std::make_unique<ModulationGestureProbe> (*processor.apvts.getParameter (id)));
        std::vector<float> expected;
        auto& canvas = curveTool<juce::Component> (*editor, 9, "Curve");
        const auto start = curveCanvasPosition (canvas, 5, .7f);
        canvas.mouseDown (modulationMouseEvent (canvas, start, start));
        require (processor.isUserEditActive() && probes[5]->starts == 1, "Old-stroke boundary fixture never opened its gesture");
        probes[5]->onEnd = [&]
        {
            if (restoring) processor.setStateInformation (initial.getData(), (int) initial.getSize());
            else setParameter (processor, "lfo10Point32", -.93f);
            expected = parameters (processor);
        };
        clickMatrixControl (*editor, "lfo10" + juce::String (action));
        require (! expected.empty() && parameters (processor) == expected && ! processor.isUserEditActive(),
                 "Starting a tool after drawing overwrote the old stroke's host end callback: " + std::string (action));
        for (const auto& probe : probes) require (probe->starts == probe->ends, "Old-stroke transition retained a parameter gesture");
        if (restoring) require (! processor.canUndo() && ! processor.canRedo(), "Old-stroke transition retained obsolete project history");
    }
}

void testCurveToolsAsyncFileTransactions()
{
    PresetTestDirectory files; AnyvocAudioProcessor processor; installCurveToolsFixture (processor, 9);
    auto services = std::make_shared<MemoryLfoCurveServices>(); auto editor = createCurveToolsEditor (processor, files, services, 9);
    const auto validFile = files.directory.getChildFile ("Incoming.anycurve");
    require (validFile.replaceWithText (independentCurveXml()), "Could not write asset fixture");
    const auto beforeSave = save (processor); const auto target = files.directory.getChildFile ("Saved");
    clickMatrixControl (*editor, "lfo10SaveCurve"); services->reply (0, target);
    anyvoc::LfoCurveData saved;
    require (anyvoc::LfoCurveDocument::loadFile (target.withFileExtension ("anycurve"), saved).wasOk()
                 && saved.points == curvePoints (processor, 9) && saved.interpolation == 0
                 && save (processor) == beforeSave && ! processor.canUndo(), "Saving a real .anycurve asset failed or altered sound/history");
    const auto beforeLoad = parameters (processor); clickMatrixControl (*editor, "lfo10LoadCurve"); services->reply (1, validFile);
    std::array<float, 33> flat; flat.fill (.6f); expectCurveOnly (processor, beforeLoad, 9, flat, 1, "file curve load");
    expectOneCurveUndo (processor, beforeLoad, "file load");
    clearMatrixHistory (processor);
    const auto alternate = files.directory.getChildFile ("Alternate.anycurve");
    require (alternate.replaceWithText (independentCurveXml (-.6f)), "Could not create alternate curve asset");
    // A chooser completion is single-use, even when its first load is a no-op
    // and the original parameter snapshot therefore remains valid.
    clickMatrixControl (*editor, "lfo10LoadCurve"); auto duplicateLoad = services->requests.back().completion;
    const auto repeated = parameters (processor); services->reply (services->requests.size() - 1, validFile); duplicateLoad (alternate);
    require (parameters (processor) == repeated && ! processor.canUndo(), "A repeated chooser callback applied a second curve");
    clickMatrixControl (*editor, "lfo10SaveCurve"); auto duplicateSave = services->requests.back().completion;
    services->reply (services->requests.size() - 1, files.directory.getChildFile ("Once.anycurve"));
    const auto twice = files.directory.getChildFile ("Twice.anycurve"); duplicateSave (twice);
    require (! twice.exists() && parameters (processor) == repeated, "A repeated save callback wrote another file");
    // Services are allowed to complete synchronously, too.
    services->replyImmediately = true; services->immediateFile = alternate;
    const auto synchronousBefore = parameters (processor); clickMatrixControl (*editor, "lfo10LoadCurve");
    std::array<float, 33> negativeFlat; negativeFlat.fill (-.6f);
    expectCurveOnly (processor, synchronousBefore, 9, negativeFlat, 1, "synchronous chooser load");
    expectOneCurveUndo (processor, synchronousBefore, "synchronous chooser load");
    services->replyImmediately = false;
    for (const int shape : { 0, 4 })
    {
        installCurveToolsFixture (processor, 9); processor.setLfoShape (9, shape); clearMatrixHistory (processor);
        const auto hidden = curvePoints (processor, 9); const auto beforeAsset = parameters (processor);
        const auto assetFile = files.directory.getChildFile ("MatchingShape" + juce::String (shape) + ".anycurve");
        clickMatrixControl (*editor, "lfo10SaveCurve"); services->reply (services->requests.size() - 1, assetFile);
        anyvoc::LfoCurveData asset;
        require (anyvoc::LfoCurveDocument::loadFile (assetFile, asset).wasOk()
                     && (shape == 4 ? asset.points == hidden : asset.points != hidden)
                     && parameters (processor) == beforeAsset && ! processor.canUndo(), "Saving the matching shape asset changed its source or used the wrong points");
        clickMatrixControl (*editor, "lfo10LoadCurve"); services->reply (services->requests.size() - 1, assetFile);
        expectCurveOnly (processor, beforeAsset, 9, asset.points, asset.interpolation, "explicit matching Basic/Random load");
        require (processor.getLfoShape (9) == 5, "Loading a matching asset failed to select Custom");
        expectOneCurveUndo (processor, beforeAsset, "matching Basic/Random asset load");
        expectHistoryResult (processor.undoUserEdit(), "Restore original Basic/Random shape after load");
        require (processor.getLfoShape (9) == shape && curvePoints (processor, 9) == hidden && parameters (processor) == beforeAsset,
                 "Load undo failed to restore original mode and all hidden Custom points");
    }
    // The XML codec preserves float data; applying it to the legacy .01 host
    // parameters intentionally quantizes. Subsequent exports describe what is
    // actually stored, not the higher precision incoming asset.
    installCurveToolsFixture (processor, 9);
    juce::String preciseXml = "<ANYVOC_LFO_CURVE schema='1' interpolation='0'>";
    const std::array<const char*, 4> incoming { "0.12345678", "-0.456789", "0.0625", "0.125" };
    const std::array<float, 4> quantized { .12f, -.46f, .06f, .13f };
    std::array<float, 33> expectedQuantized {};
    for (int point = 0; point < 33; ++point)
    {
        preciseXml += "<POINT value='" + juce::String (incoming[(size_t) point % 4]) + "'/>";
        expectedQuantized[(size_t) point] = quantized[(size_t) point % 4];
    }
    preciseXml += "</ANYVOC_LFO_CURVE>";
    const auto preciseFile = files.directory.getChildFile ("Precise.anycurve");
    require (preciseFile.replaceWithText (preciseXml), "Could not prepare the precision boundary asset");
    const auto precisionBefore = parameters (processor);
    clickMatrixControl (*editor, "lfo10LoadCurve"); services->reply (services->requests.size() - 1, preciseFile);
    expectCurveOnly (processor, precisionBefore, 9, expectedQuantized, 0, "legacy .01 precision after asset load");
    const auto actualStored = curvePoints (processor, 9); clickMatrixControl (*editor, "lfo10CopyCurve");
    anyvoc::LfoCurveData exported;
    require (anyvoc::LfoCurveDocument::parse (services->clipboard, exported).wasOk() && exported.points == actualStored,
             "COPY exported incoming float precision instead of the actual stored curve");
    const auto quantizedFile = files.directory.getChildFile ("Quantized.anycurve");
    const auto beforeExport = parameters (processor); const int undoCount = processor.getUndoCount();
    clickMatrixControl (*editor, "lfo10SaveCurve"); services->reply (services->requests.size() - 1, quantizedFile);
    require (anyvoc::LfoCurveDocument::loadFile (quantizedFile, exported).wasOk() && exported.points == actualStored
                 && parameters (processor) == beforeExport && processor.getUndoCount() == undoCount,
             "SAVE did not preserve the actual quantized values and sound history");
    for (int kind = 0; kind < 3; ++kind)
    {
        clearMatrixHistory (processor); const auto before = save (processor);
        const auto bad = files.directory.getChildFile ("Bad" + juce::String (kind) + ".anycurve");
        if (kind != 0) require (bad.replaceWithText (kind == 1 ? "<ANYVOC_LFO_CURVE/>" : juce::String::repeatedString (" ", 16385)), "Could not write malformed asset fixture");
        clickMatrixControl (*editor, "lfo10LoadCurve"); services->reply (services->requests.size() - 1, bad);
        require (save (processor) == before && ! processor.canUndo() && curveTool<juce::Label> (*editor, 9, "ToolsStatus").getText().contains ("Cannot load curve"),
                 "Missing, malformed or oversized asset did not fail atomically");
    }
    editor.reset();
    for (const bool saving : { false, true }) for (int interrupt = 0; interrupt < 8; ++interrupt)
    {
        installCurveToolsFixture (processor, 9); services = std::make_shared<MemoryLfoCurveServices>();
        editor = createCurveToolsEditor (processor, files, services, 9); const auto replacement = save (processor);
        clickMatrixControl (*editor, saving ? "lfo10SaveCurve" : "lfo10LoadCurve");
        if (interrupt == 0) processor.setStateInformation (replacement.getData(), (int) replacement.getSize());
        else if (interrupt == 1) setParameter (processor, "lfo10Point32", -.93f);
        else if (interrupt == 2) setParameter (processor, "lfo10Chaos", 2);
        else if (interrupt == 3) clickMatrixControl (*editor, "lfo10Tools");
        else if (interrupt == 4) clickMatrixControl (*editor, "LfoDetailClose10");
        else if (interrupt == 5)
        {
            findComponent<juce::ComboBox> (*editor, "LFOLabSelector10")->setSelectedItemIndex (0, juce::sendNotificationSync); dispatchEditorActions();
        }
        else if (interrupt == 6) editor.reset();
        else clickMatrixControl (*editor, saving ? "lfo10SaveCurve" : "lfo10LoadCurve"); // superseding chooser
        const auto interrupted = parameters (processor);
        const auto outgoing = files.directory.getChildFile ("Stale" + juce::String (interrupt) + ".anycurve");
        services->reply (0, saving ? outgoing : validFile);
        require (parameters (processor) == interrupted && ! processor.isUserEditActive() && ! outgoing.exists(),
                 "A stale asynchronous curve request wrote sound or disk: save=" + std::to_string (saving) + " interrupt=" + std::to_string (interrupt));
        editor.reset();
    }
}

void installMacroDemoFixture (AnyvocAudioProcessor& processor, int section)
{
    processor.setCurrentProgram (26);
    setParameter (processor, "master", .42f); setParameter (processor, "macro1", .4f);
    for (int oscillator = 1; oscillator <= 3; ++oscillator)
    {
        const auto prefix = "osc" + juce::String (oscillator);
        setParameter (processor, (prefix + "Phase").toRawUTF8(), 0);
        setParameter (processor, (prefix + "PhaseRandom").toRawUTF8(), 0);
        setParameter (processor, (prefix + "RandomUnison").toRawUTF8(), 0);
    }
    processor.setLfoShape (0, 1); setParameter (processor, "lfo1Rate", .4f); setParameter (processor, "lfo1Mode", 1);
    processor.setLfoShape (1, 0); setParameter (processor, "lfo2Rate", 1.2f); setParameter (processor, "lfo2Mode", 1);
    if (section >= 1)
    {
        require (processor.assignModulation (1, 28, .4f), "Could not assign the demonstration LFO-to-macro route");
        setParameter (processor, "mod4Polarity", 1);
    }
    if (section >= 2)
    {
        require (processor.assignModulation (2, 7, .65f), "Could not assign the demonstration pan route");
        processor.setModulationSource (4, section == 3 ? 24 : 6, true);
    }
    if (section == 3)
    {
        setParameter (processor, "macro8", .1f);
        require (processor.assignModulation (6, 35, .5f) && processor.assignModulation (24, 28, .2f),
                 "Could not assign the demonstration macro feedback routes");
    }
}

void createPreviews (const juce::File& directory)
{
    require (directory.createDirectory().wasOk(), "Could not create preview output directory");
    PresetTestDirectory previewFiles;
    const auto previewLibrary = previewFiles.directory.getChildFile ("Library");
    const auto previewSettings = previewFiles.directory.getChildFile ("Preferences.xml");
    require (previewLibrary.createDirectory().wasOk(), "Could not create isolated preview library");
    AnyvocAudioProcessor processor;
    processor.setCurrentProgram (2); // Pad / Northern Lights
    std::unique_ptr<juce::AudioProcessorEditor> editor (
        new AnyvocAudioProcessorEditor (processor, previewLibrary, previewSettings));
    require (editor != nullptr, "Processor did not create an editor");
    editor->setVisible (true);

    const auto capture = [&] (const char* name)
    {
        const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        require (snapshot.isValid() && snapshot.getWidth() == editor->getWidth()
                    && snapshot.getHeight() == editor->getHeight(),
                 "Editor snapshot has invalid dimensions");
        const auto file = directory.getChildFile (name);
        auto stream = file.createOutputStream();
        require (stream != nullptr, "Could not open preview PNG output");
        stream->setPosition (0);
        stream->truncate();
        juce::PNGImageFormat format;
        require (format.writeImageToStream (snapshot, *stream), "Could not encode preview PNG");
        stream->flush();
        std::cout << "PREVIEW " << file.getFullPathName() << '\n';
    };
    const auto selectPage = [&] (const char* name)
    {
        auto* button = findButton (*editor, name);
        require (button != nullptr && static_cast<bool> (button->onClick),
                 std::string ("Editor page button not found: ") + name);
        button->onClick();
    };

    editor->setSize (1120, 760);
    selectPage ("SYNTH");
    capture ("Synth.png");
    selectPage ("MOD");
    capture ("MOD.png");
    selectPage ("FX");
    capture ("FX.png");
    selectPage ("SYNTH");
    editor->setSize (1008, 684);
    capture ("Synth-small.png");
    editor->setSize (1568, 1064);
    capture ("Synth-large.png");

    const auto openProgram = [&] (int program)
    {
        editor.reset();
        processor.setCurrentProgram (program);
        editor.reset (new AnyvocAudioProcessorEditor (processor, previewLibrary, previewSettings));
        require (editor != nullptr, "Processor did not recreate its editor");
        editor->setVisible (true);
        editor->setSize (1120, 760);
        selectPage ("SYNTH");
    };
    openProgram (10); // Sample / Copper Bell
    capture ("Sample.png");
    selectPage ("EDIT"); // First matching EDIT is oscillator A.
    capture ("Sample-Lab.png");
    openProgram (11); // Granular / Cloud Bell
    capture ("Granular.png");
    selectPage ("EDIT");
    capture ("Source-Lab.png");
    openProgram (12); // LFO / Painted Steps
    selectPage ("MOD");
    capture ("Drawable-LFO.png");
    selectPage ("EDIT LFO 1");
    capture ("LFO-Lab.png");
    editor->setSize (1008, 684);
    capture ("LFO-Lab-small.png");
    openProgram (2); // Wavetable / Northern Lights
    selectPage ("EDIT");
    capture ("Wavetable-Lab.png");
    selectPage ("HARMONICS");
    auto* harmonics = findButton (*editor, "HARMONICS");
    auto* waveform = findButton (*editor, "WAVE");
    require (harmonics != nullptr && harmonics->getToggleState()
                 && waveform != nullptr && ! waveform->getToggleState(),
             "Visible Wavetable Lab did not switch to the harmonics view");
    capture ("Wavetable-Harmonics.png");
    selectPage ("WAVE");
    editor->setSize (1008, 684);
    capture ("Wavetable-Lab-small.png");
    editor.reset();
    auto phaseStudy = spectralIntegrationFixture();
    anyvoc::WavetableDocument previewTable;
    require (previewTable.load (phaseStudy), "Could not prepare spectral preview table");
    std::vector<anyvoc::WavetableDocument::HarmonicEdit> previewPartials;
    for (int harmonic = 1; harmonic <= 64; ++harmonic)
        previewPartials.push_back ({ harmonic, .4f / std::sqrt ((float) harmonic),
                                    .42f * std::sin ((float) harmonic * .35f), true, true });
    require (previewTable.setHarmonics (0, previewPartials)
                 && processor.applyEditedWavetable (0, previewTable.data(), "Phase Study"),
             "Could not install spectral preview table");
    setParameter (processor, "osc1Position", 0);
    editor.reset (new AnyvocAudioProcessorEditor (processor, previewLibrary, previewSettings));
    editor->setVisible (true); editor->setSize (1120, 760);
    selectPage ("EDIT"); selectPage ("PHASE");
    capture ("Wavetable-Phase.png");
    editor->setSize (1008, 684);
    capture ("Wavetable-Phase-small.png");
    editor->setSize (1120, 760);
    auto* harmonicPage = findComponent<juce::ComboBox> (*editor, "wavetableHarmonicPage");
    require (harmonicPage != nullptr, "Harmonic page selector missing from preview");
    harmonicPage->setSelectedId (16, juce::sendNotificationSync);
    selectPage ("HARMONICS");
    capture ("Wavetable-Harmonics-high.png");
    editor->setSize (1008, 684);
    selectPage ("PHASE");
    capture ("Wavetable-Phase-high-small.png");
    editor.reset();
    require (processor.applyEditedWavetable (0, oppositePhaseMorphFixture(), "Spectral Morph Study"),
             "Could not install morph preview table");
    setParameter (processor, "osc1Position", .5f);
    editor.reset (new AnyvocAudioProcessorEditor (processor, previewLibrary, previewSettings));
    editor->setVisible (true); editor->setSize (1120, 760);
    selectPage ("EDIT");
    auto* morphMode = findComponent<juce::ComboBox> (*editor, "wavetableMorphMode");
    require (morphMode != nullptr, "Morph method selector missing from preview");
    morphMode->setSelectedId (2, juce::sendNotificationSync);
    selectPage ("MORPH");
    capture ("Wavetable-Spectral-morph.png");
    editor->setSize (1008, 684);
    capture ("Wavetable-Spectral-morph-small.png");
    openProgram (13); // Motion / Dual Current
    selectPage ("FILTER");
    capture ("Filter.png");
    editor->setSize (1008, 684);
    capture ("Filter-small.png");
    editor->setSize (1120, 760);
    setParameter (processor, "filterRouting", 2);
    setParameter (processor, "filterBalance", .68f);
    setParameter (processor, "osc1FilterRoute", 1);
    setParameter (processor, "osc2FilterRoute", 2);
    setParameter (processor, "osc3FilterRoute", 0);
    setParameter (processor, "subFilterRoute", 3);
    setParameter (processor, "noiseFilterRoute", 2);
    selectPage ("FILTER"); // Refresh derived labels/parallel emphasis without a UI timer.
    capture ("Filter-parallel.png");
    openProgram (14); // Motion / Folded Orbit
    capture ("Warp-Synth.png");
    selectPage ("WARP");
    capture ("Warp.png");
    editor->setSize (1008, 684);
    capture ("Warp-small.png");
    openProgram (2);
    selectPage ("BROWSE");
    capture ("Browser.png");
    editor->setSize (1008, 684);
    capture ("Browser-small.png");
    editor->setSize (1120, 760);
    auto* browserSearch = findComponent<juce::TextEditor> (*editor, "presetSearch");
    require (browserSearch != nullptr, "Preset browser search was missing from preview");
    browserSearch->setText ("motion", false); browserSearch->onTextChange();
    capture ("Browser-filtered.png");
    openProgram (15);
    selectPage ("VOICING");
    capture ("Voicing.png");
    editor->setSize (1008, 684);
    capture ("Voicing-small.png");
    openProgram (10);
    selectPage ("VOICING");
    capture ("Voicing-Sample.png");
    openProgram (2);
    processor.beginUserEdit ("Filter cutoff sweep");
    processor.setUserParameter ("cutoff", 4800);
    processor.endUserEdit();
    refreshEditorTimers();
    capture ("History-ready.png");
    editor->setSize (1008, 684);
    capture ("History-ready-small.png");
    expectHistoryResult (processor.undoUserEdit(), "Prepare history redo screenshot");
    refreshEditorTimers();
    editor->setSize (1120, 760);
    capture ("History-redo.png");
    editor->setSize (1008, 684);
    capture ("History-redo-small.png");
    openProgram (6); // Motion / Tide Machine: cutoff and wavetable motion.
    setParameter (processor, "mod2Source", 1); // Show both target rings for the selected LFO.
    selectModulationSource (*editor, 1);
    capture ("Modulation-Rings.png");
    editor->setSize (1008, 684);
    capture ("Modulation-Rings-small.png");
    openProgram (13);
    selectPage ("FILTER"); selectModulationSource (*editor, 6);
    capture ("Modulation-Macro-Filter.png");
    openProgram (14);
    selectPage ("WARP"); selectModulationSource (*editor, 1);
    capture ("Modulation-Warp.png");
    openProgram (16); selectPage ("WARP");
    capture ("Crossmod-PM.png");
    editor->setSize (1008, 684);
    capture ("Crossmod-PM-small.png");
    openProgram (17); selectPage ("WARP");
    capture ("Crossmod-FM.png");
    openProgram (6); selectPage ("MOD");
    setParameter (processor, "mod1AuxSource", 6); setParameter (processor, "mod1Curve", .28f);
    setParameter (processor, "mod1AuxCurve", -.24f); setParameter (processor, "macro1", .7f);
    for (const int route : { 8, 9, 12, 16, 24, 25, 26, 27, 28, 29, 30, 31 }) configureMatrixPayload (processor, route);
    refreshEditorTimers();
    clickMatrixControl (*editor, "MatrixExpand");
    capture ("Matrix-expanded.png");
    editor->setSize (1008, 684); capture ("Matrix-expanded-small.png");
    editor->setSize (1120, 760); clickMatrixControl (*editor, "mod1Edit");
    capture ("Matrix-detail.png");
    editor->setSize (1008, 684); capture ("Matrix-detail-small.png");
    clickMatrixControl (*editor, "MatrixDetailClose"); clickMatrixControl (*editor, "MatrixCompact");
    for (int page = 0; page < 3; ++page) clickMatrixControl (*editor, "MatrixNextPage");
    editor->setSize (1120, 760); capture ("Matrix-last-page.png");
    editor->setSize (1008, 684); capture ("Matrix-last-page-small.png");
    openProgram (18); selectPage ("MOD");
    findComponent<juce::ComboBox> (*editor, "LfoCardSelect1")->setSelectedItemIndex (9, juce::sendNotificationSync);
    findComponent<juce::ComboBox> (*editor, "LfoCardSelect2")->setSelectedItemIndex (2, juce::sendNotificationSync);
    findComponent<juce::ComboBox> (*editor, "EnvelopeSelect")->setSelectedItemIndex (2, juce::sendNotificationSync);
    clickMatrixControl (*editor, "MacroBank2"); refreshEditorTimers();
    capture ("Modulators-expanded.png");
    editor->setSize (1008, 684); capture ("Modulators-expanded-small.png");
    editor->setSize (1120, 760); clickMatrixControl (*editor, "openModulationSources");
    capture ("Sources.png");
    editor->setSize (1008, 684); capture ("Sources-small.png");
    clickMatrixControl (*editor, "SourcePaletteClose");
    editor->setSize (1120, 760); clickMatrixControl (*editor, "lfo10Edit");
    capture ("LFO10-Lab.png");
    editor->setSize (1008, 684); capture ("LFO10-Lab-small.png");
    openProgram (19); selectPage ("MOD");
    findComponent<juce::ComboBox> (*editor, "LfoCardSelect1")->setSelectedItemIndex (8, juce::sendNotificationSync);
    findComponent<juce::ComboBox> (*editor, "LfoCardSelect2")->setSelectedItemIndex (9, juce::sendNotificationSync);
    findComponent<juce::ComboBox> (*editor, "EnvelopeSelect")->setSelectedItemIndex (3, juce::sendNotificationSync);
    clickMatrixControl (*editor, "MacroBank2"); refreshEditorTimers();
    capture ("Four-Horizons.png");
    editor->setSize (1008, 684); capture ("Four-Horizons-small.png");
    openProgram (20); openEnvelopeLab (*editor, 0); refreshEditorTimers();
    capture ("Envelope-Lab-Amp.png");
    editor->setSize (1008, 684); capture ("Envelope-Lab-Amp-small.png");
    editor->setSize (1120, 760);
    findComponent<juce::ComboBox> (*editor, "EnvelopeLabSelect")->setSelectedItemIndex (1, juce::sendNotificationSync);
    dispatchEditorActions(); refreshEditorTimers(); capture ("Envelope-Lab-Sync.png");
    editor->setSize (1008, 684); capture ("Envelope-Lab-Sync-small.png");
    openProgram (21); openEnvelopeLab (*editor, 2); refreshEditorTimers();
    capture ("Envelope-Lab-Legato.png");
    editor->setSize (1008, 684); capture ("Envelope-Lab-Legato-small.png");
    editor->setSize (1120, 760);
    findComponent<juce::ComboBox> (*editor, "EnvelopeLabSelect")->setSelectedItemIndex (3, juce::sendNotificationSync);
    dispatchEditorActions(); capture ("Envelope-Lab-Fourth.png");
    editor->setSize (1008, 684); capture ("Envelope-Lab-Fourth-small.png");
    openProgram (22); openLfoMotionLab (*editor, 0); refreshEditorTimers();
    capture ("LFO-Swing.png");
    editor->setSize (1008, 684); capture ("LFO-Swing-small.png");
    editor->setSize (1120, 760); processor.setLfoRateMode (0, 0);
    setParameter (processor, "lfo1Rate", .7f); setParameter (processor, "lfo1Phase", .375f); setParameter (processor, "lfo1FollowSwing", 0);
    refreshEditorTimers(); capture ("LFO-Motion.png");
    editor->setSize (1008, 684); capture ("LFO-Motion-small.png");
    openProgram (23); openLfoMotionLab (*editor, 0); refreshEditorTimers();
    capture ("LFO-Audio.png");
    editor->setSize (1008, 684); capture ("LFO-Audio-small.png");
    editor->setSize (1120, 760); clickMatrixControl (*editor, "LfoDetailClose1"); clickMatrixControl (*editor, "mod2Edit");
    capture ("LFO-Phase-Routing.png");
    editor->setSize (1008, 684); capture ("LFO-Phase-Routing-small.png");
    const auto evolveChaosPreview = [&] (int blocks, bool noteOn)
    {
        for (int block = 0; block < blocks; ++block)
        {
            render (processor, 2048, noteOn && block == 0 ? std::vector<Event> { { 0, juce::MidiMessage::noteOn (1, 60, .8f) } } : std::vector<Event> {}, 256);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            juce::Timer::callPendingTimersSynchronously();
        }
        refreshEditorTimers();
    };
    openProgram (24); openLfoMotionLab (*editor, 0); processor.prepareToPlay (48000, 512);
    evolveChaosPreview (64, true); capture ("LFO-Chaos-Lorenz.png");
    editor->setSize (1008, 684); capture ("LFO-Chaos-Lorenz-small.png");
    editor->setSize (1120, 760); evolveChaosPreview (128, false); capture ("LFO-Chaos-Evolved.png");
    editor->setSize (1008, 684); capture ("LFO-Chaos-Evolved-small.png");
    openProgram (25); openLfoMotionLab (*editor, 0); processor.prepareToPlay (48000, 512);
    evolveChaosPreview (64, true); capture ("LFO-Chaos-Rossler.png");
    editor->setSize (1008, 684); capture ("LFO-Chaos-Rossler-small.png");

    openProgram (12); openLfoMotionLab (*editor, 0); clickMatrixControl (*editor, "lfo1Tools");
    capture ("LFO-Curve-Tools.png");
    editor->setSize (1008, 684); capture ("LFO-Curve-Tools-small.png");
    editor->setSize (1120, 760);
    curveTool<juce::ComboBox> (*editor, 0, "GridX").setSelectedId (8, juce::sendNotificationSync);
    curveTool<juce::ComboBox> (*editor, 0, "GridY").setSelectedId (8, juce::sendNotificationSync);
    setCurveToolToggle (*editor, 0, "SnapX", true); setCurveToolToggle (*editor, 0, "SnapY", true);
    selectCurveRange (*editor, 0, 8, 24);
    setCurveToolSlider (*editor, 0, "OffsetX", 2); setCurveToolSlider (*editor, 0, "OffsetY", .1);
    setCurveToolSlider (*editor, 0, "ScaleX", 1.5); setCurveToolSlider (*editor, 0, "ScaleY", .7);
    clickMatrixControl (*editor, "lfo1ApplyTransform");
    capture ("LFO-Curve-Selection.png");
    editor->setSize (1008, 684); capture ("LFO-Curve-Selection-small.png");
    openProgram (26); installMacroDemoFixture (processor, 1);
    processor.prepareToPlay (48000, 512);
    render (processor, 48000, { { 0, juce::MidiMessage::noteOn (1, 60, .75f) } });
    refreshEditorTimers();
    require (processor.getMacroModulationSnapshot().hasActiveVoice, "Macro preview did not render a real active voice");
    capture ("Macro-Live.png");
    editor->setSize (1008, 684); capture ("Macro-Live-small.png");
    openProgram (26); installMacroDemoFixture (processor, 3);
    processor.prepareToPlay (48000, 512);
    render (processor, 48000, { { 0, juce::MidiMessage::noteOn (1, 60, .75f) } });
    clickMatrixControl (*editor, "MacroBank2"); refreshEditorTimers();
    capture ("Macro-Bank2-Live.png");
    editor->setSize (1008, 684); capture ("Macro-Bank2-Live-small.png");
    editor->setSize (1120, 760); selectPage ("MOD"); clickMatrixControl (*editor, "mod4Edit"); refreshEditorTimers();
    capture ("Macro-Routing.png");
    editor->setSize (1008, 684); capture ("Macro-Routing-small.png");
}

void createPresetDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create demo output directory");
    constexpr int rate = 48000, sectionFrames = rate * 5;
    juce::AudioBuffer<float> output (2, sectionFrames * 3);
    output.clear();
    AnyvocAudioProcessor processor;
    const std::array<int, 3> programs { 2, 10, 11 };
    for (int section = 0; section < 3; ++section)
    {
        processor.setCurrentProgram (programs[static_cast<size_t> (section)]);
        processor.prepareToPlay (rate, 512);
        std::vector<Event> notes;
        if (section == 1)
        {
            const std::array<int, 6> melody { 60, 67, 72, 64, 67, 76 };
            for (int index = 0; index < static_cast<int> (melody.size()); ++index)
            {
                const int start = index * 30000;
                notes.push_back ({ start, juce::MidiMessage::noteOn (1, melody[static_cast<size_t> (index)], .82f) });
                notes.push_back ({ start + 32000, juce::MidiMessage::noteOff (1, melody[static_cast<size_t> (index)]) });
            }
        }
        else
            for (const int note : section == 0 ? std::array<int, 3> { 60, 64, 67 }
                                               : std::array<int, 3> { 48, 55, 60 })
            {
                notes.push_back ({ 0, juce::MidiMessage::noteOn (1, note, .75f) });
                notes.push_back ({ 168000, juce::MidiMessage::noteOff (1, note) });
            }
        auto audio = render (processor, sectionFrames, notes, 512);
        require (rms (audio) > .001, "Factory preset was silent during demo rendering");
        audio.applyGainRamp (0, 1920, 0.0f, 1.0f);
        audio.applyGainRamp (sectionFrames - 5760, 5760, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, section * sectionFrames, audio, channel, 0, sectionFrames);
        processor.releaseResources();
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open preset demo WAV");
    stream->setPosition (0);
    if (auto* diskStream = dynamic_cast<juce::FileOutputStream*> (stream.get()))
        diskStream->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream,
        juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()),
             "Could not encode preset demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName() << " (0-5s pad, 5-10s sample, 10-15s granular)\n";
}

void createWarpDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create warp demo output directory");
    constexpr int rate = 48000, sectionLength = rate * 3;
    juce::AudioBuffer<float> output (2, sectionLength * 4);
    output.clear();
    for (int section = 0; section < 4; ++section)
    {
        AnyvocAudioProcessor processor;
        setParameter (processor, "osc1Level", .65f);
        setParameter (processor, "osc1Position", 0);
        setParameter (processor, "osc1Unison", 1);
        setParameter (processor, "osc2Level", 0);
        setParameter (processor, "osc3Level", 0);
        setParameter (processor, "subLevel", 0);
        setParameter (processor, "noiseLevel", 0);
        setParameter (processor, "filterEnabled", 0);
        setParameter (processor, "filter2Enabled", 0);
        setParameter (processor, "drive", 0);
        setParameter (processor, "chorusMix", 0);
        setParameter (processor, "delayMix", 0);
        setParameter (processor, "reverbMix", 0);
        setParameter (processor, "master", .6f);
        setParameter (processor, "ampAttack", .01f);
        setParameter (processor, "ampDecay", .1f);
        setParameter (processor, "ampSustain", 1);
        setParameter (processor, "ampRelease", .08f);
        if (section >= 1)
        {
            setParameter (processor, "osc1Warp1Mode", 1);
            setParameter (processor, "osc1Warp1Amount", .8f);
        }
        if (section >= 2)
        {
            setParameter (processor, "osc1Warp2Mode", 6);
            setParameter (processor, "osc1Warp2Amount", .65f);
        }
        if (section == 3)
        {
            setParameter (processor, "osc1Warp1Amount", .35f);
            setParameter (processor, "osc1Warp2Amount", .4f);
            setParameter (processor, "lfo1Rate", .7f);
            setParameter (processor, "lfo1Mode", 1);
            setParameter (processor, "lfo2Rate", .43f);
            setParameter (processor, "lfo2Mode", 1);
            require (processor.assignModulation (1, 12, .6f) && processor.assignModulation (2, 13, .5f),
                     "Could not configure the warp modulation demo");
        }
        processor.prepareToPlay (rate, 512);
        auto audio = render (processor, sectionLength,
            { { 0, juce::MidiMessage::noteOn (1, 60, .85f) },
              { rate * 11 / 4, juce::MidiMessage::noteOff (1, 60) } }, 512);
        require (rms (audio) > .005, "A warp demo section was silent");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f);
        audio.applyGainRamp (sectionLength - 2400, 2400, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open warp demo output");
    stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()),
             "Could not encode warp demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-3s dry, 3-6s slot 1, 6-9s dual slots, 9-12s modulated dual slots)\n";
}

void createCrossModulationDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create cross-modulation demo directory");
    constexpr int rate = 48000, sectionLength = rate * 3;
    juce::AudioBuffer<float> output (2, sectionLength * 5); output.clear();
    const std::array<float, 5> depths { 0, .24f, .3f, .85f, .85f };
    for (int section = 0; section < 5; ++section)
    {
        AnyvocAudioProcessor processor; prepareCrossModulationFixture (processor);
        setParameter (processor, "osc2Semi", 19);
        setParameter (processor, "osc1Warp1CrossMode", static_cast<float> (section));
        setParameter (processor, "osc1Warp1Amount", depths[static_cast<size_t> (section)]);
        setParameter (processor, "ampAttack", .012f);
        processor.prepareToPlay (rate, 512);
        auto audio = render (processor, sectionLength,
            { { 0, juce::MidiMessage::noteOn (1, 60, .85f) },
              { rate * 11 / 4, juce::MidiMessage::noteOff (1, 60) } }, 512);
        require (rms (audio) > .001, "A cross-modulation demo section was silent");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f);
        audio.applyGainRamp (sectionLength - 2400, 2400, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open cross-modulation demo output");
    stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()),
             "Could not encode cross-modulation demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-3s dry, 3-6s FM, 6-9s PM, 9-12s AM, 12-15s RM; oscillator B is inaudible as a direct source)\n";
}

void createMatrixDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create matrix demo directory");
    constexpr int rate = 48000, sectionLength = rate * 3;
    juce::AudioBuffer<float> output (2, sectionLength * 5); output.clear();
    const std::array<float, 5> auxiliary { 0, .25f, .5f, 1, 1 };
    for (int section = 0; section < 5; ++section)
    {
        AnyvocAudioProcessor processor;
        processor.setCurrentProgram (0); audibleSettings (processor);
        for (const auto* id : { "osc2Level", "osc3Level", "subLevel", "noiseLevel", "chorusMix", "delayMix", "reverbMix", "drive" })
            setParameter (processor, id, 0);
        setParameter (processor, "osc1Position", .66f); setParameter (processor, "osc1Level", .6f);
        setParameter (processor, "osc1Unison", 1); setParameter (processor, "osc1PhaseRandom", 0);
        setParameter (processor, "cutoff", 900); setParameter (processor, "resonance", .25f);
        setParameter (processor, "ampAttack", .02f); setParameter (processor, "ampSustain", 1);
        setParameter (processor, "ampRelease", .08f); setParameter (processor, "master", .6f);
        setParameter (processor, "lfo1Rate", 1); setParameter (processor, "lfo1Mode", 1);
        setParameter (processor, "mod1Source", 1); setParameter (processor, "mod1Dest", 1);
        setParameter (processor, "mod1Amount", .7f); setParameter (processor, "mod1AuxSource", 6);
        setParameter (processor, "macro1", auxiliary[static_cast<size_t> (section)]);
        setParameter (processor, "mod1Curve", section == 4 ? .5f : 0);
        processor.prepareToPlay (rate, 512);
        auto audio = render (processor, sectionLength,
            { { 0, juce::MidiMessage::noteOn (1, 48, .85f) },
              { rate * 11 / 4, juce::MidiMessage::noteOff (1, 48) } }, 512);
        require (rms (audio) > .001 && audio.getMagnitude (0, sectionLength) < .99f,
                 "A matrix demonstration section was silent or clipped");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f);
        audio.applyGainRamp (sectionLength - 2400, 2400, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open matrix demo output");
    stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()),
             "Could not encode matrix demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (LFO 1 -> cutoff, AUX Macro 1: 0-3s 0%, 3-6s 25%, 6-9s 50%, 9-12s 100%; 12-15s 100% with source curve +50%)\n";
}

void createSourcesDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create expanded sources demo directory");
    constexpr int rate = 48000, sectionLength = rate * 4;
    juce::AudioBuffer<float> output (2, sectionLength * 4); output.clear();
    for (int section = 0; section < 4; ++section)
    {
        AnyvocAudioProcessor processor;
        processor.setCurrentProgram (section < 2 ? 18 : 19);
        setParameter (processor, section < 2 ? "macro5" : "macro8", section % 2 == 0 ? 0.0f : 1.0f);
        processor.prepareToPlay (rate, 512);
        std::vector<Event> notes;
        for (const int note : { 48, 55, 60 })
        {
            notes.push_back ({ 0, juce::MidiMessage::noteOn (1, note, .75f) });
            notes.push_back ({ rate * 3, juce::MidiMessage::noteOff (1, note) });
        }
        auto audio = render (processor, sectionLength, notes, 512);
        require (rms (audio) > .001 && audio.getMagnitude (0, sectionLength) < .99f,
                 "An expanded sources demonstration section was silent or clipped");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f);
        audio.applyGainRamp (sectionLength - 4800, 4800, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open expanded sources demo output");
    stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()),
             "Could not encode expanded sources demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-4s Prism Weave Macro 5 0%, 4-8s Macro 5 100%; 8-12s Four Horizons Macro 8 0%, 12-16s Macro 8 100%; fixed gain)\n";
}

void createEnvelopeDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create envelope demo directory");
    constexpr int rate = 48000, sectionLength = rate * 4;
    juce::AudioBuffer<float> output (2, sectionLength * 5); output.clear();
    for (int section = 0; section < 5; ++section)
    {
        AnyvocAudioProcessor processor;
        processor.setCurrentProgram (section < 3 ? 20 : 21);
        setParameter (processor, "tempo", section == 2 ? 150.0f : 100.0f);
        if (section == 0)
            for (const auto* prefix : envelopePrefixes) for (const auto* curve : { "AttackCurve", "DecayCurve", "ReleaseCurve" })
                setEnvelopeValue (processor, prefix, curve, 0);
        if (section == 3)
            for (const auto* prefix : envelopePrefixes) setEnvelopeValue (processor, prefix, "RetriggerLegato", 0);
        processor.prepareToPlay (rate, 512);
        std::vector<Event> notes;
        if (section < 3)
            for (int beat = 0; beat < 4; ++beat)
                for (const int note : { 48, 55, 60 })
                {
                    notes.push_back ({ beat * 36000, juce::MidiMessage::noteOn (1, note, .72f) });
                    notes.push_back ({ beat * 36000 + 30000, juce::MidiMessage::noteOff (1, note) });
                }
        else
        {
            const std::array<int, 7> melody { 60, 67, 64, 72, 69, 67, 60 };
            for (size_t note = 0; note < melody.size(); ++note)
            {
                const int start = static_cast<int> (note) * 22000;
                notes.push_back ({ start, juce::MidiMessage::noteOn (1, melody[note], .8f) });
                notes.push_back ({ start + 27000, juce::MidiMessage::noteOff (1, melody[note]) });
            }
        }
        auto audio = render (processor, sectionLength, notes, 512);
        require (rms (audio) > .001 && audio.getMagnitude (0, sectionLength) < .99f, "An envelope demo section was silent or clipped");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f); audio.applyGainRamp (sectionLength - 4800, 4800, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel) output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open envelope demo output"); stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()), "Could not encode envelope demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-4s Clockwork Bloom linear/100 BPM, 4-8s curved/100 BPM, 8-12s curved/150 BPM; "
                 "12-16s Second Breath legato retrigger off, 16-20s on; fixed gain)\n";
}

void createLfoMotionDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create LFO motion demo directory");
    constexpr int rate = 48000, sectionLength = rate * 4;
    juce::AudioBuffer<float> output (2, sectionLength * 5); output.clear();
    for (int section = 0; section < 5; ++section)
    {
        AnyvocAudioProcessor processor; processor.setCurrentProgram (section < 3 ? 22 : 23);
        setParameter (processor, "tempo", 120);
        if (section < 3)
        {
            setParameter (processor, "swingAmount", section == 0 ? 0.0f : .6f);
            if (section == 2) setParameter (processor, "lfo1Direction", 1);
        }
        else setParameter (processor, "lfo1AudioRateHz", section == 3 ? 110.0f : 220.0f);
        processor.prepareToPlay (rate, 512); std::vector<Event> notes;
        if (section < 3)
            for (const int note : { 48, 55, 60 })
            {
                notes.push_back ({ 0, juce::MidiMessage::noteOn (1, note, .72f) });
                notes.push_back ({ rate * 3, juce::MidiMessage::noteOff (1, note) });
            }
        else
        {
            const std::array<int, 4> melody { 60, 67, 64, 72 };
            for (size_t note = 0; note < melody.size(); ++note)
            {
                const int start = static_cast<int> (note) * 36000;
                notes.push_back ({ start, juce::MidiMessage::noteOn (1, melody[note], .8f) });
                notes.push_back ({ start + 28800, juce::MidiMessage::noteOff (1, melody[note]) });
            }
        }
        auto audio = render (processor, sectionLength, notes, 512);
        require (rms (audio) > .001 && audio.getMagnitude (0, sectionLength) < .99f, "An LFO motion demo section was silent or clipped");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f); audio.applyGainRamp (sectionLength - 4800, 4800, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel) output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open LFO motion demo output"); stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()), "Could not encode LFO motion demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-4s Swing Carousel straight, 4-8s swing 60%, 8-12s swing 60%/reverse, all 120 BPM; "
                 "12-16s Phase Lantern audio LFO 110 Hz, 16-20s 220 Hz; fixed gain)\n";
}

void createChaosDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create Chaos demo directory");
    constexpr int rate = 48000, sectionLength = rate * 10;
    juce::AudioBuffer<float> output (2, sectionLength * 2); output.clear();
    for (int section = 0; section < 2; ++section)
    {
        AnyvocAudioProcessor processor;
        // One identical voice and gain: only the chaos algorithm changes between sections.
        prepareLfoMotionAudio (processor, 0); processor.setLfoShape (0, section + 6);
        setParameter (processor, "lfo1Rate", 2); setParameter (processor, "ampRelease", .3f);
        processor.prepareToPlay (rate, 512);
        auto audio = render (processor, sectionLength,
            { { 0, juce::MidiMessage::noteOn (1, 60, .8f) }, { rate * 9, juce::MidiMessage::noteOff (1, 60) } }, 512);
        require (rms (audio) > .001 && audio.getMagnitude (0, sectionLength) < .99f, "Chaos demo section was silent or clipped");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f); audio.applyGainRamp (sectionLength - 4800, 4800, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel) output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open Chaos demo output"); stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()), "Could not encode Chaos demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName() << " (0-10s Lorenz, 10-20s Rossler; same C4 voice, 2 Hz nominal rate, fixed gain)\n";
}

void createMacroDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create macro demo directory");
    constexpr int rate = 48000, sectionLength = rate * 5;
    juce::AudioBuffer<float> output (2, sectionLength * 4); output.clear();
    for (int section = 0; section < 4; ++section)
    {
        AnyvocAudioProcessor processor; installMacroDemoFixture (processor, section);
        processor.prepareToPlay (rate, 512);
        auto audio = render (processor, sectionLength,
            { { 0, juce::MidiMessage::noteOn (1, 60, .75f) }, { rate * 4, juce::MidiMessage::noteOff (1, 60) } }, 512);
        require (rms (audio) > .001 && audio.getMagnitude (0, sectionLength) < .99f, "Macro demo section was silent or clipped");
        for (int channel = 0; channel < 2; ++channel) for (int sample = 0; sample < sectionLength; ++sample)
            require (std::isfinite (audio.getSample (channel, sample)), "Macro demo contained a non-finite sample");
        audio.applyGainRamp (0, 960, 0.0f, 1.0f); audio.applyGainRamp (sectionLength - 4800, 4800, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel) output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open macro demo output"); stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()), "Could not encode macro demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-5s fixed Macro 1 .4; 5-10s add LFO 1 to Macro 1; 10-15s add LFO 2 pan with Macro 1 AUX;"
                 " 15-20s Macro 1/8 feedback and Macro 8 AUX; same C4/base voice and fixed gain, later sections change multiple routes)\n";
}

void createVoicingDemo (const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Could not create voicing demo directory");
    constexpr int rate = 48000, sectionLength = rate * 3;
    juce::AudioBuffer<float> output (2, sectionLength * 4); output.clear();
    const std::array<int, 8> melody { 60, 67, 72, 64, 69, 67, 72, 60 };
    for (int section = 0; section < 4; ++section)
    {
        AnyvocAudioProcessor processor;
        processor.setCurrentProgram (0);
        for (const char* id : { "osc2Level", "osc3Level", "subLevel", "chorusMix", "delayMix", "reverbMix" })
            setParameter (processor, id, 0);
        setParameter (processor, "osc1Position", .56f);
        setParameter (processor, "osc1Level", .52f);
        setParameter (processor, "cutoff", 4500);
        setParameter (processor, "resonance", .1f);
        setParameter (processor, "ampAttack", .035f);
        setParameter (processor, "ampDecay", .16f);
        setParameter (processor, "ampSustain", .45f);
        setParameter (processor, "ampRelease", .1f);
        setParameter (processor, "master", .55f);
        setParameter (processor, "voiceMode", static_cast<float> (std::min (section, 2)));
        setParameter (processor, "glideTime", section == 3 ? .18f : 0);
        processor.prepareToPlay (rate, 512);
        std::vector<Event> notes;
        for (int index = 0; index < static_cast<int> (melody.size()); ++index)
        {
            const int onset = index * 12000;
            notes.push_back ({ onset, juce::MidiMessage::noteOn (1, melody[static_cast<size_t> (index)], .8f) });
            notes.push_back ({ onset + 16000, juce::MidiMessage::noteOff (1, melody[static_cast<size_t> (index)]) });
        }
        auto audio = render (processor, sectionLength, notes, 127);
        require (rms (audio) > .005, "Voicing demonstration section was silent");
        audio.applyGainRamp (0, 480, 0.0f, 1.0f);
        audio.applyGainRamp (sectionLength - 2400, 2400, 1.0f, 0.0f);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, section * sectionLength, audio, channel, 0, sectionLength);
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "Could not open voicing demo output");
    stream->setPosition (0);
    if (auto* disk = dynamic_cast<juce::FileOutputStream*> (stream.get())) disk->truncate();
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
        .withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    require (writer != nullptr && writer->writeFromAudioSampleBuffer (output, 0, output.getNumSamples()),
             "Could not encode voicing demo WAV");
    writer.reset();
    std::cout << "DEMO " << file.getFullPathName()
              << " (0-3s Poly, 3-6s Mono, 6-9s Legato, 9-12s Legato with 180ms glide)\n";
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    if (argc > 1)
    {
        if (argc != 3 || (std::string (argv[1]) != "--preview" && std::string (argv[1]) != "--demo"
                          && std::string (argv[1]) != "--warp-demo" && std::string (argv[1]) != "--voicing-demo"
                          && std::string (argv[1]) != "--crossmod-demo" && std::string (argv[1]) != "--matrix-demo"
                          && std::string (argv[1]) != "--sources-demo" && std::string (argv[1]) != "--envelopes-demo"
                          && std::string (argv[1]) != "--lfo-motion-demo" && std::string (argv[1]) != "--chaos-demo"
                          && std::string (argv[1]) != "--macro-demo"))
        {
            std::cerr << "Usage: AnyvocProcessorTests [--preview <directory> | --demo <wav-file> | --warp-demo <wav-file> | --voicing-demo <wav-file> | --crossmod-demo <wav-file> | --matrix-demo <wav-file> | --sources-demo <wav-file> | --envelopes-demo <wav-file> | --lfo-motion-demo <wav-file> | --chaos-demo <wav-file> | --macro-demo <wav-file>]\n";
            return 2;
        }
        try
        {
            const juce::File output (juce::String::fromUTF8 (argv[2]));
            if (std::string (argv[1]) == "--preview") createPreviews (output);
            else if (std::string (argv[1]) == "--warp-demo") createWarpDemo (output);
            else if (std::string (argv[1]) == "--voicing-demo") createVoicingDemo (output);
            else if (std::string (argv[1]) == "--crossmod-demo") createCrossModulationDemo (output);
            else if (std::string (argv[1]) == "--matrix-demo") createMatrixDemo (output);
            else if (std::string (argv[1]) == "--sources-demo") createSourcesDemo (output);
            else if (std::string (argv[1]) == "--envelopes-demo") createEnvelopeDemo (output);
            else if (std::string (argv[1]) == "--lfo-motion-demo") createLfoMotionDemo (output);
            else if (std::string (argv[1]) == "--chaos-demo") createChaosDemo (output);
            else if (std::string (argv[1]) == "--macro-demo") createMacroDemo (output);
            else createPresetDemo (output);
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL Preview artifact: " << error.what() << '\n';
            return 1;
        }
    }
    const std::vector<std::pair<const char*, std::function<void()>>> tests {
        { "DAW state roundtrip", testStateRoundtrip },
        { "Preset file identity, rename and self-contained DAW restore", testPresetIdentityAndFileRoundtrip },
        { "Legacy preset identity and malformed metadata rollback", testPresetIdentityLegacyAndValidation },
        { "Failed preset save/load preserves sound, identity and existing files", testPresetIoFailureRollback },
        { "Browser selection/favourites preserve audio and explicit actions load", testPresetBrowserSilentSelectionAndExplicitLoad },
        { "Deterministic factory presets and defaults", testDeterministicFactoryPresets },
        { "First host MIDI after preset and project restore", testFirstMidiAfterStateAndPreset },
        { "Self-contained wavetable state and preset files", testEmbeddedWavetable },
        { "Edited wavetable amplitude, state and floating-point export", testEditedWavetableStateAndExport },
        { "Invalid wavetable edits and atomic rollback", testEditedWavetableRejection },
        { "Legacy wavetable normalization across state versions", testLegacyWavetableNormalization },
        { "Open wavetable editor protects newer DAW state", testOpenWavetableEditorStateReplacement },
        { "Embedded stereo sample state and preset files", testEmbeddedSampleState },
        { "Version 1 migration and required parameter validation", testLegacyStateMigration },
        { "Drawable LFO points and selection state roundtrip", testDrawableLfoStateRoundtrip },
        { "Version 2 migration and legacy shape automation", testVersionTwoMigrationAndShapeAutomation },
        { "Dual-filter state, preset defaults and host audio automation", testDualFilterStateAndAudio },
        { "Version 4 migration and legacy destination automation", testVersionFourMigrationAndDestinationAutomation },
        { "Extended filter modulation assignment and bank persistence", testAdditionalModulationDestinations },
        { "Dual-warp state, preset defaults and waveform preview", testWarpStateAndPreview },
        { "Version 5 migration and legacy destination choice counts", testVersionFiveWarpMigration },
        { "Warp modulation assignment, persistence and audible routing", testWarpModulationAssignmentsAndAudio },
        { "Voicing state, appended host parameters and preset defaults", testVoicingStateAndPresetDefaults },
        { "Version 6 voicing migration and required schema 7 controls", testVersionSixVoicingMigration },
        { "Voicing host parameters reach note priority and glide audio", testVoicingParametersReachHostAudio },
        { "Voicing editor attachment, phase units and mode availability", testVoicingEditorControls },
        { "Sound history drag grouping, no-ops and redo branching", testHistoryGroupingAndBranches },
        { "Sound history compound LFO strokes and modulation routes", testHistoryCompoundEdits },
        { "Sound history host isolation, conflicts and project boundaries", testHistoryHostIsolationAndBoundaries },
        { "Sound history reentrant host callbacks and restore boundaries", testHistoryReentrantHostCallbacks },
        { "Sound history excludes partially published worker-thread projects", testHistoryWorkerPublicationBoundary },
        { "Sound history retains assets and presets after source deletion", testHistoryAssetsAndDeletedSources },
        { "Sound history Save As identity and bounded retained memory", testHistorySaveIdentityAndBudgets },
        { "Sound history preserves active wavetable, sample and granular audio", testHistoryAudioContinuity },
        { "Sound history GUI provenance, gestures, shortcuts and editor teardown", testHistoryUiProvenanceAndGestures },
        { "Sound history invalidates stale wavetable Lab snapshots", testHistoryWavetableRevisionAndStaleLab },
        { "Harmonic phase assets survive projects, files and history", testSpectralAssetsThroughProcessorAndFiles },
        { "Spectral editor page navigation, phase strokes and history", testSpectralEditorPagesAndPhaseHistory },
        { "Waveform and spectral morph UI publish one complete sound edit", testSpectralMorphUiIntegration },
        { "Nominal modulation ranges follow source polarity and cutoff octaves", testModulationNominalRanges },
        { "Modulation source selection is silent and ring edits group history", testModulationRingSelectionAndHistory },
        { "Modulation rings stop across synchronous host project replacement", testModulationRingHostReplacement },
        { "Modulation rings protect automation and close cancelled gestures", testModulationRingAutomationAndCancellation },
        { "Cross-modulation state, presets and appended parameter compatibility", testCrossModulationStateAndParameterCompatibility },
        { "Schema 1-7 cross-mode migration and schema 8 atomic rejection", testVersionSevenCrossModulationMigration },
        { "Unified warp mode history and reentrant project boundaries", testUnifiedWarpModeHistoryAndHostBoundaries },
        { "Cross-modulation host parameters reach muted-source carrier audio", testCrossModulationProcessorAudio },
        { "Advanced matrix appends 360 controls after the complete legacy host prefix", testAdvancedMatrixParameterCompatibility },
        { "Schema 1-13 matrix migration and complete schema 14 preset validation", testAdvancedMatrixStateAndMigration },
        { "Thirty-two matrix rows preserve bypassed routes and reset reused slots", testAdvancedMatrixCapacityAndReuse },
        { "Complete matrix payloads move and clear as single undoable edits", testAdvancedMatrixResetAndReorderHistory },
        { "Matrix compound helpers stop at host restore and automation boundaries", testAdvancedMatrixHostBoundaries },
        { "Last-row polarity, curves, auxiliary control and bypass reach real audio", testAdvancedMatrixProcessorAudio },
        { "All matrix pages and detail controls reflect host changes and undo", testAdvancedMatrixEditorNavigationAndControls },
        { "Matrix handles and rings preserve host changes across cancelled gestures", testAdvancedMatrixEditorMoveAndCancellation },
        { "Matrix UI choices, bypass and sliders survive host callbacks and editor destruction", testAdvancedMatrixUiHostCallbacks },
        { "Expanded sources append 396 controls without changing the complete v0.13 prefix", testExpandedSourceParameterCompatibility },
        { "Expanded source state roundtrips, schema 9 migration and current-schema atomic validation", testExpandedSourceStateValidation },
        { "Every source bank preserves legacy choices, compound history and effective assignment", testExpandedSourceSelectionAndAssignment },
        { "Paired source/LFO selectors preserve synchronous host edits and project replacements", testExpandedSourceHostBoundaries },
        { "All fifteen new primary and auxiliary sources match independent legacy audio references", testExpandedSourcesReachAudio },
        { "All twenty-eight factory programs sound and new presets use expanded sources", testExpandedFactoryPresets },
        { "Ten LFOs, four envelopes, eight macros and source palette are editable at minimum size", testExpandedSourceEditorNavigation },
        { "Composite matrix source controls survive host callbacks and editor destruction", testExpandedSourceUiHostCallbacks },
        { "LFO 10 drawing and curve presets preserve host state and survive editor destruction", testExpandedLfoCurveHostCallbacks },
        { "LFO 10 composite shape UI survives host callbacks and editor destruction", testExpandedLfoShapeUiHostCallbacks },
        { "Actual LFO 10 knob drops and duplicate re-enable survive host callbacks and editor teardown", testExpandedSourceDropHostCallbacks },
        { "Advanced envelopes append 48 controls after the complete v0.14 host prefix", testAdvancedEnvelopeParameterCompatibility },
        { "Advanced envelope schema 14 roundtrips, schema 10 migration and atomic validation", testAdvancedEnvelopeStateAndMigration },
        { "Delay, hold and three curves reach all four independent envelope source outputs", testAdvancedEnvelopesReachAudio },
        { "Four envelopes synchronize all timed stages to manual/host BPM with invalid-clock fallback", testAdvancedEnvelopeTempoAudio },
        { "Each envelope independently retriggers only its routed legato sound", testAdvancedEnvelopeLegatoIsolation },
        { "Envelope Lab navigation, stage/curve gestures and musical divisions preserve independent history", testAdvancedEnvelopeEditorNavigationAndGestures },
        { "Envelope graph gestures stop across host restore, related automation and editor teardown", testAdvancedEnvelopeGraphHostCallbacks },
        { "Envelope curve, division and flag controls survive host callbacks and editor teardown", testAdvancedEnvelopeControlHostCallbacks },
        { "LFO motion appends 116 host parameters after the unchanged v0.15 prefix", testLfoMotionParameterCompatibility },
        { "LFO motion schema 14 roundtrips, schema 11 migration and strict atomic validation", testLfoMotionStateAndMigration },
        { "LFO rate modes and all phase destinations preserve complete history and remembered selectors", testLfoRateModesAndPhaseDestinationHistory },
        { "LFO rate and phase destination helpers stop at host and lifetime boundaries", testLfoMotionHelperHostBoundaries },
        { "All ten LFO motion controls reach audio and preserve legacy/tempo rate precedence", testLfoMotionControlsReachAudio },
        { "All ten LFO phase destinations match an independent steady phase offset", testLfoPhaseDestinationsReachAudio },
        { "LFO motion Lab controls and phase matrix targets work at minimum size", testLfoMotionEditorControls },
        { "LFO motion controls and composite rate/destination selectors survive host callbacks", testLfoMotionUiHostCallbacks },
        { "Real LFO phase drops and duplicate re-enable survive host callbacks and editor teardown", testLfoPhaseDropHostCallbacks },
        { "Chaos appends ten parameters, preserves legacy mappings and validates schema 14 atomically", testChaosParameterCompatibilityAndState },
        { "All Chaos shape selectors preserve compound history and synchronous host boundaries", testChaosShapeHistoryAndHostBoundaries },
        { "All twenty Chaos source paths reach evolving audio, reproduce reset and preserve block partition", testChaosAllSourcesReachEvolvingAudio },
        { "Chaos rate and swing bindings work while phase routing and remembered legacy controls stay isolated", testChaosRateBindingsAndInactivePhase },
        { "All Chaos Labs gate inactive edits and display only actual evolving or held DSP trajectories", testChaosEditorGatingAndRealTrajectory },
        { "Chaos unified shape UI survives host changes, project restore, destruction and hidden pages", testChaosShapeUiHostCallbacks },
        { "Curve tools keep ten independent view states and numeric edits preserve other LFOs", testCurveToolsViewStateAndTenLfoIsolation },
        { "Curve drawing preserves legacy input, snaps both axes and cancels late hidden or Chaos strokes", testCurveToolsDrawSnapping },
        { "Selected curve transforms and numeric history match independent values and actual audio", testCurveToolsSelectedTransformsAndAudio },
        { "Curve clipboard buttons transfer complete curves and reject invalid or stale reads atomically", testCurveToolsClipboardTransactions },
        { "Curve tools preserve exact host callback boundaries and all 46 related parameter guards", testCurveToolsHostCallbackBoundaries },
        { "Curve asset buttons use real files while deferred chooser replies respect sound and view lifetime", testCurveToolsAsyncFileTransactions },
        { "Macro destinations append 64 controls and migrate schema 1-13 with strict schema 14 validation", testMacroDestinationCompatibilityAndState },
        { "All macro target banks preserve remembered selectors, full capacity and single-action history", testMacroDestinationBanksCapacityAndHistory },
        { "Macro destination helpers preserve all nine selectors across host and lifetime callbacks", testMacroDestinationHelperHostBoundaries },
        { "All eight macro targets match independent audio values and publish actual voice snapshots", testMacroDestinationsReachAudioAndSnapshots },
        { "Actual macro drops, hidden banks, matrix targets and live labels preserve stored bases", testMacroDestinationEditorRoutingAndLiveValues },
        { "Macro drops and matrix choices survive host callbacks, hidden pages and editor destruction", testMacroDestinationUiHostCallbacks },
        { "Unified warp editor modes, sources and host automation", testUnifiedWarpEditorControls },
        { "Host BPM, PPQ and manual tempo fallback", testProcessorHostAndManualTempo },
        { "Invalid sample imports and embedded sample rollback", testSampleImportRejection },
        { "Modulation assignment deduplication and capacity", testModulationAssignment },
        { "Malformed state rejection with atomic rollback", testMalformedStateRejection },
        { "Arpeggiator notes, release and sustain", testArpeggiatorLifecycle },
        { "Arp block partition invariance and cross-channel panic", testArpBlockPartitionAndChannelPanic },
        { "Random and boundary parameter rendering", testParameterStress }
    };
    int failures = 0;
    for (const auto& test : tests)
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
    std::cout << "Processor validation: " << tests.size() << " cases, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
