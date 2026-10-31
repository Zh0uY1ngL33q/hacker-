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

    // 攻击协作接口（由 Aura 在发出攻击包前调用，冗余保险；持续模型由 onBaseTickEvent 主导）：
    //   arm()   —— 立即应用一次本地状态（断疾跑/下蹲），仅当玩家在空中时生效。
    //   disarm()—— 恢复本地疾跑/下蹲（落地或关模块时由 onBaseTickEvent 调用）。
    void arm();
    void disarm();

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    bool mArmed = false;          // 暴击窗口是否激活（= 玩家在空中且模块启用）

    void applyLocal(class Actor* player);
};
