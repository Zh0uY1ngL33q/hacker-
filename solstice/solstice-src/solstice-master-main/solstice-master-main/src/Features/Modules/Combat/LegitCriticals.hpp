#pragma once
//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>

class LegitCriticals : public ModuleBase<LegitCriticals> {
public:
    enum class Mode {
        Sneaking,       // 只下蹲（置 sneak 标志），不动疾跑
        StopSprinting,  // 只阻断疾跑（清 sprint 标志），不下蹲
        Both            // 下蹲 + 阻断疾跑
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>(
        "Mode",
        "Airborne legit crits (real crouch + stop sprint): Sneaking=crouch(crit); StopSprinting=stop sprint only(no crit); Both=both",
        Mode::Sneaking,
        "Sneaking",
        "StopSprinting",
        "Both");

    // PushDown：暴击窗口内每 tick 把下落速度压到 -0.1，维持 fallDistance>0，
    // 使跳跃顶点/上升段也暴击（对齐 z1yr addon 的 SetMotion(-0.1)）。
    // 独立开关：若怀疑被检测或只在真正下落段打人，可关闭。
    BoolSetting mPushDown = BoolSetting(
        "PushDown",
        "While crit window, force fall speed to -0.1 each tick to keep fallDistance>0 (crit at apex/rising); disable if unneeded",
        true);

    LegitCriticals() : ModuleBase("LegitCriticals", "Legit critical hits: cancel sprint and crouch before attacking while airborne", ModuleCategory::Combat, 0, false) {
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

    // 攻击协作接口（由 Aura 在发出攻击包前调用）：
    //   仅在攻击前触发一个短时暴击窗口（CRIT_WINDOW tick），窗口内临时置真实下蹲 +
    //   断疾跑 + (可选)PushDown，攻击后自动恢复。模块【不空中持续介入】，无攻击时零干扰。
    //   onPacketOutEvent 在窗口内同步改写发出的 PAI 包（网络侧蹲伏标志）作双保险。
    void arm();
    void disarm();                // 攻击窗口结束/关模块时恢复本地输入，防残留

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    static constexpr int CRIT_WINDOW = 3;  // 攻击触发后的介入窗口(tick)：覆盖攻击当拍+后续，保证 PAI 带蹲
    bool mArmed = false;          // 暴击窗口是否激活（仅由 arm() 在攻击前触发，不全局自动开启）
    int  mTtl   = 0;              // 窗口剩余 tick；递减到 0 自动 disarm
};
