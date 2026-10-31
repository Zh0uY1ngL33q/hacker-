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
        "LegitCriticals 动作：Sneaking=只下蹲；StopSprinting=只断疾跑；Both=两者都做（均需在空中）",
        Mode::StopSprinting,
        "Sneaking",
        "StopSprinting",
        "Both");

    LegitCriticals() : ModuleBase("LegitCriticals", "Legit critical hits: cancel sprint and crouch before attacking while airborne", ModuleCategory::Combat, 0, false) {
        addSettings(&mMode);

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
    //   arm()   —— 进入暴击窗口：断疾跑（StopSprinting/Both）+ 下蹲（Sneaking/Both），
    //              仅当玩家在空中时生效；窗口持续到下一 tick 自动 disarm()。
    //   disarm()—— 离开窗口，恢复本地疾跑/下蹲（由 onBaseTickEvent 在下一 tick 自动调用）。
    void arm();
    void disarm();

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    bool mArmed = false;          // 暴击窗口是否激活
    uint64_t mArmTick = 0;        // 进入窗口的 tick 计数（用于下一 tick 自动恢复）
    static uint64_t sTick;        // 全局 tick 计数（每 BaseTickEvent +1）

    void applyLocal(class Actor* player);
};
