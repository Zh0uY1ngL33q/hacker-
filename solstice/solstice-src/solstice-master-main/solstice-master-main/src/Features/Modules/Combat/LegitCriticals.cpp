//
// LegitCriticals - legit critical hits while airborne.
//
// 机制（对齐 z1yr addon module/combat/criticals.py，已验证）：
//   Bedrock 暴击由"真实下蹲状态 + 下落速度(fallDistance>0)"决定，服务器从最近一份
//   PlayerAuthInput 与 Actor 状态判定。因此本模块在暴击窗口(玩家空中)内：
//     1) 真实下蹲：置本地 MoveInputComponent::mIsSneakDown（让玩家进入真实下蹲状态机，
//        类比 z1yr 的 ChangeSneakState），并同步改写发出的 PAI 包的 SNEAK 标志；
//     2) 断疾跑（StopSprinting/Both）：清本地 mIsSprinting + 置 PAI 的 STOP_SPRINTING；
//     3) 强制下坠（PushDown 开关，默认开）：每 tick 把 StateVectorComponent::mVelocity.y
//        压到 -0.1，维持 fallDistance>0（对齐 z1yr 的 SetMotion(-0.1)），使跳跃顶点/上升段也暴击。
//
// 防卡蹲：onBaseTickEvent 每 tick 校正本地状态——窗口关闭(落地)时无条件把 mIsSneakDown 清 false；
// onDisable 也无条件清一次，保证关模块后本地零残留、可正常行走。
//

#include "LegitCriticals.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
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
    mArmed = false;
    // 无条件恢复本地下蹲，防止关模块后卡蹲残留（事件已 deafen，onBaseTickEvent 不再校正）
    if (auto* ci = ClientInstance::get()) {
        if (auto* player = ci->getLocalPlayer()) {
            if (auto* moveInput = player->getMoveInputComponent()) {
                moveInput->mIsSneakDown = false;
            }
        }
    }
}

void LegitCriticals::onBaseTickEvent(BaseTickEvent& event) {
    auto* player = event.mActor; // 本地玩家
    if (!player) return;

    const bool airborne = !player->isOnGround();
    mArmed = airborne; // 空中=开窗口，落地=关窗口

    auto* moveInput = player->getMoveInputComponent();
    if (!moveInput) return;

    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    if (airborne) {
        // 真实下蹲：置本地蹲键标志（真实 sneak 状态机），对齐 z1yr ChangeSneakState。
        moveInput->mIsSneakDown = doSneak;
        // 断疾跑：仅 StopSprinting/Both 才清本地疾跑；否则不动，交给游戏/Sprint。
        if (doStopSprint) moveInput->mIsSprinting = false;

        // 强制下坠：每 tick 压 Y 速度为 -0.1，维持 fallDistance>0（对齐 z1yr SetMotion(-0.1)）。
        if (mPushDown.mValue) {
            if (auto* state = player->getStateVectorComponent()) {
                state->mVelocity.y = -0.1f;
            }
        }
    } else {
        // 落地/地面：无条件恢复下蹲（防卡蹲）。疾跑交给游戏恢复，不强制置 true。
        moveInput->mIsSneakDown = false;
    }
}

// 攻击前由 Aura 调用（冗余保险）：立即标记窗口开启，不等下一 tick。
void LegitCriticals::arm() {
    auto* ci = ClientInstance::get();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player || player->isOnGround()) return;
    mArmed = true;
}

// 网络侧：窗口内（即空中）改写发出的 PlayerAuthInput（服务器信任此包判定蹲伏/疾跑）。
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (!mArmed) return;
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

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
        // 下蹲：置 sneak 标志，并清 STOP_SNEAKING。服务器据此判定蹲伏，配合下落速度即为合法暴击。
        paip.mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
        paip.mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}
