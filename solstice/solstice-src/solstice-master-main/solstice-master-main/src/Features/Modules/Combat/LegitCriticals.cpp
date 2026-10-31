//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//
// Behavior:
//   When the local player is airborne, edit the outgoing PlayerAuthInput so the
//   server sees the desired input state for a legit critical hit. The Mode
//   selects which input flags to touch:
//   - Sneaking:      only set the sneak flags (crouch), leave sprint untouched.
//   - StopSprinting: only clear the sprint flags, leave sneak untouched.
//   - Both:          clear sprint AND set sneak.
//   All three only act while airborne (a crit is impossible on the ground).
//

#include "LegitCriticals.hpp"

#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

void LegitCriticals::onEnable() {
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

void LegitCriticals::onDisable() {
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    // Only act while airborne. On the ground a critical hit is impossible anyway.
    if (player->isOnGround()) return;

    auto paip = event.getPacket<PlayerAuthInputPacket>();
    if (!paip) return;

    // Each mode independently decides which input flags to modify.
    const bool doSneak       = (mMode.mValue == Mode::Sneaking     || mMode.mValue == Mode::Both);
    const bool doStopSprint  = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    if (doStopSprint) {
        // 中断疾跑: strip every sprint input flag so the hit is a non-sprint (crit-eligible) hit.
        paip->mInputData &= ~AuthInputAction::START_SPRINTING;
        paip->mInputData &= ~AuthInputAction::SPRINTING;
        paip->mInputData &= ~AuthInputAction::SPRINT_DOWN;
        paip->mInputData &= ~AuthInputAction::STOP_SPRINTING;
    }

    if (doSneak) {
        // 下蹲: set the sneaking input flags so the server sees a crouched state.
        paip->mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
        paip->mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}
