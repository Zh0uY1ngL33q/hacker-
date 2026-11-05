#pragma once
//
// One authoritative outcome per mapping entry.
//
// SigManager and OffsetProvider used to keep four parallel, independently
// written maps (mSigs / mSigMatchCounts / mSigMissing / mSigImplausible).
// "mSigs[name] != 0" was then used as the definition of success by the startup
// report, so an entry that had been rejected as ambiguous or implausible still
// printed "[signatures] found X @ ..." and still fed its value to consumers.
// That is the state confusion this file exists to remove: an entry is Ok only
// when it was uniquely matched, passed the value check and stayed in the module.
//
// Header-only on purpose: CMake globs only src/*.cpp, so adding a .cpp would
// reshuffle the unity-build groups.
//

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class ResolveStatus {
    Ok,           // exactly one match, value plausible, address in range
    Missing,      // pattern did not match at all
    Ambiguous,    // matched more than once -- REFUSED, not "found"
    Inconsistent, // find() hit but the independent count saw 0 -- REFUSED
    Implausible,  // matched, but the derived index/offset cannot be real
    OutOfRange,   // resolved address outside the host module
};

inline const char* resolveStatusName(ResolveStatus s)
{
    switch (s) {
        case ResolveStatus::Ok:           return "ok";
        case ResolveStatus::Missing:      return "missing";
        case ResolveStatus::Ambiguous:    return "ambiguous";
        case ResolveStatus::Inconsistent: return "inconsistent";
        case ResolveStatus::Implausible:  return "implausible";
        case ResolveStatus::OutOfRange:   return "out-of-range";
    }
    return "unknown";
}

struct ResolveEntry {
    ResolveStatus status = ResolveStatus::Missing;
    uintptr_t     address = 0;   // where the pattern matched (+ offset where relevant)
    long long     value = 0;     // derived vtable index / member offset, when any
    int           matches = 0;   // number of hits seen (capped)
};

/// Not internally synchronised. Both owners already serialise their writes with
/// their own mSigMutex; all reads happen after the parallel scan has joined.
class ResolveTable {
public:
    void set(const std::string& name, ResolveStatus status,
             uintptr_t address = 0, long long value = 0, int matches = 0)
    {
        mEntries[name] = ResolveEntry{status, address, value, matches};
    }

    ResolveEntry get(const std::string& name) const
    {
        const auto it = mEntries.find(name);
        return it == mEntries.end() ? ResolveEntry{} : it->second;
    }

    bool allOk() const
    {
        for (const auto& [name, e] : mEntries)
            if (e.status != ResolveStatus::Ok) return false;
        return !mEntries.empty();
    }

    size_t count(ResolveStatus s) const
    {
        size_t n = 0;
        for (const auto& [name, e] : mEntries)
            if (e.status == s) ++n;
        return n;
    }

    std::vector<std::string> namesWith(ResolveStatus s) const
    {
        std::vector<std::string> out;
        for (const auto& [name, e] : mEntries)
            if (e.status == s) out.push_back(name);
        return out;
    }

    size_t size() const { return mEntries.size(); }
    void clear() { mEntries.clear(); }

private:
    std::unordered_map<std::string, ResolveEntry> mEntries;
};
