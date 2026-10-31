//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//
// Timing model (the fix for the race with the attack packet):
//   A Bedrock critical hit is decided by the player's sprint/sneak state, which
//   the server learns from the most recent PlayerAuthInput packet. Patching only
//   the outgoing PlayerAuthInput used to race with Aura's attack: depending on
//   BaseTickEvent listener order, Aura could send the attack before this module
//   had dropped sprint, so the server still saw a sprinting player -> no crit.
//
//   Now Aura explicitly drives the sequence: right before it sends an attack it
//   calls arm(), which opens a "crit window" (drops the local sprint flag and,
//   per Mode, crouches), and the outgoing PlayerAuthInput is patched while that
//   window is open. onBaseTickEvent auto-closes the window on the NEXT tick
//   (disarm), restoring normal sprint/jump. This makes the ordering deterministic
//   instead of relying on listener registration order. The window is also force-
//   closed immediately when the player lands.

#include "LegitCriticals.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/MoveInputComponent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

uint64_t LegitCriticals::sTick = 0;

void LegitCriticals::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

void LegitCriticals::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
    disarm(); // 关闭模块时恢复本地疾跑/下蹲，避免卡在断疾跑状态
}

// 每 tick 递增计数器；并在下一 tick 自动关闭上一 tick 打开的暴击窗口。
void LegitCriticals::onBaseTickEvent(BaseTickEvent& event) {
    sTick++;

    auto player = event.mActor;
    if (!player) return;

    // 落地立即恢复，且不再维持窗口。
    if (player->isOnGround()) {
        if (mArmed) disarm();
        return;
    }

    // 窗口持续到下一 tick 自动恢复（避免持续压制疾跑）。
    if (mArmed && sTick > mArmTick) {
        disarm();
        return;
    }

    // 窗口仍激活：每 tick 重新应用本地状态，抵消 Sprint 模块每 tick 把 mIsSprinting 置 true。
    if (mArmed) applyLocal(player);
}

// 攻击前由 Aura 调用：进入暴击窗口。仅空中生效。
void LegitCriticals::arm() {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
    if (player->isOnGround()) return; // 地面无法暴击，不打开窗口

    mArmed = true;
    mArmTick = sTick;
    applyLocal(player);
}

// 下一 tick 自动调用（或落地/关模块时）：恢复本地疾跑与下蹲。
void LegitCriticals::disarm() {
    if (!mArmed) return;
    mArmed = false;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (auto* moveInput = player->getMoveInputComponent()) {
        // 恢复疾跑：允许本地重新疾跑（若开了 Sprint 模块，它会自行重新置 true）。
        moveInput->mIsSprinting = true;
        // 恢复下蹲：清除本地下蹲位。
        if (mMode.mValue == Mode::Sneaking || mMode.mValue == Mode::Both) {
            moveInput->mIsSneakDown = false;
        }
    }
}

// 本地侧：在暴击窗口内根据 Mode 置位。
void LegitCriticals::applyLocal(Actor* player) {
    if (!player) return;
    if (auto* moveInput = player->getMoveInputComponent()) {
        if (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both) {
            moveInput->mIsSprinting = false;
        }
        if (mMode.mValue == Mode::Sneaking || mMode.mValue == Mode::Both) {
            moveInput->mIsSneakDown = true;
        }
    }
}

// 网络侧：仅在暴击窗口内改写发出的 PlayerAuthInput。
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (!mArmed) return;
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
    if (player->isOnGround()) return; // 额外保险

    auto paip = event.getPacket<PlayerAuthInputPacket>();
    if (!paip) return;

    applyInput(*paip);
}

void LegitCriticals::applyInput(PlayerAuthInputPacket& paip) {
    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    if (doStopSprint) {
        // 阻断疾跑：显式置 STOP_SPRINTING（告诉服务器停止疾跑），并清除所有"正在疾跑/请求疾跑"位。
        paip.mInputData |= AuthInputAction::STOP_SPRINTING;
        paip.mInputData &= ~AuthInputAction::START_SPRINTING;
        paip.mInputData &= ~AuthInputAction::SPRINTING;
        paip.mInputData &= ~AuthInputAction::SPRINT_DOWN;
    }

    if (doSneak) {
        // 下蹲：置 sneak 标志，并清 STOP_SNEAKING。
        paip.mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
        paip.mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}
