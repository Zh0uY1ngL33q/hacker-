//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//
// Timing model (reference: z1yr addon module/combat/criticals.py):
//   A Bedrock crit is decided by the player's falling/sneak state, which the
//   server learns from the most recent PlayerAuthInput. Patching a SINGLE
//   outgoing PAI used to race with Aura's attack: depending on BaseTickEvent
//   listener order the attack could go out before the PAI was patched, so the
//   server still saw a sprinting, non-sneaking player -> no crit (the "和攻击同
//   时发生 / 攻击完成才执行" symptom).
//
//   The robust fix, copied from the proven addon, is to HOLD the modified input
//   state CONTINUOUSLY while the player is airborne: every airborne tick the
//   local MoveInputComponent is set AND the outgoing PAI is patched, so the
//   attack always rides on a correctly-modified input no matter the order. On
//   landing the state is restored (disarm), recovering normal sprint/jump. The
//   previous 1-tick window is gone. arm() (called by Aura before an attack) is
//   now just a redundant immediate re-apply; onBaseTickEvent is the authority.

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
    disarm(); // 关闭模块时恢复本地疾跑/下蹲，避免卡在断疾跑状态
}

// 持续模型：空中则维持暴击窗口（每 tick 重新应用本地状态，抵消 Sprint 模块每 tick 把
// mIsSprinting 置 true），落地则恢复疾跑/下蹲。
void LegitCriticals::onBaseTickEvent(BaseTickEvent& event) {
    auto* player = event.mActor; // 本地玩家
    if (!player) return;

    if (player->isOnGround()) {
        if (mArmed) disarm();   // 落地恢复疾跑/下蹲（即"恢复正常疾跑跳跃"）
        return;
    }

    // 空中：打开/维持暴击窗口，每 tick 重新应用本地状态。
    mArmed = true;
    applyLocal(player);
}

// 攻击前由 Aura 调用（冗余保险）：立即应用一次本地状态；真正维持由 onBaseTickEvent 负责。
void LegitCriticals::arm() {
    auto* player = ClientInstance::get()->getLocalPlayer();
    if (!player || player->isOnGround()) return;
    mArmed = true;
    applyLocal(player);
}

// 落地/关模块时：恢复本地疾跑与下蹲。
void LegitCriticals::disarm() {
    if (!mArmed) return;
    mArmed = false;

    auto* player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (auto* moveInput = player->getMoveInputComponent()) {
        moveInput->mIsSprinting = true;                       // 恢复疾跑
        if (mMode.mValue == Mode::Sneaking || mMode.mValue == Mode::Both) {
            moveInput->mIsSneakDown = false;                  // 恢复下蹲
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

// 网络侧：窗口内（即空中）改写发出的 PlayerAuthInput，与本地状态一致。
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (!mArmed) return;
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto* player = ClientInstance::get()->getLocalPlayer();
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
