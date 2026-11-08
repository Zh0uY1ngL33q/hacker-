//
// Created by vastrakai on 6/25/2024.
//

#include "MinecraftSim.hpp"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <libhat/Access.hpp>
#include <spdlog/spdlog.h>
#include <SDK/OffsetProvider.hpp>

namespace {

// Simulation is plain data, and the SDK's layout for it is the one thing about
// this class that is still trustworthy: mTimer at 0x00, mDeltaTime at 0x08,
// mTimerMultiplier at 0x0C, mTime at 0x18. While the game runs at normal speed
// the multiplier and the partial-tick value both read exactly 1.0f and
// mDeltaTime stays inside one tick, which together is specific enough to
// recognise a Simulation from a bare pointer.
bool looksLikeSimulation(uintptr_t address)
{
    if (address < 0x10000) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    const auto* raw = reinterpret_cast<const float*>(address);
    const float timer = raw[0];
    const float delta = raw[2];
    const float multiplier = raw[3];
    const float time = raw[6];
    return std::fabs(multiplier - 1.f) <= 1e-4f
        && std::fabs(time - 1.f) <= 1e-4f
        && delta >= 0.f && delta <= 0.5f
        && timer >= 0.f && timer <= 40.f;
}

constexpr int kScanBytes = 0x200;
constexpr int kDumpFirst = 0xC0;
constexpr int kDumpLast = 0x100;

// -2 = not probed, -1 = no candidate found, otherwise a byte offset into the
// MinecraftSim object.
std::atomic<int> sGameSimOffset{ -2 };
std::atomic<int> sRenderSimOffset{ -2 };
std::atomic<unsigned long long> sLastProbeTick{ 0 };
bool sLayoutDumped = false;
std::mutex sProbeMutex;

// A failed probe used to be final. The first caller is whichever module reads the
// simulation clock first -- LevelInfo and Nametags both call getSimTimer() from
// their RenderEvent, i.e. on the very first rendered frame -- and at that point
// the world does not exist yet, so the paired Simulation pointers are not there.
// The failure was cached as -1 for the rest of the session, and every later
// setSimTimer() returned before writing anything. Timer, Speed, Fly, LongJump and
// the animation swing rate were therefore inert with a probe that was working
// correctly; it simply was not allowed to look again.
//
// Retry on a backoff instead of pinning a negative result. A success stays cached
// forever, because a located offset cannot become wrong while the object lives.
constexpr unsigned long long kProbeRetryMs = 2000;

void dumpLayout(const void* self, size_t available)
{
    const auto base = reinterpret_cast<uintptr_t>(self);
    for (int offset = kDumpFirst; offset <= kDumpLast && static_cast<size_t>(offset) + 8 <= available; offset += 8)
    {
        const uintptr_t value = *reinterpret_cast<const uintptr_t*>(base + offset);
        if (value < 0x10000) continue;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(value), &mbi, sizeof(mbi)) != sizeof(mbi)) continue;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;

        const auto* asFloat = reinterpret_cast<const float*>(value);
        spdlog::info("[sim]   +0x{:03X} -> 0x{:X} f[0]={:.4f} f[2]={:.4f} f[3]={:.4f} f[6]={:.4f}",
                     offset, value,
                     static_cast<double>(asFloat[0]), static_cast<double>(asFloat[2]),
                     static_cast<double>(asFloat[3]), static_cast<double>(asFloat[6]));
    }
}

int resolveGameSimOffset(const void* self)
{
    std::lock_guard<std::mutex> lock(sProbeMutex);
    const int cached = sGameSimOffset.load(std::memory_order_relaxed);
    // A located offset is permanent.
    if (cached >= 0) return cached;
    // A previous failure is only final for the backoff window: the Simulation
    // pair appears once the world is created, which is later than the first
    // caller. See kProbeRetryMs.
    if (cached == -1)
    {
        const unsigned long long last = sLastProbeTick.load(std::memory_order_relaxed);
        if (last != 0 && GetTickCount64() - last < kProbeRetryMs) return -1;
    }
    sLastProbeTick.store(GetTickCount64(), std::memory_order_relaxed);

    const auto base = reinterpret_cast<uintptr_t>(self);
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(self, &mbi, sizeof(mbi)) != sizeof(mbi))
    {
        sGameSimOffset.store(-1, std::memory_order_relaxed);
        return -1;
    }

    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    const size_t available = regionEnd > base ? static_cast<size_t>(regionEnd - base) : 0;

    int found = -1;
    float foundTimer = 0.f;
    for (int offset = 0; offset + 16 <= kScanBytes && static_cast<size_t>(offset) + 16 <= available; offset += 8)
    {
        const uintptr_t first = *reinterpret_cast<const uintptr_t*>(base + offset);
        const uintptr_t second = *reinterpret_cast<const uintptr_t*>(base + offset + 8);
        if (!looksLikeSimulation(first) || !looksLikeSimulation(second)) continue;

        const float timer = *reinterpret_cast<const float*>(first);
        spdlog::info("[sim] Simulation pair at +0x{:03X}/+0x{:03X} timer={:.2f}", offset, offset + 8, static_cast<double>(timer));
        if (found < 0 || std::fabs(timer - 20.f) < std::fabs(foundTimer - 20.f))
        {
            found = offset;
            foundTimer = timer;
        }
    }

    if (found < 0)
    {
        // Nothing matched, so the object at hand is either not the MinecraftSim
        // the layout describes or the Simulation layout moved. Dump the fields
        // around the two offsets that are known independently (GameSession at
        // 0xD8 via its own signature, another object pointer at 0xF0 via the
        // getter at 0x1A25A30) so the next round starts from data. Dump once:
        // the probe now retries every kProbeRetryMs and would otherwise repeat
        // this block for the whole session.
        if (!sLayoutDumped)
        {
            sLayoutDumped = true;
            spdlog::warn("[sim] no Simulation pair in the first 0x{:X} bytes; known pointers:", kScanBytes);
            dumpLayout(self, available);
        }
        sGameSimOffset.store(-1, std::memory_order_relaxed);
        sRenderSimOffset.store(-1, std::memory_order_relaxed);
        return -1;
    }

    spdlog::info("[sim] MinecraftSim_mGameSim unresolved by signature, located at +0x{:03X} by structure", found);
    sGameSimOffset.store(found, std::memory_order_relaxed);
    sRenderSimOffset.store(found + 8, std::memory_order_relaxed);
    return found;
}

} // namespace

// MinecraftSim_mGameSim / mRenderSim do not resolve on 1.21.120, so both offsets
// are 0. hat::member_at() at offset 0 reads the first qword of the object, which
// is its vtable pointer: a non-null pointer to a function table, so every caller
// happily carries on and writes into it (setSimTimer does exactly that). Locate
// the two Sim pointers by structure instead, and return null only when that also
// fails.
Simulation* MinecraftSim::getGameSim()
{
    const int offset = OffsetProvider::MinecraftSim_mGameSim;
    if (offset > 0)
        return hat::member_at<Simulation*>(this, offset);

    const int probed = resolveGameSimOffset(this);
    if (probed < 0)
        return nullptr;
    return *reinterpret_cast<Simulation**>(reinterpret_cast<uintptr_t>(this) + probed);
}

Simulation* MinecraftSim::getRenderSim()
{
    const int offset = OffsetProvider::MinecraftSim_mRenderSim;
    if (offset > 0)
        return hat::member_at<Simulation*>(this, offset);

    // Probing happens through mGameSim: the two addresses are adjacent fields of
    // the same object, so one scan answers both.
    if (resolveGameSimOffset(this) < 0)
        return nullptr;
    const int probed = sRenderSimOffset.load(std::memory_order_relaxed);
    if (probed < 0)
        return nullptr;
    return *reinterpret_cast<Simulation**>(reinterpret_cast<uintptr_t>(this) + probed);
}

void MinecraftSim::setSimTimer(float timer)
{
    if (auto* game = getGameSim()) game->mTimer = timer;
    if (auto* render = getRenderSim()) render->mTimer = timer;
}

void MinecraftSim::setSimSpeed(float speed)
{
    if (auto* game = getGameSim()) game->mTimerMultiplier = speed;
    if (auto* render = getRenderSim()) render->mTimerMultiplier = speed;
}

float MinecraftSim::getSimTimer()
{
    auto* game = getGameSim();
    return game ? game->mTimer : 0.f;
}

float MinecraftSim::getSimSpeed()
{
    auto* game = getGameSim();
    return game ? game->mTimerMultiplier : 1.f;
}

GameSession* MinecraftSim::getGameSession()
{
    const int offset = OffsetProvider::MinecraftSim_mGameSession;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<GameSession*>(this, offset);
}
