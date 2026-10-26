#pragma once
//
// Locating the executable region of the host module.
//
// The previous implementation scanned `moduleData.subspan(0x1000, 0x7a7baa9)`,
// which is a snapshot of one specific Minecraft build:
//
//   * 0x1000 is the RVA of the first section, not the start of the image.
//   * 0x7a7baa9 is that build's .text VirtualSize. On 1.21.120 the real size is
//     0x8b6e8a9, so roughly 18 MB of code was never scanned at all. Every
//     signature living there silently resolved to 0.
//   * The NetEase packer blanks the section names, so `get_section_data(".text")`
//     cannot be used to recover the range either.
//   * There is more than one executable section. Scanning only the first one
//     misses the rest by design.
//
// So derive the ranges from the PE section table at runtime instead. That is
// version independent: it only assumes the host is a normal PE image.
//

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <Windows.h>
#include <libhat.hpp>

class CodeSection {
public:
    struct Range {
        uint32_t    rva             = 0;
        uint32_t    size            = 0;
        uint32_t    characteristics = 0;
        std::string name;   // empty when the packer blanked it
        bool        isBlank         = false;
    };

    /// Executable ranges of the host module, in PE order. Computed once and
    /// cached; the section table cannot change while the image is loaded.
    static const std::vector<Range>& ranges();

    /// True when at least one executable range could be derived.
    static bool available();

    /// First match of `sig`, searching the executable ranges in PE order.
    /// Returns an empty scan_result when nothing matches.
    static hat::scan_result find(const hat::signature_view& sig);

    /// How many executable ranges contain at least one match, capped at `cap`.
    /// A cheap ambiguity probe: anything above 1 means the signature is not
    /// specific enough and callers should not trust the first hit blindly.
    static int countMatches(const hat::signature_view& sig, int cap = 2);

    /// A near-miss of a signature: where it probably moved to, and which concrete
    /// bytes there no longer match the pattern.
    struct FuzzyMatch {
        uintptr_t            rva = 0;   // offset from the module base
        std::vector<int>     diffPositions;  // indexes into the pattern
        std::vector<uint8_t> actual;         // bytes found at `rva`, pattern.size() long
    };

    /// Exact-match scanning is all or nothing: one changed immediate and the
    /// signature is reported as missing, with no information about where it went
    /// or what changed. This searches for the same pattern while tolerating up to
    /// `maxMismatch` concrete bytes that no longer match, and returns up to `cap`
    /// candidates in address order.
    ///
    /// Used only for diagnosis of an unsupported game version, never on the hot
    /// path: the relaxed comparison cannot reject a candidate after a single byte,
    /// so it is several times slower than find().
    static std::vector<FuzzyMatch> fuzzyFind(const hat::signature_view& sig,
                                             int maxMismatch, int cap = 4);

    /// Heuristic "has the packer decrypted the code yet" check.
    ///
    /// Encrypted bytes have an entropy near 8.0 and contain no x64 function
    /// prologues. Decrypted code contains both. Sampling is spread across the
    /// ranges so lazily-decrypted tail pages are noticed.
    ///
    /// This deliberately does not use a signature probe: a probe taken from one
    /// game version can never match another, which made the old wait loop burn
    /// its full timeout on every update.
    static bool looksDecrypted(double* entropyOut = nullptr, double* densityOut = nullptr);

    /// Polls looksDecrypted() until it succeeds or `timeoutMs` elapses.
    /// Returns the number of milliseconds waited.
    static int waitUntilDecrypted(int timeoutMs = 30000, int intervalMs = 100);
};
