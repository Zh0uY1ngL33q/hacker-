//
// Created by vastrakai on 7/2/2024.
//

#include "Velocity.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/SetActorMotionPacket.hpp>

void Velocity::onEnable()
{
    gFeatureManager->mDispatcher->listen<PacketInEvent, &Velocity::onPacketInEvent>(this);
}

void Velocity::onDisable()
{
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &Velocity::onPacketInEvent>(this);
}

void Velocity::onPacketInEvent(PacketInEvent& event) const
{
    if (!event.mPacket || event.mPacket->getId() != PacketID::SetActorMotion) return;

    auto* player = ClientInstance::get() ? ClientInstance::get()->getLocalPlayer() : nullptr;
    if (!player) return;

    {
        auto packet = std::reinterpret_pointer_cast<SetActorMotionPacket>(event.mPacket);
        if (!packet) return;

        if (packet->mRuntimeID == player->getRuntimeID())
        {
            if (mMode.mValue == Mode::Full)
            {
                event.setCancelled(true);
            }
            else
            {
                glm::vec3 motion = packet->mMotion;
                motion.x *= mHorizontal.mValue;
                motion.z *= mHorizontal.mValue;
                motion.y *= mVertical.mValue;
                packet->mMotion = motion;
            }
        }
    }
}


