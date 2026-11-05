//
// Created by vastrakai on 6/25/2024.
//

#include "HookManager.hpp"

#include <MinHook.h>
#include <spdlog/spdlog.h>

#include "Hooks/ActorHooks/ActorModelHook.hpp"
#include "Hooks/ActorHooks/AnimationHooks.hpp"
#include "Hooks/ActorHooks/NametagRenderHook.hpp"
#include "Hooks/ContainerHooks/ContainerScreenControllerHook.hpp"
#include "Hooks/MiscHooks/FireBlockChangedHook.hpp"
#include "Hooks/MiscHooks/KeyHook.hpp"
#include "Hooks/MiscHooks/MouseHook.hpp"
#include "Hooks/MiscHooks/PreGameHook.hpp"
#include "Hooks/NetworkHooks/ConnectionRequestHook.hpp"
#include "Hooks/NetworkHooks/PacketReceiveHook.hpp"
#include "Hooks/NetworkHooks/PacketSendHook.hpp"
#include "Hooks/NetworkHooks/RakPeerHooks.hpp"
#include "Hooks/RenderHooks/ActorRenderDispatcherHook.hpp"
#include "Hooks/RenderHooks/D3DHook.hpp"
#include "Hooks/RenderHooks/HoverTextRendererHook.hpp"
#include "Hooks/RenderHooks/ItemRendererHook.hpp"
#include "Hooks/RenderHooks/LookInputHook.hpp"
#include "Hooks/RenderHooks/RenderItemInHandHook.hpp"
#include "Hooks/RenderHooks/SetupAndRenderHook.hpp"
#include "Hooks/ActorHooks/NametagRenderHook.hpp"


#define ADD_HOOK(hook) hooks.emplace_back(std::make_shared<hook>())

void HookManager::init(bool initLp)
{
    if (initLp)
    {
        std::vector<std::shared_ptr<Hook>> hooks;
        ADD_HOOK(BaseTickHook);
        for (auto& hook : hooks)
        {
            hook->init();
            mHooks.emplace_back(hook);
        }
    }
    else
    {
        std::vector<std::shared_ptr<Hook>> hooks;
        ADD_HOOK(KeyHook);
        ADD_HOOK(SetupAndRenderHook);
        ADD_HOOK(D3DHook);
        ADD_HOOK(ContainerScreenControllerHook);
        ADD_HOOK(MouseHook);
        ADD_HOOK(ConnectionRequestHook);
        ADD_HOOK(PacketReceiveHook);
        ADD_HOOK(PacketSendHook);
        ADD_HOOK(RakPeerHooks);
        ADD_HOOK(ActorRenderDispatcherHook);
        ADD_HOOK(AnimationHooks);
        ADD_HOOK(HoverTextRendererHook);
        ADD_HOOK(LookInputHook);
        ADD_HOOK(ActorModelHook);
        ADD_HOOK(ItemRendererHook);
        ADD_HOOK(FireBlockChangedHook);
        // RenderItemInHandDescription's constructor runs many times per frame and
        // its only consumer is Glint (disabled by default; visuals are parked).
        // Its init() used to call MH_EnableHook on that single hot target in the
        // middle of startup, which freezes every game thread. That is exactly
        // where this build hung: the log ends at "Created detour for ..." with no
        // "Enabled detour" line. The combined MH_EnableHook(MH_ALL_HOOKS) below
        // cannot avoid it because the single-hook enable is what deadlocks.
        // Do not create this detour at all until Glint is actually needed.
        // ADD_HOOK(RenderItemInHandHook);
        ADD_HOOK(PreGameHook);
        ADD_HOOK(NametagRenderHook);



        for (auto& hook : hooks)
        {
            hook->init();
            mHooks.emplace_back(hook);
        }

        const MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
        spdlog::info("[GUI probe] MH_EnableHook(all)={} (0=success)", static_cast<int>(enableStatus));
    }
}

void HookManager::shutdown()
{
    for (auto& hook : mHooks)
    {
        hook->shutdown();
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    mHooks.clear();
}

