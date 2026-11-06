//
// Created by vastrakai on 6/25/2024.
//

#include "OffsetProvider.hpp"
#include "CodeSection.hpp"

#include <Solstice.hpp>
#include <Utils/MemUtils.hpp>
#include <libhat.hpp>
#include <cstring>

#define NOW std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()

namespace {
bool guardedCopy(void* destination, const void* source, size_t size)
{
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}

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
        mStatus.set(name, ResolveStatus::Missing);
        return {};
    }

    // Ambiguity is a REFUSAL, not a warning. countMatches() now walks exactly
    // the same ranges in the same order as find(), so >1 means the first hit
    // cannot be identified as the intended function. Returning an empty result
    // makes the initializer leave its variable at 0 instead of publishing a
    // guess that later looks like a successful resolution.
    const int matches = CodeSection::countMatches(sig, 2);
    const uintptr_t resolved = reinterpret_cast<uintptr_t>(result.get()) + offset;

    {
        std::lock_guard lock(mSigMutex);
        mSigMatchCounts[name] = matches;
        mSigMissing[name]     = false;
        mSigScanCount++;

        // Strict uniqueness: exactly one match. A count of 0 means find() and
        // countMatches() disagreed (different readable windows, or the image
        // changed under us) -- publishing that address would be a guess.
        if (matches != 1) {
            mSigs[name] = 0;
            mStatus.set(name,
                        matches > 1 ? ResolveStatus::Ambiguous : ResolveStatus::Inconsistent,
                        resolved, 0, matches);
            return {};
        }

        mSigs[name] = resolved;
        mStatus.set(name, ResolveStatus::Ok, resolved, 0, matches);
    }
    return result;
}

bool OffsetProvider::readOffsetOperand(const std::string& name, uintptr_t match,
                                       int operandOffset, void* output, size_t width)
{
    const auto module = hat::process::get_process_module();
    const uintptr_t base = module.address();
    const size_t moduleSize = module.get_module_data().size();
    uintptr_t operand = 0;
    bool inModule = output != nullptr && width != 0 && width <= moduleSize &&
                    match >= base && match - base < moduleSize;

    if (inModule) {
        const size_t matchOffset = static_cast<size_t>(match - base);
        if (operandOffset >= 0) {
            const size_t delta = static_cast<size_t>(operandOffset);
            inModule = delta <= moduleSize - matchOffset &&
                       matchOffset + delta <= moduleSize - width;
            if (inModule) operand = base + matchOffset + delta;
        } else {
            // Avoid negating INT_MIN in signed arithmetic.
            const size_t delta = static_cast<size_t>(-(static_cast<long long>(operandOffset)));
            inModule = delta <= matchOffset && matchOffset - delta <= moduleSize - width;
            if (inModule) operand = base + matchOffset - delta;
        }
    }

    const bool readable = inModule && MemUtils::isReadableRange(operand, width);
    const bool copied = readable && guardedCopy(output, reinterpret_cast<const void*>(operand), width);
    if (copied) return true;

    std::lock_guard lock(mSigMutex);
    const auto previous = mStatus.get(name);
    mSigs[name] = 0;
    mStatus.set(name, inModule ? ResolveStatus::Inconsistent : ResolveStatus::OutOfRange,
                previous.address, 0, previous.matches);
    return false;
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
    // callVirtualFunc() rejects index <= 0 (MSVC slot 0 is the scalar-deleting
    // destructor). Reporting index 0 as Ok would claim a usable mapping that
    // the call layer silently turns into a no-op.
    const bool zeroIndex = isIndex && value == 0;

    // Called from the initializers, which initialize() runs under an OpenMP
    // parallel for, so the record has to be serialised like the other maps.
    std::lock_guard lock(mSigMutex);
    const auto e = mStatus.get(name);
    if (inRange && !insideVtable && !zeroIndex) {
        // Attach the value that was actually published to the Ok entry, so the
        // report shows the derived offset rather than just the match address.
        mStatus.set(name, ResolveStatus::Ok, e.address, value, e.matches);
        return true;
    }

    mSigImplausible[name] = value;
    mSigs[name] = 0;   // a rejected entry must never satisfy "mSigs != 0"
    mStatus.set(name, ResolveStatus::Implausible, e.address, value, e.matches);
    return false;
}

void OffsetProvider::registerHardcoded(const std::string& name, int value)
{
    // Called once, serially, from initialize() before the parallel scan.
    mHardcoded.emplace_back(name, value);
}

void OffsetProvider::rejectOffset(const std::string& name, ResolveStatus status, long long value)
{
    // Called from the initializers under the OpenMP parallel for, so the map
    // writes are serialised exactly like scanSig()/plausibleOffset().
    std::lock_guard lock(mSigMutex);
    const auto e = mStatus.get(name);
    mSigs[name] = 0;
    mSigImplausible[name] = value;
    mStatus.set(name, status, e.address, value, e.matches);
}

bool OffsetProvider::initialize()
{
    const int64_t start = NOW;

    // DEFINE_FIELD constants are registered through deferred initializers so the
    // registration never runs during static initialization. Do it once, before
    // the parallel scan, then print the complete list: a constant that was never
    // verified at runtime must be visible as such, not hidden in a header.
    for (const auto& audit : mAuditInitializers) audit();

    #pragma omp parallel for
    for (int i = 0; i < mSigInitializers.size(); i++)
    {
        mSigInitializers[i]();
    }
    const uint64_t end = NOW;

    // Classify by STATUS, never by "mSigs != 0". The old loop printed
    // "[offsets] found X @ ..." for entries that plausibleOffset() had already
    // rejected, which is exactly how an error mapping impersonated a success.
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ok)) {
        const auto e = mStatus.get(name);
        Solstice::console->info("[offsets] ok {} @ {} value={}", name,
                                MemUtils::getMbMemoryString(e.address), e.value);
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ambiguous)) {
        const auto e = mStatus.get(name);
        Solstice::console->critical("[offsets] AMBIGUOUS {} matched {} places @ {} -- refused, value stays 0",
                                    name, e.matches, MemUtils::getMbMemoryString(e.address));
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Inconsistent)) {
        const auto e = mStatus.get(name);
        if (e.matches == 1)
            Solstice::console->critical("[offsets] INCONSISTENT {} -- operand memory was unreadable; refused", name);
        else
            Solstice::console->critical("[offsets] INCONSISTENT {} -- find() hit but the count saw {} matches; refused",
                                        name, e.matches);
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Implausible)) {
        const auto e = mStatus.get(name);
        Solstice::console->critical("[offsets] IMPLAUSIBLE {} derived {} -- refused, value stays 0", name, e.value);
    }
    for (const auto& name : mStatus.namesWith(ResolveStatus::Missing))
        Solstice::console->critical("[offsets] MISSING {}", name);
    for (const auto& name : mStatus.namesWith(ResolveStatus::OutOfRange)) {
        const auto e = mStatus.get(name);
        Solstice::console->critical("[offsets] OUT-OF-RANGE {} -- operand at match {} exceeds the module; refused",
                                    name, MemUtils::getMbMemoryString(e.address));
    }

    // Bounds check now runs in EVERY build. It used to sit inside
    // #ifndef __DEBUG__, but CMakeLists.txt defines __DEBUG__ unconditionally,
    // so this validation had never been compiled into the shipped DLL.
    const auto mc     = hat::process::get_process_module().address();
    const auto mcSize = hat::process::get_process_module().get_module_data().size();
    for (const auto& name : mStatus.namesWith(ResolveStatus::Ok)) {
        const auto e = mStatus.get(name);
        // The module covers mc <= address < mc + mcSize, right-open: an
        // address exactly at mc + mcSize is outside it. Subtracting only after
        // the lower-bound check avoids unsigned wraparound, and dropping the
        // old zero-address short-circuit also catches a null result, which
        // otherwise stayed classified as Ok.
        if (e.address < mc || e.address - mc >= mcSize) {
            Solstice::console->critical("[offsets] OUT-OF-RANGE {} @ {}", name,
                                        MemUtils::getMbMemoryString(e.address));
            mStatus.set(name, ResolveStatus::OutOfRange, e.address, e.value, e.matches);
            mSigs[name] = 0;
        }
    }

    // The verdict covers SCANNED mappings only. Hardcoded constants are listed
    // separately and never contribute to an "everything is OK" conclusion:
    // CLASS_FIELD declarations and literal offsets in other headers are not in
    // this list, so this is a DEFINE_FIELD audit, not a whole-SDK one.
    const bool ok = mStatus.allOk();
    Solstice::console->info(
        "[offsets] scanned mappings: ok={}/{} missing={} ambiguous={} inconsistent={} implausible={} out-of-range={} (verdict={}) in {}ms",
        mStatus.count(ResolveStatus::Ok), mStatus.size(),
        mStatus.count(ResolveStatus::Missing), mStatus.count(ResolveStatus::Ambiguous),
        mStatus.count(ResolveStatus::Inconsistent), mStatus.count(ResolveStatus::Implausible),
        mStatus.count(ResolveStatus::OutOfRange), ok ? "OK" : "NOT TRUSTWORTHY", end - start);

    if (!mHardcoded.empty()) {
        Solstice::console->warn("[offsets] hardcoded mappings: {} UNVERIFIED (DEFINE_FIELD audit only; CLASS_FIELD and literal offsets are NOT covered)",
                                mHardcoded.size());
        for (const auto& [name, value] : mHardcoded)
            Solstice::console->warn("[audit]   {} = 0x{:X}", name, static_cast<unsigned>(value));
    }

    // No self-termination. The outcome belongs to the caller, which decides
    // whether to abort, dump diagnostics or let a human opt in explicitly.
    mIsInitialized = ok;
    return ok;
}

void OffsetProvider::deinitialize()
{
    spdlog::info("[offsets] deinitializing...");

    mSigInitializers.clear();
    mSigs.clear();
    mSigMatchCounts.clear();
    mSigMissing.clear();
    mSigImplausible.clear();
    mStatus.clear();
    mHardcoded.clear();

    mIsInitialized = false;
}

