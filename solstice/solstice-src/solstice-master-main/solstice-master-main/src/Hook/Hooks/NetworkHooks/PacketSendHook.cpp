//
// Created by vastrakai on 6/28/2024.
//
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include "PacketSendHook.hpp"
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/World/Level.hpp>

#include "Features/Events/ChatEvent.hpp"
#include "Features/Events/PacketOutEvent.hpp"

class ChatEvent;
// Trampoline: same signature as the detoured function, no C++ objects, so
// gating here keeps a wrong vtable slot from crashing inside the real body.
void* PacketSendHook::onPacketSend(void* _this, Packet* packet) {
    if (!mDetour || !InstanceLocator::isPlausibleObject(packet))
        return mDetour ? mDetour->getOriginal<&onPacketSend>()(_this, packet) : nullptr;
    return onPacketSendImpl(_this, packet);
}

std::unique_ptr<Detour> PacketSendHook::mDetour = nullptr;

void* PacketSendHook::onPacketSendImpl(void* _this, Packet* packet) {
    // Call the original function
    auto original = mDetour->getOriginal<&onPacketSendImpl>();

    if (packet->getId() == PacketID::Text)
    {
        auto textPacket = reinterpret_cast<TextPacket*>(packet);
        if (gFeatureManager && gFeatureManager->mCommandManager)
        {
            std::string message = textPacket->mMessage;
            auto event = nes::make_holder<ChatEvent>(message);
            gFeatureManager->mDispatcher->trigger(event);
            if (event->isCancelled()) return nullptr;
        }
    }

    auto holda = nes::make_holder<PacketOutEvent>(packet);
    gFeatureManager->mDispatcher->trigger(holda);
    if (holda->isCancelled()) return nullptr;

    if (packet->getId() == PacketID::PlayerAuthInput)
    {
        auto authPacket = reinterpret_cast<PlayerAuthInputPacket*>(packet);
        authPacket->mInteractRots = authPacket->mRot;
    }

    if (packet->getId() == PacketID::PlayerAuthInput) {
        auto input = reinterpret_cast<PlayerAuthInputPacket*>(packet);
        sendPacket(input);
        return nullptr;
    }

    return original(_this, packet);
}

void PacketSendHook::sendInputPacket(std::shared_ptr<PlayerAuthInputPacket> packet) {
    sendPacket(packet.get());
}

void PacketSendHook::sendPacket(PlayerAuthInputPacket* packet) {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto sender = ClientInstance::get()->getPacketSender();

    if (mCurrentTick == 0 && packet->mClientTick > mCurrentTick) mCurrentTick = packet->mClientTick;
    mCurrentTick++;

    packet->mClientTick = mCurrentTick;
    if (auto player = ClientInstance::get()->getLocalPlayer()) {
        if (auto* level = player->getLevel();
            level && InstanceLocator::isPlausibleObject(level))
        {
            if (auto* levelData = level->getLevelData();
                levelData && InstanceLocator::isReadableMemory(levelData, 0x40))
            {
                levelData->setTick(mCurrentTick);
            }
        }
    }

    sender->sendToServer(packet);
}

void PacketSendHook::init() {
    // The instance is null while signatures are unresolved or the heap locator
    // has not found the game yet; vtable[2] of null is what crashed at init.
    auto* ci = ClientInstance::get();
    if (!InstanceLocator::isPlausibleObject(ci))
    {
        spdlog::warn("LoopbackPacketSender::send skipped -- no ClientInstance yet");
        return;
    }
    mDetour = std::make_unique<Detour>("LoopbackPacketSender::send", reinterpret_cast<void*>(ci->getPacketSender()->vtable[2]), &PacketSendHook::onPacketSend);
}
