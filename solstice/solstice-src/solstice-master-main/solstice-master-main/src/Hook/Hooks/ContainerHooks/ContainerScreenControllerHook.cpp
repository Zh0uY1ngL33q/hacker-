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

    // Repairing the entry address must not activate an offset-zero member read
    // while Actor_mContainerManagerModel is still unresolved on this build.
    const int modelOffset = OffsetProvider::Actor_mContainerManagerModel;
    if (modelOffset <= 0 || modelOffset >= 0x8000 ||
        !gFeatureManager || !gFeatureManager->mDispatcher)
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
