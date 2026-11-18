#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

class AnyvocAudioProcessor;

class AnyvocAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AnyvocAudioProcessorEditor (AnyvocAudioProcessor&,
                                         juce::File libraryDirectory = {}, juce::File librarySettings = {});
    ~AnyvocAudioProcessorEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Interface;
    std::unique_ptr<Interface> interface;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnyvocAudioProcessorEditor)
};
