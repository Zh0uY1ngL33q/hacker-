//
// Created by vastrakai on 7/7/2024.
//

#include "ESP.hpp"

#include <cmath>
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

    const glm::vec2 ciFovRaw       = ci->getFovRaw();
    const glm::vec2 ciFovConverted = ci->getFov();
    const glm::vec2 transformFov   = RenderUtils::transform.mFov;
    const glm::vec2 mathFov        = MathUtils::fov;

    // Two parameter sets for the same frame, the same matrix and the same point.
    // getBoxPoints() reads the unqualified `origin` / `fov` / `displaySize`, i.e.
    // the MathUtils statics, while the matrix it uses is
    // RenderUtils::transform.mMatrix. OpenGLGui assigns both sets in one block
    // today, but nothing keeps them equal afterwards and the probe was only ever
    // reading the transform copy. Projecting the point twice is the only way to
    // see whether the probe and the renderer are looking through the same
    // numbers; when they agree the two lines are identical, which is also the
    // control that proves this comparison is not vacuous.
    const auto projectWith = [&](const glm::vec3& origin, const glm::vec2& fov,
                                 glm::vec3& view, glm::vec2& screen) {
        const glm::vec3 rel = playerRender - origin;
        const glm::vec4 hom(rel, 1.f);
        view = { glm::dot(m[0], hom), glm::dot(m[1], hom), glm::dot(m[2], hom) };
        screen = { -1.f, -1.f };
        return m.OWorldToScreen(origin, playerRender, screen, fov, display);
    };

    glm::vec3 mathView{ 0.f }, transformView{ 0.f };
    glm::vec2 mathScreen{ -1.f, -1.f }, transformScreen{ -1.f, -1.f };
    const bool mathOk      = projectWith(mathOrigin, mathFov, mathView, mathScreen);
    const bool transformOk = projectWith(transformOrigin, transformFov, transformView, transformScreen);
    const bool comparable  = mathOk && transformOk;

    const glm::vec2 screenDelta{ mathScreen.x - transformScreen.x,
                                 mathScreen.y - transformScreen.y };
    const glm::vec3 viewDelta  = mathView - transformView;
    const bool originsMatch = mathOrigin == transformOrigin;
    const bool fovsMatch    = mathFov == transformFov;

    // A non-zero fourth component is a translation term -- and that is all it is.
    // Whether it duplicates the caller's own origin subtraction is a separate
    // question that the data can answer: a view matrix that already subtracts
    // exactly T has row.w == -dot(row.xyz, T), so the residual below is zero if
    // and only if the caller subtracts the same origin a second time. Any other
    // non-zero w is a translation of unknown provenance and is reported as such,
    // never as double subtraction.
    constexpr float kAffineEps = 1e-4f;
    const glm::vec3 rowW{ m[0][3], m[1][3], m[2][3] };
    const glm::vec3 axisDotOrigin{ glm::dot(glm::vec3(m[0]), transformOrigin),
                                   glm::dot(glm::vec3(m[1]), transformOrigin),
                                   glm::dot(glm::vec3(m[2]), transformOrigin) };
    const glm::vec3 residual{ rowW.x + axisDotOrigin.x,
                              rowW.y + axisDotOrigin.y,
                              rowW.z + axisDotOrigin.z };
    const bool hasTranslation   = std::fabs(rowW.x) > kAffineEps
                               || std::fabs(rowW.y) > kAffineEps
                               || std::fabs(rowW.z) > kAffineEps;
    const bool duplicatesOrigin = std::fabs(residual.x) <= kAffineEps
                               && std::fabs(residual.y) <= kAffineEps
                               && std::fabs(residual.z) <= kAffineEps;
    const char* affineVerdict = !hasTranslation
                              ? "NO_TRANSLATION_TERM"
                              : (duplicatesOrigin ? "DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED"
                                                  : "AFFINE_TRANSLATION_TERM_PRESENT");

    // Two different ratios, and the fov pair is not the display: the first is the
    // display's width/height, the second is fov.y/fov.x. They only coincide when
    // the conversion that built the pair used this same display.
    const double displayAspect      = display.y > 0.f
                                    ? static_cast<double>(display.x) / static_cast<double>(display.y) : 0.0;
    const double fovFactorRatio     = transformFov.x != 0.f
                                    ? static_cast<double>(transformFov.y) / static_cast<double>(transformFov.x) : 0.0;
    const double mathFovFactorRatio = mathFov.x != 0.f
                                    ? static_cast<double>(mathFov.y) / static_cast<double>(mathFov.x) : 0.0;

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

    spdlog::info("[ESP probe] fov ciFovRaw=({:.6f},{:.6f}) ciFovConverted=({:.6f},{:.6f}) "
                 "transformFov=({:.6f},{:.6f}) mathFov=({:.6f},{:.6f})",
                 static_cast<double>(ciFovRaw.x), static_cast<double>(ciFovRaw.y),
                 static_cast<double>(ciFovConverted.x), static_cast<double>(ciFovConverted.y),
                 static_cast<double>(transformFov.x), static_cast<double>(transformFov.y),
                 static_cast<double>(mathFov.x), static_cast<double>(mathFov.y));
    spdlog::info("[ESP probe] display displayWidth={:.1f} displayHeight={:.1f} "
                 "displayAspect={:.6f} fovFactorRatio={:.6f} mathFovFactorRatio={:.6f}",
                 static_cast<double>(display.x), static_cast<double>(display.y),
                 displayAspect, fovFactorRatio, mathFovFactorRatio);

    // A: the parameter set getBoxPoints() actually passes (MathUtils statics).
    // B: the transform snapshot the probe used before this round. Same matrix,
    // same frame, same point. The camera sits at eye height while the render
    // position may be the feet, so neither is expected to be exactly centred.
    spdlog::info("[ESP probe] math view: mathOk={} mathScreen=({:.2f},{:.2f}) "
                 "mathViewX={:.4f} mathViewY={:.4f} mathViewZ={:.4f}",
                 mathOk, static_cast<double>(mathScreen.x), static_cast<double>(mathScreen.y),
                 static_cast<double>(mathView.x), static_cast<double>(mathView.y),
                 static_cast<double>(mathView.z));
    spdlog::info("[ESP probe] transform view: transformOk={} transformScreen=({:.2f},{:.2f}) "
                 "transformViewX={:.4f} transformViewY={:.4f} transformViewZ={:.4f}",
                 transformOk,
                 static_cast<double>(transformScreen.x), static_cast<double>(transformScreen.y),
                 static_cast<double>(transformView.x), static_cast<double>(transformView.y),
                 static_cast<double>(transformView.z));
    // comparable=false means at least one path rejected the point, so the deltas
    // are the difference between two (-1,-1) sentinels and mean nothing.
    spdlog::info("[ESP probe] control comparable={} screenDelta=({:.4f},{:.4f}) "
                 "viewDelta=({:.4f},{:.4f},{:.4f}) originsMatch={} fovsMatch={}",
                 comparable, static_cast<double>(screenDelta.x),
                 static_cast<double>(screenDelta.y),
                 static_cast<double>(viewDelta.x), static_cast<double>(viewDelta.y),
                 static_cast<double>(viewDelta.z), originsMatch, fovsMatch);

    // The three vectors OWorldToScreen dots, with their fourth components: the
    // raw evidence behind the affine verdict, printed uncut so the classification
    // above can be re-derived by hand instead of trusted.
    spdlog::info("[ESP probe] basis v0(OwTS row0)=({:.6f},{:.6f},{:.6f}) v0w={:.6f} "
                 "v1(OwTS row1)=({:.6f},{:.6f},{:.6f}) v1w={:.6f} "
                 "v2(OwTS row2)=({:.6f},{:.6f},{:.6f}) v2w={:.6f}",
                 static_cast<double>(m[0][0]), static_cast<double>(m[0][1]),
                 static_cast<double>(m[0][2]), static_cast<double>(m[0][3]),
                 static_cast<double>(m[1][0]), static_cast<double>(m[1][1]),
                 static_cast<double>(m[1][2]), static_cast<double>(m[1][3]),
                 static_cast<double>(m[2][0]), static_cast<double>(m[2][1]),
                 static_cast<double>(m[2][2]), static_cast<double>(m[2][3]));
    // One term per row: the w the matrix carries, the w a matrix would carry if
    // it had already baked in transformOrigin, and the residual. residual == 0 on
    // all three rows is the only form of this that proves double subtraction.
    spdlog::info("[ESP probe] affine verdict={} rowW=({:.6f},{:.6f},{:.6f}) "
                 "axisDotTransformOrigin=({:.6f},{:.6f},{:.6f}) "
                 "residual_w_plus_axisDotOrigin=({:.6f},{:.6f},{:.6f})",
                 affineVerdict,
                 static_cast<double>(rowW.x), static_cast<double>(rowW.y),
                 static_cast<double>(rowW.z),
                 static_cast<double>(axisDotOrigin.x), static_cast<double>(axisDotOrigin.y),
                 static_cast<double>(axisDotOrigin.z),
                 static_cast<double>(residual.x), static_cast<double>(residual.y),
                 static_cast<double>(residual.z));

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
