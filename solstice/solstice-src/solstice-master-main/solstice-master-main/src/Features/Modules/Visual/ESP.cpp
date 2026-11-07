//
// Created by vastrakai on 7/7/2024.
//

#include "ESP.hpp"

#include <cmath>
#include <cstdio>
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
//
// The capture has two blocks, and only the second one is authoritative:
//   preliminary  matrixCaptureFrame  -- matrix, both parameter sets, fov, affine
//                                       verdict. Matrix only, no test point.
//   paired       controlCaptureFrame -- the same point projected twice, once per
//                                       parameter set, plus every matrix fact
//                                       re-read on that same frame.
// The paired block never reuses the preliminary block's numbers, so the two
// cannot be combined across frames; sameFrame only reports whether they coincide.
// ---------------------------------------------------------------------------

constexpr int   kProbeSearchBudget    = 900;   // counted in searches, not frames
constexpr int   kSearchIntervalFrames = 20;    // 15..30: at most one search per N frames
constexpr int   kProbeSearchLogEvery  = 100;   // one progress line per 100 searches
constexpr int   kProbeProgressEvery   = 300;   // gate-deferral log throttle, in frames
constexpr int   kProbeSlowRetryFrames = 600;   // frames, after the budget is spent
constexpr float kTestPointMinDepth    = 0.1f;  // view-space z must be < -this
constexpr float kMinSensitivity       = 0.15f; // tan(off-axis angle) below which a point is LOW_SENSITIVITY
constexpr int   kMaxActorsScanned     = 256;   // bound on a single search
constexpr const char* kTestSourceNonLocal = "nonlocal_actor_aabb_corner";
constexpr const char* kTestSourceLocal    = "local_actor_aabb_corner_low_sensitivity";

// An enum rather than the string itself: the sensitivity decision has to be made
// on the kind of source, and comparing const char* addresses is a trap even when
// both sides happen to come from the same literal.
enum class TestPointSource { NonLocal, Local };

void probeProjectionOnce()
{
    static uint64_t sFrame           = 0;   // monotonic RenderEvent counter
    static uint64_t sPreliminaryFrame = 0;
    static uint64_t sLastSearchFrame  = 0;
    static int      sSearches         = 0;
    static bool     sPreliminaryDone  = false;
    static bool     sPairedDone       = false;
    static bool     sGaveUp           = false;

    ++sFrame;

    if (sPreliminaryDone && sPairedDone) return;

    auto* ci     = ClientInstance::get();
    auto* lr     = ci     ? ci->getLevelRenderer()               : nullptr;
    auto* rp     = lr     ? lr->getRendererPlayer()              : nullptr;
    auto* cam    = rp     ? rp->getCameraPos()                   : nullptr;
    auto* player = ci     ? ci->getLocalPlayer()                 : nullptr;

    // The display size is written by OpenGLGui before the event is dispatched, so
    // a zero here means the frame is not set up yet. Sampling it anyway would
    // record a degenerate aspect and a pixel of (0,0) as if they were measurements.
    const glm::vec2 display = MathUtils::displaySize;

    // ---------------------------------------------------------------------
    // Gate. Everything below needs these four, so they are checked once. The
    // missing links are collected instead of stopping at the first: one line
    // naming all of them says whether this is load order or a broken mapping.
    // ---------------------------------------------------------------------
    std::string missing;
    const auto note = [&](const char* name) {
        if (!missing.empty()) missing += ", ";
        missing += name;
    };
    if (!ci) note("clientInstance");
    if (!cam) note("cameraPos");
    if (!(display.x > 0.f && display.y > 0.f)) note("displaySize");
    if (RenderUtils::transform.mSource == -1) note("transformNotAssigned");
    if (!missing.empty()) {
        // Three lines at most: a gate that never opens must not become a log tap.
        static int sGateLogs = 0;
        if (sGateLogs < 3 && (sGateLogs == 0 || (sFrame % kProbeProgressEvery) == 0)) {
            ++sGateLogs;
            spdlog::info("[ESP probe] frame={} not ready: {}", sFrame, missing);
        }
        return;   // exit this sample only -- the probe stays armed
    }

    // Everything below is read on THIS frame and never cached: a paired sample
    // that mixed a matrix from frame N with a point from frame M would describe a
    // projection the renderer never performed, which is the whole point of the
    // frame stamps printed next to both blocks.
    const glmatrixf& m = RenderUtils::transform.mMatrix;
    const glm::vec3 camera          = *cam;
    const glm::vec3 transformOrigin = RenderUtils::transform.mOrigin;
    const glm::vec3 mathOrigin      = MathUtils::origin;

    const glm::vec2 ciFovRaw       = ci->getFovRaw();
    const glm::vec2 ciFovConverted = ci->getFov();
    const glm::vec2 transformFov   = RenderUtils::transform.mFov;
    const glm::vec2 mathFov        = MathUtils::fov;

    // A non-zero fourth component is a translation term -- and that is all it is.
    // Whether it duplicates the caller's own origin subtraction is a separate
    // question the data can answer: a view matrix that already subtracts exactly T
    // has row.w == -dot(row.xyz, T), so the residual is zero if and only if the
    // caller subtracts the same origin a second time.
    //
    // The verdict is computed against MathUtils::origin, because that is the origin
    // getBoxPoints() actually passes to OWorldToScreen. The transform copy is kept
    // as a second residual so a frame where the two disagree is visible instead of
    // silently deciding the answer.
    constexpr float kAffineEps = 1e-4f;
    const glm::vec3 rowW{ m[0][3], m[1][3], m[2][3] };
    const glm::vec3 axisDotMathOrigin{ glm::dot(glm::vec3(m[0]), mathOrigin),
                                       glm::dot(glm::vec3(m[1]), mathOrigin),
                                       glm::dot(glm::vec3(m[2]), mathOrigin) };
    const glm::vec3 axisDotTransformOrigin{ glm::dot(glm::vec3(m[0]), transformOrigin),
                                            glm::dot(glm::vec3(m[1]), transformOrigin),
                                            glm::dot(glm::vec3(m[2]), transformOrigin) };
    const glm::vec3 residualMath{ rowW.x + axisDotMathOrigin.x,
                                  rowW.y + axisDotMathOrigin.y,
                                  rowW.z + axisDotMathOrigin.z };
    const glm::vec3 residualTransform{ rowW.x + axisDotTransformOrigin.x,
                                       rowW.y + axisDotTransformOrigin.y,
                                       rowW.z + axisDotTransformOrigin.z };

    const bool hasTranslation = std::fabs(rowW.x) > kAffineEps
                             || std::fabs(rowW.y) > kAffineEps
                             || std::fabs(rowW.z) > kAffineEps;
    const bool duplicatesMathOrigin = std::fabs(residualMath.x) <= kAffineEps
                                   && std::fabs(residualMath.y) <= kAffineEps
                                   && std::fabs(residualMath.z) <= kAffineEps;
    const bool duplicatesTransformOrigin = std::fabs(residualTransform.x) <= kAffineEps
                                        && std::fabs(residualTransform.y) <= kAffineEps
                                        && std::fabs(residualTransform.z) <= kAffineEps;
    const auto classify = [&](bool duplicates) {
        if (!hasTranslation) return "NO_TRANSLATION_TERM";
        return duplicates ? "DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED"
                          : "AFFINE_TRANSLATION_TERM_PRESENT";
    };
    const char* affineVerdict            = classify(duplicatesMathOrigin);
    const char* affineVerdictIfTransform = classify(duplicatesTransformOrigin);
    const bool originsAgree = mathOrigin == transformOrigin;

    const double displayAspect      = static_cast<double>(display.x) / static_cast<double>(display.y);
    const double fovFactorRatio     = transformFov.x != 0.f
                                    ? static_cast<double>(transformFov.y) / static_cast<double>(transformFov.x) : 0.0;
    const double mathFovFactorRatio = mathFov.x != 0.f
                                    ? static_cast<double>(mathFov.y) / static_cast<double>(mathFov.x) : 0.0;

    // Two emitters, so the preliminary block and the paired block cannot drift
    // apart in content or field names.
    const auto logMatrixFacts = [&](const char* tag, uint64_t frame) {
        spdlog::info("[ESP probe] [{}] frame={} matrixSource={} "
                     "(0=game,1=located,2=synthesised,3=unvalidated,-1=unset)",
                     tag, frame, RenderUtils::transform.mSource);
        spdlog::info("[ESP probe] [{}] origins camera=({:.3f},{:.3f},{:.3f}) "
                     "transformOrigin=({:.3f},{:.3f},{:.3f}) mathOrigin=({:.3f},{:.3f},{:.3f}) "
                     "cameraToTransform=({:.3f},{:.3f},{:.3f})",
                     tag,
                     camera.x, camera.y, camera.z,
                     transformOrigin.x, transformOrigin.y, transformOrigin.z,
                     mathOrigin.x, mathOrigin.y, mathOrigin.z,
                     camera.x - transformOrigin.x, camera.y - transformOrigin.y,
                     camera.z - transformOrigin.z);
        spdlog::info("[ESP probe] [{}] fov ciFovRaw=({:.6f},{:.6f}) ciFovConverted=({:.6f},{:.6f}) "
                     "transformFov=({:.6f},{:.6f}) mathFov=({:.6f},{:.6f})",
                     tag,
                     static_cast<double>(ciFovRaw.x), static_cast<double>(ciFovRaw.y),
                     static_cast<double>(ciFovConverted.x), static_cast<double>(ciFovConverted.y),
                     static_cast<double>(transformFov.x), static_cast<double>(transformFov.y),
                     static_cast<double>(mathFov.x), static_cast<double>(mathFov.y));
        spdlog::info("[ESP probe] [{}] display displayWidth={:.1f} displayHeight={:.1f} "
                     "displayAspect={:.6f} fovFactorRatio={:.6f} mathFovFactorRatio={:.6f}",
                     tag,
                     static_cast<double>(display.x), static_cast<double>(display.y),
                     displayAspect, fovFactorRatio, mathFovFactorRatio);
        spdlog::info("[ESP probe] [{}] basis v0(OwTS row0)=({:.6f},{:.6f},{:.6f}) v0w={:.6f} "
                     "v1(OwTS row1)=({:.6f},{:.6f},{:.6f}) v1w={:.6f} "
                     "v2(OwTS row2)=({:.6f},{:.6f},{:.6f}) v2w={:.6f}",
                     tag,
                     static_cast<double>(m[0][0]), static_cast<double>(m[0][1]),
                     static_cast<double>(m[0][2]), static_cast<double>(m[0][3]),
                     static_cast<double>(m[1][0]), static_cast<double>(m[1][1]),
                     static_cast<double>(m[1][2]), static_cast<double>(m[1][3]),
                     static_cast<double>(m[2][0]), static_cast<double>(m[2][1]),
                     static_cast<double>(m[2][2]), static_cast<double>(m[2][3]));
        spdlog::info("[ESP probe] [{}] mat16 glmColumnMajor {:.6f} {:.6f} {:.6f} {:.6f} | "
                     "{:.6f} {:.6f} {:.6f} {:.6f} | {:.6f} {:.6f} {:.6f} {:.6f} | "
                     "{:.6f} {:.6f} {:.6f} {:.6f}",
                     tag,
                     static_cast<double>(m[0][0]), static_cast<double>(m[0][1]),
                     static_cast<double>(m[0][2]), static_cast<double>(m[0][3]),
                     static_cast<double>(m[1][0]), static_cast<double>(m[1][1]),
                     static_cast<double>(m[1][2]), static_cast<double>(m[1][3]),
                     static_cast<double>(m[2][0]), static_cast<double>(m[2][1]),
                     static_cast<double>(m[2][2]), static_cast<double>(m[2][3]),
                     static_cast<double>(m[3][0]), static_cast<double>(m[3][1]),
                     static_cast<double>(m[3][2]), static_cast<double>(m[3][3]));
    };
    // Production origin first; the transform copy is a control, never an
    // alternative basis for the verdict.
    const auto logAffine = [&](const char* tag) {
        spdlog::info("[ESP probe] [{}] affine verdict={} (production origin = mathOrigin) "
                     "rowW=({:.6f},{:.6f},{:.6f}) axisDotMathOrigin=({:.6f},{:.6f},{:.6f}) "
                     "residualMath=({:.6f},{:.6f},{:.6f})",
                     tag, affineVerdict,
                     static_cast<double>(rowW.x), static_cast<double>(rowW.y),
                     static_cast<double>(rowW.z),
                     static_cast<double>(axisDotMathOrigin.x), static_cast<double>(axisDotMathOrigin.y),
                     static_cast<double>(axisDotMathOrigin.z),
                     static_cast<double>(residualMath.x), static_cast<double>(residualMath.y),
                     static_cast<double>(residualMath.z));
        spdlog::info("[ESP probe] [{}] affine control originsAgree={} "
                     "axisDotTransformOrigin=({:.6f},{:.6f},{:.6f}) "
                     "residualTransform=({:.6f},{:.6f},{:.6f}) verdictIfTransformOrigin={}",
                     tag, originsAgree,
                     static_cast<double>(axisDotTransformOrigin.x),
                     static_cast<double>(axisDotTransformOrigin.y),
                     static_cast<double>(axisDotTransformOrigin.z),
                     static_cast<double>(residualTransform.x), static_cast<double>(residualTransform.y),
                     static_cast<double>(residualTransform.z), affineVerdictIfTransform);
    };

    // ---------------------------------------------------------------------
    // Stage 1: preliminary snapshot. Matrix only, taken as soon as the gate
    // passes. It is explicitly NOT paired with any test point and must not be
    // combined with the paired block below if the frames differ.
    // ---------------------------------------------------------------------
    if (!sPreliminaryDone) {
        sPreliminaryDone = true;
        sPreliminaryFrame = sFrame;
        spdlog::info("[ESP probe] ===== preliminary snapshot matrixCaptureFrame={} "
                     "(matrix only, NOT paired with a test point) =====", sPreliminaryFrame);
        logMatrixFacts("preliminary", sPreliminaryFrame);
        logAffine("preliminary");
    }

    if (sPairedDone) return;

    // ---------------------------------------------------------------------
    // Search is rate limited. Scanning the actor list every frame is a real cost
    // for a one-shot diagnostic, so a search runs at most once per
    // kSearchIntervalFrames, and the attempt budget counts searches, not frames.
    // ---------------------------------------------------------------------
    const uint64_t sinceSearch = sFrame - sLastSearchFrame;
    const bool searchDue = sSearches == 0
                        || (sGaveUp ? sinceSearch >= kProbeSlowRetryFrames
                                    : sinceSearch >= kSearchIntervalFrames);
    if (!searchDue) return;
    sLastSearchFrame = sFrame;
    ++sSearches;

    const auto projectWith = [&](const glm::vec3& origin, const glm::vec2& fov,
                                 const glm::vec3& world,
                                 glm::vec3& view, glm::vec2& screen) {
        const glm::vec3 rel = world - origin;
        const glm::vec4 hom(rel, 1.f);
        view = { glm::dot(m[0], hom), glm::dot(m[1], hom), glm::dot(m[2], hom) };
        screen = { -1.f, -1.f };
        return m.OWorldToScreen(origin, world, screen, fov, display);
    };

    const auto cornersOf = [](const AABB& box, glm::vec3 out[8]) {
        int i = 0;
        for (int xi = 0; xi < 2; ++xi)
            for (int yi = 0; yi < 2; ++yi)
                for (int zi = 0; zi < 2; ++zi)
                    out[i++] = { xi ? box.mMax.x : box.mMin.x,
                                 yi ? box.mMax.y : box.mMin.y,
                                 zi ? box.mMax.z : box.mMin.z };
    };

    // Both parameter sets must see the point in front of them, otherwise the two
    // projections are not comparable. Among the usable ones, keep the one furthest
    // off the view axis: S = sqrt(viewX^2 + viewY^2) / (-viewZ) is the tangent of
    // the angle between the point and the axis, and it is the only thing that
    // makes a parameter difference visible as a pixel difference.
    glm::vec3 testWorld{ 0.f };
    TestPointSource testSourceKind = TestPointSource::NonLocal;
    bool havePoint = false;
    uintptr_t testActor = 0;
    float bestScore = -1.f;

    const auto consider = [&](const glm::vec3& p, TestPointSource kind, uintptr_t actor) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
        const glm::vec3 relM = p - mathOrigin;
        const glm::vec3 relT = p - transformOrigin;
        const glm::vec3 vm{ glm::dot(m[0], glm::vec4(relM, 1.f)),
                            glm::dot(m[1], glm::vec4(relM, 1.f)),
                            glm::dot(m[2], glm::vec4(relM, 1.f)) };
        const float zt = glm::dot(m[2], glm::vec4(relT, 1.f));
        if (vm.z >= -kTestPointMinDepth || zt >= -kTestPointMinDepth) return;
        const float lateral = std::sqrt(vm.x * vm.x + vm.y * vm.y);
        const float score = lateral / -vm.z;
        if (score <= bestScore) return;
        bestScore = score;
        testWorld = p;
        testSourceKind = kind;
        testActor = actor;
        havePoint = true;
    };

    const auto actors = ActorUtils::getActorList(false, true);
    int scanned = 0;
    for (auto* actor : actors) {
        if (!actor || actor == player) continue;
        if (++scanned > kMaxActorsScanned) break;
        if (!actor->getAABBShapeComponent()) continue;
        glm::vec3 corners[8];
        cornersOf(actor->getAABB(), corners);
        for (const auto& p : corners) {
            consider(p, TestPointSource::NonLocal, reinterpret_cast<uintptr_t>(actor));
        }
    }

    // Fallback: the local player's own box. It projects, but it sits close to the
    // view axis, so the source string says so and the sensitivity flag below will
    // normally mark it LOW_SENSITIVITY.
    if (!havePoint && player && player->getAABBShapeComponent()) {
        glm::vec3 corners[8];
        cornersOf(player->getAABB(), corners);
        for (const auto& p : corners) {
            consider(p, TestPointSource::Local, reinterpret_cast<uintptr_t>(player));
        }
    }

    if (!havePoint) {
        if (!sGaveUp) {
            char why[200];
            snprintf(why, sizeof(why),
                     "no usable test point: %zu visible actors, %d examined, "
                     "none of their 8 AABB corners is finite and in front of both "
                     "parameter sets (z < -%.2f)",
                     actors.size(), scanned, static_cast<double>(kTestPointMinDepth));
            if (sSearches == 1 || sSearches % kProbeSearchLogEvery == 0) {
                spdlog::info("[ESP probe] search {} (frame {}): {}", sSearches, sFrame, why);
            }
            if (sSearches >= kProbeSearchBudget) {
                sGaveUp = true;
                spdlog::warn("[ESP probe] control never captured after {} searches "
                             "(one per {} frames): {}. Retrying silently every {} frames.",
                             sSearches, kSearchIntervalFrames, why, kProbeSlowRetryFrames);
            }
        }
        return;
    }

    // ---------------------------------------------------------------------
    // Stage 2: the paired sample. Every fact printed here is read on this frame,
    // so the block stands on its own even when it is not the preliminary frame.
    // ---------------------------------------------------------------------
    glm::vec3 mathView{ 0.f }, transformView{ 0.f };
    glm::vec2 mathScreen{ -1.f, -1.f }, transformScreen{ -1.f, -1.f };
    const bool mathOk      = projectWith(mathOrigin, mathFov, testWorld, mathView, mathScreen);
    const bool transformOk = projectWith(transformOrigin, transformFov, testWorld,
                                         transformView, transformScreen);
    const bool comparable  = mathOk && transformOk;

    // screenDelta is a sensitivity measure, not an equality proof: two different
    // parameter sets produce the same pixel whenever the point lies on the view
    // axis, and a point near that axis hides the difference almost entirely.
    // Equality is decided by originsMatch / fovsMatch, never by this number.
    const glm::vec2 screenDelta{ mathScreen.x - transformScreen.x,
                                 mathScreen.y - transformScreen.y };
    const glm::vec3 viewDelta  = mathView - transformView;
    const bool originsMatch = mathOrigin == transformOrigin;
    const bool fovsMatch    = mathFov == transformFov;

    // Two independent reasons to distrust screenDelta here. A score below the
    // threshold means the point is nearly on the view axis, so a parameter
    // difference barely moves it. The local fallback is flagged from its source on
    // top of that: it sits ~0.3 blocks ahead, which inflates S while saying
    // nothing about a scene at distance -- a near point is not a better probe.
    const bool lowSensitivity = bestScore < kMinSensitivity
                             || testSourceKind == TestPointSource::Local;
    const char* testSourceName = testSourceKind == TestPointSource::Local
                               ? kTestSourceLocal : kTestSourceNonLocal;

    glm::vec3 playerRender{ 0.f };
    bool playerKnown = false;
    if (player) {
        if (auto* rpc = player->getRenderPositionComponent()) {
            playerRender = rpc->mPosition;
            playerKnown = true;
        }
    }

    sPairedDone = true;

    spdlog::info("[ESP probe] ===== paired sample matrixCaptureFrame={} controlCaptureFrame={} "
                 "sameFrame={} searches={} =====",
                 sPreliminaryFrame, sFrame, sPreliminaryFrame == sFrame, sSearches);
    // The same-frame guarantee is structural: this block re-reads every value, it
    // does not reuse the preliminary block's numbers. sameFrame only reports
    // whether the two blocks happen to coincide.
    logMatrixFacts("paired", sFrame);
    logAffine("paired");
    spdlog::info("[ESP probe] testPoint source={} actor={:#x} world=({:.3f},{:.3f},{:.3f}) "
                 "sensitivityScore={:.4f} sensitivity={} "
                 "testViewX={:.4f} testViewY={:.4f} testViewZ={:.4f} transformTestViewZ={:.4f} "
                 "mathOk={} transformOk={}",
                 testSourceName, testActor, testWorld.x, testWorld.y, testWorld.z,
                 static_cast<double>(bestScore),
                 lowSensitivity ? "LOW_SENSITIVITY" : "OK",
                 static_cast<double>(mathView.x), static_cast<double>(mathView.y),
                 static_cast<double>(mathView.z),
                 static_cast<double>(transformView.z),
                 mathOk, transformOk);
    spdlog::info("[ESP probe] localPlayer render=({:.3f},{:.3f},{:.3f}) known={} "
                 "playerToCamera=({:.3f},{:.3f},{:.3f})",
                 playerRender.x, playerRender.y, playerRender.z, playerKnown,
                 playerRender.x - camera.x, playerRender.y - camera.y,
                 playerRender.z - camera.z);
    // A: the parameter set getBoxPoints() actually passes (MathUtils statics).
    // B: the transform snapshot. Same matrix, same frame, same point.
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
    spdlog::info("[ESP probe] control comparable={} originsMatch={} fovsMatch={} "
                 "screenDelta=({:.4f},{:.4f}) viewDelta=({:.4f},{:.4f},{:.4f}) "
                 "screenDeltaUsable={}",
                 comparable, originsMatch, fovsMatch,
                 static_cast<double>(screenDelta.x), static_cast<double>(screenDelta.y),
                 static_cast<double>(viewDelta.x), static_cast<double>(viewDelta.y),
                 static_cast<double>(viewDelta.z),
                 comparable && !lowSensitivity);
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
