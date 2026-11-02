//
// Created by alteik on 04/09/2024.
//

#include "Reach.hpp"
#include <SDK/SigManager.hpp>
#include <Utils/Buffer.hpp>
#include <Utils/MemUtils.hpp>

static uintptr_t func = 0x0;

void Reach::onInit() {
    func = SigManager::Reach;

    // BlockReach is meant to land on a rip-relative read of the block-reach
    // constant, so addr2+disp+4 is that constant. On 1.21.120 the pattern matches
    // an unrelated int-to-double conversion stub instead ("cvtdq2pd xmm0, xmm0;
    // movsd [rdx], xmm0"), and the arithmetic then produced 0x9C19EC6 -- about
    // 17 MB past the end of the image. onDisable wrote 5.7f through that pointer.
    // Verify the address before keeping it.
    uintptr_t addr2 = SigManager::BlockReach;
    if (!addr2) return;
    addr2 += 4;
    if (!MemUtils::isInHostModule(addr2, sizeof(int32_t))) return;

    int offset2 = *reinterpret_cast<int*>(addr2);
    const uintptr_t candidate = addr2 + offset2 + 4;
    if (!MemUtils::isInHostModule(candidate, sizeof(float))) {
        spdlog::warn("Reach: BlockReach does not point at a constant read; block reach left alone");
        mBlockReachPtr = nullptr;
        return;
    }
    mBlockReachPtr = reinterpret_cast<float*>(candidate);
}

void Reach::onEnable() {
    // Never patch address zero when the version-specific signature is missing.
    // This is a safety gate, not an adaptation of Reach.
    if (!func || !mBlockReachPtr) {
        spdlog::warn("Reach disabled: required signatures are unresolved");
        mEnabled = false;
        mWantedState = false;
        return;
    }
    MemUtils::ReadBytes((void*)func, mOriginalData, sizeof(mOriginalData));

    mDetour = AllocateBuffer((void*)func);
    MemUtils::writeBytes((uintptr_t)mDetour, mDetourBytes, sizeof(mDetourBytes));

    auto toOriginalAddrRip1 = MemUtils::GetRelativeAddress((uintptr_t)mDetour + sizeof(mDetourBytes) + 1, func + 11);

    MemUtils::writeBytes((uintptr_t)mDetour + sizeof(mDetourBytes), "\xE9", 1);
    MemUtils::writeBytes((uintptr_t)mDetour + sizeof(mDetourBytes) + 1, &toOriginalAddrRip1, sizeof(int32_t));

    auto newRelRip1 = MemUtils::GetRelativeAddress(func + 1, (uintptr_t)mDetour + 4);

    MemUtils::writeBytes(func, "\xE9", 1);
    MemUtils::writeBytes(func + 1, &newRelRip1, sizeof(int32_t));
    MemUtils::NopBytes(func + 5, 6);

    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Reach::onBaseTickEvent>(this);
}

void Reach::onDisable() {
    if (!mDetour) return;
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Reach::onBaseTickEvent>(this);
    
    MemUtils::writeBytes(func, mOriginalData, sizeof(mOriginalData));
    FreeBuffer(mDetour);
    mDetour = nullptr;

    MemUtils::Write((uintptr_t) mBlockReachPtr, 5.7f);
}

void Reach::onBaseTickEvent(class BaseTickEvent &event) {
    MemUtils::Write((uintptr_t) mDetour, mCombatReach.mValue);
    MemUtils::Write((uintptr_t) mBlockReachPtr, mBlockReach.mValue);
}