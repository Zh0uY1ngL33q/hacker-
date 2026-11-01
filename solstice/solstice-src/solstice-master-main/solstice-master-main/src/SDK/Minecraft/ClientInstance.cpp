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
    // These are not angles. OWorldToScreen multiplies the projected offset by
    // them, so each one is 1/tan(fov/2) for that axis: about (0.87, 1.43) for the
    // game's default 70 degree vertical fov at 16:9.
    //
    // On 1.21.120 the offsets below no longer hold that field. The ESP probe reads
    // (0.00, -1.7014118e+38), i.e. -FLT_MAX, and a multiplier that size makes
    // every projection fail -- which is why ESP, Tracers, Nametags and ItemESP all
    // drew nothing even once the camera origin was correct.
    float x = hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovX);
    float y = hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovY);

    const auto usable = [](float v) { return std::isfinite(v) && v > 0.1f && v < 10.f; };
    if (usable(x) && usable(y))
        return { x, y };

    // Rebuild the factors for the default fov instead, so world-to-screen keeps
    // producing sane coordinates. This is an approximation: if the player has
    // changed the fov setting the boxes will be slightly off-centre, but visible
    // beats blank, and the real offsets can be filled in once they are known.
    constexpr float kPi = 3.14159265358979323846f;
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
    const int offset = OffsetProvider::ClientInstance_mPacketSender;
    if (offset <= 0 || offset >= 0x8000) return nullptr;
    // Current 1.21.120 hits include [vtable+0x228] and [vtable+0x9a0],
    // not ClientInstance fields. A small positive number is not sufficient.
    const auto match = OffsetProvider::mSigMatchCounts.find("ClientInstance_mPacketSender");
    if (match == OffsetProvider::mSigMatchCounts.end() || match->second != 1)
        return nullptr;
    return readPacketSenderMember(this, offset);
}

GuiData* ClientInstance::getGuiData()
{
    return hat::member_at<GuiData*>(this, OffsetProvider::ClientInstance_mGuiData);
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
    // idk why but the original virtual func's result was inverted
    // so im just gonna recreate that here because i'm too lazy to refactor everything
    auto* game = getMinecraftGame();
    if (!InstanceLocator::isPlausibleGame(game))
        return true; // pretend the mouse is grabbed: the safe state, no input lost
    return !game->getMouseGrabbed();
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
