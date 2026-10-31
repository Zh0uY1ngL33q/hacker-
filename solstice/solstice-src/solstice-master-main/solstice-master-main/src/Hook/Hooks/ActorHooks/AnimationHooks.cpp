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
    // A real held-item ModelView matrix (glm::mat4) is 16 finite floats. The
    // 1.21.120 held-item bob/block matrix now comes from GameRenderer_bobHurt
    // (RVA 0x2D72340) — its rdx is a mat4* output. We still gate the event
    // behind a plausibility check so we never write through a non-matrix pointer
    // if that sig ever drifts back to a false positive (ViewModel/Animations
    // would otherwise corrupt memory or do nothing).
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

    // Only the local player's swing is configurable, and this getter runs for
    // every animating entity. Skipping the rest keeps the detour off the hot
    // path -- dispatching would allocate an event holder per actor per tick.
    auto* ci = ClientInstance::get();
    auto* local = ci ? ci->getLocalPlayer() : nullptr;
    if (!actor || actor != local) return result;

    auto holder = nes::make_holder<SwingDurationEvent>(result);
    gFeatureManager->mDispatcher->trigger(holder);
    return holder->mSwingDuration;
}

void* AnimationHooks::doBobHurt(void* _this, glm::mat4* matrix)
{
    // One-time diagnostic, logged right after the original fills the matrix.
    // Confirms at runtime that GameRenderer_bobHurt's rdx really is a held-item
    // bob/block mat4 (plausible=true). If the sig ever drifts to a false positive
    // again, plausible=false and the event below is skipped — no corruption.
    {
        static bool reportedInvocation = false;
        if (!reportedInvocation) {
            reportedInvocation = true;
            spdlog::warn("[AnimationHooks] doBobHurt invoked (GameRenderer_bobHurt={:#x}); matrix={} plausible={}",
                         SigManager::GameRenderer_bobHurt,
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
    if (!func) {
        // 1.21.120: the function was recompiled, so the 1.21.44 pattern no longer
        // matches and the detour died with MH_ERROR_NOT_EXECUTABLE at 0x0.
        // Verified offline against the runtime image (ImageBase 0x7FF714E80000):
        //   RVA 0x58FCE90 -- prologue `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40`,
        //   returns the duration in eax (`cvttss2si eax, xmm0 ; ret`) and keys off
        //   the precomputed HashedString constants for haste / mining fatigue,
        //   which is what Bedrock's swing-duration getter depends on.
        //   Its consumer is the swing-progress updater at RVA 0x590D270:
        //     call 0x58FCE90                 ; eax = duration
        //     mov edx,[rbx+0x880] / inc edx  ; swing tick counter
        //     cmp edx,eax / jl ...           ; wrap when finished
        //     cvtdq2ps / divss / movss [rbx+0x87C], xmm1   ; mSwingProgress = tick/duration
        //   so overriding the return value is exactly "swing speed".
        const auto moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        func = moduleBase + 0x58FCE90;
        spdlog::warn("[AnimationHooks] Mob::getCurrentSwingDuration sig unresolved; using verified RVA 0x58FCE90 (base {:#x})", moduleBase);
    }
    if (func)
        mSwingDetour = std::make_unique<Detour>("Mob::getCurrentSwingDuration", reinterpret_cast<void*>(func), &getCurrentSwingDuration);
    else
        spdlog::warn("[AnimationHooks] swing-duration hook unavailable; the swing time setting stays inert.");

    // 1.21.120: the legacy SigManager::BobHurt sig now resolves to a false-positive
    // string builder (0xCE0260) and is unusable. The correct, verified replacement
    // is GameRenderer_bobHurt (RVA 0x2D72340): standard prologue, and a clean
    // layout — rcx = _this, rdx = glm::mat4* OUTPUT (the held-item bob/block
    // matrix: 16 floats written into it at 0x2D7244E..0x2D724A7), xmm2 = scalar.
    // doBobHurt calls the original first (filling rdx), then fires BobHurtEvent
    // with that matrix, so ViewModel (translate) and Animations (small-items /
    // sword-guard block pose) can modify the held item. isPlausibleMat4 still gates
    // the event so we never write through a non-matrix pointer if the sig drifts.
    constexpr bool kEnableBobHurtHook = true;
    if (kEnableBobHurtHook) {
        func = SigManager::GameRenderer_bobHurt;
        if (func) {
            mBobHurtDetour = std::make_unique<Detour>("GameRenderer_bobHurt", reinterpret_cast<void*>(func), &doBobHurt);
        } else {
            spdlog::warn("[AnimationHooks] GameRenderer_bobHurt sig unresolved; BobHurtEvent stays inert.");
        }
    }

    // The 1.21.120 swing-trace detour that used to live here (hooking
    // ItemInHandRenderer_render_bytepatch - 0x10C) never fired in any test session
    // and the duration getter is now located directly, so the probe is gone rather
    // than leaving a detour on a function that turns out not to be on the path.
}
