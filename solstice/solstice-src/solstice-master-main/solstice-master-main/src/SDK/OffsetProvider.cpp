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
    // The scan is the expensive part and only reads module memory, so it stays
    // outside the lock. Only the map updates below need serialising: initialize()
    // drives these initializers from an OpenMP parallel for, and std::unordered_map
    // is not safe for concurrent insertion (it corrupts its bucket chains, which
    // shows up as an unrelated crash later).

    // The executable ranges come from the PE section table rather than a
    // hardcoded subspan. The old `subspan(0x1000, 0x7a7baa9)` was a snapshot of
    // one build and skipped everything past its end (on 1.21.120 that is about
    // 18 MB of code, where every signature silently resolved to 0).
    auto result = CodeSection::find(sig);

    if (!result.has_result()) {
        std::lock_guard lock(mSigMutex);
        mSigs[name]       = 0;
        mSigMissing[name] = true;
        mSigScanCount++;
        return {};
    }

    // A signature that matches in more than one place is the most dangerous
    // failure mode: the scan "succeeds", but the address may not be the function
    // we think it is. Record it so initialize() can report it instead of letting
    // a wrong offset propagate into a crash somewhere else entirely.
    const int    matches  = CodeSection::countMatches(sig, 4);
    const uintptr_t resolved = reinterpret_cast<uintptr_t>(result.get()) + offset;

    {
        std::lock_guard lock(mSigMutex);
        mSigs[name]           = resolved;
        mSigMatchCounts[name] = matches;
        mSigMissing[name]     = false;
        mSigScanCount++;
    }
    return result;
}

bool OffsetProvider::plausibleOffset(const std::string& name, long long value, bool isIndex)
{
    // The limits come from measurement, not taste. In the 1.21.44 reference
    // build the largest genuine member offset this project uses is 0x15E8
    // (Actor_mSerializedSkin) and the largest vtable index is 436
    // (Level_getBlockPalette) out of 478 ClientInstance slots. Both limits below
    // sit well above those and still far below what a mis-matched signature
    // produces, which is in the millions.
    const long long limit = isIndex ? 4096 : 0x8000;
    const bool inRange = value >= 0 && value < limit;
    // No real member of a polymorphic object lives in the first eight bytes:
    // that is where MSVC keeps the vtable pointer. MinecraftGame's profanity
    // context resolved to 1 here while the 1.21.44 reference build gives 400,
    // so it is a mis-matched signature rather than a field, and member_at()
    // would perform an unaligned read that ends in a garbage pointer.
    const bool insideVtable = !isIndex && value > 0 && value < 8;
    if (inRange && !insideVtable) return true;

    // Called from the initializers, which initialize() runs under an OpenMP
    // parallel for, so the record has to be serialised like the other maps.
    std::lock_guard lock(mSigMutex);
    mSigImplausible[name] = value;
    return false;
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

    // A signature can resolve to a place that is not the function it was written
    // for. The scan then reports success, the offset arithmetic divides whatever
    // displacement happened to sit there, and the consumer dereferences it. These
    // are the ones plausibleOffset() refused; naming them is the point, because
    // silence is what let Actor_swing feed 17305961 into a vtable lookup.
    if (!mSigImplausible.empty()) {
        for (const auto& [name, value] : mSigImplausible) {
            Solstice::console->critical(
                "[offsets] {} resolved to {} - not a possible vtable index or member "
                "offset, kept 0; the signature matched the wrong instruction",
                name, value);
        }
        Solstice::console->warn(
            "[offsets] {} signature(s) rejected as implausible; features that need them "
            "stay disabled instead of dereferencing wild memory",
            mSigImplausible.size());
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
    mSigImplausible.clear();

    mIsInitialized = false;
}

