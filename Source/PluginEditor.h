#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>

class AnyvocAudioProcessor;

// Platform boundary for reusable LFO curve assets. The default implementation
// uses the system clipboard and asynchronous native file chooser.
class LfoCurveServices
{
public:
    virtual ~LfoCurveServices() = default;
    virtual juce::String readClipboard() = 0;
    virtual void writeClipboard (const juce::String&) = 0;
    // Reply on the message thread, immediately or later; an empty file means cancel.
    virtual void chooseCurveFile (bool save, std::function<void (juce::File)> completion) = 0;
};

class AnyvocAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AnyvocAudioProcessorEditor (AnyvocAudioProcessor&,
                                         juce::File libraryDirectory = {}, juce::File librarySettings = {},
                                         std::shared_ptr<LfoCurveServices> curveServices = {});
    ~AnyvocAudioProcessorEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Interface;
    std::unique_ptr<Interface> interface;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnyvocAudioProcessorEditor)
};
