#pragma once
//
// Created for the 1.21.120 port.
//

#include <Features/Modules/Module.hpp>

// Corrects the walk when the aim is turned away from the camera direction.
//
// Two modes, because the two useful behaviours are not the same shape:
//
//   Smooth  rotates the local input continuously by delta * Strength. At 1 the walk
//           follows the body exactly (walks straight at the target, same as Aura's
//           Strafe); at low values it keeps the camera heading with a gentle bend.
//
//   Silent  rotates the input by the full delta and then snaps each component to
//           -1/0/1, the way LiquidBounce's MovementCorrection.SILENT does it. A
//           small aim change rounds back to "forward" and never moves you off the
//           line; only a large one becomes a diagonal step. That is the mode that
//           reads as "still walking forward, but the turn is taken into account".
class MoveFix : public ModuleBase<MoveFix> {
public:
    enum class Mode {
        Smooth,
        Silent,
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>(
        "Mode",
        "Smooth bends continuously; Silent snaps to the nearest of the eight walking "
        "directions so small aim changes leave the line untouched.",
        Mode::Silent, "Smooth", "Silent");

    NumberSetting mStrength = NumberSetting(
        "Strength",
        "Smooth only: how much of the body's turn is applied to the walk. "
        "0 = walk where the camera looks (vanilla), 1 = walk where the body faces.",
        0.35f, 0.0f, 1.0f, 0.05f);

    MoveFix() : ModuleBase("MoveFix", "Corrects the walk when the aim is turned",
                           ModuleCategory::Movement, 0, false) {
        addSettings(&mMode, &mStrength);
        VISIBILITY_CONDITION(mStrength, mMode.mValue == Mode::Smooth);

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
        if (mMode.mValue == Mode::Silent) return "Silent";
        return fmt::format("{}%", static_cast<int>(mStrength.mValue * 100.f));
    }

private:
    float mLastYaw = 0.f;
    bool mHasLastYaw = false;
};
