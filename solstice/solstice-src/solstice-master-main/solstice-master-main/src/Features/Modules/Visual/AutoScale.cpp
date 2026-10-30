//
// Created by vastrakai on 6/30/2024.
//

#include "AutoScale.hpp"

#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

namespace {
    // The reserved GuiData offsets are unverified on 1.21.120 and AutoScale
    // *writes* through them; a wrong pointer turned onEnable into a wild
    // write (+299B6B). Same guard ChatUtils uses for its tryGuiData.
    GuiData* tryGuiData()
    {
        auto* ci = ClientInstance::get();
        if (!InstanceLocator::isPlausibleObject(ci)) return nullptr;
        auto* guiData = ci->getGuiData();
        return InstanceLocator::isReadableMemory(guiData, 0x40) ? guiData : nullptr;
    }
}

void AutoScale::onEnable()
{
    auto* guiData = tryGuiData();
    if (!guiData)
    {
        // Nothing to scale: refuse rather than listening with no source.
        setEnabled(false);
        return;
    }

    // Store the old values
    mOldGuiScale = guiData->mGuiScale;
    mOldScaleMultiplier = guiData->mScalingMultiplier;
    mOldScaledResolution = guiData->mResolutionScaled;

    // Set the new values
    guiData->mGuiScale = mScaleSetting.mValue;
    guiData->mScalingMultiplier = 1 / mScaleSetting.mValue;
    guiData->mResolutionScaled = glm::vec2(guiData->mResolution.x * guiData->mScalingMultiplier, guiData->mResolution.y * guiData->mScalingMultiplier);

    gFeatureManager->mDispatcher->listen<RenderEvent, &AutoScale::onRenderEvent>(this);
}

void AutoScale::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &AutoScale::onRenderEvent>(this);

    // Restore the old values
    if (auto* guiData = tryGuiData())
    {
        guiData->mGuiScale = mOldGuiScale;
        guiData->mScalingMultiplier = mOldScaleMultiplier;
        guiData->mResolutionScaled = mOldScaledResolution;
    }
}

void AutoScale::onRenderEvent(RenderEvent& event)
{
    float scale = mScaleSetting.mValue;
    auto* guiData = tryGuiData();
    if (!guiData) return;
    guiData->mGuiScale = scale;
    guiData->mScalingMultiplier = 1 / scale;
    guiData->mResolutionScaled = glm::vec2(guiData->mResolution.x * guiData->mScalingMultiplier, guiData->mResolution.y * guiData->mScalingMultiplier);
}
