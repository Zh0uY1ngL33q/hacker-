//
// Created by vastrakai on 6/24/2024.
//

#include "ClientInstance.hpp"

#include <windows.h>
#include <libhat/Access.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
// getPacketSender() inspects sender->vtable and constructs a fallback object of
// this type. ClientInstance.hpp only forward-declares it, and a unity group that
// does not happen to include the definition elsewhere fails with C2027.
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>

#include "MinecraftGame.hpp"
#include "InstanceLocator.hpp"

#include <Utils/Structs.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <cmath>
#include <spdlog/spdlog.h>
#include <atomic>

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
    auto* sender = readPacketSenderMember(this, OffsetProvider::ClientInstance_mPacketSender);
    if (sender && sender->vtable)
        return sender;

    // 37 call sites dereference the result immediately, so a null pointer here is a
    // crash rather than a dropped packet. Answer with an object whose vtable is
    // empty instead: LoopbackPacketSender::send refuses it, nothing is sent, and
    // the reason is logged once.
    static LoopbackPacketSender sUnavailable{};
    static bool warned = false;
    if (!warned)
    {
        warned = true;
        spdlog::warn("[net] packet sender unavailable (member 0x{:X} reads 0x{:X}); outgoing packets are dropped",
                     OffsetProvider::ClientInstance_mPacketSender,
                     reinterpret_cast<uintptr_t>(sender));
    }
    return &sUnavailable;
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
    return MemUtils::callVirtualFunc<BlockSource*>(OffsetProvider::ClientInstance_getBlockSource, this);
}

Options* ClientInstance::getOptions()
{
    return MemUtils::callVirtualFunc<Options*>(OffsetProvider::ClientInstance_getOptions, this);
}

std::string ClientInstance::getScreenName()
{
    const int slot = static_cast<int>(OffsetProvider::ClientInstance_getScreenName);
    if (slot < 2 || slot >= 4096)
        return "no_screen";

    // The wrapper behind this slot (0x1A19600 on this build) does
    // `mov rcx,[rcx+0xD8]; mov rax,[rcx]; ... call [rax+0x658]` -- it hands the
    // call to a sub-object and then builds the string out of what comes back.
    // 0xD8 has not been confirmed to be an object pointer on 1.21.120, and a
    // wrong pointer there is a jump into arbitrary memory rather than a null
    // read, so refuse the call unless the member looks like an object.
    void* sub = hat::member_at<void*>(this, 0xD8);
    if (!InstanceLocator::isPlausibleObject(sub))
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            spdlog::warn("[ui] getScreenName: ClientInstance+0xD8 (0x{:X}) is not a plausible object; reporting no_screen",
                         reinterpret_cast<uintptr_t>(sub));
        }
        return "no_screen";
    }

    std::string name = "no_screen";
    name = MemUtils::callVirtualFunc<std::string&, std::string&>(slot, this, name);
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
    // the only screen that holds the mouse, every other one releases it. The name
    // comes from virtual slot 266, which forwards into a sub-object; if that chain
    // is not usable the answer falls back to cursor visibility rather than to a
    // constant, because a constant true here stops every module hotkey.
    const std::string screen = getScreenName();

    // One line every five seconds while the answer is being established: a slot
    // that never worked and a slot that works both look identical from inside the
    // modules that consume this, and "hotkeys do nothing" is the only symptom.
    // The name itself is the verdict -- hud_screen means the chain is live.
    static std::atomic<long long> sLastLog{ 0 };
    const long long now = static_cast<long long>(GetTickCount64());
    long long expected = sLastLog.load(std::memory_order_relaxed);
    if (now - expected >= 5000 && sLastLog.compare_exchange_strong(expected, now))
        spdlog::info("[ui] screen=\"{}\"", screen);

    if (!screen.empty() && screen != "no_screen")
        return screen != "hud_screen";

    // The name is unavailable, so fall back to what the OS knows: the game hides
    // the cursor while it owns the mouse for gameplay and shows it whenever a
    // screen needs pointing at. Reporting "screen open" here is what killed every
    // hotkey -- KeyHook gates all module binds on this single answer -- so the
    // fallback leans the other way and assumes gameplay unless the cursor says
    // otherwise.
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (GetCursorInfo(&cursor))
        return (cursor.flags & CURSOR_SHOWING) != 0;
    return false;
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
