#pragma once
//
// Created by vastrakai on 7/22/2024.
//

#include <Features/Modules/Module.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <SDK/Minecraft/Actor/Components/ActorHeadRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/MobBodyRotationComponent.hpp>


class Freecam : public ModuleBase<Freecam> {
public:
    enum class Mode
    {
        Normal,
        Detached
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>("Mode", "The mode of the freecam\nNormal: Fakes the player's position\nDetached: Moves independently of the player", Mode::Normal, "Normal", "Detached");
    NumberSetting mSpeed = NumberSetting("Speed", "Speed of the freecam", 5.5f, 0.1f, 10.0f, 0.1f);
    BoolSetting mDisableOnLagback = BoolSetting("Disable On Lagback", "Disable freecam if you receive a teleport", true);

    Freecam() : ModuleBase<Freecam>("Freecam", "Move independently of your player", ModuleCategory::Player, 0, false) {
        addSetting(&mMode);
        addSetting(&mSpeed);
        addSetting(&mDisableOnLagback);

        mNames = {
            {Lowercase, "freecam"},
            {LowercaseSpaced, "freecam"},
            {Normal, "Freecam"},
            {NormalSpaced, "Freecam"}
        };

        gFeatureManager->mDispatcher->listen<LookInputEvent, &Freecam::onLookInputEvent>(this);
    }

    ActorRotationComponent mLastRot;
    ActorHeadRotationComponent mLastHeadRot;
    MobBodyRotationComponent mLastBodyRot;
    glm::vec3 mAABBMin;
    glm::vec3 mAABBMax;
    glm::vec3 mSvPos;
    glm::vec3 mSvPosOld;
    glm::vec3 mOldPos;

    // Detached mode vars
    glm::vec3 mOrigin;
    glm::vec3 mOldOrigin;
    glm::vec2 mRotRads;

    std::map<CameraMode, glm::vec2> mOriginalRotRads;
    std::unordered_map<EntityId, int> mCameras;
    bool mResetRot = false;
    glm::vec2 mHeadYaw;
    int mLastCameraState = 0;


    glm::vec3 getLerpedOrigin()
    {
        // getGameSim() answers null when the simulation clock cannot be located on
        // this build, and this used to dereference it unconditionally -- enabling
        // Freecam then read through null on the very first frame. One tick at
        // 20 tps is the fallback when the real delta time is unavailable.
        float deltaTime = 0.05f;
        if (auto* client = ClientInstance::get())
            if (auto* sim = client->getMinecraftSim())
                if (auto* game = sim->getGameSim())
                    if (game->mDeltaTime > 0.f && game->mDeltaTime < 1.f)
                        deltaTime = game->mDeltaTime;
        return MathUtils::lerp(mOldOrigin, mOrigin, deltaTime);
    }

    void onEnable() override;
    void onDisable() override;
    void onPacketInEvent(class PacketInEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);
    void onActorRenderEvent(class ActorRenderEvent& event);
    void onLookInputEvent(class LookInputEvent& event);

    std::string getSettingDisplay() override
    {
        return mMode.mValues[mMode.as<int>()];
    }

};
