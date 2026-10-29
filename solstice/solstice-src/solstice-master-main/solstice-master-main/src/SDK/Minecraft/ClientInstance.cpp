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
    float x = hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovX); // 0x6F0
    float y = hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovY); // 0x704
    return { x, y };
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
    return MemUtils::callVirtualFunc<ClientInputHandler*>(OffsetProvider::ClientInstance_getInputHandler, this);
}

KeyboardMouseSettings* ClientInstance::getKeyboardSettings()
{
    return getInputHandler()->getMappingFactory()->getKeyboardMouseSettings();
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
