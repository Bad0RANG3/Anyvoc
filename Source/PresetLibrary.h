#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace anyvoc
{
struct PresetEntry
{
    juce::String id, name, category, source;
    juce::File file;
    int factoryIndex = -1;
    bool favourite = false;
};

enum class PresetSort { name, category };

/** Thread-safe offline preset index. Refresh scans on its own worker; no callbacks
    are posted. Poll revision/scanning/status from the UI. Never call from audio. */
class PresetLibrary
{
public:
    static juce::File defaultPresetDirectory();
    static juce::File defaultSettingsFile();
    static juce::String factoryId (const juce::String& fullFactoryName);
    static juce::String userId (const juce::File& file);

    PresetLibrary (const juce::StringArray& factoryNames,
                   juce::File directory = defaultPresetDirectory(),
                   juce::File settings = defaultSettingsFile());
    ~PresetLibrary();
    PresetLibrary (const PresetLibrary&) = delete;
    PresetLibrary& operator= (const PresetLibrary&) = delete;

    juce::File getDirectory() const;
    std::vector<PresetEntry> getEntries() const;
    std::vector<PresetEntry> query (const juce::String& search, const juce::String& category,
                                  const juce::String& source, bool favouritesOnly,
                                  PresetSort sort) const;
    void refresh();
    bool isScanning() const;
    std::uint64_t getRevision() const;
    juce::String getStatus() const;
    juce::Result setFavourite (const juce::String& id, bool favourite);

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
