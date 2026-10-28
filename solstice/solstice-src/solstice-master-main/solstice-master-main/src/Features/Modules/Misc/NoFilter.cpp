//
// Created by vastrakai on 7/19/2024.
//

#include "NoFilter.hpp"

#include <Windows.h>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/MinecraftGame.hpp>
#include <SDK/Minecraft/UIProfanityContext.hpp>
#include <SDK/OffsetProvider.hpp>

namespace {
    // Keep SEH separate from functions with C++ objects requiring unwinding
    // (MSVC C2712). An unavailable filter must not become an offset-zero write.
    void setNoFilterContextEnabled(bool enabled)
    {
        const int contextOffset = OffsetProvider::MinecraftGame_mProfanityContext;
        const int enabledOffset = OffsetProvider::UIProfanityContext_mEnabled;
        if (contextOffset <= 0 || contextOffset >= 0x8000 ||
            enabledOffset <= 0 || enabledOffset >= 0x8000)
            return;

        __try {
            auto* client = ClientInstance::get();
            if (!InstanceLocator::isPlausibleObject(client)) return;
            auto* game = client->getMinecraftGame();
            if (!InstanceLocator::isPlausibleGame(game)) return;
            if (!InstanceLocator::isReadableMemory(
                    reinterpret_cast<const char*>(game) + contextOffset, sizeof(void*)))
                return;
            auto* context = game->getProfanityContext();
            // UIProfanityContext is not assumed to have a vtable.
            if (!InstanceLocator::isReadableMemory(context, enabledOffset + sizeof(bool)))
                return;
            context->setEnabled(enabled);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                      ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            // Stale/read-only context: leave the game's filter unchanged.
        }
    }
}

void NoFilter::onEnable()
{
    setNoFilterContextEnabled(false);
}

void NoFilter::onDisable()
{
    setNoFilterContextEnabled(true);
}
