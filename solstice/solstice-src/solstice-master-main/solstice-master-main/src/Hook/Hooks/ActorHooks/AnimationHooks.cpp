//
// Created by vastrakai on 7/18/2024.
//

#include "AnimationHooks.hpp"

#include <Features/Events/SwingDurationEvent.hpp>
#include <Features/Events/BobHurtEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>

#include <cfloat>
#include <windows.h>

namespace {
    // A real held-item ModelView matrix (glm::mat4) is 16 finite floats. On
    // 1.21.120 SigManager::BobHurt resolves to a false-positive string-builder
    // (0xCE0260) whose second register is not a matrix, so the pointer handed to
    // doBobHurt is garbage. Gate the event behind a plausibility check so we
    // never write through a non-matrix pointer (ViewModel/Animations would
    // otherwise corrupt memory or do nothing).
    bool isPlausibleMat4(const glm::mat4* m)
    {
        if (!m) return false;
        if (!InstanceLocator::isReadableMemory(m, sizeof(glm::mat4))) return false;
        const float* p = reinterpret_cast<const float*>(m);
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(p[i])) return false;
            if (std::fabs(p[i]) > 1e8f) return false;
        }
        return true;
    }
}

std::unique_ptr<Detour> AnimationHooks::mSwingDetour;
std::unique_ptr<Detour> AnimationHooks::mBobHurtDetour;
std::unique_ptr<Detour> AnimationHooks::mSwingTraceDetour;

int AnimationHooks::getCurrentSwingDuration(Actor* actor)
{
    auto original = mSwingDetour->getOriginal<&getCurrentSwingDuration>();
    int result = original(actor);

    auto holder = nes::make_holder<SwingDurationEvent>(result);
    gFeatureManager->mDispatcher->trigger(holder);
    result = holder->mSwingDuration;

    return result;
}

void* AnimationHooks::doBobHurt(void* _this, glm::mat4* matrix)
{
    // One-time diagnostic, logged BEFORE touching the target so that a crash
    // inside the (possibly false-positive) original still leaves a trace in
    // solstice.log. This confirms whether the 1.21.120 BobHurt sig is actually
    // a held-item/bob matrix transform. On the current dump SigManager::BobHurt
    // resolves to 0xCE0260 (a string builder), so expect plausible=false until
    // a real sig is found and ViewModel/Animations will stay inert.
    {
        static bool reportedInvocation = false;
        if (!reportedInvocation) {
            reportedInvocation = true;
            spdlog::warn("[AnimationHooks] doBobHurt invoked (BobHurt sig={:#x}); matrix={} plausible={}",
                         SigManager::BobHurt,
                         reinterpret_cast<const void*>(matrix),
                         isPlausibleMat4(matrix));
        }
    }

    auto original = mBobHurtDetour->getOriginal<&doBobHurt>();
    auto result = original(_this, matrix);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return result;

    // Only forward a real matrix to modules. If the pointer is not a plausible
    // glm::mat4 (false-positive sig), skip triggering so ViewModel/Animations
    // never write through garbage and never silently no-op on a dead hook.
    if (!isPlausibleMat4(matrix)) {
        return result;
    }

    auto holder = nes::make_holder<BobHurtEvent>(_this, matrix);
    gFeatureManager->mDispatcher->trigger(holder);

    return result;
}

void AnimationHooks::swingTraceEntry(void* _this, void* a1, void* a2, void* a3)
{
    // TEMP trace (1.21.120): getCurrentSwingDuration was inlined into the caller
    // of the hand-render swing function below, so we can no longer hook it. To find
    // the inlined swing-duration computation we hook this function and dump the call
    // stack once. The caller is where swing progress (xmm2) is built from the
    // duration. Pure read-only logging; the original is always invoked.
    static bool reported = false;
    if (!reported) {
        reported = true;
        void* frames[24] = {};
        USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
        spdlog::warn("[SwingTrace] handRenderSwing invoked; stack frames={}", n);
        for (USHORT i = 0; i < n; ++i)
            spdlog::warn("  [SwingTrace] #{:02d} {:#x}", i, reinterpret_cast<uintptr_t>(frames[i]));
    }
    auto original = mSwingTraceDetour->getOriginal<&swingTraceEntry>();
    return original(_this, a1, a2, a3);
}

void AnimationHooks::init()
{
    uintptr_t func = SigManager::Mob_getCurrentSwingDuration;
    mSwingDetour = std::make_unique<Detour>("Mob::getCurrentSwingDuration", reinterpret_cast<void*>(func), &getCurrentSwingDuration);

    // The 1.21.120 BobHurt sig resolves to a false-positive string builder
    // (RVA 0xCE0260); its second register is not a glm::mat4*. Installing this
    // detour would route a real game call into a garbage target (crash/corrupt),
    // so the safe choice is to NOT install it until the real held-item matrix
    // hook lands (see ItemInHandRenderer_applyTransform @ 0x2DA7BC4 /
    // GameRenderer_bobHurt @ 0x2D72340 RE). ViewModel/Animations bob-hurt stay
    // inert rather than crashing. Flip kEnableBobHurtHook once wired.
    constexpr bool kEnableBobHurtHook = false;
    if (kEnableBobHurtHook) {
        func = SigManager::BobHurt;
        mBobHurtDetour = std::make_unique<Detour>("bobHurt", reinterpret_cast<void*>(func), &doBobHurt);
    } else {
        spdlog::warn("[AnimationHooks] BobHurt detour DISABLED: 1.21.120 sig is a "
                     "false positive (string builder @ 0xCE0260). ViewModel/Animations "
                     "bob-hurt is inert until a real held-item matrix hook lands.");
    }

    // TEMP TRACE (1.21.120): hook the hand-render swing function to capture its
    // caller. getCurrentSwingDuration is inlined there, so the caller is where the
    // swing-duration is actually computed. The bytepatch sig resolves to 0x58EB4EC
    // (inside this function); the function start is 0x10C bytes earlier (0x58EB3E0).
    constexpr bool kTraceSwingCaller = true;
    if (kTraceSwingCaller && SigManager::ItemInHandRenderer_render_bytepatch) {
        uintptr_t swingFn = SigManager::ItemInHandRenderer_render_bytepatch - 0x10C;
        mSwingTraceDetour = std::make_unique<Detour>("handRenderSwingTrace", reinterpret_cast<void*>(swingFn), &swingTraceEntry);
    }
}
