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
    // it would serialise the whole parallel for loop.
    const int matches = result.has_result() ? CodeSection::countMatches(sig, 4) : 0;

    std::lock_guard lock(mSigMutex);
    mSigScanCount++;

    if (!result.has_result()) {
        mSigs[name] = 0;
        return {};
    }

    // A signature that matches in more than one place resolves to *an* address
    // rather than *the* address. That failure mode produces a crash far away
    // from the actual mistake, so record the count and let initialize() report it.
    mSigMatchCounts[name] = matches;

    if (offset == 0) mSigs[name] = reinterpret_cast<uintptr_t>(result.get());
    else mSigs[name] = reinterpret_cast<uintptr_t>(result.rel(offset));

    return result;
}

void SigManager::initialize()
{
    int64_t start = NOW;
    #pragma omp parallel for
    for (int i = 0; i < mSigInitializers.size(); i++) {
        mSigInitializers[i]();
    }
    int64_t end = NOW;
    int64_t diff = end - start;

    for (const auto& sig : mSigs) {
        if (sig.second != 0) Solstice::console->info("[signatures] found {} @ {}", sig.first, MemUtils::getMbMemoryString(sig.second));
    }

    int ambiguous = 0;
    for (const auto& sig : mSigs) {
        if (sig.second == 0) continue;

        const auto it = mSigMatchCounts.find(sig.first);
        if (it == mSigMatchCounts.end() || it->second <= 1) continue;

        ambiguous++;
        Solstice::console->warn("[signatures] {} @ {} matched {} places (ambiguous)",
                                sig.first, MemUtils::getMbMemoryString(sig.second), it->second);
    }

    for (const auto& sig : mSigs) {
        if (sig.second == 0) Solstice::console->critical("[signatures] failed to find {}", sig.first);

    }

    if (ambiguous > 0) {
        Solstice::console->warn("[signatures] {} of {} signatures are ambiguous", ambiguous, mSigs.size());
    }

#ifndef __DEBUG__
    auto mc = hat::process::get_process_module().address();
    auto mcSize = hat::process::get_process_module().get_module_data().size();

    // Terminate if we failed to find a signature
    for (const auto& sig : mSigs) {
        // Make a crash log if the sig is out of bounds
        if (sig.second < mc || sig.second > mc + mcSize) {
            ExceptionHandler::makeCrashLog("An error occurred while initializing: Signature out of bounds!", 0xFF00);
            __fastfail(0);
            std::exit(0);
        }

        if (sig.second == 0) {
            ExceptionHandler::makeCrashLog("An error occurred while initializing!", 0xFF02);
            __fastfail(0);
            std::exit(0);
        }

    }
#endif

    Solstice::console->info("[signatures] initialized in {}ms, {} total sigs scanned", diff, mSigScanCount);
    mIsInitialized = true;
}

void SigManager::deinitialize()
{
    spdlog::info("[signatures] deinitializing...");
    mSigInitializers.clear();
    mSigs.clear();
    mSigMatchCounts.clear();

    mIsInitialized = false;
}
