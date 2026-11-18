#include "WavetableDocument.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using anyvoc::WavetableDocument;
constexpr int length = WavetableDocument::frameLength;

void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

void expectAudio (const juce::AudioBuffer<float>& actual,
                  const juce::AudioBuffer<float>& expected, double tolerance = 2.0e-6)
{
    require (actual.getNumChannels() == expected.getNumChannels()
                 && actual.getNumSamples() == expected.getNumSamples(), "Wavetable dimensions changed");
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int sample = 0; sample < actual.getNumSamples(); ++sample)
            require (std::isfinite (actual.getSample (channel, sample))
                         && std::abs (actual.getSample (channel, sample)
                                     - expected.getSample (channel, sample)) <= tolerance,
                     "Wavetable content differs at sample " + std::to_string (sample));
}

juce::AudioBuffer<float> sineFrames (int frames, int sourceLength = length)
{
    juce::AudioBuffer<float> audio (1, frames * sourceLength);
    for (int frame = 0; frame < frames; ++frame)
        for (int sample = 0; sample < sourceLength; ++sample)
            audio.setSample (0, frame * sourceLength + sample, static_cast<float> (
                (.15 + .1 * frame) * std::sin (juce::MathConstants<double>::twoPi * sample / sourceLength)));
    return audio;
}

// Direct sinusoidal projections give an independent check of the FFT editor's
// amplitude and phase behavior without depending on its internal representation.
struct Partial { double sine = 0, cosine = 0; };
Partial partial (const float* frame, int harmonic)
{
    Partial result;
    for (int sample = 0; sample < length; ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi * harmonic * sample / length;
        result.sine += frame[sample] * std::sin (angle) * 2.0 / length;
        result.cosine += frame[sample] * std::cos (angle) * 2.0 / length;
    }
    return result;
}

void testCanonicalLoadAndValidation()
{
    WavetableDocument document;
    require (document.frameCount() == 1 && document.data().getNumChannels() == 1
                 && document.data().getNumSamples() == length, "Empty editor must have one canonical frame");
    require (document.data().getMagnitude (0, length) == 0, "Empty editor frame was not silent");
    const auto source = sineFrames (3, 1024);
    require (document.load (source, 1024), "Valid 1024-sample cycles could not be imported");
    require (document.frameCount() == 3 && document.data().getNumSamples() == 3 * length,
             "Import did not convert cycles to canonical 2048-sample frames");
    expectAudio (document.data(), sineFrames (3), 3.0e-6);
    require (document.frameData (-1) == nullptr && document.frameData (3) == nullptr,
             "Invalid frame indices returned an address");
    const auto before = document.data();
    juce::AudioBuffer<float> empty;
    require (! document.load (empty), "An empty audio buffer was accepted as a wavetable");
    auto invalid = sineFrames (1);
    invalid.setSample (0, 53, std::numeric_limits<float>::quiet_NaN());
    require (! document.load (invalid), "A non-finite frame was accepted");
    invalid.setSample (0, 53, 129.0f);
    require (! document.load (invalid), "An out-of-range source sample was accepted");
    auto tooMany = sineFrames (33);
    require (! document.load (tooMany), "A table with more than 32 frames was accepted");
    expectAudio (document.data(), before);
    require (! document.canUndo() && ! document.canRedo(), "Loading a document retained edit history");
    juce::AudioBuffer<float> stereo (2, length + 3);
    stereo.clear();
    for (int sample = 0; sample < length; ++sample)
    {
        stereo.setSample (0, sample, .3f);
        stereo.setSample (1, sample, .7f);
    }
    require (document.load (stereo) && document.frameCount() == 1,
             "A complete frame with an incomplete trailing fragment was rejected");
    for (int sample = 0; sample < length; ++sample)
        require (std::abs (document.frameData (0)[sample] - .5f) < 1.0e-6f,
                 "Stereo import did not average the channels");
    const auto averaged = document.data();
    stereo.setSample (1, length + 1, std::numeric_limits<float>::quiet_NaN());
    require (! document.load (stereo), "A non-finite discarded tail was accepted");
    expectAudio (document.data(), averaged);
}

void testDrawingAndGroupedHistory()
{
    WavetableDocument document;
    const auto initial = document.data();
    document.beginEdit();
    require (document.draw (0, 20, -.8f, 120, .8f), "Drawing a segment failed");
    require (std::abs (document.frameData (0)[20] + .8f) < 1.0e-6f
                 && std::abs (document.frameData (0)[70]) < 1.0e-6f
                 && std::abs (document.frameData (0)[120] - .8f) < 1.0e-6f,
             "Drawn line did not include its endpoints and linear midpoint");
    require (document.draw (0, 200, .6f, 180, -.6f), "Reverse-direction drawing failed");
    require (std::abs (document.frameData (0)[180] + .6f) < 1.0e-6f
                 && std::abs (document.frameData (0)[200] - .6f) < 1.0e-6f,
             "Reverse-direction drawing swapped the values incorrectly");
    require (document.draw (0, 500, 0, 500, 2.0f), "Single-point drawing failed");
    require (document.frameData (0)[500] == 1.0f, "Drawing did not clamp its final value");
    document.endEdit();
    const auto edited = document.data();
    require (document.canUndo() && document.undo(), "A grouped gesture was not undoable");
    expectAudio (document.data(), initial);
    require (! document.canUndo() && document.canRedo(), "A gesture produced more than one undo entry");
    require (document.redo(), "A grouped gesture was not redoable");
    expectAudio (document.data(), edited);
    require (document.undo(), "Could not return to the pre-edit document");
    require (document.draw (0, 3, 0, 3, .25f), "Replacement edit failed");
    require (! document.canRedo(), "A new edit retained obsolete redo history");
    const auto valid = document.data();
    require (! document.draw (1, 0, 0, 1, 1)
                 && ! document.draw (0, 0, std::numeric_limits<float>::infinity(), 1, 1),
             "Invalid drawing was accepted");
    expectAudio (document.data(), valid);
}

void testHarmonicAmplitudeAndPhase()
{
    juce::AudioBuffer<float> audio (1, length);
    constexpr double offset = .61;
    for (int sample = 0; sample < length; ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi * sample / length;
        audio.setSample (0, sample, static_cast<float> (.4 * std::sin (angle + offset)
                                                      + .2 * std::cos (3 * angle)));
    }
    WavetableDocument document;
    require (document.load (audio), "Harmonic fixture failed to load");
    const auto spectrum = document.harmonics (0);
    require (std::abs (spectrum[0] - .4f) < 1.0e-5f && std::abs (spectrum[2] - .2f) < 1.0e-5f
                 && spectrum[1] < 1.0e-5f, "Harmonic analysis reported incorrect amplitudes");
    const auto beforeThird = partial (document.frameData (0), 3);
    require (document.setHarmonic (0, 1, .7f), "Changing the fundamental failed");
    const auto fundamental = partial (document.frameData (0), 1);
    const auto afterThird = partial (document.frameData (0), 3);
    require (std::abs (fundamental.sine - .7 * std::cos (offset)) < 2.0e-5
                 && std::abs (fundamental.cosine - .7 * std::sin (offset)) < 2.0e-5,
             "Changing a harmonic lost its phase or set the wrong amplitude");
    require (std::abs (beforeThird.sine - afterThird.sine) < 2.0e-5
                 && std::abs (beforeThird.cosine - afterThird.cosine) < 2.0e-5,
             "Editing one harmonic changed another partial");
    require (document.setHarmonic (0, 64, .15f), "The highest editable harmonic was rejected");
    const auto upper = partial (document.frameData (0), 64);
    require (std::abs (std::hypot (upper.sine, upper.cosine) - .15) < 2.0e-5,
             "The highest editable harmonic was not synthesized correctly");
    const auto valid = document.data();
    require (! document.setHarmonic (0, 0, .3f) && ! document.setHarmonic (0, 1024, .3f)
                 && ! document.setHarmonic (0, 1, std::numeric_limits<float>::quiet_NaN()),
             "An invalid harmonic edit was accepted");
    expectAudio (document.data(), valid);
    require (document.undo(), "Harmonic edit could not be undone");
    require (document.harmonics (0)[63] < 1.0e-5f, "Undo did not remove the added harmonic");
    for (int sample = 0; sample < length; ++sample)
    {
        const auto angle = juce::MathConstants<double>::twoPi * sample / length;
        audio.setSample (0, sample, static_cast<float> (.8 * std::cos (angle) + .7 * std::cos (3 * angle)));
    }
    require (document.load (audio) && document.setHarmonic (0, 1, 1), "Additive harmonic edit failed");
    require (std::abs (document.frameData (0)[0] - 1.7f) < 2.0e-5f
                 && std::abs (document.harmonics (0)[2] - .7f) < 2.0e-5f,
             "Additive peaks above unity were clipped or implicitly normalized");
}

void testFrameStructureAndMorph()
{
    WavetableDocument document;
    const auto original = sineFrames (3);
    require (document.load (original), "Frame fixture failed to load");
    require (document.duplicateFrame (1) && document.frameCount() == 4, "Duplicate did not insert a frame");
    for (int sample = 0; sample < length; ++sample)
        require (document.frameData (1)[sample] == document.frameData (2)[sample]
                     && document.frameData (3)[sample] == original.getSample (0, 2 * length + sample),
                 "Duplicate changed frame order or the copied waveform");
    require (document.deleteFrame (2), "Deleting the duplicate failed");
    expectAudio (document.data(), original);
    require (document.undo() && document.frameCount() == 4, "Undo lost the deleted frame");
    require (document.redo() && document.frameCount() == 3, "Redo did not delete the same frame");
    while (document.frameCount() > 1) require (document.deleteFrame (0), "Deleting an earlier frame failed");
    require (! document.deleteFrame (0) && document.frameCount() == 1, "Editor allowed deletion of its last frame");
    while (document.frameCount() < WavetableDocument::maximumFrames)
        require (document.duplicateFrame (0), "Duplicate stopped before the maximum frame count");
    require (! document.duplicateFrame (0) && document.frameCount() == 32, "Frame count exceeded its maximum");

    auto anchors = sineFrames (5);
    for (int sample = 0; sample < length; ++sample)
        anchors.setSample (0, 4 * length + sample, sample < length / 2 ? .75f : -.75f);
    require (document.load (anchors) && document.morph (0, 4), "Morph between endpoint frames failed");
    for (int frame = 0; frame < 5; ++frame)
        for (int sample = 0; sample < length; ++sample)
        {
            const float weight = frame / 4.0f;
            const float expected = anchors.getSample (0, sample) * (1 - weight)
                                 + anchors.getSample (0, 4 * length + sample) * weight;
            require (std::abs (document.frameData (frame)[sample] - expected) < 2.0e-6f,
                     "Morph did not preserve endpoints and interpolate interior frames");
        }
    require (document.undo(), "Morph did not form one undoable operation");
    expectAudio (document.data(), anchors);
}

void testGenerationAndProcessing()
{
    WavetableDocument document;
    for (int shape = 0; shape < 5; ++shape)
    {
        require (document.generate (0, shape), "Waveform generation failed");
        double energy = 0;
        for (int sample = 0; sample < length; ++sample)
        {
            const auto value = document.frameData (0)[sample];
            require (std::isfinite (value) && std::abs (value) <= 1.001f, "Generated waveform was invalid");
            energy += value * value;
        }
        require (energy / length > .05, "Generated waveform was effectively silent");
        if (shape == 0)
            require (std::abs (document.harmonics (0)[0] - 1.0f) < 2.0e-5f
                         && document.harmonics (0)[1] < 2.0e-5f, "Sine generator created extra harmonics");
    }
    require (document.generate (0, 5) && document.data().getMagnitude (0, length) == 0,
             "Clear did not empty the selected frame");
    auto audio = sineFrames (1);
    require (document.load (audio) && document.processFrame (0, 0), "Normalization failed");
    require (std::abs (document.data().getMagnitude (0, length) - 1.0f) < 1.0e-5f,
             "Normalization did not bring the peak to unity");
    for (int sample = 0; sample < length; ++sample)
        audio.setSample (0, sample, audio.getSample (0, sample) + .3f);
    require (document.load (audio) && document.processFrame (0, 1), "DC removal failed");
    double mean = 0;
    for (int sample = 0; sample < length; ++sample) mean += document.frameData (0)[sample] / length;
    require (std::abs (mean) < 1.0e-6, "DC removal left an offset");
    const auto centred = document.data();
    require (document.processFrame (0, 2), "Waveform inversion failed");
    for (int sample = 0; sample < length; ++sample)
        require (std::abs (document.frameData (0)[sample] + centred.getSample (0, sample)) < 1.0e-6,
                 "Invert did not reverse the waveform polarity");
    for (int sample = 0; sample < length; ++sample) audio.setSample (0, sample, sample % 2 ? .8f : -.8f);
    require (document.load (audio) && document.processFrame (0, 3), "Periodic smoothing failed");
    require (document.data().getRMSLevel (0, 0, length) < .2f, "Smoothing did not attenuate alternating high-frequency samples");
    const auto processed = document.data();
    require (! document.processFrame (1, 0) && ! document.processFrame (0, 4)
                 && ! document.generate (0, 6), "An invalid processing operation was accepted");
    expectAudio (document.data(), processed);
}

void testHistoryCapacityAndReload()
{
    WavetableDocument document;
    for (int edit = 1; edit <= 25; ++edit)
        require (document.draw (0, 100, 0, 100, edit / 32.0f), "History fixture edit failed");
    int undos = 0;
    while (document.undo()) ++undos;
    require (undos == 20 && std::abs (document.frameData (0)[100] - 5.0f / 32) < 1.0e-6f,
             "History did not retain exactly the most recent 20 edit operations");
    int redos = 0;
    while (document.redo()) ++redos;
    require (redos == 20 && std::abs (document.frameData (0)[100] - 25.0f / 32) < 1.0e-6f,
             "Redo did not reconstruct the retained history");
    require (document.load (sineFrames (2)), "Replacement document failed to load");
    require (! document.canUndo() && ! document.canRedo(), "Loading another table preserved stale history");
    const auto baseline = document.data();
    require (document.draw (0, 100, 0, 100, .8f), "Snapshot history fixture failed");
    auto synchronized = document.data();
    synchronized.setSample (0, 100, .75f);
    require (document.replaceCurrentData (synchronized), "A valid DSP snapshot could not replace the current frame data");
    require (document.undo(), "Snapshot replacement cleared the previous edit history");
    expectAudio (document.data(), baseline);
    require (! document.canUndo() && document.redo(), "Snapshot replacement created an extra undo entry");
    expectAudio (document.data(), synchronized);
    require (document.draw (1, 50, 0, 50, -.6f), "Second snapshot history edit failed");
    const auto laterEdit = document.data();
    require (document.undo(), "Could not create redo history before snapshot replacement");
    synchronized.applyGain (.5f);
    require (document.replaceCurrentData (synchronized) && document.canRedo() && document.redo(),
             "Snapshot replacement discarded the pending redo operation");
    expectAudio (document.data(), laterEdit);
    require (document.undo(), "Redo after synchronization did not remain undoable");
    expectAudio (document.data(), synchronized);
    document.beginEdit();
    require (! document.replaceCurrentData (baseline), "Snapshot replacement interrupted an active gesture");
    expectAudio (document.data(), synchronized);
    document.endEdit();
    require (! document.replaceCurrentData (sineFrames (1)), "Snapshot replacement changed the document dimensions");
    auto invalid = synchronized;
    invalid.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
    require (! document.replaceCurrentData (invalid), "Snapshot replacement accepted a non-finite sample");
    expectAudio (document.data(), synchronized);
}

double wrapped (double phase) { return phase - std::floor (phase + .5); }

void expectPartial (const float* frame, int harmonic, double amplitude, double phase, double tolerance = 3.0e-6)
{
    const auto actual = partial (frame, harmonic);
    const auto radians = juce::MathConstants<double>::twoPi * phase;
    require (std::abs (actual.sine - amplitude * std::cos (radians)) < tolerance
                 && std::abs (actual.cosine - amplitude * std::sin (radians)) < tolerance,
             "Independent projection mismatch for harmonic " + std::to_string (harmonic));
}

double dc (const float* frame, bool nyquist = false)
{
    double result = 0;
    for (int i = 0; i < length; ++i) result += frame[i] * (nyquist && i % 2 ? -1.0 : 1.0) / length;
    return result;
}

void addSine (juce::AudioBuffer<float>& audio, int frame, int harmonic, double amplitude, double phase)
{
    for (int i = 0; i < length; ++i)
        audio.addSample (0, frame * length + i, static_cast<float> (amplitude * std::sin (
            juce::MathConstants<double>::twoPi * (static_cast<double> (harmonic) * i / length + phase))));
}

void testFullSpectrumBatchAndPreservation()
{
    juce::AudioBuffer<float> audio (1, length * 3); audio.clear();
    addSine (audio, 0, 9, .2, .1); addSine (audio, 2, 700, .1, -.2);
    for (int i = 0; i < length; ++i) audio.setSample (0, length + i, .18f + (i % 2 ? -.07f : .07f));
    addSine (audio, 1, 1, .35, .125);
    addSine (audio, 1, 127, .21, -.3125);
    addSine (audio, 1, 511, .17, .49);
    addSine (audio, 1, 1023, .14, -.499);
    addSine (audio, 1, 17, 1.6, .2); // Phase-only must retain amplitudes above the editor's 0..1 write range.
    WavetableDocument document;
    require (document.load (audio), "Full-spectrum fixture failed");
    const auto analysed = document.harmonicSpectrum (1);
    for (const auto& expected : std::vector<std::array<double, 3>> {
             { 1, .35, .125 }, { 127, .21, -.3125 }, { 511, .17, .49 }, { 1023, .14, -.499 }, { 17, 1.6, .2 } })
    {
        const auto index = static_cast<size_t> (expected[0] - 1);
        require (std::abs (analysed.amplitudes[index] - expected[1]) < 3.0e-6
                     && std::abs (wrapped (analysed.phases[index] - expected[2])) < 3.0e-6,
                 "Full-spectrum analysis lost a low/high harmonic's amplitude or sine-relative phase");
    }
    const auto before = document.data();
    require (document.setHarmonics (1, {
        { 1, .7f, -.25f, true, true }, { 127, 0, .1875f, false, true },
        { 511, .33f, 0, true, false }, { 1023, .24f, 1000.625f, true, true },
        { 17, 0, -.3f, false, true } }), "Valid full-spectrum batch failed");
    expectPartial (document.frameData (1), 1, .7, -.25);
    expectPartial (document.frameData (1), 127, .21, .1875);
    expectPartial (document.frameData (1), 511, .33, .49);
    expectPartial (document.frameData (1), 1023, .24, -.375);
    expectPartial (document.frameData (1), 17, 1.6, -.3);
    require (std::abs (dc (document.frameData (1)) - dc (before.getReadPointer (0, length))) < 1.0e-6
                 && std::abs (dc (document.frameData (1), true) - dc (before.getReadPointer (0, length), true)) < 1.0e-6,
             "Batch edits changed DC or Nyquist");
    for (int harmonic : { 2, 63, 128, 510, 512, 1022 })
    {
        const auto initial = partial (before.getReadPointer (0, length), harmonic);
        const auto final = partial (document.frameData (1), harmonic);
        require (std::abs (initial.sine - final.sine) < 2.0e-6 && std::abs (initial.cosine - final.cosine) < 2.0e-6,
                 "A batch edit changed an unselected bin");
    }
    for (int frame : { 0, 2 }) for (int i = 0; i < length; ++i)
        require (document.frameData (frame)[i] == before.getSample (0, frame * length + i), "A batch edit changed another frame");
    const auto after = document.data();
    require (document.undo() && ! document.canUndo(), "One spectral batch created multiple history entries");
    expectAudio (document.data(), before, 0);
    require (document.redo(), "Full-spectrum batch redo failed"); expectAudio (document.data(), after, 0);
    require (document.setHarmonic (0, 1023, .4f), "Compatibility amplitude setter rejected harmonic 1023");
    expectPartial (document.frameData (0), 1023, .4, 0);
}

void testSpectralBatchAtomicityAndNoOps()
{
    WavetableDocument document;
    require (document.load (sineFrames (2)), "Batch validation fixture failed");
    const auto before = document.data();
    const auto spectrum = document.harmonicSpectrum (0);
    std::vector<WavetableDocument::HarmonicEdit> same;
    for (int harmonic = 1; harmonic <= WavetableDocument::maximumHarmonics; ++harmonic)
        same.push_back ({ harmonic, spectrum.amplitudes[static_cast<size_t> (harmonic - 1)],
                         spectrum.phases[static_cast<size_t> (harmonic - 1)], true, true });
    require (document.setHarmonics (0, same) && ! document.canUndo(), "Reapplying the analysed spectrum made a false edit");
    expectAudio (document.data(), before, 0);
    require (document.setHarmonics (0, {}) && ! document.canUndo(), "An empty batch made history");
    const float nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
    for (const auto& invalid : std::vector<WavetableDocument::HarmonicEdit> {
             { 0, .2f, 0, true, false }, { 1024, .2f, 0, true, false },
             { 1, nan, 0, false, true }, { 1, .2f, infinity, true, false } })
        require (! document.setHarmonics (0, { { 1, .8f, 0, true, false }, invalid }),
                 "An invalid tail entry did not reject the entire batch");
    require (! document.setHarmonics (-1, {}) && ! document.setHarmonics (2, same), "Invalid spectral frame accepted");
    expectAudio (document.data(), before, 0); require (! document.canUndo(), "Rejected batch created history");
    require (document.setHarmonics (0, { { 1, .6f, .2f, true, true }, { 1, .45f, 0, true, false },
                                        { 1, 0, -.125f, false, true } }), "Duplicate field merge failed");
    expectPartial (document.frameData (0), 1, .45, -.125);
    require (document.undo(), "Could not create redo branch");
    require (document.setHarmonics (0, same) && document.canRedo(), "No-op analysis roundtrip cleared redo");
    require (! document.setHarmonics (0, { { 2, .5f, nan, true, true } }) && document.canRedo(),
             "Rejected edit cleared redo");
    std::vector<WavetableDocument::HarmonicEdit> excessive;
    for (int harmonic = 1; harmonic <= WavetableDocument::maximumHarmonics; ++harmonic)
        excessive.push_back ({ harmonic, 1, .25f, true, true });
    require (! document.setHarmonics (0, excessive), "An additive batch exceeding ±128 was accepted or clipped");
    expectAudio (document.data(), before, 0);
    require (document.canRedo(), "Rejected peak-overflow batch cleared redo");
    document.beginEdit();
    require (document.setHarmonics (0, { { 1023, .2f, .1f, true, true } })
                 && document.setHarmonics (1, { { 80, .4f, -.2f, true, true } }), "Grouped spectral writes failed");
    document.endEdit();
    require (document.undo() && ! document.canUndo(), "A grouped multi-frame edit was not one undo");
    expectAudio (document.data(), before, 0);
}

void testSilentPhaseAndWrappedPhase()
{
    WavetableDocument document;
    const auto silent = document.data();
    require (document.setHarmonics (0, { { 1023, 0, .37f, false, true }, { 1, 0, 10000.25f, false, true } })
                 && ! document.canUndo(), "Changing silent phase generated audio or history");
    expectAudio (document.data(), silent, 0);
    require (document.setHarmonics (0, { { 73, .6f, 10000.25f, true, true } }), "Large finite phase wrap failed");
    expectPartial (document.frameData (0), 73, .6, .25);
    const auto snapshot = document.data();
    require (document.setHarmonics (0, { { 73, 0, -9999.75f, false, true } }), "Equivalent wrapped phase failed");
    expectAudio (document.data(), snapshot, 0);
    require (document.undo() && ! document.canUndo(), "Equivalent whole-turn phase made an extra undo entry");
    expectAudio (document.data(), silent, 0);
    juce::AudioBuffer<float> tiny (1, length); tiny.clear();
    addSine (tiny, 0, 123, 2.0e-7, .31);
    require (document.load (tiny) && document.setHarmonics (0, { { 123, 0, -.2f, false, true } }), "Sub-noise-floor phase edit rejected");
    expectAudio (document.data(), tiny, 0);
    require (! document.canUndo() && document.harmonicSpectrum (0).phases[122] == 0,
             "Silent phase policy changed a tiny existing bin");
}

void testSpectralMorphAndLegacyPath()
{
    juce::AudioBuffer<float> anchors (1, length * 5); anchors.clear();
    for (int i = 0; i < length; ++i)
    {
        anchors.setSample (0, i, -.12f + (i % 2 ? -.04f : .04f));
        anchors.setSample (0, 4 * length + i, .28f + (i % 2 ? .02f : -.02f));
    }
    addSine (anchors, 0, 1, .5, 0); addSine (anchors, 4, 1, .5, -.5);
    addSine (anchors, 0, 1023, .22, .45); addSine (anchors, 4, 1023, .22, -.45);
    addSine (anchors, 4, 17, .4, .2); // A silent left endpoint must borrow the right phase.
    addSine (anchors, 0, 511, .3, -.3); // A silent right endpoint must borrow the left phase.
    WavetableDocument waveform, explicitWaveform, spectral;
    require (waveform.load (anchors) && explicitWaveform.load (anchors) && spectral.load (anchors), "Morph fixtures failed");
    require (waveform.morph (0, 4) && explicitWaveform.morph (0, 4, 0), "Legacy morph failed");
    expectAudio (waveform.data(), explicitWaveform.data(), 0);
    expectPartial (waveform.frameData (2), 1, 0, 0);
    require (spectral.morph (0, 4, 1), "Spectral morph failed");
    expectPartial (spectral.frameData (2), 1, .5, -.25);
    for (int frame = 1; frame < 4; ++frame)
    {
        const double mix = frame / 4.0;
        expectPartial (spectral.frameData (frame), 1023, .22, .45 + .1 * mix);
        expectPartial (spectral.frameData (frame), 17, .4 * mix, .2);
        expectPartial (spectral.frameData (frame), 511, .3 * (1 - mix), -.3);
        require (std::abs (dc (spectral.frameData (frame)) - (-.12 + .4 * mix)) < 2.0e-6
                     && std::abs (dc (spectral.frameData (frame), true) - (.04 - .06 * mix)) < 2.0e-6,
                 "Spectral morph failed to interpolate real DC/Nyquist bins");
    }
    for (int frame : { 0, 4 }) for (int i = 0; i < length; ++i)
        require (spectral.frameData (frame)[i] == anchors.getSample (0, frame * length + i), "Spectral morph changed an endpoint");
    require (spectral.undo() && ! spectral.canUndo(), "A multi-frame spectral morph fragmented history");
    expectAudio (spectral.data(), anchors, 0);
    require (! spectral.morph (0, 4, 2) && ! spectral.morph (4, 0, 1) && spectral.canRedo(), "Invalid morph changed history");
    require (spectral.morph (0, 1, 1) && spectral.canRedo(), "Adjacent-frame no-op cleared redo");
    expectAudio (spectral.data(), anchors, 0);

    juce::AudioBuffer<float> surrounded (1, length * 7); surrounded.clear();
    surrounded.copyFrom (0, length, anchors, 0, 0, anchors.getNumSamples());
    addSine (surrounded, 0, 73, .61, .13); addSine (surrounded, 6, 920, .38, -.4);
    require (spectral.load (surrounded) && spectral.morph (1, 5, 1), "Interior range spectral morph failed");
    for (int frame : { 0, 1, 5, 6 }) for (int i = 0; i < length; ++i)
        require (spectral.frameData (frame)[i] == surrounded.getSample (0, frame * length + i),
                 "Spectral morph changed an endpoint or an out-of-range frame");
    juce::AudioBuffer<float> opposite (1, length * 3); opposite.clear();
    addSine (opposite, 0, 33, .4, .2);
    for (int i = 0; i < length; ++i) opposite.setSample (0, 2 * length + i, -opposite.getSample (0, i));
    require (spectral.load (opposite) && spectral.morph (0, 2, 1), "Arbitrary-start half-turn morph failed");
    expectPartial (spectral.frameData (1), 33, .4, -.05);

    // Bounded endpoints can have unbounded additive intermediate peaks when
    // many partial phases meet. Reject the complete candidate, without clipping.
    juce::AudioBuffer<float> dispersed (1, length * 3); dispersed.clear();
    std::uint32_t seed = 0x12345678u;
    for (int harmonic = 1; harmonic <= 512; ++harmonic)
    {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        const double phase = seed & 1 ? .24 : -.24;
        addSine (dispersed, 0, harmonic, 1, phase);
        addSine (dispersed, 2, harmonic, 1, -phase);
    }
    require (dispersed.getMagnitude (0, dispersed.getNumSamples()) < 128 && spectral.load (dispersed),
             "Overflow-morph endpoints were not individually valid");
    require (! spectral.morph (0, 2, 1) && ! spectral.canUndo(), "An overflowing spectral morph was clipped or partially committed");
    expectAudio (spectral.data(), dispersed, 0);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const std::vector<std::pair<const char*, std::function<void()>>> tests {
        { "Canonical cycle conversion and invalid import rollback", testCanonicalLoadAndValidation },
        { "Drawing, grouped gestures and redo invalidation", testDrawingAndGroupedHistory },
        { "Harmonic amplitude, phase and independent partials", testHarmonicAmplitudeAndPhase },
        { "Frame duplicate/delete boundaries and endpoint morphing", testFrameStructureAndMorph },
        { "Waveform generation and offline processing", testGenerationAndProcessing },
        { "Bounded undo history and document replacement", testHistoryCapacityAndReload },
        { "Full-spectrum amplitude/phase batches and untouched-bin preservation", testFullSpectrumBatchAndPreservation },
        { "Spectral batch validation, no-ops, bounded peaks and grouped history", testSpectralBatchAtomicityAndNoOps },
        { "Silent phase policy and large signed phase wrapping", testSilentPhaseAndWrappedPhase },
        { "Spectral shortest-arc morph and unchanged legacy waveform path", testSpectralMorphAndLegacyPath }
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
    std::cout << "Wavetable validation: " << tests.size() << " cases, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
