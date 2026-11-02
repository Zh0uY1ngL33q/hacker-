#pragma once
//
// Created for the 1.21.120 port.
//

#include <Features/Modules/Module.hpp>

// Blends the walk between "where the camera looks" and "where the body faces".
//
// The three states, measured in game with the aim turned onto a target that sits
// left-front of the camera:
//
//   off        walk along the camera, body facing the target  -- straight line,
//              fastest, and impossible for vanilla to produce
//   Strength 1 walk along the body, i.e. straight at the target -- this is what
//              Aura's own Strafe sub-option produces, and the two are equivalent
//   Strength k the body's turn is applied only k of the way, so the walk keeps
//              the camera heading with a bend towards the aim -- still moving
//              forward as asked, with the turn showing up as an arc
//
// The default sits between the last two on purpose: full strength duplicates
// Strafe, and zero is the broken vanilla state.
class MoveFix : public ModuleBase<MoveFix> {
public:
    NumberSetting mStrength = NumberSetting(
        "Strength",
        "How much of the body's turn is applied to the walk. "
        "0 = walk where the camera looks (vanilla), 1 = walk where the body faces "
        "(same as Aura's Strafe).",
        0.35f, 0.0f, 1.0f, 0.05f);

    MoveFix() : ModuleBase("MoveFix", "Blends the walk between the camera and the aim",
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
