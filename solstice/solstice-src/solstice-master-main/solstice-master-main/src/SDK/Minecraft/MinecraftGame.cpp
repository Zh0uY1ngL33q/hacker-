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
    // Solstice Full Integration -- MAPPING SEMANTICS vs CALL SAFETY.
    //
    // Batch-1 (517646e) proved the STATIC mapping for Minecraft 1.21.120.0:
    //   pattern hit 0x1CBC3E4 -> disp32 0x520 -> /8 -> vtable slot 164.
    // OffsetProvider.hpp therefore carries operand 10 and resolves to 164.
    // That is a MAPPING fact and is preserved as an evidence-backed SDK result.
    // It is NOT a statement that calling through slot 164 is safe.
    //
    // Still UNVERIFIED at runtime: the identity of the function behind slot
    // 164, the MinecraftGame object's lifetime and starting address, the playUi
    // thread constraint, and the running image's identity. 3.6.2-D/E observed
    // an unreadable vtable on a *located* MinecraftGame object
    // (this=0x13134090, vtable=0x3573F2CD48F1B9AF), so a bare virtual call
    // here is a known access-violation site, not a hypothetical one.
    //
    // b32d185 kept playUi inert (operand 3 -> 3238145 -> index >= 4096 ->
    // early return). Correcting the mapping must NOT silently turn that inert
    // call into an activated, unverified virtual call. Keep the call REFUSED by
    // default, matching the stable baseline's observable behaviour (playUi is
    // an optional sound effect; nothing in startup depends on it).
    //
    // Enabling it requires BOTH (a) runtime semantic validation of slot 164 on
    // the exact running image AND (b) the explicit opt-in below. Even then,
    // isValidVirtualCall must confirm the object, vtable, slot and executable
    // target are readable. No build target in this integration defines the
    // opt-in macro, so the released binary can never take the call path.
    const int index = OffsetProvider::MinecraftGame_playUi;
    if (index < 2 || index >= 4096)
        return;

#if defined(SOLSTICE_PLAYUI_SEMANTIC_VERIFIED)
    // Opt-in ONLY after runtime semantic validation has been recorded.
    if (!MemUtils::isValidVirtualCall(this, index))
        return;
    MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
#else
    (void)soundName;
    (void)volume;
    (void)pitch;
    return; // PLAYUI_SEMANTIC_VALIDATION: UNVERIFIED UNTIL PROVEN
#endif
}
