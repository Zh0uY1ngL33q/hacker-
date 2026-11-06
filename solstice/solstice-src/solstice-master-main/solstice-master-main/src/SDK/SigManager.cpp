//
// Created by vastrakai on 6/24/2024.
//

#include "SigManager.hpp"
#include "CodeSection.hpp"

#include <iostream>
#include <Solstice.hpp>
#include <Utils/Logger.hpp>
#include <Utils/MemUtils.hpp>
#include <chrono>
#include <cstring>
#include <omp.h>
#include <libhat.hpp>

#define NOW std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()

namespace {
enum class Rel32Read { Ok, OutOfRange, Unreadable };

bool guardedReadInt32(const void* source, int32_t& value)
{
    __try {
        std::memcpy(&value, source, sizeof(value));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

Rel32Read resolveRel32(uintptr_t match, int operandOffset, uintptr_t& target)
{
    const auto module = hat::process::get_process_module();
    const uintptr_t base = module.address();
    const size_t size = module.get_module_data().size();
    if (size < sizeof(int32_t) || match < base || match - base >= size)
        return Rel32Read::OutOfRange;

    const size_t matchOffset = static_cast<size_t>(match - base);
    size_t operandOffsetInModule = 0;
    if (operandOffset >= 0) {
        const size_t delta = static_cast<size_t>(operandOffset);
        if (delta > size - matchOffset || matchOffset + delta > size - sizeof(int32_t))
            return Rel32Read::OutOfRange;
        operandOffsetInModule = matchOffset + delta;
    } else {
        const size_t delta = static_cast<size_t>(-(static_cast<long long>(operandOffset)));
        if (delta > matchOffset || matchOffset - delta > size - sizeof(int32_t))
            return Rel32Read::OutOfRange;
        operandOffsetInModule = matchOffset - delta;
    }

    const uintptr_t operand = base + operandOffsetInModule;
    if (!MemUtils::isReadableRange(operand, sizeof(int32_t)))
        return Rel32Read::Unreadable;

    int32_t displacement = 0;
    if (!guardedReadInt32(reinterpret_cast<const void*>(operand), displacement))
        return Rel32Read::Unreadable;

    // Compute in image-relative signed space so a hostile displacement cannot
    // wrap uintptr_t and impersonate an in-module address.
    const int64_t resolvedOffset = static_cast<int64_t>(operandOffsetInModule) +
                                   sizeof(int32_t) + static_cast<int64_t>(displacement);
    if (resolvedOffset < 0 || static_cast<uint64_t>(resolvedOffset) >= size)
        return Rel32Read::OutOfRange;
    target = base + static_cast<size_t>(resolvedOffset);
    return Rel32Read::Ok;
}
}

hat::scan_result SigManager::scanSig(hat::signature_view sig, const std::string& name,
                                     SigType type, int offset)
{
    // The old code scanned
    //
    //     data.subspan(0x1000, 0x7a7baa9)
    //
    // which is the first section's RVA plus one specific build's VirtualSize. On
    // 1.21.120 .text is 0x8b6e8a9 bytes, so everything past 0x7a7baa9 -- about
    // 18 MB, a fifth of the code -- was never looked at, and every signature
    // living there failed no matter how correct the pattern was.
    //
    // CodeSection derives the executable ranges from the PE section table of the
    // *running* image instead, which is version independent.
    auto result = CodeSection::find(sig);

    // Computed outside the lock: it is a full scan, and holding the mutex across
    // it would serialise the whole parallel for loop. countMatches() now uses
    // the same ranges/order as find(), so the count describes the very hit we
    // are about to publish.
    const int matches = result.has_result() ? CodeSection::countMatches(sig, 2) : 0;

    std::lock_guard lock(mSigMutex);
    mSigScanCount++;

    if (!result.has_result()) {
        mSigs[name] = 0;
        mStatus.set(name, ResolveStatus::Missing);
        return {};
    }

    // A signature that matches in more than one place resolves to *an* address
    // rather than *the* address. Refuse it: returning an empty result keeps the
    // variable at 0 instead of publishing a guess that initialize() would then
    // have reported as "found".
    // Strict uniqueness: exactly one match. matches == 0 means find() and the
    // independent count disagreed, so the hit is not trustworthy either.
    if (matches != 1) {
        mSigs[name] = 0;
        mSigMatchCounts[name] = matches;
        mStatus.set(name,
                    matches > 1 ? ResolveStatus::Ambiguous : ResolveStatus::Inconsistent,
                    reinterpret_cast<uintptr_t>(result.get()), 0, matches);
        return {};
    }

    mSigMatchCounts[name] = matches;
    uintptr_t resolved = reinterpret_cast<uintptr_t>(result.get());
    if (type == SigType::RefSig) {
        const auto relStatus = resolveRel32(resolved, offset, resolved);
        if (relStatus != Rel32Read::Ok) {
            mSigs[name] = 0;
            mStatus.set(name,
                        relStatus == Rel32Read::OutOfRange
                            ? ResolveStatus::OutOfRange : ResolveStatus::Inconsistent,
                        reinterpret_cast<uintptr_t>(result.get()), 0, matches);
            return {};
        }
    }
    mSigs[name] = resolved;
    mStatus.set(name, ResolveStatus::Ok, resolved, 0, matches);
    return result;
}

bool SigManager::initialize()
{
    const int64_t start = NOW;
    #pragma omp parallel for
    for (int i = 0; i < mSigInitializers.size(); i++) {
        mSigInitializers[i]();
    }
    const int64_t diff = NOW - start;

    // Classify by status, not by "mSigs != 0". Ambiguous and out-of-range
    // entries were previously reported as found and then used anyway.
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ok)) {
        const auto e = mStatus.get(name);
        Solstice::console->info("[signatures] ok {} @ {}", name, MemUtils::getMbMemoryString(e.address));
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ambiguous)) {
        const auto e = mStatus.get(name);
        Solstice::console->critical("[signatures] AMBIGUOUS {} matched {} places @ {} -- refused",
                                    name, e.matches, MemUtils::getMbMemoryString(e.address));
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Inconsistent)) {
        const auto e = mStatus.get(name);
        if (e.matches == 1)
            Solstice::console->critical("[signatures] INCONSISTENT {} -- rel32 operand memory was unreadable; refused", name);
        else
            Solstice::console->critical("[signatures] INCONSISTENT {} -- find() hit but the count saw {} matches; refused",
                                        name, e.matches);
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Missing))
        Solstice::console->critical("[signatures] MISSING {}", name);
    for (const auto& name : mStatus.namesWith(ResolveStatus::OutOfRange)) {
        const auto e = mStatus.get(name);
        Solstice::console->critical("[signatures] OUT-OF-RANGE {} -- rel32 operand or target from {} exceeds the module; refused",
                                    name, MemUtils::getMbMemoryString(e.address));
    }

    // Bounds validation now runs in every build (CMake defines __DEBUG__
    // unconditionally, so the old #ifndef block never compiled).
    const auto mc     = hat::process::get_process_module().address();
    const auto mcSize = hat::process::get_process_module().get_module_data().size();
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ok)) {
        const auto e = mStatus.get(name);
        // Module covers mc <= address < mc + mcSize, right-open. Subtract after
        // the lower-bound test so no unsigned wraparound is possible; a null
        // result is now caught too instead of being skipped by the old guard.
        if (e.address < mc || e.address - mc >= mcSize) {
            Solstice::console->critical("[signatures] OUT-OF-RANGE {} @ {}", name,
                                        MemUtils::getMbMemoryString(e.address));
            mStatus.set(name, ResolveStatus::OutOfRange, e.address, 0, e.matches);
            mSigs[name] = 0;
        }
    }

    // Scanned mappings only; this is not a verdict on the whole SDK.
    const bool ok = mStatus.allOk();
    Solstice::console->info(
        "[signatures] scanned mappings: ok={}/{} missing={} ambiguous={} inconsistent={} out-of-range={} (verdict={}) in {}ms",
        mStatus.count(ResolveStatus::Ok), mStatus.size(),
        mStatus.count(ResolveStatus::Missing), mStatus.count(ResolveStatus::Ambiguous),
        mStatus.count(ResolveStatus::Inconsistent), mStatus.count(ResolveStatus::OutOfRange),
        ok ? "OK" : "NOT TRUSTWORTHY", diff);

    // Outcome is returned to Solstice::initialize(); no self-termination here.
    mIsInitialized = ok;
    return ok;
}

void SigManager::deinitialize()
{
    spdlog::info("[signatures] deinitializing...");
    mSigInitializers.clear();
    mSigs.clear();
    mSigMatchCounts.clear();
    mStatus.clear();

    mIsInitialized = false;
}
