//
// Created by vastrakai on 6/24/2024.
//

#include "MinecraftGame.hpp"
#include <map>
#include <memory>
#include <Windows.h>
#include <spdlog/spdlog.h>
#include <libhat/Access.hpp>
#include <SDK/OffsetProvider.hpp>
#include <Utils/MemUtils.hpp>

#include "ClientInstance.hpp"
#include "InstanceLocator.hpp"

namespace {
    // A member offset used with hat::member_at() has to be a small positive
    // number. When a signature fails, the offset stays 0 and member_at() returns
    // the vtable pointer instead of the member -- reading a vtable as an object is
    // what crashed before. This gate keeps the fast path but never runs it with a
    // value that cannot be right.
    bool usableOffset(int offset)
    {
        return offset > 0 && offset < 0x8000;
    }

    // 3.6.2-E.1 (P0-2): read-only, VirtualQuery-guarded diagnosis of *why* the
    // virtual-call guard rejected. It never calls through `object` and never
    // dereferences a member; every read goes through MemUtils::isReadableRange
    // first, exactly like isValidVirtualCall itself. The vtable and target
    // addresses are logged ONLY when the reads that produced them succeeded, so a
    // rejected call can be explained post-mortem without risking a second fault.
    // Called only on the rejection path, so it emits nothing on the (common) pass.
    void diagnosePlayUiRejection(void* object, int index)
    {
        const DWORD tid = GetCurrentThreadId();
        if (!object) {
            spdlog::warn("[playUi] guard rejected: object is null | index={} threadId={}",
                         index, tid);
            return;
        }
        const uintptr_t obj = reinterpret_cast<uintptr_t>(object);
        if (!MemUtils::isReadableRange(obj, sizeof(uintptr_t))) {
            spdlog::warn("[playUi] guard rejected: object memory unreadable | this={:#X} index={} threadId={}",
                         obj, index, tid);
            return;
        }
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(obj);
        if (!MemUtils::isReadableRange(vtable, sizeof(uintptr_t))) {
            // `vtable` was read from a confirmed-readable 8-byte window, so it is
            // a valid address value to print; the window it points at is not.
            spdlog::warn("[playUi] guard rejected: vtable pointer unreadable | this={:#X} index={} vtable={:#X} threadId={}",
                         obj, index, vtable, tid);
            return;
        }
        const uintptr_t slot = vtable + static_cast<size_t>(index) * sizeof(uintptr_t);
        if (!MemUtils::isReadableRange(slot, sizeof(uintptr_t))) {
            spdlog::warn("[playUi] guard rejected: slot unreadable | this={:#X} index={} vtable={:#X} slot={:#X} threadId={}",
                         obj, index, vtable, slot, tid);
            return;
        }
        const uintptr_t target = *reinterpret_cast<const uintptr_t*>(slot);
        if (!MemUtils::isPatchableCode(target, 1)) {
            // `target` was read from a confirmed-readable slot window, so it is a
            // valid address value to print; it just is not executable code.
            spdlog::warn("[playUi] guard rejected: target not executable | this={:#X} index={} vtable={:#X} target={:#X} threadId={}",
                         obj, index, vtable, target, tid);
            return;
        }
        // Reached only if isValidVirtualCall disagreed with our own probes; keep a
        // fallback line rather than silently dropping the rejection.
        spdlog::warn("[playUi] guard rejected: unclassified (post-check) | this={:#X} index={} vtable={:#X} target={:#X} threadId={}",
                     obj, index, vtable, target, tid);
    }
}

MinecraftGame* MinecraftGame::getInstance()
{
    // One cache owner, validated through the known map/client/Sim layout. The
    // abandoned MainView chain and a first-qword vtable test are not game IDs.
    auto* game = InstanceLocator::locateGame();
    return InstanceLocator::isPlausibleGame(game) ? game : nullptr;
}

ClientInstance* MinecraftGame::getPrimaryClientInstance()
{
    return InstanceLocator::firstClientFromGame(this, OffsetProvider::MinecraftGame_mClientInstances);
}

UIProfanityContext* MinecraftGame::getProfanityContext()
{
    // SUSPECT in 1.21.120: this resolves to 1 (the 1.21.44 reference build gives
    // 400), so plausibleOffset() rejects it and leaves 0. Either way member_at()
    // would return the vtable pointer, which is why the gate is here and not at
    // the call sites.
    if (!usableOffset(OffsetProvider::MinecraftGame_mProfanityContext))
        return nullptr;
    return hat::member_at<UIProfanityContext*>(this, OffsetProvider::MinecraftGame_mProfanityContext);
}

bool MinecraftGame::getMouseGrabbed()
{
    // SUSPECT in 1.21.120 (-964358145): member_at() would read a wild address.
    if (!usableOffset(OffsetProvider::MinecraftGame_mMouseGrabbed))
        return false;
    return hat::member_at<bool>(this, OffsetProvider::MinecraftGame_mMouseGrabbed);
}

void MinecraftGame::setMouseGrabbed(bool grabbed)
{
    if (!usableOffset(OffsetProvider::MinecraftGame_mMouseGrabbed))
        return;
    hat::member_at<bool>(this, OffsetProvider::MinecraftGame_mMouseGrabbed) = grabbed;
}

void MinecraftGame::playUi(const std::string& soundName, float volume, float pitch)
{
    // SUSPECT in 1.21.120 (3238145): a vtable slot that far out is a jump into
    // unmapped memory. A sound is not worth a crash.
    int index = OffsetProvider::MinecraftGame_playUi;
    if (index < 2 || index >= 4096)
        return;
    // The slot was derived from a disp32 scan and divided by 8, so it is a
    // vtable index, not a byte offset. Before jumping through it, confirm the
    // object, its vtable, and the slot target are all real executable code.
    // This rejects a slot that resolved to the wrong function for the running
    // game version, or a stale/readable-but-detached MinecraftGame, instead of
    // taking an access violation inside callVirtualFunc.
    if (!MemUtils::isValidVirtualCall(this, index)) {
        // 3.6.2-E.1 (P0-2): explain *why* without masking the rejection. A
        // rejected playUi is an optional sound effect, so the skip is safe -- but
        // it must never be reported as a successful init step, and the reason has
        // to be auditable.
        diagnosePlayUiRejection(this, index);
        return;
    }
    MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
}
