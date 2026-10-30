//
// Created by alteik on 11/11/2024.
//

#include "FastEat.hpp"
#include <SDK/SigManager.hpp>
#include <Utils/Buffer.hpp>
#include <Utils/MemUtils.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <cstring>

static uintptr_t func;

void FastEat::onInit()
{
    func = SigManager::FastEat;
}

void FastEat::onEnable()
{
    // The trampoline replays `dec eax; mov [rdi+0x7c8],eax`. A unique
    // signature alone does not prove those instructions are present.
    constexpr unsigned char expected[] = {0xFF, 0xC8, 0x89, 0x87, 0xC8, 0x07, 0x00, 0x00};
    const auto match = SigManager::mSigMatchCounts.find("FastEat");
    if (!func || match == SigManager::mSigMatchCounts.end() || match->second != 1 ||
        !InstanceLocator::isReadableMemory(reinterpret_cast<void*>(func), sizeof(expected)) ||
        std::memcmp(reinterpret_cast<void*>(func), expected, sizeof(expected)) != 0) {
        spdlog::warn("FastEat disabled: patch site does not match the supported instruction layout");
        mEnabled = false;
        mWantedState = false;
        return;
    }
    MemUtils::ReadBytes((void*)func, mOriginalData, sizeof(mOriginalData));

    mDetour = AllocateBuffer((void*)func);
    if (!mDetour) {
        spdlog::error("FastEat disabled: failed to allocate trampoline");
        mEnabled = false;
        mWantedState = false;
        return;
    }
    MemUtils::writeBytes((uintptr_t)mDetour, mDetourBytes, sizeof(mDetourBytes));

    auto toOriginalAddrRip1 = MemUtils::GetRelativeAddress((uintptr_t)mDetour + sizeof(mDetourBytes) + 1, func + 8);

    MemUtils::writeBytes((uintptr_t)mDetour + sizeof(mDetourBytes), "\xE9", 1);
    MemUtils::writeBytes((uintptr_t)mDetour + sizeof(mDetourBytes) + 1, &toOriginalAddrRip1, sizeof(int32_t));

    auto newRelRip1 = MemUtils::GetRelativeAddress(func + 1, (uintptr_t)mDetour + 4);

    MemUtils::writeBytes(func, "\xE9", 1);
    MemUtils::writeBytes(func + 1, &newRelRip1, sizeof(int32_t));
    MemUtils::NopBytes(func + 5, 3);

    gFeatureManager->mDispatcher->listen<BaseTickEvent, &FastEat::onBaseTickEvent>(this);
}

void FastEat::onDisable()
{
    if (!mDetour) return;
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &FastEat::onBaseTickEvent>(this);

    MemUtils::writeBytes(func, mOriginalData, sizeof(mOriginalData));
    FreeBuffer(mDetour);
    mDetour = nullptr;
}

void FastEat::onBaseTickEvent(BaseTickEvent& event)
{
    int32_t speed = mSpeed.as<int>();
    byte eatSpeed = (speed >= 1 && speed <= 10) ? static_cast<byte>(0x00 + speed) : 0x01;
    MemUtils::writeBytes(reinterpret_cast<uintptr_t>(mDetour), &eatSpeed, sizeof(byte));
}
