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

        // Project the local player's own position. It sits at the camera, so a
        // working chain puts it at the centre of the screen; anything else means
        // the matrix convention is wrong rather than the actor list. OWorldToScreen
        // returns false when the view-space z is positive, which is exactly the
        // symptom a transposed or differently-handed matrix produces -- and that
        // looks identical to "no entities" from the outside.
        if (player) {
            if (auto* selfPos = player->getRenderPositionComponent()) {
                glm::vec3 self = selfPos->mPosition;
                self.y += 1.0f;                       // roughly eye height
                glm::vec2 screen{-1.f, -1.f};
                const glm::vec3 rel = self - RenderUtils::transform.mOrigin;
                const auto& m = RenderUtils::transform.mMatrix;
                const float vz = glm::dot(m[2], glm::vec4(rel.x, rel.y, rel.z, 1.f));
                const bool ok = m.OWorldToScreen(RenderUtils::transform.mOrigin, self, screen,
                                                 RenderUtils::transform.mFov, MathUtils::displaySize);
                spdlog::info("[ESP probe] self ok={} screen=({:.1f},{:.1f}) viewZ={:.2f}",
                             ok, screen.x, screen.y, vz);
                // row2 came back as (0,0,0,1), which cannot be the view-space Z
                // axis: it makes viewZ constant and every entity is rejected by
                // OWorldToScreen's `z > 0` guard. Print all sixteen floats so the
                // layout is visible instead of inferred -- a 3x4 matrix with a
                // padding row, a transposed one, and a wrong offset all look the
                // same from two rows.
                spdlog::info("[ESP probe] mat16 {:.4f} {:.4f} {:.4f} {:.4f} | "
                             "{:.4f} {:.4f} {:.4f} {:.4f} | "
                             "{:.4f} {:.4f} {:.4f} {:.4f} | "
                             "{:.4f} {:.4f} {:.4f} {:.4f}",
                             m[0][0], m[0][1], m[0][2], m[0][3],
                             m[1][0], m[1][1], m[1][2], m[1][3],
                             m[2][0], m[2][1], m[2][2], m[2][3],
                             m[3][0], m[3][1], m[3][2], m[3][3]);
            }
        }

        // A zero-sized AABB collapses all eight corners onto one pixel, which is
        // a third way to get a blank screen with every projection input correct.
        if (!actors.empty()) {
            if (actors[0]->getAABBShapeComponent()) {
                const AABB box = actors[0]->getAABB();
                spdlog::info("[ESP probe] actor0 min=({:.2f},{:.2f},{:.2f}) max=({:.2f},{:.2f},{:.2f})",
                             box.mMin.x, box.mMin.y, box.mMin.z,
                             box.mMax.x, box.mMax.y, box.mMax.z);
            } else {
                spdlog::info("[ESP probe] actor0 has no AABBShapeComponent -- every box is skipped");
            }
        }
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
