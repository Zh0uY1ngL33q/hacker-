//
// Created by alteik on 25/09/2024.
//

#include "EditionFaker.hpp"
#include <spdlog/spdlog.h>
#include <SDK/SigManager.hpp>
#include <Utils/Buffer.hpp>
#include <Utils/MemUtils.hpp>
#include <Features/Events/ConnectionRequestEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/ConnectionRequest.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

static uintptr_t func;
static uintptr_t func2;
static uintptr_t func3;
static uintptr_t func4;
static uintptr_t func5;

void EditionFaker::onInit() {
    func = SigManager::ConnectionRequest_create_DeviceOS;
    func2 = SigManager::ConnectionRequest_create_DefaultInputMode;
    func3 = SigManager::ConnectionRequest_create_CurrentInputMode;
    func4 = SigManager::InputModeBypass;
    func5 = SigManager::InputModeBypassFix;
}

bool EditionFaker::inject()
{
    patchedBlocks = 0;
    int32_t DefaultInputMode = mInputMethod.as<int>();

    // Each block patches an independent function, and on 1.21.120 two of the three
    // signatures do not resolve at all -- create_DefaultInputMode,
    // create_CurrentInputMode and InputModeBypass are missing. Patching them
    // unconditionally wrote a jmp to an address built from an unset value, so
    // enabling this module crashed. Patch what resolved, skip the rest, and say
    // which parts are live.
    if (MemUtils::isPatchableCode(func2, 8) && MemUtils::isPatchableCode(func3, 8)) { // default & current input mode spoof
        MemUtils::ReadBytes((void*)func2, mOriginalInputData1, sizeof(mOriginalInputData1));
        MemUtils::ReadBytes((void*)func3, mOriginalInputData2, sizeof(mOriginalInputData2));

        MemUtils::writeBytes(func2, "\xBF", 1);
        MemUtils::writeBytes(func2+1, &DefaultInputMode, sizeof(uint32_t));
        MemUtils::NopBytes(func2+5, 3);

        MemUtils::writeBytes(func3, "\xBF", 1);
        MemUtils::writeBytes(func3+1, &DefaultInputMode, sizeof(uint32_t));
        MemUtils::NopBytes(func3+5, 3);

        patchedBlocks |= kInputMode;
    }

    if (MemUtils::isPatchableCode(func, 8)) { // os
        MemUtils::ReadBytes((void*)func, mOriginalData, sizeof(mOriginalData));
        MemUtils::writeBytes(func+1, &DefaultInputMode, sizeof(uint32_t));
        patchedBlocks |= kOs;
    }

    if (MemUtils::isPatchableCode(func4, 8) && MemUtils::isPatchableCode(func5, 8)) { // input mode bypass
        MemUtils::ReadBytes((void*)func4, mOriginalData1, sizeof(mOriginalData1));
        MemUtils::ReadBytes((void*)func5, mOriginalData2, sizeof(mOriginalData2));

        mDetour1 = AllocateBuffer((void*)func4);
        mDetour2 = AllocateBuffer((void*)func5);

        MemUtils::writeBytes((uintptr_t)mDetour1, mDetourBytes1, sizeof(mDetourBytes1));
        MemUtils::writeBytes((uintptr_t)mDetour2, mDetourBytes2, sizeof(mDetourBytes2));

        auto toOriginalAddrRip1 = MemUtils::GetRelativeAddress((uintptr_t)mDetour1 + sizeof(mDetourBytes1) + 1, func4+5);
        auto toOriginalAddrRip2 = MemUtils::GetRelativeAddress((uintptr_t)mDetour2 + sizeof(mDetourBytes2) + 1, func5+5);

        MemUtils::writeBytes((uintptr_t)mDetour1 + sizeof(mDetourBytes1), "\xE9", 1);
        MemUtils::writeBytes((uintptr_t)mDetour2 + sizeof(mDetourBytes2), "\xE9", 1);

        MemUtils::writeBytes((uintptr_t)mDetour1 + sizeof(mDetourBytes1) + 1, &toOriginalAddrRip1, sizeof(int32_t));
        MemUtils::writeBytes((uintptr_t)mDetour2 + sizeof(mDetourBytes2) + 1, &toOriginalAddrRip2, sizeof(int32_t));

        auto newRelRip1 = MemUtils::GetRelativeAddress(func4 + 1, (uintptr_t)mDetour1);
        auto newRelRip2 = MemUtils::GetRelativeAddress(func5 + 1, (uintptr_t)mDetour2);

        MemUtils::writeBytes(func4, "\xE9", 1);
        MemUtils::writeBytes(func5, "\xE9", 1);

        MemUtils::writeBytes(func4+1, &newRelRip1, sizeof(int32_t));
        MemUtils::writeBytes(func5+1, &newRelRip2, sizeof(int32_t));

        patchedBlocks |= kBypass;
    }

    const int live = ((patchedBlocks & kInputMode) ? 1 : 0)
                   + ((patchedBlocks & kOs) ? 1 : 0)
                   + ((patchedBlocks & kBypass) ? 1 : 0);
    spdlog::info("EditionFaker: input-mode {}, os {}, bypass {} ({} of 3 blocks patched)",
                 (patchedBlocks & kInputMode) ? "on" : "unresolved",
                 (patchedBlocks & kOs) ? "on" : "unresolved",
                 (patchedBlocks & kBypass) ? "on" : "unresolved",
                 live);
    return patchedBlocks != 0;
}

void EditionFaker::eject() {
    if (patchedBlocks & kInputMode) { // default & current input mode spoof
        MemUtils::writeBytes(func2, mOriginalInputData1, sizeof(mOriginalInputData1));
        MemUtils::writeBytes(func3, mOriginalInputData2, sizeof(mOriginalInputData2));
    }

    if (patchedBlocks & kOs) { // os
        MemUtils::writeBytes(func+1, &mOriginalData, sizeof(uint32_t));
    }

    if (patchedBlocks & kBypass) { // input mode bypass
        MemUtils::writeBytes(func4, mOriginalData1, sizeof(mOriginalData1));
        MemUtils::writeBytes(func5, mOriginalData2, sizeof(mOriginalData2));

        FreeBuffer(mDetour1);
        FreeBuffer(mDetour2);
        mDetour1 = nullptr;
        mDetour2 = nullptr;
    }
    patchedBlocks = 0;
}

void EditionFaker::spoofEdition() {
    int32_t InputMode = mInputMethod.as<int>();
    int32_t Os = mOs.as<int>();

    // Only touch blocks whose patch actually landed; the rest have no address to
    // write into, and mDetour1/mDetour2 stay null when the bypass is missing.
    if (patchedBlocks & kInputMode) {
        MemUtils::writeBytes(func2+1, &InputMode, sizeof(uint32_t));
        MemUtils::writeBytes(func3+1, &InputMode, sizeof(uint32_t));
    }

    if (patchedBlocks & kOs) {
        MemUtils::writeBytes(func+1, &Os, sizeof(uint32_t)); // changing directly, u can get this addr by using debugger in CE while joining world
    }

    if ((patchedBlocks & kBypass) && mDetour1) {
        MemUtils::writeBytes((uintptr_t)mDetour1+2, &InputMode, sizeof(int32_t)); // spoofing only this cuz "bypass fix" should always be on 1 (mouse)
    }
}

void EditionFaker::onEnable() {
    inject();
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &EditionFaker::onPacketOutEvent, nes::event_priority::ABSOLUTE_LAST>(this);
    gFeatureManager->mDispatcher->listen<ConnectionRequestEvent, &EditionFaker::onConnectionRequestEvent, nes::event_priority::ABSOLUTE_LAST>(this);
}

void EditionFaker::onDisable() {
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &EditionFaker::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<ConnectionRequestEvent, &EditionFaker::onConnectionRequestEvent>(this);
    eject();
}

void EditionFaker::onConnectionRequestEvent(ConnectionRequestEvent& event) {
    spoofEdition();
    event.mInputMode = mInputMethod.as<int>();
}

void EditionFaker::onPacketOutEvent(PacketOutEvent &event) {
    if(event.mPacket->getId() == PacketID::PlayerAuthInput) {
        auto paip = event.getPacket<PlayerAuthInputPacket>();
        paip->mInputMode = (InputMode) mInputMethod.as<int>();
    }
} 
