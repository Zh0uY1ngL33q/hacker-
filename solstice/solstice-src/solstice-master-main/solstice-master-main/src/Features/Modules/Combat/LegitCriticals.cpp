//
// LegitCriticals - legit (crouched) critical hits while airborne.
//
// 本地输入驱动（local-only）：窗口内置本地 MoveInputComponent::mIsSneakDown（真实蹲），
//   游戏会把该状态序列化进 PlayerAuthInput 发包——本地与发包同一条状态，天然一致、不会不同步。
//   冷却节律：窗口结束进入 COOLDOWN 静默，本地恢复，让你能正常跑、跳；不再被常蹲锁死。
//   攻击驱动：检测任意出站攻击包(InventoryTransaction Attack)才 arm（仅空中 + 冷却就绪，
//   已激活则刷新窗口）；Aura / TriggerBot / 手动攻击 都能触发，且只在攻击时介入（非全局）。
//   PushDown 仅窗口内：每 tick 无条件压速度到 -0.1（z1yr 同款），攻击时位置在下降
//   -> fallDistance>0。这是暴击来源；蹲只是伪装。
//   状态生效比攻击包晚一拍：第一击"播种"窗口，暴击吃在窗口内的后续攻击上。
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
    // 恢复本地输入，防残留（事件已 deafen 时不再被 onBaseTickEvent 校正）
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

    // 冷却阶段：本地恢复、不蹲，给你跑/跳的窗口；递减冷却后直接返回（不蹲）。
    if (mCooldown > 0) {
        mCooldown--;
        if (auto* mi = player->getMoveInputComponent()) mi->mIsSneakDown = false;
        return;
    }

    if (!mArmed) {
        // 不在窗口内：确保本地不蹲（与发包一致），零干扰。
        if (auto* mi = player->getMoveInputComponent()) mi->mIsSneakDown = false;
        return;
    }

    auto* moveInput = player->getMoveInputComponent();
    if (!moveInput) { disarm(); return; }

    // 落地即收窗：地面攻击本来就不暴击（fallDistance=0），继续蹲只是白限移动。
    if (player->isOnGround()) { disarm(); return; }

    const bool doSneak      = (mMode.mValue == Mode::Sneaking      || mMode.mValue == Mode::Both);
    const bool doStopSprint = (mMode.mValue == Mode::StopSprinting || mMode.mValue == Mode::Both);

    // 本地真实下蹲：游戏会序列化进 PAI 发包，本地与发包同源 -> 无不同步。
    moveInput->mIsSneakDown = doSneak;
    if (doStopSprint) moveInput->mIsSprinting = false;

    // PushDown（仅窗口内）：每 tick 无条件把垂直速度压到 -0.1（z1yr 同款）。
    // 位置因此持续下降 -> PAI 上报的位置也在降 -> 服务器 fallDistance>0。
    // 这才是暴击的来源；蹲只是伪装，不产生暴击。BaseTickEvent 在原 baseTick
    // 之前触发，速度改动当拍就被积分。
    if (mPushDown.mValue) {
        if (auto* state = player->getStateVectorComponent()) {
            state->mVelocity.y = -0.1f;
        }
    }

    // 窗口倒计时：结束后 disarm 并进入冷却（给你跑/跳的窗口）。
    if (--mTtl <= 0) {
        disarm();
        mCooldown = COOLDOWN;
    }
}

// 攻击驱动：任意出站攻击包触发（Aura / TriggerBot / 手动攻击 都算）；内部 guard 仅空中 + 冷却就绪。
void LegitCriticals::arm() {
    auto* ci = ClientInstance::get();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player || player->isOnGround()) return; // 仅空中
    if (mCooldown > 0) return;                   // 冷却中不开，避免常蹲锁死
    // 已激活则刷新窗口：连击期间状态持续，后续攻击才吃得到蹲+压速。
    mArmed = true;
    mTtl = CRIT_WINDOW;
}

// 网络侧：检测攻击包触发窗口；窗口内把 PAI 也显式打上蹲伏/断疾跑标志（与本地一致，双保险）。
void LegitCriticals::onPacketOutEvent(PacketOutEvent& event) {
    if (event.mPacket->getId() == PacketID::InventoryTransaction) {
        if (auto pkt = event.getPacket<InventoryTransactionPacket>()) {
            if (auto* cit = pkt->mTransaction.get()) {
                if (cit->type == ComplexInventoryTransaction::Type::ItemUseOnEntityTransaction) {
                    auto* iut = reinterpret_cast<ItemUseOnActorInventoryTransaction*>(cit);
                    if (iut->mActionType == ItemUseOnActorInventoryTransaction::ActionType::Attack) {
                        arm(); // 自驱动：仅空中 + 冷却就绪时开/续窗口
                    }
                }
            }
        }
    }

    if (!mArmed) return; // 不在窗口内：不碰 PAI（与本地一致）
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
