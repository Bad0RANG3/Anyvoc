#pragma once

// JUCE also compiles C graphics sources. A forced include must be inert in C;
// keep target definitions language-independent for JUCE's generated Defs.txt.
#ifdef __cplusplus

// Diagnostic-only, force-included in the isolated JUCE build. This records the
// instrumented CRT calls, not every possible allocation in an audio callback.
#include <array>
#include <cstddef>
#include <limits>

namespace anyvoc::allocation_probe
{
enum class Site { heapMalloc, heapCalloc, heapRealloc, midiMessageMalloc };

struct Event
{
    Site site {};
    std::size_t bytes {};
    const void* before {};
    const void* after {};
    const void* midiBuffer {};
};

struct Capture
{
    std::array<Event, 8192> events {};
    std::size_t count = 0;
    std::size_t overflow = 0;

    // Old entries beyond count are deliberately ignored; no heap or logging.
    void clear() noexcept { count = 0; overflow = 0; }
};

inline thread_local Capture* activeCapture = nullptr;
inline thread_local const void* activeMidiBuffer = nullptr;

class CaptureScope
{
public:
    explicit CaptureScope(Capture& capture) noexcept : previous(activeCapture)
    {
        activeCapture = &capture;
    }
    ~CaptureScope() noexcept { activeCapture = previous; }
    CaptureScope(const CaptureScope&) = delete;
    CaptureScope& operator=(const CaptureScope&) = delete;

private:
    Capture* previous;
};

class MidiBufferScope
{
public:
    explicit MidiBufferScope(const void* buffer) noexcept : previous(activeMidiBuffer)
    {
        activeMidiBuffer = buffer;
    }
    ~MidiBufferScope() noexcept { activeMidiBuffer = previous; }
    MidiBufferScope(const MidiBufferScope&) = delete;
    MidiBufferScope& operator=(const MidiBufferScope&) = delete;

private:
    const void* previous;
};

inline void record(Site site, std::size_t bytes, const void* before, const void* after) noexcept
{
    if (auto* capture = activeCapture)
    {
        if (capture->count < capture->events.size())
            capture->events[capture->count++] = { site, bytes, before, after, activeMidiBuffer };
        else if (capture->overflow != (std::numeric_limits<std::size_t>::max)())
            ++capture->overflow;
    }
}
} // namespace anyvoc::allocation_probe

#endif // __cplusplus
