//
// Created by vastrakai on 6/29/2024.
//

#include "ClickGui.hpp"

#include <Features/Events/MouseEvent.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <Features/GUI/ModernDropdown.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

static bool lastMouseState = false;
static bool isPressingShift = false;
static ModernGui modernGui = ModernGui();


void ClickGui::onEnable()
{
    auto ci = ClientInstance::get();
    lastMouseState = !ci->getMouseGrabbed();

    ci->releaseMouse();

    gFeatureManager->mDispatcher->listen<MouseEvent, &ClickGui::onMouseEvent>(this);
    gFeatureManager->mDispatcher->listen<KeyEvent, &ClickGui::onKeyEvent, nes::event_priority::FIRST>(this);
}

void ClickGui::onDisable()
{
    gFeatureManager->mDispatcher->deafen<MouseEvent, &ClickGui::onMouseEvent>(this);
    gFeatureManager->mDispatcher->deafen<KeyEvent, &ClickGui::onKeyEvent>(this);

    if (lastMouseState) {
        ClientInstance::get()->grabMouse();
    }
}

void ClickGui::onWindowResizeEvent(WindowResizeEvent& event)
{
    modernGui.onWindowResizeEvent(event); // are you okay in the head 😭
}


void ClickGui::onMouseEvent(MouseEvent& event)
{
    event.mCancelled = true;
}

void ClickGui::onKeyEvent(KeyEvent& event)
{
    if (event.mKey == VK_ESCAPE) {
        if (!modernGui.isBinding && event.mPressed) this->toggle();
        event.mCancelled = true;
    }

    // Consume all key events to prevent unintended module toggles
    if (modernGui.isBinding) {
        event.mCancelled = true;
        return;
    }

    if (event.mKey == VK_SHIFT && event.mPressed) {
        isPressingShift = true;
        event.mCancelled = true;
    }
    else {
        isPressingShift = false;
    }
}

float ClickGui::getEaseAnim(EasingUtil ease, int mode) {
    switch (mode) {
    case 0: return ease.easeOutExpo(); break;
    case 1: return mEnabled ? ease.easeOutElastic() : ease.easeOutBack(); break;
    default: return ease.easeOutExpo(); break;
    }

}

void ClickGui::onRenderEvent(RenderEvent& event)
{
    if (mEnabled) ClientInstance::get()->releaseMouse();
    static float animation = 0;
    static int styleMode = 0; // Ease enum
    static int scrollDirection = 0;
    static char h[2] = { 0 };
    static EasingUtil inEase = EasingUtil();

    float delta = ImGui::GetIO().DeltaTime;

    this->mEnabled ? inEase.incrementPercentage(delta * mEaseSpeed.mValue / 10)
    : inEase.decrementPercentage(delta * 2 * mEaseSpeed.mValue / 10);
    float inScale = getEaseAnim(inEase, mAnimation.as<int>());
    if (inEase.isPercentageMax()) inScale = 0.996;
    if (mAnimation.mValue == ClickGuiAnimation::Zoom) inScale = MathUtils::clamp(inScale, 0.0f, 0.996);
    animation = MathUtils::lerp(0, 1, inEase.easeOutExpo());

    if (animation < 0.0001f) {
        return;
    }

    // Use IO to determine the scroll direction
    if (ImGui::GetIO().MouseWheel > 0) {
        scrollDirection = -1;
    }
    else if (ImGui::GetIO().MouseWheel < 0) {
        scrollDirection = 1;
    }
    else {
        scrollDirection = 0;
    }


    if (mStyle.mValue == ClickGuiStyle::Modern)
    {
        modernGui.render(animation, inScale, scrollDirection, h, mBlurStrength.mValue, mMidclickRounding.mValue, isPressingShift);
    }

}
