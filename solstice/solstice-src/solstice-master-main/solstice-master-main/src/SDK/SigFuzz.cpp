#include "SigFuzz.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <Solstice.hpp>
#include <SDK/CodeSection.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
#include <Utils/FileUtils.hpp>

namespace {

// Collected from the OpenMP scan loop, so guarded. Only failures land here, and
// there are at most a few dozen, so a vector is fine.
std::vector<SigFuzz::Entry> g_sigFuzzEntries;
std::mutex                  g_sigFuzzMutex;

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
    Entry entry;
    entry.name    = name;
    entry.pattern = hat::to_string(sig);

    // One pass at the largest useful budget. A candidate with fewer differences is
    // also a candidate at a larger budget, so escalating would only repeat the same
    // scan over the section; the ranking below keeps the best of what came back.
    // fuzzyFind scales the budget down for patterns with few concrete bytes, where
    // a large budget would match almost everywhere.
    auto hits = CodeSection::fuzzyFind(sig, 6, 8);
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
            diff.from = static_cast<int>(sig[static_cast<size_t>(index)].value());
            diff.to   = static_cast<int>(best.actual[static_cast<size_t>(index)]);
            entry.diffs.push_back(diff);
        }
    }

    {
        std::lock_guard lock(g_sigFuzzMutex);
        g_sigFuzzEntries.push_back(entry);
    }

    if (!entry.found) {
        Solstice::console->critical(
            "[fuzz] {}: no near miss found; the function was probably rewritten or removed",
            entry.name);
        return;
    }

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

bool SigFuzz::writeReport() {
    nlohmann::json root = nlohmann::json::object();

    // Offsets first: the resolved table doubles as a version fingerprint, since
    // it only contains anything when the signature table matched this build.
    nlohmann::json offsetsFailed    = nlohmann::json::array();
    nlohmann::json offsetsAmbiguous = nlohmann::json::array();

    for (const auto& [name, address] : OffsetProvider::mSigs) {
        if (address == 0) {
            offsetsFailed.push_back(name);
            continue;
        }
        const auto it = OffsetProvider::mSigMatchCounts.find(name);
        if (it == OffsetProvider::mSigMatchCounts.end() || it->second <= 1) continue;

        nlohmann::json ambiguous = nlohmann::json::object();
        ambiguous["name"]    = name;
        ambiguous["matches"] = it->second;
        ambiguous["rva"]     = hexRva(address);
        offsetsAmbiguous.push_back(ambiguous);
    }

    nlohmann::json signaturesFailed    = nlohmann::json::array();
    nlohmann::json signaturesAmbiguous = nlohmann::json::array();

    for (const auto& [name, address] : SigManager::mSigs) {
        if (address == 0) {
            signaturesFailed.push_back(name);
            continue;
        }
        const auto it = SigManager::mSigMatchCounts.find(name);
        if (it == SigManager::mSigMatchCounts.end() || it->second <= 1) continue;

        nlohmann::json ambiguous = nlohmann::json::object();
        ambiguous["name"]    = name;
        ambiguous["matches"] = it->second;
        ambiguous["rva"]     = hexRva(address);
        signaturesAmbiguous.push_back(ambiguous);
    }

    nlohmann::json offsets = nlohmann::json::object();
    offsets["total"]     = OffsetProvider::mSigs.size();
    offsets["failed"]    = offsetsFailed;
    offsets["ambiguous"] = offsetsAmbiguous;

    nlohmann::json signatures = nlohmann::json::object();
    signatures["total"]     = SigManager::mSigs.size();
    signatures["failed"]    = signaturesFailed;
    signatures["ambiguous"] = signaturesAmbiguous;

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
    // is in one place.
    const std::string dir  = FileUtils::getSolsticeDir() + "Dump\\";
    const std::string path = dir + "sigreport.json";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::ofstream file(path);
    if (!file.is_open()) {
        Solstice::console->critical("[fuzz] could not open {} for writing", path);
        return false;
    }
    file << root.dump(2);
    file.close();

    Solstice::console->info(
        "[fuzz] wrote {}: {} near miss(es), {} unresolved offset(s), {} unresolved signature(s)",
        path, nearMisses.size(), offsetsFailed.size(), signaturesFailed.size());
    return true;
}
