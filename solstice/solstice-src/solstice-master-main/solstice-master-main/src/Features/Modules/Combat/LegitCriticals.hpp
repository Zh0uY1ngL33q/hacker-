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
        "LegitCriticals 动作（仅改发出的 PAI 包，不碰本地状态）：Sneaking=下蹲(出暴击)；StopSprinting=只断疾跑(不暴击)；Both=两者都（均仅在空中生效）",
        Mode::Sneaking,
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

    // 攻击协作接口（由 Aura 在发出攻击包前调用，冗余保险）：
    //   立即标记暴击窗口开启（仅空中生效），让本 tick 的攻击必骑在已改写的
    //   PlayerAuthInput 上。持续-空中模型由 onBaseTickEvent 主导。
    //   本模块【不修改本地 MoveInputComponent】，只改写发出的 PAI 包，因此不会
    //   卡本地下蹲、关模块后本地状态零残留。
    void arm();

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    bool mArmed = false;          // 暴击窗口是否激活（= 玩家在空中且模块启用）
};
