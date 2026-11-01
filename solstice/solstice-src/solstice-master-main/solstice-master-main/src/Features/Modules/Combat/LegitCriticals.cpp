//
// LegitCriticals - legit (crouched) critical hits while airborne.
//
// Local-input driven (local-only): within the window we set the local
//   MoveInputComponent::mIsSneakDown (real crouch); the game serializes that
//   state into PlayerAuthInput sent to the server -- local and packet share the
//   same state, so it is inherently consistent (no desync).
//   Cooldown rhythm: after the window ends we go silent for COOLDOWN ticks,
//   local restored, no crouch -- you can run/jump normally; never locked by
//   permanent crouch.
//   Attack-driven: any outbound attack packet (InventoryTransaction Attack)
//   arms the window (airborne-only + cooldown ready; if already armed, refresh
//   the window). Aura / TriggerBot / manual attack all trigger it, and it only
//   intervenes on attack (not global).
//   PushDown (window-only): each tick unconditionally push vertical velocity to
//   -0.1 (like z1yr), so during attack the position is descending -> fallDistance>0.
//   This is the actual crit source; crouch is just disguise.
//   State takes effect one tick after the attack packet: the first hit "sows"
//   the window, crits land on subsequent hits inside the window.
//

#include "LegitCriticals.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/Packets/InventoryTransactionPacket.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/MoveInputComponent.hpp>
#include <SDK/Minecraft/Actor/Components/StateVectorComponent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

void LegitCriticals::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

void LegitCriticals::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
    disarm();
}

void LegitCriticals::disarm() {
    mArmed = false;
    mTtl = 0;
    // Clear only the crouch WE wrote. MoveInputComponent is rebuilt from real
    // keyboard input every tick, so unconditionally writing false here (or in
    // the idle branches below) would eat the player's own manual crouch for a
    // tick and gain nothing.
    if (mSneakApplied) {
        mSneakApplied = false;
        if (auto* ci = ClientInstance::get()) {
            if (auto* player = ci->getLocalPlayer()) {
                if (auto* moveInput = player->getMoveInputComponent()) {
                    moveInput->mIsSneakDown = false;
                }
            }
        }
    }
}

void LegitCriticals::onBaseTickEvent(BaseTickEvent& event) {
    auto* player = event.mActor; // local player
    if (!player) return;

    // Cooldown / outside window: do NOT touch the input fields. The game rebuilds
    // MoveInputComponent from the real keyboard state every tick, so writing
    // false here would eat the player's own manual crouch for that tick.
    if (mCooldown > 0) {
        mCooldown--;
        return;
    }

    if (!mArmed) return;

    auto* moveInput = player->getMoveInputComponent();
    if (!moveInput) { disarm(); return; }

    // Land -> close window: ground attacks don't crit anyway (fallDistance=0), staying crouched just needlessly limits movement.
    if (player->isOnGround()) { disarm(); return; }

    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    // Real local crouch: the game serializes it into the PAI packet, local and packet share the same source -> no desync.
    // Only ever write true; disarm() (which clears only what we wrote) handles release.
    if (doSneak) {
        moveInput->mIsSneakDown = true;
        mSneakApplied = true;
    }
    if (doStopSprint) moveInput->mIsSprinting = false;

    // PushDown (window-only): each tick unconditionally push vertical velocity to -0.1 (like z1yr).
    // Position therefore keeps descending -> PAI-reported position also descends -> server sees fallDistance>0.
    // This is the actual crit source; crouch is just disguise, it does not produce crits. BaseTickEvent
    // fires before the original baseTick, so the velocity change is integrated the same tick.
    if (mPushDown.mValue) {
        if (auto* state = player->getStateVectorComponent()) {
            state->mVelocity.y = -0.1f;
        }
    }

    // Window countdown: after it ends, disarm and enter cooldown (gives you a run/jump window).
    if (--mTtl <= 0) {
        disarm();
        mCooldown = COOLDOWN;
    }
}

// Attack-driven: any outbound attack packet triggers it (Aura / TriggerBot / manual attack all count); internal guard is airborne-only + cooldown ready.
void LegitCriticals::arm() {
    auto* ci = ClientInstance::get();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player || player->isOnGround()) return; // airborne only
    if (mCooldown > 0) return;                   // don't open during cooldown, avoid permanent-crouch lock
    // If already armed, refresh the window: during combos the state persists so later hits get crouch+push.
    mArmed = true;
    mTtl = CRIT_WINDOW;
}

// Network side: detect attack packet to open window; inside the window also explicitly set crouch/stop-sprint flags on PAI (consistent with local, belt-and-suspenders).
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (event.mPacket->getId() == PacketID::InventoryTransaction) {
        if (auto pkt = event.getPacket<InventoryTransactionPacket>()) {
            if (auto* cit = pkt->mTransaction.get()) {
                if (cit->type == ComplexInventoryTransaction::Type::ItemUseOnEntityTransaction) {
                    auto* iut = reinterpret_cast<ItemUseOnActorInventoryTransaction*>(cit);
                    if (iut->mActionType == ItemUseOnActorInventoryTransaction::ActionType::Attack) {
                        arm(); // self-driven: opens/refreshes window only when airborne + cooldown ready
                    }
                }
            }
        }
    }

    if (!mArmed) return; // not in window: don't touch PAI (consistent with local)
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto paip = event.getPacket<PlayerAuthInputPacket>();
    if (!paip) return;

    applyInput(*paip);
}

void LegitCriticals::applyInput(PlayerAuthInputPacket& paip) {
    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    if (doStopSprint) {
        paip.mInputData |= AuthInputAction::STOP_SPRINTING;
        paip.mInputData &= ~AuthInputAction::START_SPRINTING;
        paip.mInputData &= ~AuthInputAction::SPRINTING;
        paip.mInputData &= ~AuthInputAction::SPRINT_DOWN;
    }

    if (doSneak) {
        paip.mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
        paip.mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}
