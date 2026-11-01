//
// LegitCriticals - attack-driven legit (crouched) critical hits while airborne.
//
// 机制（对齐 z1yr addon module/combat/criticals.py，已验证）：
//   Bedrock 暴击由"真实下蹲状态 + 下落速度(fallDistance>0)"决定，服务器从最近一份
//   PlayerAuthInput 与 Actor 状态判定。本模块【不空中持续介入】——仅在 Aura 发起攻击
//   前由 arm() 触发一个短时暴击窗口(CRIT_WINDOW tick)，窗口内：
//     1) 真实下蹲：置本地 MoveInputComponent::mIsSneakDown（让玩家进入真实下蹲状态机，
//        类比 z1yr 的 ChangeSneakState），并同步改写发出的 PAI 包的 SNEAK 标志；
//     2) 断疾跑（StopSprinting/Both）：清本地 mIsSprinting + 置 PAI 的 STOP_SPRINTING；
//     3) 强制下坠（PushDown 开关，默认开）：仅在"尚未下落"(vy>=-0.05) 时把 StateVectorComponent::mVelocity.y
//        压到 -0.1，维持 fallDistance>0（对齐 z1yr 的 SetMotion(-0.1)），使跳跃顶点/上升段也暴击；
//        已在下落时不碰，避免把正常下落限速变慢。PushDown 只在窗口内（攻击时）启用一下。
//   窗口倒计时结束自动 disarm 恢复本地；无攻击时零干扰（不全局启用），玩家正常行走/跳跃。
//
// 防卡蹲：disarm() 无条件把 mIsSneakDown 清 false；onDisable 也调 disarm，保证关模块后本地零残留。
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
    disarm(); // 攻击窗口结束/关模块：恢复本地输入，防卡蹲残留
}

void LegitCriticals::disarm() {
    mArmed = false;
    mTtl = 0;
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

    if (!mArmed) return; // 不在攻击窗口内：完全不介入（不全局启用）

    auto* moveInput = player->getMoveInputComponent();
    if (!moveInput) { mArmed = false; return; }

    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    // 窗口内仅在攻击时刻临时下蹲（真实 sneak 状态机），对齐 z1yr ChangeSneakState。
    moveInput->mIsSneakDown = doSneak;
    // 断疾跑：仅 StopSprinting/Both 才清本地疾跑；否则不动，交给游戏/Sprint。
    if (doStopSprint) moveInput->mIsSprinting = false;

    // PushDown：只在窗口内（攻击时）启用一下——仅当"尚未在下落"(vy >= -0.05) 时把 Y 速度压到 -0.1，
    // 维持 fallDistance>0（对齐 z1yr SetMotion(-0.1)），使跳跃顶点/上升段也暴击；已在下落不动，避免限速变慢。
    if (mPushDown.mValue) {
        if (auto* state = player->getStateVectorComponent()) {
            if (state->mVelocity.y >= -0.05f) {
                state->mVelocity.y = -0.1f;
            }
        }
    }

    // 窗口倒计时：攻击后自动恢复（Aura 持续攻击会续命，无攻击则数 tick 后解除，恢复行走/跳跃）
    if (--mTtl <= 0) {
        disarm();
    }
}

// 由 Aura 在发出攻击包前调用：仅在攻击前触发短时暴击窗口（仅空中生效），不空中持续介入。
void LegitCriticals::arm() {
    auto* ci = ClientInstance::get();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player || player->isOnGround()) return; // 仅空中
    mArmed = true;
    mTtl = CRIT_WINDOW; // 攻击触发后保持数 tick，覆盖攻击前后，保证 PAI 带蹲
}

// 网络侧：自驱动触发 + 窗口内改写发出的 PlayerAuthInput（服务器信任此包判定蹲伏/疾跑）。
// 自驱动：检测到任意出站攻击包（InventoryTransactionPacket 的 Attack 事务）即触发暴击窗口，
// 由 arm() 内部 guard 仅空中生效。这样 Aura / TriggerBot / InfiniteAura / 手动攻击 都能触发，
// 不再依赖单一模块显式调用（用户要求：攻击启用，而非只有 Aura 启用）。
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (event.mPacket->getId() == PacketID::InventoryTransaction) {
        if (auto pkt = event.getPacket<InventoryTransactionPacket>()) {
            if (auto* cit = pkt->mTransaction.get()) {
                if (cit->type == ComplexInventoryTransaction::Type::ItemUseOnActorInventoryTransaction) {
                    auto* iut = reinterpret_cast<ItemUseOnActorInventoryTransaction*>(cit);
                    if (iut->mActionType == ItemUseOnActorInventoryTransaction::ActionType::Attack) {
                        arm(); // 内部 guard：仅空中生效
                    }
                }
            }
        }
    }

    if (!mArmed) return; // 不在攻击窗口内：完全不介入（不全局启用）
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
