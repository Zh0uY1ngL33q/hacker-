//
// Created by vastrakai on 7/10/2024.
//

#include "Level.hpp"

#include <libhat.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Actor/SyncedPlayerMovementSettings.hpp>

std::unordered_map<mce::UUID, PlayerListEntry>* Level::getPlayerList()
{
    static auto vIndex = OffsetProvider::Level_getPlayerList;
    // MISSING in 1.21.120; a virtual call through slot 0 (dtor) or a huge
    // index is a wild jump. Callers must tolerate null.
    if (vIndex < 2 || vIndex >= 4096)
        return nullptr;
    const auto map = MemUtils::callVirtualFunc<std::unordered_map<mce::UUID, PlayerListEntry>*>(vIndex, this);
    return InstanceLocator::isReadableMemory(map, 0x8) ? map : nullptr;
}

HitResult* Level::getHitResult()
{
    static auto vIndex = OffsetProvider::Level_getHitResult;
    // SUSPECT in 1.21.120 (-151183361): huge/negative vtable index.
    if (vIndex < 2 || vIndex >= 4096)
        return nullptr;
    return MemUtils::callVirtualFunc<HitResult*>(vIndex, this);
}

SyncedPlayerMovementSettings* Level::getPlayerMovementSettings()
{
    static auto vIndex = OffsetProvider::Level_getPlayerMovementSettings;
    return MemUtils::callVirtualFunc<SyncedPlayerMovementSettings*>(vIndex, this);
}

std::vector<Actor*> Level::getRuntimeActorList()
{
    static auto func = SigManager::Level_getRuntimeActorList;
    std::vector<Actor*> actors;
    MemUtils::callFastcall<void>(func, this, &actors);
    return actors;
}

LevelData* Level::getLevelData()
{
    static auto vIndex = OffsetProvider::Level_getLevelData;
    // Near-miss candidate in 1.21.120 was a shared_ptr release sequence, so the
    // offset stays unresolved; a virtual call through slot 0 is a wild jump.
    if (vIndex < 2 || vIndex >= 4096)
        return nullptr;
    return MemUtils::callVirtualFunc<LevelData*>(vIndex, this);
}

class BlockPalette* Level::getBlockPalette()
{
    static auto vIndex = OffsetProvider::Level_getBlockPalette;
    return MemUtils::callVirtualFunc<class BlockPalette*>(vIndex, this);
}
