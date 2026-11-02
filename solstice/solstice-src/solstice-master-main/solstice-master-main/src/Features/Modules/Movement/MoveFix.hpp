#pragma once
//
// Created for the 1.21.120 port.
//

#include <Features/Modules/Module.hpp>

// Keeps the walking direction attached to the keys when the aim is rotated.
//
// Bedrock turns the movement input into a world direction with the actor's own
// yaw, so anything that writes ActorRotationComponent -- Aura aiming at a target,
// Scaffold facing the placement side -- also turns the walk. Holding W while Aura
// aims sideways walks sideways, which is the "impossible movement" the server can
// see and rubber-band.
//
// This rotates the input back by the same angle, so the world direction stays
// where the keys pointed. The slider is how much of that rotation is cancelled:
// 0 leaves the game alone, 1 preserves the direction completely, values in
// between split the difference (which is what most clients expose).
class MoveFix : public ModuleBase<MoveFix> {
public:
    NumberSetting mStrength = NumberSetting(
        "Strength",
        "Fraction of the aim rotation that is cancelled out of the walk direction. "
        "0 = vanilla, 1 = the direction you pressed is kept exactly.",
        1.0f, 0.0f, 1.0f, 0.05f);

    MoveFix() : ModuleBase("MoveFix", "Keeps your walk direction when the aim is rotated",
                           ModuleCategory::Movement, 0, false) {
        addSettings(&mStrength);

        mNames = {
            {Lowercase, "movefix"},
            {LowercaseSpaced, "move fix"},
            {Normal, "MoveFix"},
            {NormalSpaced, "Move Fix"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);

    std::string getSettingDisplay() override
    {
        return fmt::format("{}%", static_cast<int>(mStrength.mValue * 100.f));
    }

private:
    float mLastYaw = 0.f;
    bool mHasLastYaw = false;
};
