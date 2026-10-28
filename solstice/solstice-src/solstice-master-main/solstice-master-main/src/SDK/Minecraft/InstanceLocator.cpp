//
// Locate MinecraftGame / ClientInstance from the heap, not from signatures.
//
// On 1.21.120 the whole MainView -> BedrockPlatformUWP -> MinecraftGame chain
// changed shape. The two failing entries are *struct offsets* (a member of
// MainView and a member of BedrockPlatformUWP), so no byte pattern can recover
// them: the signature finds code that touches the member, and that code is still
// there, but the member moved. Guessing an offset is worse than failing, because
// hat::member_at() will happily read a field that is not a pointer and hand back
// garbage -- exactly what crashed at Solstice.dll+15DF7B.
//
// So this file stops asking the game where its objects are and reads them out of
// its heap instead. MinecraftGame holds
//
//     std::map<uint8_t, std::shared_ptr<ClientInstance>> mClientInstances
//
// at an offset that is still known (OffsetProvider::MinecraftGame_mClientInstances,
// recovered from code that iterates the map -- it is unchanged at 0xB60 in this
// build). An MSVC std::map is self-describing:
//
//     map + 0x00 : _Myhead          (points at the sentinel node)
//     map + 0x08 : _Mysize
//     node + 0x00 : _Left   node + 0x08 : _Parent   node + 0x10 : _Right
//     node + 0x18 : _Color (1 == black)
//     node + 0x19 : _Isnil (1 for the sentinel)
//
// For a non-empty map: head->_Parent is the root, root->_Parent is the head,
// head->_Left is the leftmost node and the leftmost node has no left child. The
// first node's value is the shared_ptr<ClientInstance>.
//
// Every step is validated and the scan gives up instead of dereferencing
// something unverified, so a wrong assumption costs a log line, not a crash.
//

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <SDK/Minecraft/MinecraftGame.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/OffsetProvider.hpp>
#include <Solstice.hpp>

#include "InstanceLocator.hpp"

namespace {

    struct Region {
        uintptr_t begin;
        uintptr_t end;
    };

    bool readableIn(const Region* regs, size_t count, uintptr_t address, size_t size)
    {
        if (count == 0 || address < regs[0].begin)
            return false;

        size_t lo = 0, hi = count;
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (regs[mid].begin <= address)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == 0)
            return false;

        const Region& r = regs[lo - 1];
        return address >= r.begin && address + size <= r.end;
    }

    bool isMainModulePointer(const void* p);
    uintptr_t mainModuleBase();
    uintptr_t mainModuleEnd();

    struct Found {
        MinecraftGame* game;
        ClientInstance* client;
        uintptr_t mapAddress;
        uint32_t scannedMb;
        uint32_t candidates;
        bool simValidated;
        uint32_t treesSeen;    // maps whose sentinel/size invariants held
        uint32_t clientsSeen;  // candidates whose value looked like a ClientInstance
        uint32_t gamesSeen;    // MinecraftGame candidates (incl. empty-map at menu)
        uint32_t rttiSeen;     // empty-map games whose RTTI name matched
        MinecraftGame* gameRtti;  // RTTI-confirmed MinecraftGame (may be null)
        uintptr_t rttiMapAddress;
    };

    struct Candidate {
        MinecraftGame* game;
        ClientInstance* client;
        uintptr_t map;
        bool simOk;
    };

    // MSVC RTTI, read at runtime: vtable[-1] holds a pointer to a
    // CompleteObjectLocator, whose pTypeDescriptor RVA (relative to the module
    // base) names the class. Decisive for separating the real MinecraftGame
    // from lookalikes that merely hold an empty std::map at the same offset.
    // Returns 1 = name matches, 0 = readable but different, -1 = unreadable.
    int rttiClassIs(const Region* regs, size_t count, uintptr_t vtable,
                    const char* needle)
    {
        const uintptr_t base = mainModuleBase();
        const uintptr_t colAddr = vtable - sizeof(void*);
        if (!base || vtable < 8)
            return -1;
        if (!readableIn(regs, count, colAddr, sizeof(void*)))
            return -1;
        const uintptr_t col = *reinterpret_cast<const uintptr_t*>(colAddr);
        if (!readableIn(regs, count, col, 0x10))
            return -1;
        // x64 COL: signature, offset, cdOffset, pTypeDescriptor (RVA), ...
        const uint32_t tdRva = *reinterpret_cast<const uint32_t*>(col + 0x0C);
        if (tdRva == 0 || tdRva > 0x2000000ull)
            return 0;
        const uintptr_t td = base + tdRva;
        // TypeDescriptor: vfptr, spare, then the decorated name.
        if (!readableIn(regs, count, td, 0x10))
            return 0;
        char name[72];
        const uintptr_t nameAddr = td + 0x10;
        size_t n = 0;
        for (; n < sizeof(name) - 1; ++n) {
            if (!readableIn(regs, count, nameAddr + n, 1))
                return n == 0 ? -1 : 0;
            name[n] = *reinterpret_cast<const char*>(nameAddr + n);
            if (name[n] == '\0')
                break;
        }
        name[sizeof(name) - 1] = '\0';
        if (!strstr(name, needle)) {
            // One-shot diagnostics: what does this build's RTTI actually name?
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true) && Solstice::console)
                Solstice::console->warn("rtti probe: candidate class name: {}", name);
        }
        return strstr(name, needle) ? 1 : 0;
    }

    // No C++ objects with destructors in here: MSVC refuses __try in such a
    // function, and the whole point is to survive a stale region.
    Found scanRegions(const Region* regs, size_t count, int mapOffset, int simOffset)
    {
        Found found{};
        found.scannedMb = 0;
        const uintptr_t lowest = count ? regs[0].begin : 0;
        const uintptr_t highest = count ? regs[count - 1].end : 0;

        constexpr size_t kMaxCandidates = 8;
        Candidate candidates[kMaxCandidates] = {};
        size_t candidateCount = 0;

        for (size_t i = 0; i < count; ++i) {
            // One validated candidate is enough; the sim check only exists to
            // prefer the *right* one when several maps match.
            bool haveValidated = false;
            for (size_t c = 0; c < candidateCount; ++c)
                haveValidated = haveValidated || candidates[c].simOk;
            if (haveValidated)
                break;

            const Region& r = regs[i];
            found.scannedMb += static_cast<uint32_t>((r.end - r.begin) / (1024 * 1024));

            __try {
                for (uintptr_t map = (r.begin + 7) & ~static_cast<uintptr_t>(7);
                     map + 0x10 <= r.end; map += 8)
                {
                    const uintptr_t head = *reinterpret_cast<const uintptr_t*>(map);
                    // Cheap rejection before the binary search: the sentinel is
                    // an aligned pointer into the same private memory we walk.
                    if ((head & 7) || head < lowest || head >= highest)
                        continue;
                    if (!readableIn(regs, count, head, 0x40))
                        continue;
                    // The sentinel is a black, nil node.
                    if (*reinterpret_cast<const unsigned char*>(head + 0x18) != 1)
                        continue;
                    if (*reinterpret_cast<const unsigned char*>(head + 0x19) != 1)
                        continue;

                    const uint64_t size = *reinterpret_cast<const uint64_t*>(map + 0x08);
                    if (size > 0x2000)
                        continue;
                    ++found.treesSeen;

                    const uintptr_t root = *reinterpret_cast<const uintptr_t*>(head + 0x08);
                    if (!readableIn(regs, count, root, 0x20))
                        continue;
                    if (*reinterpret_cast<const uintptr_t*>(root + 0x08) != head)
                        continue;
                    if (size == 0) {
                        // Empty map: still tells us where the game is (the sentinel
                        // exists as soon as the ctor runs). Record it, because the
                        // title screen is a legitimate place to inject.
                        auto* game = reinterpret_cast<MinecraftGame*>(map - mapOffset);
                        if (!readableIn(regs, count, reinterpret_cast<uintptr_t>(game), 0x10))
                            continue;
                        const uintptr_t gameVtable = *reinterpret_cast<const uintptr_t*>(game);
                        if (!isMainModulePointer(reinterpret_cast<const void*>(gameVtable)))
                            continue;
                        const int rtti = rttiClassIs(regs, count, gameVtable, "MinecraftGame");
                        if (rtti == 1) {
                            // A named MinecraftGame beats any structural lookalike.
                            ++found.rttiSeen;
                            found.gameRtti = game;
                            found.rttiMapAddress = map;
                        } else if (!found.game) {
                            // Barely trusted fallback; superseded by gameRtti.
                            ++found.gamesSeen;
                            found.game = game;
                            found.mapAddress = map;
                        }
                        continue;
                    }

                    const uintptr_t first = *reinterpret_cast<const uintptr_t*>(head);
                    if (!readableIn(regs, count, first, 0x40))
                        continue;
                    // Leftmost node of an MSVC tree has no left child.
                    if (*reinterpret_cast<const uintptr_t*>(first) != 0)
                        continue;

                    // pair<const uint8_t, shared_ptr<ClientInstance>>: the key is
                    // one byte, so the pointer is 8-byte aligned at +0x28 in the
                    // standard layout, but +0x20 is also tried so a compiler that
                    // packs the pair differently still works.
                    const size_t valueOffsets[2] = { 0x28, 0x20 };
                    ClientInstance* client = nullptr;
                    for (size_t k = 0; k < 2 && !client; ++k) {
                        const uintptr_t candidate =
                            *reinterpret_cast<const uintptr_t*>(first + valueOffsets[k]);
                        if (!readableIn(regs, count, candidate, 0x10))
                            continue;
                        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(candidate);
                        if (!isMainModulePointer(reinterpret_cast<const void*>(vtable)))
                            continue;

                        // shared_ptr keeps its control block right next to the
                        // pointer, and the control block also starts with a vtable
                        // from the game image. Two unrelated pointers in a row are a
                        // coincidence; two with game vtables are a shared_ptr.
                        const uintptr_t control =
                            *reinterpret_cast<const uintptr_t*>(first + valueOffsets[k] + 8);
                        if (!readableIn(regs, count, control, 0x10))
                            continue;
                        const uintptr_t controlVtable = *reinterpret_cast<const uintptr_t*>(control);
                        if (!isMainModulePointer(reinterpret_cast<const void*>(controlVtable)))
                            continue;

                        client = reinterpret_cast<ClientInstance*>(candidate);
                    }
                    if (!client)
                        continue;
                    ++found.clientsSeen;

                    auto* game = reinterpret_cast<MinecraftGame*>(map - mapOffset);
                    // Not every candidate game is polymorphic at offset 0 (the
                    // first qword need not be a vtable), so no vtable test here:
                    // the client above was already validated as a shared_ptr
                    // pair plus a live MinecraftSim, which is what counts. The
                    // RTTI check is a bonus counter only.
                   ++found.candidates;
                    if (candidateCount >= kMaxCandidates)
                        continue;
                    {
                        bool simOk = false;
                        if (simOffset > 0 && simOffset < 0x8000) {
                            const uintptr_t simField = reinterpret_cast<uintptr_t>(client) + simOffset;
                            if (readableIn(regs, count, simField, 8)) {
                                const uintptr_t sim = *reinterpret_cast<const uintptr_t*>(simField);
                                if (readableIn(regs, count, sim, 0x10) &&
                                    isMainModulePointer(reinterpret_cast<const void*>(
                                        *reinterpret_cast<const uintptr_t*>(sim))))
                                {
                                    simOk = true;
                                }
                            }
                        }
                        candidates[candidateCount++] = { game, client, map, simOk };
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                // The region changed while we walked it (page freed or
                // decommitted). Nothing here is worth crashing the game for.
                continue;
            }
        }

        // Prefer a candidate that also matched the MinecraftSim member; fall back
        // to the first structurally valid map otherwise.
        for (size_t c = 0; c < candidateCount; ++c) {
            if (candidates[c].simOk) {
                found.game = candidates[c].game;
                found.client = candidates[c].client;
                found.mapAddress = candidates[c].map;
                found.simValidated = true;
                return found;
            }
        }
        if (candidateCount > 0) {
            found.game = candidates[0].game;
            found.client = candidates[0].client;
            found.mapAddress = candidates[0].map;
        }
        return found;
    }

    std::vector<Region> collectRegions()
    {
        std::vector<Region> regions;
        regions.reserve(1024);

        uintptr_t address = 0x10000;
        MEMORY_BASIC_INFORMATION mbi{};
        while (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) == sizeof(mbi)) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t end = base + mbi.RegionSize;

            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.RegionSize >= 0x1000) {
                const DWORD prot = mbi.Protect;
                const bool usable =
                    !(prot & PAGE_GUARD) && !(prot & PAGE_NOACCESS) &&
                    (prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
                if (usable) {
                    regions.push_back({ base, end });
                }
            }

            if (end <= address) // defensive: VirtualQuery must make progress
                break;
            address = end;
        }

        std::sort(regions.begin(), regions.end(),
                  [](const Region& a, const Region& b) { return a.begin < b.begin; });
        return regions;
    }

    uintptr_t mainModuleBase()
    {
        static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        return base;
    }

    uintptr_t mainModuleEnd()
    {
        static const uintptr_t end = [] {
            const uintptr_t base = mainModuleBase();
            if (!base)
                return uintptr_t(0);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            return base + nt->OptionalHeader.SizeOfImage;
        }();
        return end;
    }

    bool isMainModulePointer(const void* p)
    {
        const uintptr_t value = reinterpret_cast<uintptr_t>(p);
        const uintptr_t base = mainModuleBase();
        const uintptr_t end = mainModuleEnd();
        return base != 0 && value >= base && value < end;
    }

    bool isReadable(const void* p, size_t size)
    {
        if (!p)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD) || mbi.Protect == 0)
            return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        return reinterpret_cast<uintptr_t>(p) + size <= base + mbi.RegionSize;
    }

} // namespace

namespace InstanceLocator {

    bool isReadableMemory(const void* p, size_t size)
    {
        return isReadable(p, size);
    }

    bool isPlausibleObject(const void* p)
    {
        // A game object starts with its vtable pointer, which lives in the game's
        // own image. A field that is not an object (an offset, a flag, a string)
        // fails this, which is what turns "silent garbage" into "not found".
        if (!isReadable(p, sizeof(void*)))
            return false;
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(p);
        return isMainModulePointer(reinterpret_cast<const void*>(vtable));
    }

    ClientInstance* locate(MinecraftGame** outGame, uint32_t* outDiagnostics)
    {
        static std::mutex mutex;
        static std::chrono::steady_clock::time_point lastAttempt{};
        static std::chrono::steady_clock::time_point lastFullScan{};
        static std::atomic<ClientInstance*> cachedClient{ nullptr };
        static std::atomic<MinecraftGame*> cachedGame{ nullptr };

        if (ClientInstance* cached = cachedClient.load(std::memory_order_acquire)) {
            if (outGame)
                *outGame = cachedGame.load(std::memory_order_acquire);
            return cached;
        }

        std::lock_guard<std::mutex> lock(mutex);
        if (ClientInstance* cached = cachedClient.load(std::memory_order_acquire)) {
            if (outGame)
                *outGame = cachedGame.load(std::memory_order_acquire);
            return cached;
        }

        const int mapOffset = OffsetProvider::MinecraftGame_mClientInstances;
        if (mapOffset <= 0 || mapOffset > 0x8000)
            return nullptr;
        const int simOffset = OffsetProvider::ClientInstance_mMinecraftSim;

        const auto now = std::chrono::steady_clock::now();

        // Cheap path: a known MinecraftGame is revalidated and its map read in
        // microseconds. A full 500 MB heap scan synchronously on the caller's
        // (a game) thread froze the client for 7-12 s, so it must stay rare.
        if (MinecraftGame* known = cachedGame.load(std::memory_order_acquire)) {
            // A MinecraftGame need not start with a vtable, so revalidation is
            // readability, not isPlausibleObject.
            if (isReadable(reinterpret_cast<const void*>(known), 0x40)) {
                if (ClientInstance* c = firstClientFromGame(known, mapOffset)) {
                    cachedClient.store(c, std::memory_order_release);
                    if (outGame)
                        *outGame = known;
                    return c;
                }
                // Still no client. The map fills only once a world is joined;
                // retry the cheap path and only pay for a full scan if this
                // game never produces one within the window below.
                if (outGame)
                    *outGame = known;
                if (now - lastFullScan < std::chrono::seconds(180))
                    return nullptr;
            } else {
                cachedGame.store(nullptr, std::memory_order_release);
            }
        }

        // Full-scan throttle: at most every 10 s without a cached game.
        if (lastAttempt.time_since_epoch().count() != 0 && now - lastAttempt < std::chrono::seconds(10))
            return nullptr;
        lastAttempt = now;

        std::vector<Region> regions = collectRegions();

        const auto started = std::chrono::steady_clock::now();
        const Found found = scanRegions(regions.data(), regions.size(), mapOffset, simOffset);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        lastFullScan = now;

        if (outDiagnostics) {
            outDiagnostics[0] = static_cast<uint32_t>(regions.size());
            outDiagnostics[1] = found.scannedMb;
            outDiagnostics[2] = found.candidates;
            outDiagnostics[3] = static_cast<uint32_t>(elapsedMs);
        }

        // Prefer the RTTI-confirmed game; a structural lookalike is only a
        // fallback.
        const MinecraftGame* reportedGame = found.gameRtti ? found.gameRtti : found.game;
        if (!found.client) {
            if (Solstice::console) {
                Solstice::console->warn(
                    "heap scan: no ClientInstance yet ({} regions, {} MB, {} ms) "
                    "trees={} clients={} games={} rtti={} game@{}",
                    regions.size(), found.scannedMb, elapsedMs,
                    found.treesSeen, found.clientsSeen, found.gamesSeen,
                    found.rttiSeen, reinterpret_cast<uintptr_t>(reportedGame));
            }
            // Cache whichever game we report (RTTI-confirmed preferred): the
            // cheap path costs microseconds per call, and full scans then
            // happen only every 180 s instead of every 30.
            if (reportedGame)
                cachedGame.store(const_cast<MinecraftGame*>(reportedGame), std::memory_order_release);
            if (outGame && reportedGame)
                *outGame = const_cast<MinecraftGame*>(reportedGame);
            return nullptr;
        }

        cachedGame.store(found.game, std::memory_order_release);
        cachedClient.store(found.client, std::memory_order_release);
        if (outGame)
            *outGame = found.game;

        if (Solstice::console) {
            Solstice::console->info(
                "heap scan: MinecraftGame 0x{:X} (map at 0x{:X}), ClientInstance 0x{:X} "
                "({} regions, {} MB, {} candidate(s), sim check {}, {} ms)",
                reinterpret_cast<uintptr_t>(found.game), found.mapAddress,
                reinterpret_cast<uintptr_t>(found.client), regions.size(),
                found.scannedMb, found.candidates,
                found.simValidated ? "ok" : "n/a", elapsedMs);
        }
        return found.client;
    }

    MinecraftGame* locateGame()
    {
        MinecraftGame* game = nullptr;
        locate(&game, nullptr);
        return game;
    }

    ClientInstance* firstClientFromGame(MinecraftGame* game, int mapOffset)
    {
        // Re-validate the map at deref time and walk the first node with raw
        // pointer math (no std::map object, hence no destructor, hence legal
        // __try) so a page that dies between the big scan and this call is
        // caught by the handler instead of crashing the game.
        const uintptr_t map = reinterpret_cast<uintptr_t>(game) + static_cast<uintptr_t>(mapOffset);
        __try {
            if (!isReadable(reinterpret_cast<const void*>(map), 0x10))
                return nullptr;

            const uintptr_t head = *reinterpret_cast<const uintptr_t*>(map);
            if (!isReadable(reinterpret_cast<const void*>(head), 0x20))
                return nullptr;
            if (*reinterpret_cast<const unsigned char*>(head + 0x19) != 1) // sentinel isnil
                return nullptr;

            const uintptr_t root = *reinterpret_cast<const uintptr_t*>(head + 0x08);
            if (!isReadable(reinterpret_cast<const void*>(root), 0x20))
                return nullptr;
            if (*reinterpret_cast<const uintptr_t*>(root + 0x08) != head)
                return nullptr;

            const uintptr_t first = *reinterpret_cast<const uintptr_t*>(head);
            if (!isReadable(reinterpret_cast<const void*>(first), 0x40))
                return nullptr;
            if (*reinterpret_cast<const unsigned char*>(first + 0x19) == 1)
                return nullptr; // empty map
            if (*reinterpret_cast<const uintptr_t*>(first) != 0)
                return nullptr; // not the leftmost node

            const size_t valueOffsets[2] = { 0x28, 0x20 };
            for (size_t k = 0; k < 2; ++k) {
                const uintptr_t candidate =
                    *reinterpret_cast<const uintptr_t*>(first + valueOffsets[k]);
                if (!isReadable(reinterpret_cast<const void*>(candidate), 0x10))
                    continue;
                const uintptr_t control =
                    *reinterpret_cast<const uintptr_t*>(first + valueOffsets[k] + 8);
                if (!isReadable(reinterpret_cast<const void*>(control), 0x10))
                    continue;
                return reinterpret_cast<ClientInstance*>(candidate);
            }
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

} // namespace InstanceLocator
