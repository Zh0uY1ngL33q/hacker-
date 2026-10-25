//
// Created by vastrakai on 6/25/2024.
//

#include "OffsetProvider.hpp"
#include "CodeSection.hpp"

#include <Solstice.hpp>
#include <Utils/MemUtils.hpp>
#include <libhat.hpp>

#define NOW std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()

hat::scan_result OffsetProvider::scanSig(hat::signature_view sig, const std::string& name, int offset)
{
    mSigScanCount++;

    // The executable ranges come from the PE section table rather than a
    // hardcoded subspan. The old `subspan(0x1000, 0x7a7baa9)` was a snapshot of
    // one build and skipped everything past its end (on 1.21.120 that is about
    // 18 MB of code, where every signature silently resolved to 0).
    auto result = CodeSection::find(sig);

    if (!result.has_result()) {
        mSigs[name] = 0;
        mSigMissing[name] = true;
        return {};
    }

    // A signature that matches in more than one place is the most dangerous
    // failure mode: the scan "succeeds", but the address may not be the function
    // we think it is. Record it so initialize() can report it instead of letting
    // a wrong offset propagate into a crash somewhere else entirely.
    const int matches = CodeSection::countMatches(sig, 4);
    mSigMatchCounts[name] = matches;

    // -1 here means "the PATTERN was found but points somewhere implausible",
    // which callers detect via the value itself. The offset arithmetic stays
    // identical to the previous implementation so existing signatures keep
    // their meaning.
    mSigs[name] = reinterpret_cast<uintptr_t>(result.get()) + offset;
    mSigMissing[name] = false;
    return result;
}

void OffsetProvider::initialize()
{
    int64_t start = NOW;
    #pragma omp parallel for
    for (int i = 0; i < mSigInitializers.size(); i++)
    {
        mSigInitializers[i]();
    }
    uint64_t end = NOW;

    int ambiguous = 0;

    for (const auto& sig : mSigs)
    {
        if (sig.second != 0)
        {
            const auto it = mSigMatchCounts.find(sig.first);
            const int  matches = it == mSigMatchCounts.end() ? 1 : it->second;

            if (matches > 1) {
                // Found, but not uniquely. Report loudly: this is the case that
                // silently produces a wrong address instead of a missing one.
                ambiguous++;
                Solstice::console->warn("[offsets] {} @ {} matched {} places (ambiguous)",
                                        sig.first, MemUtils::getMbMemoryString(sig.second), matches);
            } else {
                Solstice::console->info("[offsets] found {} @ {}", sig.first, MemUtils::getMbMemoryString(sig.second));
            }
        }
    }

    for (const auto& sig : mSigs)
    {
        if (sig.second == 0)
        {
            Solstice::console->critical("[offsets] failed to find {}", sig.first);
        }
    }

    if (ambiguous > 0) {
        Solstice::console->warn("[offsets] {} of {} signatures are ambiguous", ambiguous, mSigs.size());
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

    Solstice::console->info("[offsets] initialized in {}ms, {} total sigs scanned", end - start, mSigScanCount);
    mIsInitialized = true;
}

void OffsetProvider::deinitialize()
{
    spdlog::info("[offsets] deinitializing...");

    mSigInitializers.clear();
    mSigs.clear();
    mSigMatchCounts.clear();
    mSigMissing.clear();

    mIsInitialized = false;
}

