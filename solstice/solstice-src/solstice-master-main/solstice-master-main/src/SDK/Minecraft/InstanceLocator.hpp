#pragma once
//
// Runtime locator for MinecraftGame / ClientInstance.
//
// Exists because the MainView -> BedrockPlatformUWP -> MinecraftGame chain is
// described by struct offsets that changed in 1.21.120, and no signature can
// recover an offset. See InstanceLocator.cpp.
//

#include <cstdint>

class MinecraftGame;
class ClientInstance;

namespace InstanceLocator {
    /// True if `p` points at readable memory that starts with a game vtable.
    /// Use before dereferencing any pointer that came from a signature, because a
    /// missing offset makes hat::member_at() return a field, not a pointer.
    bool isPlausibleObject(const void* p);

    /// Committed, readable memory of at least `size` bytes. Use for pointers that
    /// do not necessarily start with a vtable (GuiData, Options, ...).
    bool isReadableMemory(const void* p, size_t size);

    /// Scan the heap for MinecraftGame's client-instance map and return the first
    /// ClientInstance. Cached after the first success; throttled after a failure.
    /// outDiagnostics (optional, 4 entries): regions, MB scanned, candidates, ms.
    ClientInstance* locate(MinecraftGame** outGame, uint32_t* outDiagnostics);

    /// Same scan, MinecraftGame only.
    MinecraftGame* locateGame();
}
