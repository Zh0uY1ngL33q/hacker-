//
// Created by vastrakai on 7/7/2024.
//

#include "ESP.hpp"

#include <string>

#include <Features/FeatureManager.hpp>
#include <Features/Events/ActorRenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Rendering/LevelRenderer.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>

namespace {

// ---------------------------------------------------------------------------
// One-shot projection diagnostic.
//
// The projection has four inputs the source cannot justify on 1.21.120 -- which
// matrix was used, whether the vectors it dots carry a translation, what unit
// the fov pair is stored in, and which coordinate the boxes are measured against
// -- and a blank ESP does not say which of them is wrong. This records them once
// so the next question is answered by a log read instead of a build.
//
// Nothing here changes a value. No transpose, no normalisation, no zeroing: every
// number is read exactly as the renderer uses it, in the same frame, which is why
// the probe runs at the top of the RenderEvent rather than from a timer.
// ---------------------------------------------------------------------------

constexpr int kProbeAttemptBudget   = 900;   // ~15 s at 60 fps
constexpr int kProbeProgressEvery   = 300;   // one progress line per ~5 s
constexpr int kProbeSlowRetryFrames = 600;   // ~10 s, silent, once the budget is spent

void probeProjectionOnce()
{
    static int  sAttempts = 0;
    static int  sSlowTick = 0;
    static bool sCaptured = false;
    static bool sGaveUp   = false;

    if (sCaptured) return;

    // Once the budget is spent the probe stops narrating but keeps looking, at a
    // rate that costs nothing. An object that appears later -- a world that
    // finished loading, a respawn -- must still be sampled, so a null link early
    // in the session can never disable the diagnostic for good.
    if (sGaveUp && ++sSlowTick < kProbeSlowRetryFrames) return;
    sSlowTick = 0;

    auto* ci     = ClientInstance::get();
    auto* lr     = ci     ? ci->getLevelRenderer()               : nullptr;
    auto* player = ci     ? ci->getLocalPlayer()                 : nullptr;
    auto* rp     = lr     ? lr->getRendererPlayer()              : nullptr;
    auto* cam    = rp     ? rp->getCameraPos()                   : nullptr;
    auto* rpc    = player ? player->getRenderPositionComponent() : nullptr;

    // The display size is written by OpenGLGui before the event is dispatched, so
    // a zero here means the frame is not set up yet. Sampling it anyway would
    // record a degenerate aspect and a pixel of (0,0) as if they were measurements.
    const glm::vec2 display = MathUtils::displaySize;

    // Every missing link is collected instead of stopping at the first: one line
    // naming all of them says whether this is load order or a broken mapping.
    const char* missing[7];
    int missingCount = 0;
    const auto require = [&](bool present, const char* name) {
        if (!present && missingCount < 7) missing[missingCount++] = name;
    };
    require(ci != nullptr,     "clientInstance");
    require(lr != nullptr,     "levelRenderer");
    require(rp != nullptr,     "rendererPlayer");
    require(cam != nullptr,    "cameraPos");
    require(player != nullptr, "localPlayer");
    require(rpc != nullptr,    "renderPositionComponent");
    require(display.x > 0.f && display.y > 0.f, "displaySize");

    if (missingCount > 0) {
        ++sAttempts;
        if (!sGaveUp) {
            const auto say = [&]() {
                std::string out;
                for (int i = 0; i < missingCount; ++i) {
                    if (i) out += ", ";
                    out += missing[i];
                }
                return out;
            };
            if (sAttempts == 1) {
                spdlog::info("[ESP probe] not ready (attempt {}/{}), missing: {}",
                             sAttempts, kProbeAttemptBudget, say());
            } else if (sAttempts % kProbeProgressEvery == 0) {
                spdlog::info("[ESP probe] still not ready (attempt {}/{}), missing: {}",
                             sAttempts, kProbeAttemptBudget, say());
            }
            if (sAttempts >= kProbeAttemptBudget) {
                sGaveUp = true;
                spdlog::warn("[ESP probe] no complete sample in {} attempts; last missing: {}. "
                             "Retrying silently every {} frames.",
                             sAttempts, say(), kProbeSlowRetryFrames);
            }
        }
        return;   // exit this sample only -- the probe stays armed
    }

    // Every input is present in this frame, so this is the sample. `captured` is
    // set here and nowhere else: a partial sample is never reported as one.
    sCaptured = true;

    const glmatrixf& m = RenderUtils::transform.mMatrix;

    const glm::vec3 playerRender    = rpc->mPosition;
    const glm::vec3 playerInXform   = RenderUtils::transform.mPlayerPos;
    const glm::vec3 camera          = *cam;
    const glm::vec3 transformOrigin = RenderUtils::transform.mOrigin;
    const glm::vec3 mathOrigin      = MathUtils::origin;

    const glm::vec2 fovRaw  = ci->getFovRaw();
    const glm::vec2 fovUsed = RenderUtils::transform.mFov;
    const glm::vec2 fovNow  = ci->getFov();

    // Exactly what OWorldToScreen does with this point, in its own order: subtract
    // the origin, then dot the three vectors it treats as rows. These are VIEW
    // coordinates, not clip coordinates -- no projection has been applied yet, so
    // there is no w here and none is invented.
    const glm::vec3 rel = playerRender - transformOrigin;
    const glm::vec4 rel4(rel, 1.f);
    const float viewX = glm::dot(m[0], rel4);
    const float viewY = glm::dot(m[1], rel4);
    const float viewZ = glm::dot(m[2], rel4);

    glm::vec2 screen{ -1.f, -1.f };
    const bool projected = m.OWorldToScreen(transformOrigin, playerRender, screen,
                                            fovUsed, display);

    // Two different ratios that were previously conflated: the display's own
    // width/height, and the fov factor pair's y/x. The second only equals the
    // screen aspect if the conversion that produced it used the same display.
    const double displayAspect  = display.y > 0.f
                                ? static_cast<double>(display.x) / static_cast<double>(display.y) : 0.0;
    const double fovFactorRatio = fovUsed.x != 0.f
                                ? static_cast<double>(fovUsed.y) / static_cast<double>(fovUsed.x) : 0.0;

    const glm::vec3 playerToCamera    = playerRender - camera;
    const glm::vec3 cameraToTransform = camera - transformOrigin;

    spdlog::info("[ESP probe] ===== captured on attempt {} =====", sAttempts + 1);
    spdlog::info("[ESP probe] matrixSource={} (0=game,1=located,2=synthesised,3=unvalidated,-1=unset)",
                 RenderUtils::transform.mSource);

    spdlog::info("[ESP probe] positions playerRender=({:.3f},{:.3f},{:.3f}) "
                 "playerRenderInTransform=({:.3f},{:.3f},{:.3f}) camera=({:.3f},{:.3f},{:.3f})",
                 playerRender.x, playerRender.y, playerRender.z,
                 playerInXform.x, playerInXform.y, playerInXform.z,
                 camera.x, camera.y, camera.z);
    spdlog::info("[ESP probe] origins transformOrigin=({:.3f},{:.3f},{:.3f}) "
                 "mathOrigin=({:.3f},{:.3f},{:.3f})",
                 transformOrigin.x, transformOrigin.y, transformOrigin.z,
                 mathOrigin.x, mathOrigin.y, mathOrigin.z);
    spdlog::info("[ESP probe] deltas playerToCamera=({:.3f},{:.3f},{:.3f}) "
                 "cameraToTransform=({:.3f},{:.3f},{:.3f})",
                 playerToCamera.x, playerToCamera.y, playerToCamera.z,
                 cameraToTransform.x, cameraToTransform.y, cameraToTransform.z);

    spdlog::info("[ESP probe] fov fovRaw=({:.6f},{:.6f}) fovUsed=({:.6f},{:.6f}) "
                 "fovNow=({:.6f},{:.6f})",
                 static_cast<double>(fovRaw.x), static_cast<double>(fovRaw.y),
                 static_cast<double>(fovUsed.x), static_cast<double>(fovUsed.y),
                 static_cast<double>(fovNow.x), static_cast<double>(fovNow.y));
    spdlog::info("[ESP probe] display displayWidth={:.1f} displayHeight={:.1f} "
                 "displayAspect={:.6f} fovFactorRatio={:.6f}",
                 static_cast<double>(display.x), static_cast<double>(display.y),
                 displayAspect, fovFactorRatio);

    // The camera sits at eye height while the render position may be the feet, so
    // a player point is only expected near the centre on the horizontal axis. No
    // correction is applied: the correction is what this line exists to measure.
    spdlog::info("[ESP probe] view self: viewOk={} screen=({:.2f},{:.2f}) "
                 "viewX={:.4f} viewY={:.4f} viewZ={:.4f}",
                 projected, static_cast<double>(screen.x), static_cast<double>(screen.y),
                 static_cast<double>(viewX), static_cast<double>(viewY),
                 static_cast<double>(viewZ));

    // The three vectors OWorldToScreen dots, with the component that decides
    // whether the origin subtraction is already baked into the matrix. A non-zero
    // w means the matrix carries translation while the caller subtracts the origin
    // itself, i.e. the camera offset is applied twice.
    spdlog::info("[ESP probe] basis v0(OwTS row0)=({:.6f},{:.6f},{:.6f}) v0w={:.6f} "
                 "v1(OwTS row1)=({:.6f},{:.6f},{:.6f}) v1w={:.6f} "
                 "v2(OwTS row2)=({:.6f},{:.6f},{:.6f}) v2w={:.6f}",
                 static_cast<double>(m[0][0]), static_cast<double>(m[0][1]),
                 static_cast<double>(m[0][2]), static_cast<double>(m[0][3]),
                 static_cast<double>(m[1][0]), static_cast<double>(m[1][1]),
                 static_cast<double>(m[1][2]), static_cast<double>(m[1][3]),
                 static_cast<double>(m[2][0]), static_cast<double>(m[2][1]),
                 static_cast<double>(m[2][2]), static_cast<double>(m[2][3]));

    // Raw storage in the order glm lays it out in memory (column-major, i.e.
    // m[0][0..3], m[1][0..3], ...). Printed untouched so a transposed matrix, a
    // 3x4 block with a padding column and a wrong offset cannot look identical.
    spdlog::info("[ESP probe] mat16 glmColumnMajor {:.6f} {:.6f} {:.6f} {:.6f} | "
                 "{:.6f} {:.6f} {:.6f} {:.6f} | {:.6f} {:.6f} {:.6f} {:.6f} | "
                 "{:.6f} {:.6f} {:.6f} {:.6f}",
                 static_cast<double>(m[0][0]), static_cast<double>(m[0][1]),
                 static_cast<double>(m[0][2]), static_cast<double>(m[0][3]),
                 static_cast<double>(m[1][0]), static_cast<double>(m[1][1]),
                 static_cast<double>(m[1][2]), static_cast<double>(m[1][3]),
                 static_cast<double>(m[2][0]), static_cast<double>(m[2][1]),
                 static_cast<double>(m[2][2]), static_cast<double>(m[2][3]),
                 static_cast<double>(m[3][0]), static_cast<double>(m[3][1]),
                 static_cast<double>(m[3][2]), static_cast<double>(m[3][3]));

    // A zero-sized AABB collapses all eight corners onto one pixel, which is a
    // third way to get a blank screen with every projection input correct.
    const auto actors = ActorUtils::getActorList(false, true);
    spdlog::info("[ESP probe] actors visible={}", actors.size());
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
    spdlog::info("[ESP probe] ===== end of capture =====");
}

} // namespace

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
    probeProjectionOnce();

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
