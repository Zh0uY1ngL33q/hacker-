#pragma once
//
// LegitCriticals - legit (crouched) critical hits while airborne.
//

// 设计：本地输入驱动（local-only），与网络天然一致（无不同步）。
//   暴击窗口(CRIT_WINDOW tick)内：置本地 MoveInputComponent::mIsSneakDown（真实下蹲），
//   游戏会把蹲状态序列化进 PlayerAuthInput 发给服务器——本地与发包同源，绝不会不同步。
//   窗口结束后进入冷却(COOLDOWN tick)：本地恢复、不再蹲，期间你可以正常跑、跳。
//   这种"蹲几拍 -> 静默几拍"的节律保证你始终有窗口能跳跃，不会被锁死。
//   攻击驱动：检测到任意出站攻击包(InventoryTransaction Attack)才开窗口（仅空中），
//   因此 Aura / TriggerBot / 手动攻击 都能触发，且只在攻击时介入（非全局）。
//   PushDown（可选，仅窗口内）：把下落速度压到 -0.1 维持 fallDistance>0，使顶点/上升段也暴击。
//   （之前"只改 PAI 包、本地不动"的版本会造成本地与服务器状态不一致 -> 不同步，已弃用。）
//

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>

class LegitCriticals : public ModuleBase<LegitCriticals> {
public:
    enum class Mode {
        Sneaking,       // 只下蹲（置 sneak），不动疾跑
        StopSprinting,  // 只阻断疾跑（清 sprint），不下蹲
        Both            // 下蹲 + 阻断疾跑
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>(
        "Mode",
        "Airborne legit crits (local sneak -> serialized into PAI, no desync): Sneaking=crouch(crit); StopSprinting=stop sprint; Both=both",
        Mode::Sneaking,
        "Sneaking",
        "StopSprinting",
        "Both");

    // PushDown：暴击窗口内每 tick 把垂直速度压到 -0.1（z1yr 的 SetMotion(-0.1) 同款，
    // 无条件、空中即压）。这是暴击的来源：攻击时位置已在下降 -> 服务器 fallDistance>0。
    // 蹲只是伪装，不产生暴击。默认开。
    BoolSetting mPushDown = BoolSetting(
        "PushDown",
        "While crit window, force fall speed to -0.1 every tick so fallDistance>0 at attack time (the actual crit source); sneak alone never crits",
        true);

    LegitCriticals() : ModuleBase("LegitCriticals", "Legit critical hits: crouch locally while airborne (consistent with PAI, no desync)", ModuleCategory::Combat, 0, false) {
        addSettings(&mMode, &mPushDown);

        mNames = {
            {Lowercase, "legitcriticals"},
            {LowercaseSpaced, "legitcriticals"},
            {Normal, "LegitCriticals"},
            {NormalSpaced, "LegitCriticals"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
    void applyInput(class PlayerAuthInputPacket& paip);

    // 攻击驱动：检测任意出站攻击包触发短时窗口（仅空中 + 冷却就绪）。
    void arm();                   // 空中+冷却就绪 -> 开窗口；已激活则刷新窗口
    void disarm();                // 恢复本地输入，防残留

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    // 窗口 10 tick：状态生效比攻击包晚一拍，第一击只是"播种"，暴击吃在窗口内的
    // 后续攻击上（Aura 连击 / 手动连点）；窗口太短的话后续攻击全落在状态外。
    static constexpr int CRIT_WINDOW  = 10; // 蹲+压速持续(tick)
    static constexpr int COOLDOWN     = 3;  // 静默冷却(tick)：本地恢复、可跑可跳
    bool mArmed   = false;          // 暴击窗口是否激活（仅由攻击包触发，不全局自动开启）
    int  mTtl     = 0;              // 窗口剩余 tick；窗口内再攻击则刷新
    int  mCooldown = 0;             // 冷却剩余 tick；>0 时本地不蹲，给你跑/跳的窗口
};
