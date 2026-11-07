//
// Created by vastrakai on 6/24/2024.
//

#include "ClientInstance.hpp"

#include <libhat/Access.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>

#include "MinecraftGame.hpp"
#include "InstanceLocator.hpp"

#include <Utils/Structs.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <cmath>

#include "KeyboardMouseSettings.hpp"
#include <Windows.h>

namespace {
    // Separate SEH leaf: no STL iterators/objects requiring C++ unwinding.
    LoopbackPacketSender* readPacketSenderMember(ClientInstance* client, int offset)
    {
        __try {
            if (!InstanceLocator::isPlausibleObject(client) ||
                !InstanceLocator::isReadableMemory(
                    reinterpret_cast<const char*>(client) + offset, sizeof(void*)))
                return nullptr;
            auto* sender = hat::member_at<LoopbackPacketSender*>(client, offset);
            return InstanceLocator::isPlausibleObject(sender) ? sender : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }
}

ClientInstance* ClientInstance::get()
{
    // Do not keep a second cache that can outlive the locator's map membership.
    return InstanceLocator::locate(nullptr, nullptr);
}

glmatrixf ClientInstance::getViewMatrix()
{
    return hat::member_at<glmatrixf>(this, OffsetProvider::ClientInstance_mViewMatrix);
}

glm::vec2 ClientInstance::getFov()
{
    // OWorldToScreen multiplies the projected offset by these, so what it needs is
    // 1/tan(fov/2) per axis -- about (0.87, 1.43) for the default 70 degree
    // vertical fov at 16:9.
    //
    // The pair this reads lives at 0x720/0x724 on 1.21.120, not 0x728/0x72C: the
    // client's own vtable has `movss xmm0,[rcx+0x720]; ret` at slot 455,
    // `movss [rcx+0x720],xmm1; ret` at 456 and `movss xmm0,[rcx+0x724]; ret` at
    // 457 -- a get/set/get triple over two adjacent floats. The old offsets read
    // -FLT_MAX, which is what made every projection fail.
    //
    // The stored form is not fixed evidence yet: it can be the multiplier itself
    // or the angle in degrees. Accept both rather than betting on one -- a
    // multiplier lands in (0.1, 10), an angle in (30, 130) -- and fall back to the
    // default only when neither reading is sane.
    constexpr float kPi = 3.14159265358979323846f;
    const auto toFactor = [](float v) -> float {
        if (!std::isfinite(v)) return 0.f;
        if (v > 0.1f && v < 10.f) return v;
        if (v > 30.f && v < 130.f)
            return 1.f / std::tan(v * 0.5f * kPi / 180.f);
        return 0.f;
    };

    const float x = toFactor(hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovX));
    const float y = toFactor(hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovY));
    if (x > 0.f && y > 0.f)
        return { x, y };

    // Rebuild the factors for the default fov instead, so world-to-screen keeps
    // producing sane coordinates. Used only when the member does not read as a fov
    // at all.
    constexpr float kDefaultVerticalFovDeg = 70.f;
    const float yFactor = 1.f / std::tan(kDefaultVerticalFovDeg * 0.5f * kPi / 180.f);

    float aspect = 16.f / 9.f;
    if (auto* guiData = getGuiData()) {
        const auto res = guiData->mResolution;
        if (res.x > 0.f && res.y > 0.f)
            aspect = res.x / res.y;
    }
    return { yFactor / aspect, yFactor };
}

MinecraftSim* ClientInstance::getMinecraftSim()
{
    return hat::member_at<MinecraftSim*>(this, OffsetProvider::ClientInstance_mMinecraftSim);
}

LevelRenderer* ClientInstance::getLevelRenderer()
{
    return hat::member_at<LevelRenderer*>(this, OffsetProvider::ClientInstance_mLevelRenderer);
}

LoopbackPacketSender* ClientInstance::getPacketSender()
{
    // The offset is now a measured constant (see OffsetProvider), not a signature
    // result: the previous signature pointed at a virtual call site and answered
    // 0x228, which is the *pointer* of vtable slot 69. Reading a member there gave
    // null every time, so every module that sends a packet -- Aura's attacks,
    // Scaffold's placements, all of chat -- was writing into nothing.
    return readPacketSenderMember(this, OffsetProvider::ClientInstance_mPacketSender);
}

GuiData* ClientInstance::getGuiData()
{
    // A wrong member offset here does not crash, it feeds garbage into every
    // projection that asks for mResolution, so validate the object before
    // handing it out. GuiData starts with a vtable in the game image, which is
    // what isPlausibleObject tests.
    auto* gui = hat::member_at<GuiData*>(this, OffsetProvider::ClientInstance_mGuiData);
    if (!InstanceLocator::isPlausibleObject(gui))
        return nullptr;
    return gui;
}

bgfx_context* ClientInstance::getBGFX()
{
    return *reinterpret_cast<bgfx_context**>(SigManager::ClientInstance_mBgfx);
}

ClientInputHandler* ClientInstance::getInputHandler()
{
    // Unresolved in 1.21.120 ("probably rewritten"); index 0 is the dtor slot
    // and a wild jump, so refuse instead of gamble.
    const int index = static_cast<int>(OffsetProvider::ClientInstance_getInputHandler);
    if (index < 2 || index >= 4096)
        return nullptr;
    return MemUtils::callVirtualFunc<ClientInputHandler*>(index, this);
}

KeyboardMouseSettings* ClientInstance::getKeyboardSettings()
{
    auto* handler = getInputHandler();
    if (!InstanceLocator::isPlausibleObject(handler))
        return nullptr;
    return handler->getMappingFactory()->getKeyboardMouseSettings();
}

MinecraftGame* ClientInstance::getMinecraftGame()
{
    return MinecraftGame::getInstance();
}

Actor* ClientInstance::getLocalPlayer()
{
    return MemUtils::callVirtualFunc<Actor*>(OffsetProvider::ClientInstance_getLocalPlayer, this);
}

BlockSource* ClientInstance::getBlockSource()
{
    // Slot 318 is a one-line getter: `mov rax,[rcx+0x118]; ret` (vtable RVA
    // 0x8DDB590, slot body 0x1A26180), so the slot and the member offset are
    // both settled. The pointee TYPE is not.
    //
    // Offline evidence on 3.9.20.300505 says the member never holds a BlockSource:
    //   * ClientInstance's own initialiser (0x1945160) nulls 0xF0 / 0x108 / 0x110
    //     / 0x118 as one contiguous block of subsystem pointers;
    //   * across 541 qword stores to [reg+0x118] in .text, ZERO are fed by a
    //     function that installs the BlockSource vtable (0x921B490);
    //   * all 73 direct calls into the three BlockSource constructors
    //     (0x5D285E0 / 0x5D28670 / 0x5D29C40) construct it in place on the stack
    //     (`lea rcx,[rbp+X]; call ctor`), never as a heap pointer into a member.
    // The one runtime observation we have agrees: the object there started with
    // 0x0F910AE0, a heap address, not a vtable in the game image.
    //
    // Handing that pointer to the 17 consumers as BlockSource*, as this getter
    // used to, turns "we do not know what this is" into a wild vtable jump. Ask
    // the same question getGuiData() asks of its member: is this even a game
    // object? A real BlockSource passes (its vtable lives in the main module);
    // anything else is refused.
    auto* blockSource = MemUtils::callVirtualFunc<BlockSource*>(
        OffsetProvider::ClientInstance_getBlockSource, this);
    if (!InstanceLocator::isPlausibleObject(blockSource))
        return nullptr;
    return blockSource;
}

Options* ClientInstance::getOptions()
{
    return MemUtils::callVirtualFunc<Options*>(OffsetProvider::ClientInstance_getOptions, this);
}

std::string ClientInstance::getScreenName()
{
    std::string name = "no_screen";
    name = MemUtils::callVirtualFunc<std::string&, std::string&>(OffsetProvider::ClientInstance_getScreenName, this, name);
    return name;
}

void ClientInstance::setDisableInput(bool disable)
{
    // SUSPECT in 1.21.120 (84423017) -- a virtual index that large is unmapped.
    const int index = static_cast<int>(OffsetProvider::ClientInstance_setDisableInput);
    if (index < 2 || index >= 4096)
        return;
    MemUtils::callVirtualFunc<void>(index, this, disable);
}

bool ClientInstance::getMouseGrabbed()
{
    // Despite the name this answers "is the mouse free", i.e. is a screen open --
    // the original virtual call is inverted on purpose, which is what the comment
    // below used to say.
    //
    // MinecraftGame_mMouseGrabbed does not resolve on 1.21.120 and the field read
    // answers false, so the inversion turned this into a constant true: every
    // caller was told a screen is always open. Scaffold broke out of its placement
    // loop on the first iteration, InvManager and MidclickAction returned before
    // doing anything, and BlockESP never drew.
    //
    // The screen name is the state the callers actually care about: hud_screen is
    // the only screen that holds the mouse, every other one releases it. When the
    // name is unavailable this falls back to "screen open", which is the same
    // answer as before, so a failure here cannot make a module run wild.
    const std::string screen = getScreenName();
    if (screen.empty() || screen == "no_screen")
        return true;
    return screen != "hud_screen";
}

void ClientInstance::grabMouse()
{
    static uintptr_t func = SigManager::ClientInstance_grabMouse;
    if (!func)
        return;
    MemUtils::callFastcall<void>(func, this);

}

void ClientInstance::releaseMouse()
{
    static uintptr_t func = SigManager::ClientInstance_releaseMouse;
    if (func)
        MemUtils::callFastcall<void>(func, this);

    if (auto* game = getMinecraftGame(); InstanceLocator::isPlausibleGame(game))
        game->setMouseGrabbed(false);
}

void ClientInstance::playUi(const std::string& soundName, float volume, float pitch)
{   // this is exactly what the game does
    if (auto* game = getMinecraftGame(); InstanceLocator::isPlausibleGame(game))
        game->playUi(soundName, volume, pitch);
}
