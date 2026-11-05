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
#include <omp.h>
#include <libhat.hpp>

#define NOW std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()

hat::scan_result SigManager::scanSig(hat::signature_view sig, const std::string& name, int offset)
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
    if (matches > 1) {
        mSigs[name] = 0;
        mSigMatchCounts[name] = matches;
        mStatus.set(name, ResolveStatus::Ambiguous,
                    reinterpret_cast<uintptr_t>(result.get()), 0, matches);
        return {};
    }

    mSigMatchCounts[name] = matches;
    const uintptr_t resolved = (offset == 0)
        ? reinterpret_cast<uintptr_t>(result.get())
        : reinterpret_cast<uintptr_t>(result.rel(offset));
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
    for (const auto& name : mStatus.namesWith(ResolveStatus::Missing))
        Solstice::console->critical("[signatures] MISSING {}", name);

    // Bounds validation now runs in every build (CMake defines __DEBUG__
    // unconditionally, so the old #ifndef block never compiled).
    const auto mc     = hat::process::get_process_module().address();
    const auto mcSize = hat::process::get_process_module().get_module_data().size();
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ok)) {
        const auto e = mStatus.get(name);
        if (e.address != 0 && (e.address < mc || e.address > mc + mcSize)) {
            Solstice::console->critical("[signatures] OUT-OF-RANGE {} @ {}", name,
                                        MemUtils::getMbMemoryString(e.address));
            mStatus.set(name, ResolveStatus::OutOfRange, e.address, 0, e.matches);
            mSigs[name] = 0;
        }
    }

    const bool ok = mStatus.allOk();
    Solstice::console->info(
        "[signatures] initialized in {}ms, {} entries: ok={} missing={} ambiguous={} out-of-range={}",
        diff, mStatus.size(), mStatus.count(ResolveStatus::Ok),
        mStatus.count(ResolveStatus::Missing), mStatus.count(ResolveStatus::Ambiguous),
        mStatus.count(ResolveStatus::OutOfRange));

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
