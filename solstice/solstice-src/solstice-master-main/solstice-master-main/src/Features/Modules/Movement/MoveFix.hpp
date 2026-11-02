#pragma once
//
// Created for the 1.21.120 port.
//

#include <Features/Modules/Module.hpp>

// Makes the walk follow the aim instead of the camera.
//
// Bedrock builds the world move direction from the camera, while Aura and
// Scaffold only rewrite the actor's rotation. The body therefore points at the
// target while W still means "the way the camera looks", so the player walks a
// straight line along the original heading with the body facing somewhere else --
// a combination vanilla cannot produce, because vanilla keeps facing and walking
// locked together.
//
// Each tick this rotates the local input by the same angle the body turned, which
// re-locks the two: W becomes "forward relative to where the body faces", and the
// walk follows the aim toward the target. The slider is how much of the turn is
// applied: 0 leaves the game alone (camera walking), 1 follows the aim exactly,
// and values in between trail behind it, which is the slight arc most clients show.
class MoveFix : public ModuleBase<MoveFix> {
public:
    NumberSetting mStrength = NumberSetting(
        "Strength",
        "How much of the aim rotation is applied to the walk. "
        "0 = walk where the camera looks, 1 = walk where the body faces.",
        1.0f, 0.0f, 1.0f, 0.05f);

    MoveFix() : ModuleBase("MoveFix", "Walks where you aim instead of where the camera looks",
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
