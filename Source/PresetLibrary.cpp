#include "PresetLibrary.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <set>

namespace anyvoc
{
namespace
{
constexpr int maximumDepth = 8;
constexpr int maximumDirectories = 10000;
constexpr int maximumItems = 50000;
constexpr juce::int64 maximumSettingsBytes = 1024 * 1024;
constexpr int maximumFavourites = 10000;
using FavouriteIds = std::set<juce::String>;

// File locks on some platforms are process-scoped. This also serializes separate
// library instances in one host while their InterProcessLocks cover other hosts.
std::mutex favouritesIoMutex;

juce::String pathKey (const juce::File& file)
{
    const auto path = file.getFullPathName().replaceCharacter ('\\', '/');
    return juce::File::areFileNamesCaseSensitive() ? path : path.toLowerCase();
}

std::filesystem::path nativePath (const juce::File& file)
{
   #if JUCE_WINDOWS
    return std::filesystem::path (file.getFullPathName().toWideCharPointer());
   #else
    return std::filesystem::u8path (file.getFullPathName().toStdString());
   #endif
}

juce::File juceFile (const std::filesystem::path& path)
{
   #if JUCE_WINDOWS
    return juce::File (juce::String (path.wstring().c_str()));
   #else
    return juce::File (juce::String::fromUTF8 (path.u8string().c_str()));
   #endif
}

bool entryLess (const PresetEntry& a, const PresetEntry& b, PresetSort sort)
{
    if (sort == PresetSort::category)
        if (const int category = a.category.compareIgnoreCase (b.category); category != 0) return category < 0;
    if (const int name = a.name.compareIgnoreCase (b.name); name != 0) return name < 0;
    if (const int category = a.category.compareIgnoreCase (b.category); category != 0) return category < 0;
    if (const int source = a.source.compareIgnoreCase (b.source); source != 0) return source < 0;
    // Exact-case ties come last, so Pad/pad stay one category ordered by name.
    if (const int category = a.category.compare (b.category); category != 0) return category < 0;
    if (const int name = a.name.compare (b.name); name != 0) return name < 0;
    if (const int source = a.source.compare (b.source); source != 0) return source < 0;
    return a.id < b.id;
}

void updateFlags (std::vector<PresetEntry>& entries, const FavouriteIds& ids)
{
    for (auto& entry : entries) entry.favourite = ids.find (entry.id) != ids.end();
}

juce::String settingsLockName (const juce::File& file)
{
    return "Anyvoc.PresetLibrary." + juce::String::toHexString (pathKey (file).hashCode64());
}

struct SettingsLock
{
    explicit SettingsLock (const juce::File& file) : lock (settingsLockName (file)), held (lock.enter (1500)) {}
    ~SettingsLock() { if (held) lock.exit(); }
    juce::InterProcessLock lock;
    bool held;
};

juce::Result readFavourites (const juce::File& file, FavouriteIds& result)
{
    if (! file.exists()) { result.clear(); return juce::Result::ok(); }
    if (! file.existsAsFile()) return juce::Result::fail ("The favourites settings path is not a file: " + file.getFullPathName());
    if (file.getSize() <= 0 || file.getSize() > maximumSettingsBytes)
        return juce::Result::fail ("The favourites settings file is empty or too large.");
    std::unique_ptr<juce::FileInputStream> input (file.createInputStream());
    if (input == nullptr || input->getStatus().failed())
        return juce::Result::fail ("Could not read favourites settings: " + file.getFullPathName());
    juce::MemoryBlock data;
    input->readIntoMemoryBlock (data, static_cast<int> (maximumSettingsBytes + 1));
    if (data.getSize() > static_cast<size_t> (maximumSettingsBytes) || input->getStatus().failed())
        return juce::Result::fail ("Could not read the complete favourites settings file.");
    const auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (static_cast<const char*> (data.getData()), static_cast<int> (data.getSize())));
    if (xml == nullptr || ! xml->hasTagName ("ANYVOC_PRESET_LIBRARY") || xml->getStringAttribute ("version") != "1")
        return juce::Result::fail ("Favourites settings are damaged or use an unsupported version; the file was preserved.");
    FavouriteIds candidate;
    for (const auto* child : xml->getChildIterator())
    {
        const auto id = child->getStringAttribute ("id");
        if (! child->hasTagName ("FAVOURITE") || id.isEmpty() || id.length() > 4096)
            return juce::Result::fail ("Favourites settings contain an invalid entry; the file was preserved.");
        candidate.insert (id);
        if (candidate.size() > static_cast<size_t> (maximumFavourites))
            return juce::Result::fail ("Favourites settings contain too many entries.");
    }
    result = std::move (candidate);
    return juce::Result::ok();
}

juce::Result writeFavourites (const juce::File& file, const FavouriteIds& ids)
{
    if (ids.size() > static_cast<size_t> (maximumFavourites))
        return juce::Result::fail ("The favourites list has reached its limit.");
    const auto madeDirectory = file.getParentDirectory().createDirectory();
    if (madeDirectory.failed()) return juce::Result::fail ("Could not create the favourites settings folder: " + madeDirectory.getErrorMessage());
    juce::XmlElement xml ("ANYVOC_PRESET_LIBRARY");
    xml.setAttribute ("version", 1);
    for (const auto& id : ids) xml.createNewChildElement ("FAVOURITE")->setAttribute ("id", id);
    const auto text = xml.toString();
    if (text.getNumBytesAsUTF8() > static_cast<size_t> (maximumSettingsBytes))
        return juce::Result::fail ("The favourites settings file would be too large.");
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (text) || ! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not save favourites settings. Check folder permissions or whether another application has locked the file.");
    return juce::Result::ok();
}

struct ScanResult
{
    std::vector<PresetEntry> users;
    int items = 0, directories = 0, skippedLinks = 0, errors = 0;
    bool limited = false, depthLimited = false;
    juce::String firstError, missingDirectory;
    std::set<juce::String> seen;

    void error (const juce::String& message)
    {
        ++errors;
        if (firstError.isEmpty()) firstError = message;
    }
    juce::String status() const
    {
        if (missingDirectory.isNotEmpty())
            return "No user presets yet. Save presets in " + missingDirectory + ".";
        juce::String text = "Found " + juce::String (static_cast<int> (users.size())) + " user presets.";
        if (skippedLinks > 0) text += " Skipped " + juce::String (skippedLinks) + " symbolic links or reparse points.";
        if (depthLimited) text += " Some folders exceed the 8-level scan depth.";
        if (limited) text += " Partial scan: the file or folder limit was reached.";
        if (errors > 0) text += " Partial scan: " + firstError;
        return text;
    }
};
}

class PresetLibrary::Impl : private juce::Thread
{
public:
    Impl (const juce::StringArray& factoryNames, juce::File folder, juce::File preferences)
        : juce::Thread ("Anyvoc preset library"), directory (std::move (folder)), settings (std::move (preferences))
    {
        std::set<juce::String> ids;
        for (int index = 0; index < factoryNames.size(); ++index)
        {
            const auto fullName = factoryNames[index].trim();
            const auto id = PresetLibrary::factoryId (fullName);
            if (! ids.insert (id).second) continue;
            const int separator = fullName.indexOf (" / ");
            PresetEntry entry;
            entry.id = id; entry.source = "Factory"; entry.factoryIndex = index;
            entry.category = separator >= 0 ? fullName.substring (0, separator).trim() : "Factory";
            entry.name = separator >= 0 ? fullName.substring (separator + 3).trim() : fullName;
            if (entry.category.isEmpty()) entry.category = "Factory";
            if (entry.name.isEmpty()) entry.name = "Untitled";
            factories.push_back (std::move (entry));
        }
        entries = factories;
        {
            const std::lock_guard<std::mutex> ioGuard (favouritesIoMutex);
            SettingsLock fileLock (settings);
            if (fileLock.held)
            {
                const auto loaded = readFavourites (settings, favourites);
                if (loaded.failed()) status = loaded.getErrorMessage();
            }
            else status = "Could not lock favourites settings; refresh to retry.";
        }
        updateFlags (entries, favourites);
        std::sort (entries.begin(), entries.end(), [] (const auto& a, const auto& b) { return entryLess (a, b, PresetSort::name); });
        if (startThread (juce::Thread::Priority::low)) refresh();
        else status = "Could not start the preset scan worker. Factory presets remain available.";
    }

    ~Impl() override
    {
        // No posted UI callbacks or detached work can outlive this model.
        signalThreadShouldExit(); notify(); stopThread (-1);
    }

    void refresh()
    {
        const std::lock_guard<std::mutex> guard (stateMutex);
        if (! isThreadRunning()) { status = "The preset scan worker is unavailable."; ++revision; return; }
        ++requested;
        scanning = true;
        status = "Scanning presets...";
        ++revision;
        notify();
    }

    void scanDirectory (const juce::File& folder, const juce::String& category, int depth, ScanResult& result)
    {
        if (threadShouldExit() || result.limited) return;
        // JUCE checks FILE_ATTRIBUTE_REPARSE_POINT on Windows, including junctions.
        if (folder.isSymbolicLink()) { ++result.skippedLinks; return; }
        if (++result.directories > maximumDirectories) { result.limited = true; return; }
        std::error_code error;
        std::filesystem::directory_iterator iterator (nativePath (folder), error), end;
        if (error) { result.error ("Could not read " + folder.getFullPathName() + ": " + juce::String (error.message())); return; }
        struct Child { juce::File file; bool directory = false; };
        std::vector<Child> children;
        while (iterator != end && ! threadShouldExit())
        {
            if (++result.items > maximumItems) { result.limited = true; break; }
            const auto file = juceFile (iterator->path());
            const auto kind = iterator->symlink_status (error);
            if (error) { result.error ("Could not inspect " + file.getFullPathName()); error.clear(); }
            else if (std::filesystem::is_symlink (kind) || file.isSymbolicLink()) ++result.skippedLinks;
            else if (std::filesystem::is_directory (kind)) children.push_back ({ file, true });
            else if (std::filesystem::is_regular_file (kind) && file.hasFileExtension ("anyvoc")) children.push_back ({ file, false });
            iterator.increment (error);
            if (error) { result.error ("Could not finish reading " + folder.getFullPathName() + ": " + juce::String (error.message())); break; }
        }
        std::sort (children.begin(), children.end(), [] (const auto& a, const auto& b)
        {
            const auto aKey = pathKey (a.file), bKey = pathKey (b.file);
            return aKey != bKey ? aKey < bKey : a.file.getFullPathName() < b.file.getFullPathName();
        });
        for (const auto& child : children)
        {
            if (threadShouldExit()) return;
            if (child.directory)
            {
                if (depth >= maximumDepth) { result.depthLimited = true; continue; }
                scanDirectory (child.file, depth == 0 ? child.file.getFileName() : category, depth + 1, result);
            }
            else
            {
                const auto id = PresetLibrary::userId (child.file);
                if (! result.seen.insert (id).second) continue;
                PresetEntry entry;
                entry.id = id; entry.name = child.file.getFileNameWithoutExtension();
                entry.category = category.isEmpty() ? "User" : category;
                entry.source = "User"; entry.file = child.file;
                result.users.push_back (std::move (entry));
            }
        }
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (-1);
            while (! threadShouldExit())
            {
                std::uint64_t ticket;
                {
                    const std::lock_guard<std::mutex> guard (stateMutex);
                    if (completed == requested) break;
                    ticket = requested;
                }
                ScanResult scan;
                if (! directory.exists() && ! directory.isSymbolicLink()) scan.missingDirectory = directory.getFullPathName();
                else
                {
                    try { scanDirectory (directory, {}, 0, scan); }
                    catch (const std::filesystem::filesystem_error& error) { scan.error (juce::String (error.what())); }
                }
                if (threadShouldExit()) return;
                auto nextEntries = factories;
                nextEntries.insert (nextEntries.end(), scan.users.begin(), scan.users.end());
                std::sort (nextEntries.begin(), nextEntries.end(), [] (const auto& a, const auto& b) { return entryLess (a, b, PresetSort::name); });
                // Hold the file lock until publication, so a concurrent favourite
                // write cannot be overwritten in memory by an older scan snapshot.
                const std::lock_guard<std::mutex> ioGuard (favouritesIoMutex);
                SettingsLock fileLock (settings);
                FavouriteIds loaded;
                const auto loadedResult = fileLock.held ? readFavourites (settings, loaded)
                    : juce::Result::fail ("Could not lock favourites settings; existing favourites were kept.");
                const std::lock_guard<std::mutex> guard (stateMutex);
                if (loadedResult.wasOk()) favourites = std::move (loaded);
                updateFlags (nextEntries, favourites);
                entries = std::move (nextEntries);
                completed = ticket;
                scanning = completed != requested;
                status = scan.status();
                if (loadedResult.failed()) status += " " + loadedResult.getErrorMessage();
                if (scanning) status = "Scanning presets again... " + status;
                ++revision;
            }
        }
    }

    juce::Result setFavourite (const juce::String& id, bool favourite)
    {
        {
            const std::lock_guard<std::mutex> guard (stateMutex);
            if (id.isEmpty() || std::none_of (entries.begin(), entries.end(), [&] (const auto& entry) { return entry.id == id; }))
                return juce::Result::fail ("This preset is no longer in the library. Refresh the list and try again.");
        }
        const std::lock_guard<std::mutex> ioGuard (favouritesIoMutex);
        SettingsLock fileLock (settings);
        if (! fileLock.held) return juce::Result::fail ("Favourites settings are busy. Try again shortly.");
        FavouriteIds merged;
        const auto loaded = readFavourites (settings, merged);
        if (loaded.failed()) return loaded;
        if (favourite) merged.insert (id); else merged.erase (id);
        const auto saved = writeFavourites (settings, merged);
        if (saved.failed()) return saved;
        const std::lock_guard<std::mutex> guard (stateMutex);
        favourites = std::move (merged);
        updateFlags (entries, favourites);
        ++revision;
        return juce::Result::ok();
    }

    const juce::File directory, settings;
    mutable std::mutex stateMutex;
    std::vector<PresetEntry> factories, entries;
    FavouriteIds favourites;
    std::uint64_t requested = 0, completed = 0, revision = 1;
    bool scanning = false;
    juce::String status = "Factory presets are ready.";
};

juce::File PresetLibrary::defaultPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Anyvoc").getChildFile ("Presets");
}
juce::File PresetLibrary::defaultSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Anyvoc").getChildFile ("preset-library.xml");
}
juce::String PresetLibrary::factoryId (const juce::String& fullFactoryName) { return "factory:" + fullFactoryName.trim(); }
juce::String PresetLibrary::userId (const juce::File& file) { return "user:" + pathKey (file); }

PresetLibrary::PresetLibrary (const juce::StringArray& factoryNames, juce::File directory, juce::File settings)
    : impl (std::make_unique<Impl> (factoryNames, std::move (directory), std::move (settings))) {}
PresetLibrary::~PresetLibrary() = default;
juce::File PresetLibrary::getDirectory() const { return impl->directory; }
std::vector<PresetEntry> PresetLibrary::getEntries() const
{
    const std::lock_guard<std::mutex> guard (impl->stateMutex);
    return impl->entries;
}
std::vector<PresetEntry> PresetLibrary::query (const juce::String& search, const juce::String& category,
                                             const juce::String& source, bool favouritesOnly, PresetSort sort) const
{
    auto entries = getEntries();
    const auto tokens = juce::StringArray::fromTokens (search, " \t\r\n", "");
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const auto& entry)
    {
        if (favouritesOnly && ! entry.favourite) return true;
        if (category.isNotEmpty() && ! entry.category.equalsIgnoreCase (category)) return true;
        if (source.isNotEmpty() && ! entry.source.equalsIgnoreCase (source)) return true;
        const auto haystack = entry.name + " " + entry.category + " " + entry.source;
        for (const auto& token : tokens) if (token.isNotEmpty() && ! haystack.containsIgnoreCase (token)) return true;
        return false;
    }), entries.end());
    std::sort (entries.begin(), entries.end(), [sort] (const auto& a, const auto& b) { return entryLess (a, b, sort); });
    return entries;
}
void PresetLibrary::refresh() { impl->refresh(); }
bool PresetLibrary::isScanning() const
{
    const std::lock_guard<std::mutex> guard (impl->stateMutex);
    return impl->scanning;
}
std::uint64_t PresetLibrary::getRevision() const
{
    const std::lock_guard<std::mutex> guard (impl->stateMutex);
    return impl->revision;
}
juce::String PresetLibrary::getStatus() const
{
    const std::lock_guard<std::mutex> guard (impl->stateMutex);
    return impl->status;
}
juce::Result PresetLibrary::setFavourite (const juce::String& id, bool favourite) { return impl->setFavourite (id, favourite); }
}
