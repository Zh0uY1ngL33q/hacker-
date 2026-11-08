//
// Created by vastrakai on 6/24/2024.
//

#include "MinecraftGame.hpp"
#include <map>
#include <memory>
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
    if (!MemUtils::isValidVirtualCall(this, index))
        return;
    MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
}
