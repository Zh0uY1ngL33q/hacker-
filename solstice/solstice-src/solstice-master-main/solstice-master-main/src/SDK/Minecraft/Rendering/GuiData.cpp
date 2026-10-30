//
// Created by vastrakai on 6/25/2024.
//

#include "GuiData.hpp"

#include <SDK/SigManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
#include <Solstice.hpp>
#include <Utils/MemUtils.hpp>
#include <Features/Events/ChatEvent.hpp>

class DummyData
{
public:
    PAD(0x120);
};

void GuiData::displayClientMessageQueued(const std::string& msg)
{
    if (!ClientInstance::get()->getLocalPlayer()) return;

    BaseTickHook::queueMsg(msg);
}

void GuiData::displayClientMessage(const std::string& msg)
{
    if (!ClientInstance::get()->getLocalPlayer()) return;

    // This is stupid.
    static std::unique_ptr<DummyData> dummyData = std::make_unique<DummyData>();
    // GuiData_displayClientMessage does not resolve in 1.21.120 (probably
    // inlined/rewritten); callFastcall with a null pointer is instant death,
    // and this is the flush path for every queued message. A message lost to
    // the console beats the game closing.
    const uintptr_t func = SigManager::GuiData_displayClientMessage;
    if (!func)
    {
        if (Solstice::console)
            Solstice::console->info("[chat] {}", msg);
        return;
    }
    MemUtils::callFastcall<void>(func, this, msg, dummyData.get(), false);

    // Dispatch the chat event
    auto holder = nes::make_holder<ChatEvent>(msg);
    gFeatureManager->mDispatcher->trigger(holder);
}