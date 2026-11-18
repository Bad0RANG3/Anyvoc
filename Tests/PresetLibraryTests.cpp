#include "PresetLibrary.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using anyvoc::PresetEntry;
using anyvoc::PresetLibrary;
using anyvoc::PresetSort;

void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

struct Sandbox
{
    Sandbox()
        : base (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("Anyvoc-PresetLibraryTests", {}, false)),
          library (base.getChildFile ("Library")), settings (base.getChildFile ("Preferences/favourites.xml"))
    {
        require (library.createDirectory().wasOk(), "Could not create isolated preset directory");
    }
    ~Sandbox() { base.deleteRecursively(); }
    juce::File base, library, settings;

    juce::File write (const juce::String& relative, const juce::String& text = "not parsed by the index") const
    {
        const auto file = library.getChildFile (relative);
        require (file.getParentDirectory().createDirectory().wasOk() && file.replaceWithText (text),
                 "Could not write preset-index fixture");
        return file;
    }
};

const juce::StringArray factoryNames { "Init", "Pad / Warm Horizon", "Bass / Amber", "Lead / Zeta" };

void finishScan (PresetLibrary& library)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
    while (library.isScanning() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (2);
    require (! library.isScanning(), "Preset scan did not finish within five seconds");
}

void refresh (PresetLibrary& library)
{
    library.refresh();
    finishScan (library);
}

const PresetEntry& entry (const std::vector<PresetEntry>& entries, const juce::String& id)
{
    const auto found = std::find_if (entries.begin(), entries.end(), [&] (const auto& item) { return item.id == id; });
    require (found != entries.end(), "Expected preset ID was absent from index: " + id.toStdString());
    return *found;
}

std::vector<PresetEntry> favourites (PresetLibrary& library)
{
    return library.query ({}, {}, {}, true, PresetSort::name);
}

void testIndexAndStableIdentity()
{
    Sandbox files;
    const auto root = files.write ("Quiet.anyvoc");
    const auto nested = files.write ("Pads/Ambient/Warm Sea.ANYVOC");
    files.write ("Bass/Amber.anyvoc");
    files.write ("ignore.wav");
    PresetLibrary library (factoryNames, files.library, files.settings);
    const auto immediate = library.getEntries();
    for (int index = 0; index < factoryNames.size(); ++index)
        require (entry (immediate, PresetLibrary::factoryId (factoryNames[index])).factoryIndex == index,
                 "Factory sounds were not available immediately with their correct program indices");
    finishScan (library);
    const auto entries = library.getEntries();
    require (entries.size() == 7, "Recursive scan omitted a preset, duplicated it, or indexed a non-preset file");
    const auto& userRoot = entry (entries, PresetLibrary::userId (root));
    require (userRoot.name == "Quiet" && userRoot.category == "User" && userRoot.source == "User"
                 && userRoot.file == root && userRoot.factoryIndex == -1,
             "Root user preset identity or category was incorrect");
    const auto& userNested = entry (entries, PresetLibrary::userId (nested));
    require (userNested.name == "Warm Sea" && userNested.category == "Pads" && userNested.file == nested,
             "Nested preset did not use its filename and first relative directory as category");
    const auto& factory = entry (entries, PresetLibrary::factoryId (factoryNames[1]));
    require (factory.name == "Warm Horizon" && factory.category == "Pad" && factory.source == "Factory",
             "Factory name/category split was incorrect");
    refresh (library);
    require (library.getEntries().size() == entries.size(), "Refreshing the same directory duplicated presets");
    const juce::StringArray reordered { factoryNames[3], factoryNames[1], factoryNames[0], factoryNames[2] };
    PresetLibrary reopened (reordered, files.library, files.settings);
    finishScan (reopened);
    require (entry (reopened.getEntries(), PresetLibrary::factoryId (factoryNames[3])).factoryIndex == 0,
             "Stable factory identity depended on its current program index");
}

void testQueryAndSort()
{
    Sandbox files;
    const auto pad = files.write ("Pads/Warm Mist.anyvoc");
    const auto unicode = files.write (juce::String::fromUTF8 ("Pads/暖冬 霞光.anyvoc"));
    files.write ("Bass/Deep Warm.anyvoc");
    files.write ("Leads/Alpha.anyvoc");
    PresetLibrary library (factoryNames, files.library, files.settings);
    finishScan (library);
    const auto results = library.query ("  WaRm\tPAD  ", {}, {}, false, PresetSort::name);
    require (results.size() == 2, "Search did not combine case-insensitive whitespace tokens with AND");
    require (library.query ("warm impossible", {}, {}, false, PresetSort::name).empty(),
             "Search returned a preset that lacked one of its tokens");
    const auto filtered = library.query ("warm", "pAdS", "uSeR", false, PresetSort::name);
    require (filtered.size() == 1 && filtered.front().id == PresetLibrary::userId (pad),
             "Category/source/search filters were not combined case-insensitively");
    require (library.query ({}, {}, "factory", false, PresetSort::name).size() == 4,
             "Source filter did not isolate factory sounds");
    const auto unicodeResult = library.query (juce::String::fromUTF8 ("霞光"), {}, "user", false, PresetSort::name);
    require (unicodeResult.size() == 1 && unicodeResult.front().id == PresetLibrary::userId (unicode)
                 && unicodeResult.front().name == juce::String::fromUTF8 ("暖冬 霞光"),
             "Unicode file identity or search text was lost during indexing");
    const auto names = library.query ({}, {}, {}, false, PresetSort::name);
    for (size_t index = 1; index < names.size(); ++index)
        require (names[index - 1].name.compareIgnoreCase (names[index].name) <= 0,
                 "Name sort was not ascending and case-insensitive");
    const auto categories = library.query ({}, {}, {}, false, PresetSort::category);
    for (size_t index = 1; index < categories.size(); ++index)
    {
        const int categoryOrder = categories[index - 1].category.compareIgnoreCase (categories[index].category);
        require (categoryOrder < 0 || (categoryOrder == 0
                    && categories[index - 1].name.compareIgnoreCase (categories[index].name) <= 0),
                 "Category sort did not order names within each category");
    }
    require (library.setFavourite (PresetLibrary::userId (pad), true).wasOk(), "Could not mark query fixture as favourite");
    require (library.query ("warm", "pads", "user", true, PresetSort::name).size() == 1
                 && library.query ("warm", {}, "factory", true, PresetSort::name).empty(),
             "Favourite restriction did not combine with the other filters");
    const juce::StringArray mixedCase { "Pad / Zebra", "pad / alpha", "PAD / Middle" };
    PresetLibrary mixed (mixedCase, files.base.getChildFile ("Empty"), files.base.getChildFile ("Mixed.xml"));
    finishScan (mixed);
    const auto sorted = mixed.query ({}, "pAd", {}, false, PresetSort::category);
    require (sorted.size() == 3 && sorted[0].name == "alpha" && sorted[1].name == "Middle"
                 && sorted[2].name == "Zebra", "Category case differences took precedence over name ordering");
}

void testRefreshMissingAndUnparsedFiles()
{
    Sandbox files;
    const auto missing = files.library.getChildFile ("NotCreated");
    PresetLibrary library (factoryNames, missing, files.settings);
    finishScan (library);
    require (library.getEntries().size() == 4, "Missing user directory hid factory sounds or created ghost entries");
    require (missing.createDirectory().wasOk(), "Could not create formerly missing library");
    const auto empty = missing.getChildFile ("Empty.anyvoc");
    const auto malformed = missing.getChildFile ("Malformed.anyvoc");
    require (empty.replaceWithText ({}) && malformed.replaceWithText ("<ANYVOC incomplete"),
             "Could not create unparsed preset fixtures");
    refresh (library);
    const auto entries = library.getEntries();
    require (entries.size() == 6 && entry (entries, PresetLibrary::userId (empty)).name == "Empty"
                 && entry (entries, PresetLibrary::userId (malformed)).name == "Malformed",
             "Index parsed or rejected file contents instead of indexing filenames for explicit load");
    const auto revision = library.getRevision();
    require (empty.deleteFile() && malformed.deleteFile(), "Could not delete preset scan fixtures");
    for (int request = 0; request < 20; ++request) library.refresh();
    finishScan (library);
    require (library.getEntries().size() == 4 && library.getRevision() > revision,
             "Coalesced refresh did not publish deleted-file removal");
}

void testFavouritePersistenceAndMerging()
{
    Sandbox files;
    const auto user = files.write ("Pads/Long Memory.anyvoc");
    const auto first = PresetLibrary::factoryId (factoryNames[1]);
    const auto second = PresetLibrary::userId (user);
    PresetLibrary a (factoryNames, files.library, files.settings);
    PresetLibrary b (factoryNames, files.library, files.settings);
    finishScan (a); finishScan (b);
    require (a.setFavourite (first, true).wasOk() && b.setFavourite (second, true).wasOk(),
             "Favourite writes failed from two live library instances");
    {
        PresetLibrary reopened (factoryNames, files.library, files.settings);
        finishScan (reopened);
        require (favourites (reopened).size() == 2,
                 "A stale instance overwrote another instance's saved favourite");
    }
    require (a.setFavourite (first, false).wasOk(), "Could not remove one favourite");
    refresh (b);
    const auto merged = favourites (b);
    require (merged.size() == 1 && merged.front().id == second,
             "Removing one favourite erased another or refresh did not see another instance's change");
    require (user.deleteFile(), "Could not temporarily remove favourited preset");
    refresh (a);
    require (favourites (a).empty(), "Deleted user preset remained selectable as a favourite");
    files.write ("Pads/Long Memory.anyvoc");
    refresh (a);
    require (favourites (a).size() == 1 && favourites (a).front().id == second,
             "Temporarily missing preset lost its persisted favourite identity");
    require (a.setFavourite ({}, true).failed() && a.setFavourite ("unknown:id", true).failed(),
             "An invalid or unindexed ID was accepted as a new favourite");
}

void testFavouriteFailureRollback()
{
    Sandbox files;
    const auto first = PresetLibrary::factoryId (factoryNames[1]);
    const auto second = PresetLibrary::factoryId (factoryNames[2]);
    const auto blocked = files.base.getChildFile ("blocked.xml");
    require (blocked.createDirectory().wasOk(), "Could not create unwritable settings fixture");
    PresetLibrary unwritable (factoryNames, files.library, blocked);
    finishScan (unwritable);
    require (unwritable.setFavourite (first, true).failed() && favourites (unwritable).empty()
                 && blocked.isDirectory(), "Failed settings write changed favourites or replaced the obstructing directory");

    PresetLibrary library (factoryNames, files.library, files.settings);
    finishScan (library);
    require (library.setFavourite (first, true).wasOk(), "Could not create valid settings before corruption");
   #if JUCE_WINDOWS
    const auto persisted = files.settings.loadFileAsString();
    {
        const auto locked = files.settings.createOutputStream();
        require (locked != nullptr && library.setFavourite (second, true).failed(),
                 "Locked settings destination did not report an atomic replacement failure");
        const auto retained = favourites (library);
        require (retained.size() == 1 && retained.front().id == first,
                 "Failed settings replacement published an unsaved favourite");
    }
    require (files.settings.loadFileAsString() == persisted,
             "Failed favourite replacement damaged existing settings bytes");
   #endif
    const juce::String corrupt = "<favourites invalid and deliberately unfinished";
    require (files.settings.replaceWithText (corrupt), "Could not corrupt isolated settings fixture");
    require (library.setFavourite (second, true).failed(), "Corrupt settings were silently overwritten");
    const auto retained = favourites (library);
    require (retained.size() == 1 && retained.front().id == first && files.settings.loadFileAsString() == corrupt,
             "Failed settings update changed memory or destroyed the existing settings file");
    refresh (library);
    require (favourites (library).size() == 1,
             "A failed settings reread discarded the last known favourites");
}

void testDirectoryBoundsAndLinks()
{
    Sandbox files;
    files.write ("Visible.anyvoc");
    juce::String deep;
    for (int depth = 0; depth < 12; ++depth) deep += "d/";
    const auto tooDeep = files.write (deep + "OutsideDepth.anyvoc");
    const auto external = files.base.getChildFile ("OutsideLibrary");
    require (external.createDirectory().wasOk()
                 && external.getChildFile ("Outside.anyvoc").replaceWithText ("unused"),
             "Could not create external link fixture");
    const auto link = files.library.getChildFile ("LinkedDirectory");
    const bool linked = external.createSymbolicLink (link, false);
    {
        PresetLibrary library (factoryNames, files.library, files.settings);
        finishScan (library);
        const auto entries = library.getEntries();
        require (entries.size() == 5, "Scan traversed beyond its depth bound or followed an external directory link");
        require (std::none_of (entries.begin(), entries.end(), [&] (const auto& item)
                 { return item.file == tooDeep || item.name == "Outside"; }),
                 "Bounded library scan exposed a file outside its traversal boundary");
    }
    if (linked) require (link.deleteFile(), "Could not remove temporary directory link");
    else std::cout << "INFO Symbolic-link creation unavailable; depth boundary was still verified\n";
}
}

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests {
        { "Recursive index, immediate factory sounds and stable IDs", testIndexAndStableIdentity },
        { "Combined search, filters, favourites and deterministic sorting", testQueryAndSort },
        { "Missing directories, unparsed files and coalesced refresh", testRefreshMissingAndUnparsedFiles },
        { "Favourite persistence and cross-instance merge", testFavouritePersistenceAndMerging },
        { "Favourite write/read failure preserves memory and disk", testFavouriteFailureRollback },
        { "Bounded recursion and external symbolic-link isolation", testDirectoryBoundsAndLinks }
    };
    int failures = 0;
    for (const auto& test : tests)
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    std::cout << "Preset library validation: " << tests.size() << " cases, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
