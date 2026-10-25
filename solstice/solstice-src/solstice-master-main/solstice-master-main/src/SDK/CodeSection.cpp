#include "CodeSection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>

#include <spdlog/spdlog.h>

namespace {

constexpr uint32_t kExecutable = IMAGE_SCN_MEM_EXECUTE;

// Pages sampled by looksDecrypted(). Spread across the whole range so lazily
// decrypted tail pages are not missed, which a prefix-only sample would do.
// Kept small because this runs in a 100ms poll loop.
constexpr size_t kSamplePages = 1024;
constexpr size_t kPageSize    = 0x1000;

/// Counts x64 prologues and int3 padding in one page, in a single pass.
/// Dispatches on the first byte instead of testing every probe at every offset,
/// which keeps the poll loop cheap (~4 MB scanned per call).
uint32_t countProbes(const uint8_t* page) {
    uint32_t hits = 0;
    for (size_t b = 0; b + 4 <= kPageSize; ++b) {
        switch (page[b]) {
            case 0x48:
                // mov [rsp+x], rbx / rcx
                if ((page[b + 1] == 0x89) &&
                    (page[b + 2] == 0x5C || page[b + 2] == 0x4C) &&
                    page[b + 3] == 0x24) {
                    hits++; b += 3; continue;
                }
                // sub rsp, x
                if (page[b + 1] == 0x83 && page[b + 2] == 0xEC) { hits++; b += 2; continue; }
                // mov rax, rsp
                if (page[b + 1] == 0x8B && page[b + 2] == 0xC4) { hits++; b += 2; continue; }
                break;
            case 0x40:
                // push rbx
                if (page[b + 1] == 0x53) { hits++; b += 1; continue; }
                break;
            case 0xCC:
                // int3 padding
                if (page[b + 1] == 0xCC && page[b + 2] == 0xCC && page[b + 3] == 0xCC) {
                    hits++; b += 3; continue;
                }
                break;
            default:
                break;
        }
    }
    return hits;
}

/// Recovers the NT header location, tolerating a tampered e_lfanew.
/// The NetEase packer is documented to mess with it, but ModuleDump and this
/// unit both need a working section table.
IMAGE_NT_HEADERS64* ntHeaders(uint8_t* baseBase) {
    if (!baseBase) return nullptr;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(baseBase);
    const auto peSig = static_cast<uint32_t>(IMAGE_NT_SIGNATURE);

    // Normal path.
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0) {
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(baseBase + dos->e_lfanew);
        if (nt->Signature == peSig) return nt;
    }

    // Tampered fallback: scan the first page for "PE\0\0" and validate that the
    // candidate actually looks like the 64-bit NT header we expect.
    constexpr size_t kHeaderScan = 0x1000;
    for (size_t off = 0; off + sizeof(IMAGE_NT_HEADERS64) < kHeaderScan; off += 4) {
        auto* cand = reinterpret_cast<IMAGE_NT_HEADERS64*>(baseBase + off);
        if (cand->Signature != peSig) continue;
        if (cand->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) continue;
        if (cand->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) continue;
        return cand;
    }
    return nullptr;
}

std::string sectionName(const IMAGE_SECTION_HEADER& sec) {
    char buf[9] = {0};
    std::memcpy(buf, sec.Name, 8);
    return std::string(buf);
}

} // namespace

const std::vector<CodeSection::Range>& CodeSection::ranges() {
    static const std::vector<Range> cached = [] {
        std::vector<Range> out;

        const auto mod   = hat::process::get_process_module();
        auto*      base  = reinterpret_cast<uint8_t*>(mod.address());
        auto*      nt    = ntHeaders(base);
        if (!nt) {
            spdlog::warn("[codesection] could not locate the PE header; falling back to the whole module");
            return out;
        }

        const uint32_t sizeOfImage = nt->OptionalHeader.SizeOfImage;
        auto*          sec         = IMAGE_FIRST_SECTION(nt);
        const WORD     count       = nt->FileHeader.NumberOfSections;

        for (WORD i = 0; i < count; ++i, ++sec) {
            if (!(sec->Characteristics & kExecutable)) continue;

            const uint32_t rva  = sec->VirtualAddress;
            const uint32_t size = sec->Misc.VirtualSize;
            if (rva == 0 || size == 0) continue;
            // Sections past SizeOfImage are not mapped, so never scan them.
            if (rva >= sizeOfImage) continue;

            Range r;
            r.rva             = rva;
            r.size            = std::min(size, sizeOfImage - rva);
            r.characteristics = sec->Characteristics;
            r.name            = sectionName(*sec);
            r.isBlank         = r.name.empty() || r.name[0] == '\0';
            out.push_back(std::move(r));
        }

        // Search the largest executable range first: that is the game's real
        // code section. The packer's own stub is executable too but much
        // smaller, and matching inside it would only produce noise.
        std::stable_sort(out.begin(), out.end(),
                         [](const Range& a, const Range& b) { return a.size > b.size; });

        for (const auto& r : out) {
            spdlog::info("[codesection] range {} RVA=0x{:X} size=0x{:X} ({:.1f} MB)",
                         r.isBlank ? "<blank>" : r.name, r.rva, r.size, r.size / (1024.0 * 1024.0));
        }
        return out;
    }();
    return cached;
}

bool CodeSection::available() {
    return !ranges().empty();
}

namespace {

/// Clamped view of one executable range inside the module mapping.
std::span<std::byte> viewOf(const CodeSection::Range& r) {
    static const auto moduleData = hat::process::get_process_module().get_module_data();
    if (r.rva >= moduleData.size()) return {};
    const size_t avail = moduleData.size() - r.rva;
    const size_t len   = std::min<size_t>(r.size, avail);
    if (len == 0) return {};
    return moduleData.subspan(r.rva, len);
}

} // namespace

hat::scan_result CodeSection::find(const hat::signature_view& sig) {
    for (const auto& r : ranges()) {
        const auto span = viewOf(r);
        if (span.empty()) continue;
        const auto res = hat::find_pattern(span, sig);
        if (res.has_result()) return res;
    }
    return {};
}

int CodeSection::countMatches(const hat::signature_view& sig, int cap) {
    if (cap <= 0) return 0;

    // Counted in the primary (largest) range only. The packer stub is not game
    // code, so a hit there does not make the signature ambiguous for our use.
    const auto& rs = ranges();
    if (rs.empty()) return 0;

    const auto span = viewOf(rs.front());
    if (span.empty()) return 0;

    std::array<std::byte*, 8> hits{};
    const size_t limit = std::min<size_t>(static_cast<size_t>(cap), hits.size());

    auto [inIt, outIt] = hat::find_all_pattern(span.begin(), span.end(),
                                               hits.begin(), hits.begin() + limit, sig);
    (void)inIt;
    return static_cast<int>(std::distance(hits.begin(), outIt));
}

bool CodeSection::looksDecrypted(double* entropyOut, double* densityOut) {
    const auto& rs = ranges();
    if (rs.empty()) return false;

    const auto span = viewOf(rs.front());
    if (span.size() < kPageSize * 8) return false;

    const size_t pages = std::min(kSamplePages, span.size() / kPageSize);
    const size_t step  = pages > 1 ? (span.size() / kPageSize - 1) / (pages - 1) : 1;

    uint64_t counts[256] = {0};
    uint64_t sampled     = 0;
    uint64_t probeHits   = 0;

    for (size_t i = 0; i < pages; ++i) {
        const size_t offset = i * step * kPageSize;
        if (offset + kPageSize > span.size()) break;
        const auto* page = reinterpret_cast<const uint8_t*>(span.data() + offset);

        for (size_t b = 0; b < kPageSize; ++b) counts[page[b]]++;
        sampled += kPageSize;

        probeHits += countProbes(page);
    }

    if (sampled == 0) return false;

    double entropy = 0.0;
    for (uint64_t c : counts) {
        if (!c) continue;
        const double p = static_cast<double>(c) / static_cast<double>(sampled);
        entropy -= p * std::log2(p);
    }

    const double density = static_cast<double>(probeHits) / static_cast<double>(pages);
    if (entropyOut) *entropyOut = entropy;
    if (densityOut) *densityOut = density;

    // Prologue density is the reliable signal. Encrypted pages score ~0.0;
    // real code scores several per page. The entropy bound only guards against
    // a pathological all-0xCC image.
    return density >= 0.5 && entropy < 7.5;
}

int CodeSection::waitUntilDecrypted(int timeoutMs, int intervalMs) {
    if (intervalMs <= 0) intervalMs = 100;

    int waited = 0;
    int lastLogged = -1;

    while (waited < timeoutMs) {
        double entropy = 0.0;
        double density = 0.0;
        if (looksDecrypted(&entropy, &density)) {
            spdlog::info("[codesection] code looks decrypted after {}ms (entropy={:.3} density={:.2}/page)",
                         waited, entropy, density);
            return waited;
        }

        // Progress line every second so a stuck wait is visible in the log.
        const int sec = waited / 1000;
        if (sec != lastLogged) {
            spdlog::info("[codesection] waiting for decrypted code... {}ms (entropy={:.3} density={:.2}/page)",
                         waited, entropy, density);
            lastLogged = sec;
        }

        Sleep(static_cast<DWORD>(intervalMs));
        waited += intervalMs;
    }

    spdlog::warn("[codesection] code did not look decrypted after {}ms; continuing anyway", timeoutMs);
    return waited;
}
