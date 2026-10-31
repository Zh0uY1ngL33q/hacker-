#pragma once
//
// Created by vastrakai on 7/18/2024.
//

#include <Hook/Hook.hpp>

class AnimationHooks : public Hook {
public:
    AnimationHooks() : Hook()
    {
        mName = "AnimationHooks";
    }

    static std::unique_ptr<Detour> mSwingDetour;
    static std::unique_ptr<Detour> mBobHurtDetour;
    static std::unique_ptr<Detour> mSwingTraceDetour;


    static int getCurrentSwingDuration(class Actor* actor);
    static void* doBobHurt(void* _this, glm::mat4* matrix);
    static void swingTraceEntry(void* _this, void* a1, void* a2, void* a3);
    void init() override;
};