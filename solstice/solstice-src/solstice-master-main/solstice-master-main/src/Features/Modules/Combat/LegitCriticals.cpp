//
// LegitCriticals - legit critical hits while airborne.
//
// 模型（参考 z1yr addon module/combat/criticals.py + 已编译通过的 ServerSneak 写法）：
//   Bedrock 暴击由"下落/下蹲"状态决定，服务器从最近一份 PlayerAuthInput 判定。
//   因此本模块【只改写发出的 PAI 包】，绝不碰本地 MoveInputComponent —— 这样：
//     1) 不卡本地下蹲（玩家能正常行走/疾跑）；
//     2) 关模块后本地状态零残留；
//     3) 空中每 tick 都改 PAI，攻击必骑在已修正的输入上，不依赖包顺序。
//   疾跑与否不影响 Bedrock 暴击判定；下蹲会自动取消疾跑，故 Sneaking/Both 模式
//   才是真正的合法暴击路径，StopSprinting（只清疾跑标志、不下蹲）本身不会出暴击。
//

#include "LegitCriticals.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

void LegitCriticals::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
}

void LegitCriticals::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &LegitCriticals::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &LegitCriticals::onPacketOutEvent>(this);
    mArmed = false; // 仅清标志；本地状态从未被本模块修改，故无需恢复
}

// 持续模型：只在本地维护 mArmed（是否空中），不修改任何本地输入。
void LegitCriticals::onBaseTickEvent(BaseTickEvent& event) {
    auto* player = event.mActor; // 本地玩家
    if (!player) return;
    mArmed = !player->isOnGround();   // 空中=开窗口，落地=关窗口
}

// 攻击前由 Aura 调用（冗余保险）：立即标记窗口开启，不等下一 tick。
void LegitCriticals::arm() {
    auto* player = ClientInstance::get()->getLocalPlayer();
    if (!player || player->isOnGround()) return;
    mArmed = true;
}

// 网络侧：窗口内（即空中）改写发出的 PlayerAuthInput。
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
        // 下蹲：置 sneak 标志，并清 STOP_SNEAKING。服务器据此判定蹲伏，
        // 配合下落速度即为合法暴击（下蹲会自动取消疾跑）。
        paip.mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
        paip.mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}
