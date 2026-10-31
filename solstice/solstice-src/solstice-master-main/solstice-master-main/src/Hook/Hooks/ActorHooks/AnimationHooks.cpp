//
// Created by vastrakai on 7/18/2024.
//

#include "AnimationHooks.hpp"

#include <Features/Events/SwingDurationEvent.hpp>
#include <Features/Events/BobHurtEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>

#include <cfloat>

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
                         reinterpret_cast<uintptr_t>(SigManager::BobHurt),
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

void AnimationHooks::init()
{
    uintptr_t func = SigManager::Mob_getCurrentSwingDuration;
    mSwingDetour = std::make_unique<Detour>("Mob::getCurrentSwingDuration", reinterpret_cast<void*>(func), &getCurrentSwingDuration);

    // TODO: Replace this with a better method of doing matrix translation
    func = SigManager::BobHurt;
    mBobHurtDetour = std::make_unique<Detour>("bobHurt", reinterpret_cast<void*>(func), &doBobHurt);
}
