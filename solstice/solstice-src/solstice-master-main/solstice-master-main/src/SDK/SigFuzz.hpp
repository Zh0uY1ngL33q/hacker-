#pragma once
//
// Signature drift diagnostics.
//
// The signature table is written for one Minecraft build. On any other build
// most patterns stop matching, and the plain failure list says nothing about
// *why*: a pattern can fail because a member offset changed, because a call
// target moved, or because the function was rewritten entirely. Those are very
// different amounts of work, and telling them apart used to require the 200 MB
// decrypted image.
//
// So when a pattern does not resolve, look for it again with a small mismatch
// budget (CodeSection::fuzzyFind). Minecraft's code keeps its shape across
// minor versions; what changes are immediates -- member offsets, call
// displacements, small constants. Recovering where the pattern went and which
// bytes no longer match is usually enough to rewrite the signature with no
// disassembly at all.
//
// The result is also written to sigreport.json next to the config files, so a
// failure can be reported by sending one small file instead of screenshots.
//

#include <cstdint>
#include <string>
#include <vector>

#include <libhat/Signature.hpp>

class SigFuzz {
public:
    /// A concrete pattern byte that no longer matches.
    struct Diff {
        int at   = 0;  // index into the pattern
        int from = 0;  // byte the pattern expects
        int to   = 0;  // byte actually found
    };

    struct Entry {
        std::string      name;
        std::string      pattern;      // as written in OffsetProvider.hpp, '?' for wildcards
        bool             found = false; // a near-miss exists
        uintptr_t        rva   = 0;
        std::string      actual;       // bytes at `rva`, same length as pattern
        std::vector<Diff> diffs;
        int              candidates = 0; // equally good candidates; >1 means ambiguous
    };

    /// Records a pattern that did not resolve, together with a near-miss if one
    /// exists. Called from the OpenMP scan loop, hence the internal lock.
    static void onFailed(const char* name, hat::signature_view sig);

    /// Writes everything collected during this session to sigreport.json in the
    /// Solstice directory, including the unresolved and ambiguous entries of both
    /// providers. Returns false when the file cannot be written.
    static bool writeReport();
};
