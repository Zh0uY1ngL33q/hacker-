#include "SigFuzz.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdlib>
#include <thread>
#include <Windows.h>

#include <Solstice.hpp>
#include <SDK/CodeSection.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/ResolveState.hpp>
#include <SDK/SigManager.hpp>
#include <Utils/FileUtils.hpp>
#include <Utils/SysUtils/ModuleDump.hpp>

namespace {

// Collected from the OpenMP scan loop, so guarded. Only failures land here, and
// there are at most a few dozen, so a vector is fine.
std::vector<SigFuzz::Entry> g_sigFuzzEntries;
std::mutex                  g_sigFuzzMutex;

// Patterns that failed to resolve, kept for the deferred near-miss analysis.
// The signature_view points at the static pattern literal in the signature
// table, so it stays valid for the lifetime of the process.
struct PendingPattern {
    std::string        name;
    hat::signature_view sig;
};
std::vector<PendingPattern> g_pendingPatterns;

// The analysis thread. Joinable on purpose: it logs through the spdlog logger,
// so the logger must outlive it - stopAsync() joins it before teardown.
std::thread             g_fuzzThread;
std::atomic<bool>       g_fuzzStop{false};

std::string hexRva(uintptr_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(value));
    return std::string(buf);
}

std::string hexBytes(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(bytes.size() * 3);
    for (const uint8_t b : bytes) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X ", static_cast<unsigned int>(b));
        out += buf;
    }
    if (!out.empty()) out.pop_back();
    return out;
}

} // namespace

void SigFuzz::onFailed(const char* name, hat::signature_view sig) {
    // Record only. The near-miss search is a serial full-section fuzzy scan
    // (~0.4 s per failed pattern on a 139 MB section); running it inline made
    // every injection burn ~16 s of CPU before the client was even hooked up,
    // which showed up as a freeze right after injection. It is diagnostics
    // only, so it runs later on a background thread.
    {
        std::lock_guard lock(g_sigFuzzMutex);
        g_pendingPatterns.push_back({name, sig});
    }
}

// Near-miss analysis, off the critical path and below normal priority so it
// never competes with the game for CPU time.
//
// Opt-in: without SOLSTICE_FUZZ=1 this returns immediately. The search is
// diagnostics only, and running it by default meant every injection burned
// ~16 s of CPU, flooded the console with 40+ [fuzz] lines (drowning the
// debug prompt) and left a detached thread writing to a logger that the
// unload path tears down - which crashed the game on eject.
void SigFuzz::analyzeAsync() {
    if (std::getenv("SOLSTICE_FUZZ") == nullptr) return;
    if (g_fuzzThread.joinable()) return; // already running

    g_fuzzStop = false;
    g_fuzzThread = std::thread([]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        std::vector<PendingPattern> pending;
        {
            std::lock_guard lock(g_sigFuzzMutex);
            pending.swap(g_pendingPatterns);
        }

        for (auto& pendingPattern : pending) {
            if (g_fuzzStop) return;

            Entry entry;
            entry.name    = pendingPattern.name;
            entry.pattern = hat::to_string(pendingPattern.sig);

            // One pass at the largest useful budget. A candidate with fewer differences is
            // also a candidate at a larger budget, so escalating would only repeat the same
            // scan over the section; the ranking below keeps the best of what came back.
            // fuzzyFind scales the budget down for patterns with few concrete bytes, where
            // a large budget would match almost everywhere.
            auto hits = CodeSection::fuzzyFind(pendingPattern.sig, 6, 8);
            if (!hits.empty()) {
                std::stable_sort(hits.begin(), hits.end(),
                                 [](const CodeSection::FuzzyMatch& a, const CodeSection::FuzzyMatch& b) {
                                     return a.diffPositions.size() < b.diffPositions.size();
                                 });

                const auto& best = hits.front();
                entry.found  = true;
                entry.rva    = best.rva;
                entry.actual = hexBytes(best.actual);

                // Two places can match with the same number of differences. That is as
                // untrustworthy as an ambiguous exact match, so do not hide it.
                const size_t bestScore = best.diffPositions.size();
                for (const auto& hit : hits) {
                    if (hit.diffPositions.size() == bestScore) entry.candidates++;
                }

                for (const int index : best.diffPositions) {
                    Diff diff;
                    diff.at   = index;
                    diff.from = static_cast<int>(pendingPattern.sig[static_cast<size_t>(index)].value());
                    diff.to   = static_cast<int>(best.actual[static_cast<size_t>(index)]);
                    entry.diffs.push_back(diff);
                }
            }

            // Log BEFORE moving the entry: std::move leaves the moved-from
            // name empty, which is why the previous run printed "[fuzz] :".
            if (!entry.found) {
                Solstice::console->critical(
                    "[fuzz] {}: no near miss found; the function was probably rewritten or removed",
                    entry.name);
            }
            else {
                std::string detail;
                for (const auto& diff : entry.diffs) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), " %d:%02X>%02X", diff.at, diff.from, diff.to);
                    detail += buf;
                }

                Solstice::console->warn("[fuzz] {}: RVA {}, {} byte(s) differ, {} candidate(s){}",
                                        entry.name, hexRva(entry.rva), entry.diffs.size(),
                                        entry.candidates, detail);
            }

            {
                std::lock_guard lock(g_sigFuzzMutex);
                g_sigFuzzEntries.push_back(std::move(entry));
            }
        }

        // Refresh the report so it also carries the near misses.
        writeReport();
    });
}

// Joins the analysis thread. Called before the logger is torn down.
void SigFuzz::stopAsync() {
    g_fuzzStop = true;
    if (g_fuzzThread.joinable()) g_fuzzThread.join();
}

bool SigFuzz::writeReport() {
    nlohmann::json root = nlohmann::json::object();

    // Both providers now publish one authoritative status per entry. Derive the
    // JSON categories from that table, NOT from mSigs: rejected mappings
    // deliberately store 0 there, so an address-based test would collapse
    // Missing / Ambiguous / Inconsistent / Implausible / OutOfRange into one
    // misleading "failed" bucket -- the console report and this file would then
    // disagree about what actually went wrong.
    auto classify = [](const ResolveTable& table) {
        nlohmann::json out = nlohmann::json::object();
        out["total"] = table.size();
        out["ok"]    = table.count(ResolveStatus::Ok);

        const ResolveStatus kRejected[] = {
            ResolveStatus::Missing, ResolveStatus::Ambiguous,
            ResolveStatus::Inconsistent, ResolveStatus::Implausible,
            ResolveStatus::OutOfRange
        };
        for (const auto status : kRejected) {
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& name : table.namesWith(status)) {
                const auto e = table.get(name);
                nlohmann::json item = nlohmann::json::object();
                item["name"]    = name;
                item["status"]  = resolveStatusName(status);
                item["matches"] = e.matches;
                if (e.address != 0) item["rva"] = hexRva(e.address);
                if (e.value != 0)   item["value"] = e.value;
                entries.push_back(item);
            }
            out[resolveStatusName(status)] = entries;
        }

        // Legacy key kept for existing consumers of sigreport.json; the
        // "ambiguous" key is already produced by the loop above.
        out["failed"] = out["missing"];
        return out;
    };

    nlohmann::json offsets    = classify(OffsetProvider::statusTable());
    nlohmann::json signatures = classify(SigManager::statusTable());

    nlohmann::json nearMisses = nlohmann::json::array();
    {
        std::lock_guard lock(g_sigFuzzMutex);
        for (const auto& entry : g_sigFuzzEntries) {
            nlohmann::json miss = nlohmann::json::object();
            miss["name"]    = entry.name;
            miss["pattern"] = entry.pattern;
            miss["found"]   = entry.found;
            if (entry.found) {
                miss["rva"]        = hexRva(entry.rva);
                miss["actual"]     = entry.actual;
                miss["candidates"] = entry.candidates;

                nlohmann::json diffs = nlohmann::json::array();
                for (const auto& diff : entry.diffs) {
                    nlohmann::json d = nlohmann::json::object();
                    d["at"]   = diff.at;
                    d["from"] = diff.from;
                    d["to"]   = diff.to;
                    diffs.push_back(d);
                }
                miss["diffs"] = diffs;
            }
            nearMisses.push_back(miss);
        }
    }

    root["offsets"]    = offsets;
    root["signatures"] = signatures;
    root["nearMisses"] = nearMisses;

    // Same directory as the image dump, so everything produced by a failing run
    // is in one place -- including when SOLSTICE_DUMP_DIR redirects the dump.
    const std::string dir  = ModuleDump::outputDir();
    const std::string path = dir + "sigreport.json";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        Solstice::console->critical("[fuzz] could not open {} for writing", path);
        return false;
    }
    // replace: this file must never be the reason the process dies. Names and
    // patterns are ASCII, but the dump path below is not, so do not rely on it.
    file << root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    const bool ok = file.good();
    file.close();

    if (!ok) {
        Solstice::console->critical("[fuzz] failed while writing {}", path);
        return false;
    }

    Solstice::console->info(
        "[fuzz] wrote {}: {} near miss(es), {} unresolved offset(s), {} unresolved signature(s)",
        path, nearMisses.size(), offsetsFailed.size(), signaturesFailed.size());
    return true;
}
