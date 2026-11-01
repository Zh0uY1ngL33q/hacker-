#pragma once

#include <cstdint>
//
// Created by vastrakai on 6/28/2024.
//


#include <complex.h>
#include <vector>
#include <SDK/Minecraft/Actor/ActorType.hpp>
#include <SDK/Minecraft/Network/Packets/InventoryTransactionPacket.hpp>

class ActorUtils {
public:
    // Monotonic counter bumped once per local-player tick (see BaseTickHook).
    // Used to memoise the entity list: on 1.21.120 the RuntimeActorList getter is
    // permanently unavailable, so every call falls back to walking the whole entt
    // registry and doing one VirtualQuery (isValidPtr) per entity - and several
    // modules call it every tick.
    static inline uint64_t gGameTick = 0;

    static std::vector<class Actor*> getActorList(bool playerOnly = true, bool excludeBots = true);
    static std::vector<Actor*> getActorsOfType(ActorType type);
    template <typename T = Actor>
    static std::vector<T*> getActorsTyped(ActorType type) {
        const std::vector<Actor*> actors = getActorsOfType(type);
        std::vector<T*> result;
        for (auto actor : actors) {
            result.push_back(static_cast<T*>(actor));
        }
        return result;
    }
    static bool isBot(Actor* actor);
    static std::shared_ptr<InventoryTransactionPacket> createAttackTransaction(Actor* actor, int slot = -1);
    static Actor* getActorFromUniqueId(int64_t uniqueId);
    static Actor* getActorFromRuntimeID(int64_t runtimeId);
};
