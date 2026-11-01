#pragma once
//
// LegitCriticals - legit (crouched) critical hits while airborne.
//
#include <unordered_map>

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

    // PushDown：仅在暴击窗口内把下落速度压到 -0.1 维持 fallDistance>0，
    // 使跳跃顶点/上升段也暴击（对齐 z1yr 的 SetMotion(-0.1)）。默认关：只靠真实下落段暴击。
    BoolSetting mPushDown = BoolSetting(
        "PushDown",
        "While crit window, force fall speed to -0.1 to keep fallDistance>0 (crit at apex/rising); off = descent crits only",
        false);

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
    void arm();
    void disarm();                // 恢复本地输入，防残留
    bool targetVulnerable(uint64_t targetId);  // i-frame gate: false if target just hit (invulnerable)

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    static constexpr int CRIT_WINDOW  = 3;  // 蹲伏爆发(tick)：覆盖攻击当拍+后续，保证暴击
    static constexpr int COOLDOWN     = 5;  // 静默冷却(tick)：本地恢复、可跑可跳，避免被锁死
    static constexpr int IFRAME_TICKS = 10; // 受伤无敌帧时长(tick)，用于抑制重复暴击
    bool mArmed   = false;          // 暴击窗口是否激活（仅由 arm() 触发，不全局自动开启）
    int  mTtl     = 0;              // 窗口剩余 tick；递减到 0 自动 disarm 并进入冷却
    int  mCooldown = 0;             // 冷却剩余 tick；>0 时本地不蹲，给你跑/跳的窗口
    int  mTick    = 0;              // 自增 tick 计数器，供 i-frame 回退判定
    std::unordered_map<uint64_t, int> mLastHitTick; // 回退用：每目标上次攻击 tick
};
