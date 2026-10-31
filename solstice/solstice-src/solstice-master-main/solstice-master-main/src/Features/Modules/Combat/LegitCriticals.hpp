#pragma once
//
// LegitCriticals - legit (non-sprint, crouched) critical hits while airborne.
//

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>

class LegitCriticals : public ModuleBase<LegitCriticals> {
public:
    enum class Mode {
        Sneaking,   // crouch + cancel sprint only while rising (vy > 0); falling -> unchanged
        Full        // crouch + cancel sprint regardless of rising/falling, while airborne
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>(
        "Mode",
        "When to crouch for a legit crit: Sneaking = only while rising; Full = always while airborne",
        Mode::Sneaking,
        "Sneaking",
        "Full");

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
    void onPacketOutEvent(class PacketOutEvent& event);
};
