#pragma once
//
// Created by vastrakai on 7/18/2024.
//

#include <Features/Modules/Module.hpp>

class Animations : public ModuleBase<Animations> {
public:
    enum class Animation {
        Default,
        Blocking,
        Test,
        Test2,
        Test3,
        Test4
    };
    EnumSettingT<Animation> mAnimation = EnumSettingT("Animation", "The animation to use", Animation::Blocking, "Default", "Blocking", "Test", "Test2", "Test3", "Test4");
    NumberSetting mSwingTime = NumberSetting("Swing Time", "The time it takes to swing your arm", 6.f, 0.f, 20.0f, 1.f);
    // Test2 / Test3 only: the three settings below are the whole options list.
    NumberSetting mSwingSpeed2 = NumberSetting("Swing Speed", "Ticks per full swing (lower swings faster)", 6.f, 1.f, 20.f, 1.f);
    NumberSetting mRotAngle2 = NumberSetting("Rotation Angle", "Peak rotation of the swing, in degrees", 45.f, 0.f, 180.f, 1.f);
    BoolSetting mSmoothRot2 = BoolSetting("Smooth Rotation", "Ease the rotation instead of sweeping it linearly", true);
    // Test4 only: how far the sword dips when an attack connects.
    NumberSetting mHitDrop = NumberSetting("Hit Drop", "How far the sword moves down when a hit lands", 0.25f, 0.f, 1.f, 0.01f);
    BoolSetting mNoSwitchAnimation = BoolSetting("No Switch Animation", "Disables the switch animation", true);
    BoolSetting mFluxSwing = BoolSetting("Flux Swing", "Flux Client styled swinging", true);
    BoolSetting mThirdPersonBlock = BoolSetting("3rd person", "Displays 3rd person animations", false);
    // Holds the rotation for third person blocking, if you need to play with it, simply add da settings
    NumberSetting mXRot = NumberSetting("X Rotation", "The custom X rotation value. (default: 0)", -57.f, -360.f, 360.f, 0.01f);
    NumberSetting mYRot = NumberSetting("Y Rotation", "The custom Y rotation value. (default: 0)", -45.16f, -360.f, 360.f, 0.01f);
    NumberSetting mZRot = NumberSetting("Z Rotation", "The custom Z rotation value. (default: 0)", 13.14f, -360.f, 360.f, 0.01f);
    BoolSetting mCustomSwingAngle = BoolSetting("Custom Swing Angle", "Changes the swing angle", true);
    BoolSetting mOnlyOnBlock = BoolSetting("Only on Block", "Only changes the swing angle when blocking", false);
    NumberSetting mSwingAngleSetting = NumberSetting("Swing Angle", "The custom swing angle value. (default: -80)", -31.f, -360.f, 360.f, 0.01f);
    BoolSetting mSmallItems = BoolSetting("Small Items", "Makes items smaller", true);

    Animations() : ModuleBase("Animations", "Change your animations!", ModuleCategory::Visual, 0, false)
    {
        addSettings(
            &mAnimation,
            &mSwingTime,
            &mSwingSpeed2,
            &mRotAngle2,
            &mSmoothRot2,
            &mHitDrop,
            &mNoSwitchAnimation,
            &mFluxSwing,
            &mThirdPersonBlock,
            // &mXRot,
            // &mYRot,
            // &mZRot,
            &mCustomSwingAngle,
            &mOnlyOnBlock,
            &mSwingAngleSetting,
            &mSmallItems
        );

        // Test2 owns the held item outright, so it exposes exactly three knobs and
        // hides everything that belongs to the other modes.
        VISIBILITY_CONDITION(mSwingSpeed2, usesCustomRotation());
        VISIBILITY_CONDITION(mRotAngle2, usesCustomRotation());
        VISIBILITY_CONDITION(mSmoothRot2, usesCustomRotation());
        VISIBILITY_CONDITION(mHitDrop, usesHitDrop());
        VISIBILITY_CONDITION(mSwingTime, !usesSpecialLayout());
        VISIBILITY_CONDITION(mNoSwitchAnimation, !usesSpecialLayout());
        VISIBILITY_CONDITION(mFluxSwing, !usesSpecialLayout());
        VISIBILITY_CONDITION(mThirdPersonBlock, !usesSpecialLayout());
        VISIBILITY_CONDITION(mCustomSwingAngle, !usesSpecialLayout());
        VISIBILITY_CONDITION(mOnlyOnBlock, mCustomSwingAngle.mValue && !usesSpecialLayout());
        VISIBILITY_CONDITION(mSwingAngleSetting, mCustomSwingAngle.mValue && !usesSpecialLayout());
        VISIBILITY_CONDITION(mSmallItems, !usesSpecialLayout());

        mNames = {
            {Lowercase, "animations"},
            {LowercaseSpaced, "animations"},
            {Normal, "Animations"},
            {NormalSpaced, "Animations"}
        };
    }

    // Test2 / Test3 share the same three-option surface.
    bool usesCustomRotation() const
    {
        return mAnimation.mValue == Animation::Test2 || mAnimation.mValue == Animation::Test3;
    }

    bool usesHitDrop() const { return mAnimation.mValue == Animation::Test4; }

    bool usesSpecialLayout() const { return usesCustomRotation() || usesHitDrop(); }

    int mSwingDuration = 0.f;
    int mOldSwingDuration = 0.f;
    float* mSwingAngle = nullptr;
    bool mShouldBlock = false;
    // Test2 phase in [0,1). Integrated from the frame delta: the engine's own
    // swing-progress fields are read through offsets that no longer line up.
    float mTest2Phase = 0.f;
    // Test4: 0 = idle, otherwise the dip progress in (0,1].
    float mTest4Phase = 0.f;

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onSwingDurationEvent(class SwingDurationEvent& event);
    void onBoneRenderEvent(class BoneRenderEvent& event);
    void onBobHurtEvent(class BobHurtEvent& event);
};