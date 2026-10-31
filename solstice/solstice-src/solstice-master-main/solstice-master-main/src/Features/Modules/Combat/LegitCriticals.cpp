//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//
// Behavior:
//   When the local player is airborne and about to attack, cancel sprint and
//   crouch (sneak) so the attack registers as a legit critical hit.
//   - Mode::Sneaking: only crouch while rising (vertical velocity > 0);
//     while falling the input is left unchanged.
//   - Mode::Full:     crouch regardless of rising/falling, as long as airborne.
//

#include "LegitCriticals.hpp"

#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Components/StateVectorComponent.hpp>

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

    auto* state = player->getStateVectorComponent();
    float vy = state ? state->mVelocity.y : 0.f;

    bool shouldCrouch = false;
    if (mMode.mValue == Mode::Sneaking) {
        // Sneaking mode: crouch only while rising; falling -> unchanged.
        shouldCrouch = (vy > 0.f);
    } else {
        // Full mode: crouch regardless of rising/falling while airborne.
        shouldCrouch = true;
    }

    if (!shouldCrouch) return;

    // 中断疾跑: strip every sprint input flag so the hit is a non-sprint (crit-eligible) hit.
    paip->mInputData &= ~AuthInputAction::START_SPRINTING;
    paip->mInputData &= ~AuthInputAction::SPRINTING;
    paip->mInputData &= ~AuthInputAction::SPRINT_DOWN;
    paip->mInputData &= ~AuthInputAction::STOP_SPRINTING;

    // 下蹲: set the sneaking input flags so the server sees a crouched, crit-eligible state.
    paip->mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
    paip->mInputData &= ~AuthInputAction::STOP_SNEAKING;
}
