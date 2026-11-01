//
// Created by vastrakai on 7/7/2024.
//

#include "RenderUtils.hpp"

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

#include "ColorUtils.hpp"
#include "MathUtils.hpp"

void RenderUtils::drawOutlinedAABB(const AABB& aabb, bool filled, const ImColor& color)
{
    std::vector<ImVec2> imPoints = MathUtils::getImBoxPoints(aabb);

    auto drawList = ImGui::GetBackgroundDrawList();

    if (filled) drawList->AddConvexPolyFilled(imPoints.data(), imPoints.size(), ImColor(color.Value.x, color.Value.y, color.Value.z, 0.25f));
    drawList->AddPolyline(imPoints.data(), imPoints.size(), color, 0, 2.0f);
}

bool RenderUtils::worldToScreen(glm::vec3 pos, ImVec2& screenPos)
{
    // Every ESP-style module calls this once per entity per frame, and until now
    // none of them ran at all (RenderEvent was never dispatched on the GL path).
    // getGuiData() can be null before the client is fully up, and dereferencing it
    // here would turn a missing overlay into a crash.
    auto* ci = ClientInstance::get();
    if (!ci) return false;
    auto* guiData = ci->getGuiData();
    if (!guiData) return false;
    auto corrected = transform.mMatrix;
    glm::vec2 screen;
    bool result = corrected.OWorldToScreen(transform.mOrigin, pos, screen, transform.mFov, guiData->mResolution);
    screenPos = ImVec2(screen.x, screen.y);
    return result;
}