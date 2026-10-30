//
// Created by vastrakai on 6/25/2024.
//

#include "BaseTickHook.hpp"

#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <Utils/Buffer.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

namespace {
    // Keep the read in a leaf without destructible C++ objects for MSVC SEH.
    void* checkedBaseTickTarget(Actor* player, int index)
    {
        if (!player || index < 2 || index >= 4096) return nullptr;
        __try {
            auto table = player->vtable;
            if (!table || !InstanceLocator::isReadableMemory(table + index, sizeof(void*)))
                return nullptr;
            void* target = reinterpret_cast<void*>(table[index]);
            return IsExecutableAddress(target) ? target : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }
}

std::unique_ptr<Detour> BaseTickHook::mDetour = nullptr;

void BaseTickHook::onBaseTick(Actor* actor)
{
    auto oFunc = mDetour->getOriginal<&onBaseTick>();
    if (actor != ClientInstance::get()->getLocalPlayer()) return oFunc(actor);

    mQueueMutex.lock();
    auto messages = mQueuedMessages;
    if (!messages.empty())
    {
        std::string messageStr = "";
        for (auto& message : messages) messageStr += message + "\n";
        ClientInstance::get()->getGuiData()->displayClientMessage(messageStr);
        mQueuedMessages.clear();
    }
    mQueueMutex.unlock();

    if (auto supplies = actor->getSupplies())
    {
        supplies->mInHandSlot = supplies->mSelectedSlot;
    }

    for (auto& [mTime, mPacket, mBypassHook] : mQueuedPackets)
    {
        spdlog::trace("Sending packet with ID: {} [queued {}ms ago] [{}]", magic_enum::enum_name(mPacket->getId()), NOW - mTime, mBypassHook ? "bypassing hook" : "not bypassing hook");
        if (mBypassHook) ClientInstance::get()->getPacketSender()->sendToServer(mPacket.get());
        else ClientInstance::get()->getPacketSender()->send(mPacket.get());
    }
    mQueuedPackets.clear();

    static bool once = false;
    if (!once)
    {
        once = true;

        auto holder = nes::make_holder<BaseTickInitEvent>(actor);
        gFeatureManager->mDispatcher->trigger(holder);
    }

    auto holder = nes::make_holder<BaseTickEvent>(actor);
    gFeatureManager->mDispatcher->trigger(holder);

    return oFunc(actor);
}

void BaseTickHook::init()
{
    // initLp path: LocalPlayer does not exist yet on some entry paths (and the
    // whole instance can be null when the SDK is unresolved); both mean "not now".
    auto* localPlayer = ClientInstance::get()
        ? ClientInstance::get()->getLocalPlayer()
        : nullptr;
    if (!InstanceLocator::isPlausibleObject(localPlayer))
    {
        spdlog::warn("Actor::baseTick skipped -- no LocalPlayer yet");
        return;
    }
    auto* target = checkedBaseTickTarget(localPlayer, OffsetProvider::Actor_baseTick);
    if (!target) {
        spdlog::warn("Actor::baseTick skipped -- unresolved index or non-executable target");
        return;
    }
    mDetour = std::make_unique<Detour>("Actor::baseTick", target, &BaseTickHook::onBaseTick);
    mDetour->enable();
}
