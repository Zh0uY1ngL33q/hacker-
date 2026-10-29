//
// Created by vastrakai on 7/8/2024.
//

#include "ActorRenderDispatcherHook.hpp"
#include <Features/Events/ActorRenderEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

#include "D3DHook.hpp"

std::unique_ptr<Detour> ActorRenderDispatcherHook::mDetour;

void ActorRenderDispatcherHook::render(ActorRenderDispatcher* _this, BaseActorRenderContext* entityRenderContext,
    Actor* entity, glm::vec3* cameraTargetPos, glm::vec3* pos, glm::vec2* rot, bool ignoreLighting)
{
    auto oFunc = mDetour->getOriginal<&render>();
    auto* client = ClientInstance::get();
    auto* localPlayer = client ? client->getLocalPlayer() : nullptr;
    if (!localPlayer) {
        return oFunc(_this, entityRenderContext, entity, cameraTargetPos, pos, rot, ignoreLighting);
    }

    auto holder = nes::make_holder<ActorRenderEvent>(_this, entityRenderContext, entity, cameraTargetPos, pos, rot, ignoreLighting, mDetour.get());
    gFeatureManager->mDispatcher->trigger(holder);
    if (holder->isCancelled())
    {
        return;
    }

    return oFunc(_this, entityRenderContext, entity, cameraTargetPos, pos, rot, ignoreLighting);
}

void ActorRenderDispatcherHook::init()
{
    // The entry is recovered offline, but Interface and Freecam currently
    // consume unvalidated game objects/components in ActorRenderEvent. Do not
    // enable this detour until those event consumers are adapted and tested.
    if (SigManager::ActorRenderDispatcher_render)
        spdlog::warn("ActorRenderDispatcher::render entry resolved; hook deferred pending event consumer validation");
    else
        spdlog::warn("ActorRenderDispatcher::render entry unresolved; hook skipped");
}
