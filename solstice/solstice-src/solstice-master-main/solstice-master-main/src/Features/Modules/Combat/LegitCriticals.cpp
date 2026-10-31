//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//
// Timing model:
//   A Bedrock critical hit is decided server-side from the player's sprint/sneak
//   state, which the server learns from the most recent PlayerAuthInput packet.
//   If we only patch the outgoing PlayerAuthInput, the attack packet and that
//   packet can race (the attack may already be on the wire before the patched
//   input is processed), so the server still sees a sprinting player -> no crit.
//
//   To make it robust we act on TWO fronts:
//   1. Local input (onBaseTickEvent): while airborne, clear the local
//      MoveInputComponent sprint flag for StopSprinting/Both. This makes the
//      game's OWN generated PlayerAuthInput clean and also satisfies any
//      client-side attack logic that reads the local sprint state.
//   2. Outgoing packet (onPacketOutEvent): patch the PlayerAuthInput we send so
//      the server's view is correct regardless of local-input ordering.
//
//   All three modes only act while airborne (a crit is impossible on the ground).
//

#include "LegitCriticals.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/MoveInputComponent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

void LegitCriticals::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

void LegitCriticals::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

// Local side: preemptively drop sprint on the same tick the attack happens so the
// game's own PlayerAuthInput (and any client-side attack checks) are clean.
void LegitCriticals::onBaseTickEvent(BaseTickEvent& event) {
    auto player = event.mActor;
    if (!player) return;

    // Only act while airborne.
    if (player->isOnGround()) return;

    if (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both) {
        if (auto* moveInput = player->getMoveInputComponent()) {
            moveInput->mIsSprinting = false;
        }
    }
}

// Network side: patch the PlayerAuthInput we actually send.
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    // Only act while airborne.
    if (player->isOnGround()) return;

    auto paip = event.getPacket<PlayerAuthInputPacket>();
    if (!paip) return;

    applyInput(*paip);
}

void LegitCriticals::applyInput(PlayerAuthInputPacket& paip) {
    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    if (doStopSprint) {
        // 中断疾跑: strip every sprint input flag so the hit is a non-sprint (crit-eligible) hit.
        paip.mInputData &= ~AuthInputAction::START_SPRINTING;
        paip.mInputData &= ~AuthInputAction::SPRINTING;
        paip.mInputData &= ~AuthInputAction::SPRINT_DOWN;
        paip.mInputData &= ~AuthInputAction::STOP_SPRINTING;
    }

    if (doSneak) {
        // 下蹲: set the sneaking input flags so the server sees a crouched state.
        paip.mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
        paip.mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}
