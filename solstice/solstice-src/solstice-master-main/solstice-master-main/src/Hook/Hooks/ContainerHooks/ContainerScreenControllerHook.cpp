//
// Created by vastrakai on 7/5/2024.
//

#include "ContainerScreenControllerHook.hpp"

#include <memory>
#include <Features/Events/ContainerScreenTickEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/OffsetProvider.hpp>

std::unique_ptr<Detour> ContainerScreenControllerHook::mDetour;

uint32_t ContainerScreenControllerHook::onContainerTick(class ContainerScreenController *csc)
{
    auto original = mDetour->getOriginal<&ContainerScreenControllerHook::onContainerTick>();

    // The offset this used to gate on (Actor_mContainerManagerModel) does not
    // resolve; getContainerManagerModel() falls back to the same member's other
    // declaration (Actor_mContainerModel, 0xCE8) and validates the result, so the
    // gate is now the returned pointer rather than a number.
    if (!gFeatureManager || !gFeatureManager->mDispatcher)
        return original(csc);
    auto* client = ClientInstance::get();
    if (!InstanceLocator::isPlausibleObject(client)) return original(csc);
    auto* player = client->getLocalPlayer();
    if (!InstanceLocator::isPlausibleObject(player)) return original(csc);
    auto* model = player->getContainerManagerModel();
    if (!InstanceLocator::isPlausibleObject(model) ||
        !InstanceLocator::isReadableMemory(model, 0x52)) return original(csc);
    if (model->mContainerType != ContainerType::Container)
    {
        spdlog::debug("Not a container - screen {} (id: {})", std::string(magic_enum::enum_name(model->mContainerType)), model->mContainerType);
        return original(csc);
    }

    auto holder = nes::make_holder<ContainerScreenTickEvent>(csc);
    gFeatureManager->mDispatcher->trigger(holder);

    return original(csc);
}

void ContainerScreenControllerHook::init()
{
    auto func = SigManager::ContainerScreenController_tick;
    mDetour = std::make_unique<Detour>("ContainerScreenController::tick", reinterpret_cast<void*>(func), &ContainerScreenControllerHook::onContainerTick);
}
