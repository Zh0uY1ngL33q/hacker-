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

    /// Records a pattern that did not resolve. The near-miss search itself is
    /// NOT done here: that is a serial full-section fuzzy scan costing ~0.4 s
    /// per failed pattern, i.e. ~16 s of CPU right after injection, and it only
    /// feeds the diagnostics report. Call analyzeAsync() once the signature
    /// table is complete to run it off the critical path.
    static void onFailed(const char* name, hat::signature_view sig);

    /// Runs the near-miss analysis on a background, low-priority thread.
    /// Opt-in only (SOLSTICE_FUZZ=1): the search is a serial full-section scan
    /// costing ~0.4 s per failed pattern (~16 s for 43 of them) and it only
    /// feeds the diagnostics report. Without the env var this does nothing.
    static void analyzeAsync();

    /// Stops the background analysis and waits for it to finish. Must be called
    /// before the logger is torn down - the analysis thread logs through it.
    static void stopAsync();

    /// Writes everything collected during this session to sigreport.json in the
    /// Solstice directory, including the unresolved and ambiguous entries of both
    /// providers. Returns false when the file cannot be written.
    static bool writeReport();
};
