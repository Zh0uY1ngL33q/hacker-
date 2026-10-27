//
// Created by vastrakai on 6/25/2024.
//

#include "ChatUtils.hpp"

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

namespace {
    // Chat is the main feedback channel, including feedback about initialization
    // failing, so it must work when ClientInstance does not. Dropping the message is
    // better than crashing before the user can read the console.
    GuiData* tryGuiData()
    {
        auto* clientInstance = ClientInstance::get();
        if (!clientInstance)
            return nullptr;

        auto* guiData = clientInstance->getGuiData();
        return InstanceLocator::isReadableMemory(guiData, 0x40) ? guiData : nullptr;
    }
}

void ChatUtils::displayClientMessage(const std::string& msg)
{
    auto* guiData = tryGuiData();
    if (!guiData)
        return;

    if (!msg.contains("\n"))
    {// FUCK YOU DONT CHANGE THE PREIX COLOR U NIGGER
        guiData->displayClientMessageQueued("§asolstice§7 » §r" + msg);
        return;
    }

    // Newline handling
    std::string formattedMsg = "§asolstice§7 » §r";
    for (const auto& c : msg)
    {
        if (c == '\n')
        {
            formattedMsg += "\n§asolstice§7 » §r";
        }
        else
        {
            formattedMsg += c;
        }
    }
    guiData->displayClientMessageQueued(formattedMsg);
}

void ChatUtils::displayClientMessageSub(const std::string& subcaption, const std::string& msg)
{
    if (auto* guiData = tryGuiData())
        guiData->displayClientMessageQueued("§asolstice§7 » §7[" + subcaption + "§7] §r" + msg);
}

void ChatUtils::displayClientMessageRaw(const std::string& msg)
{
    if (auto* guiData = tryGuiData())
        guiData->displayClientMessageQueued(msg);
}
