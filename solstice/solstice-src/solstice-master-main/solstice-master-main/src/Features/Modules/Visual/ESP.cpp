//
// Created by vastrakai on 7/7/2024.
//

#include "ESP.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/ActorRenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Rendering/LevelRenderer.hpp>

void ESP::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &ESP::onRenderEvent>(this);
}

void ESP::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &ESP::onRenderEvent>(this);
}

void ESP::onRenderEvent(RenderEvent& event)
{
    // One-shot probe. World-to-screen has four links and any of them can be null
    // or zero on 1.21.120, and a blank ESP does not say which one broke. Printing
    // them once turns that question into a log read instead of another build.
    static bool sProbed = false;
    if (!sProbed) {
        sProbed = true;
        auto* ci = ClientInstance::get();
        auto* lr = ci ? ci->getLevelRenderer() : nullptr;
        auto* player = ci ? ci->getLocalPlayer() : nullptr;
        auto* rp = lr ? lr->getRendererPlayer() : nullptr;
        auto* cam = rp ? rp->getCameraPos() : nullptr;
        auto actors = ActorUtils::getActorList(false, true);
        spdlog::info("[ESP probe] levelRenderer={:#x} rendererPlayer={:#x} cameraPos={:#x} "
                     "origin=({:.2f},{:.2f},{:.2f}) fov=({:.2f},{:.2f}) resolution=({:.0f},{:.0f}) actors={}",
                     reinterpret_cast<uintptr_t>(lr), reinterpret_cast<uintptr_t>(rp),
                     reinterpret_cast<uintptr_t>(cam),
                     MathUtils::origin.x, MathUtils::origin.y, MathUtils::origin.z,
                     MathUtils::fov.x, MathUtils::fov.y,
                     MathUtils::displaySize.x, MathUtils::displaySize.y,
                     actors.size());
    }

    if (!ClientInstance::get()->getLevelRenderer()) return;

    auto actors = ActorUtils::getActorList(false, true);
    auto localPlayer = ClientInstance::get()->getLocalPlayer();

    if (mDebug.mValue)
    {
        auto botActors = ActorUtils::getActorList(false, false);
        // Remove actors that are not bots from the botActors list
        std::erase_if(botActors, [actors](Actor* actor) {
            return std::ranges::find(actors, actor) != actors.end();
        });



        // Draw a red outline around the bot actors
        auto drawList = ImGui::GetBackgroundDrawList();
        for (auto actor : botActors)
        {
            if (actor == localPlayer && ClientInstance::get()->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) continue;
            if (actor == localPlayer && !mRenderLocal.mValue) continue;
            auto shape = actor->getAABBShapeComponent();
            if (!shape) continue;

            auto themeColor = ImColor(1.0f, 0.0f, 0.0f);

            if (actor->isPlayer())
            {
                if (gFriendManager->isFriend(actor))
                {
                    if (mShowFriends.mValue) themeColor = ImColor(0.0f, 1.0f, 0.0f);
                    else continue;
                }
            }

            AABB aabb = actor->getAABB();

            std::vector<ImVec2> imPoints = MathUtils::getImBoxPoints(aabb);

            if (mRenderFilled.mValue) drawList->AddConvexPolyFilled(imPoints.data(), imPoints.size(), ImColor(themeColor.Value.x, themeColor.Value.y, themeColor.Value.z, 0.25f));
            drawList->AddPolyline(imPoints.data(), imPoints.size(), themeColor, 0, 2.0f);
        }

    }


    auto drawList = ImGui::GetBackgroundDrawList();


    for (auto actor : actors)
    {
        if (actor == localPlayer && ClientInstance::get()->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) continue;
        if (actor == localPlayer && !mRenderLocal.mValue) continue;
        auto shape = actor->getAABBShapeComponent();
        if (!shape) continue;

        auto themeColor = ColorUtils::getThemedColor(0);

        if (actor->isPlayer())
        {
            if (gFriendManager->isFriend(actor))
            {
                if (mShowFriends.mValue) themeColor = ImColor(0.0f, 1.0f, 0.0f);
                else continue;
            }
        }

        AABB aabb = actor->getAABB();

        std::vector<ImVec2> imPoints = MathUtils::getImBoxPoints(aabb);

        if (mRenderFilled.mValue) drawList->AddConvexPolyFilled(imPoints.data(), imPoints.size(), ImColor(themeColor.Value.x, themeColor.Value.y, themeColor.Value.z, 0.25f));
        drawList->AddPolyline(imPoints.data(), imPoints.size(), themeColor, 0, 2.0f);
    }
}
