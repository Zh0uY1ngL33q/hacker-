#include "ModuleDump.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <Solstice.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
#include <Utils/FileUtils.hpp>

namespace {

constexpr uint32_t kPageSize = 0x1000;

// The .text section is ~146MB, so entropy is sampled rather than computed
// over every byte.
constexpr size_t kEntropySampleStride = 512;

// "0x1234ABCD". Local rather than fmt::format, which this translation unit
// does not otherwise depend on.
std::string hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

std::string timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// Reads the on-disk file version via the version API rather than poking at
// IMAGE_OPTIONAL_HEADER, whose field layout differs between SDK versions.
// version.lib is already in the project's link libraries.
std::string readImageVersion(HMODULE base) {
    char path[MAX_PATH] = {0};
    if (!GetModuleFileNameA(base, path, MAX_PATH)) return "unknown";

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeA(path, &handle);
    if (size == 0) return "unknown";

    std::vector<unsigned char> buf(size);
    if (!GetFileVersionInfoA(path, handle, size, buf.data())) return "unknown";

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!VerQueryValueA(buf.data(), "\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen) ||
        !ffi || ffiLen == 0) {
        return "unknown";
    }

    char out[32];
    std::snprintf(out, sizeof(out), "%u.%u.%u.%u",
               HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
               HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    return out;
}

bool pageIsReadable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return true;
}

// Section names come straight out of the PE header and the packer leaves
// arbitrary bytes in them -- not valid UTF-8. That matters because
// nlohmann::json::dump() validates UTF-8 by default and *throws* on anything
// else, and this string goes into the manifest. Escape everything outside
// printable ASCII so the name stays informative and the JSON stays valid.
std::string printableSectionName(const IMAGE_SECTION_HEADER& sec, WORD index) {
    if (sec.Name[0] == 0) {
        // NetEase blanks the code section names.
        char buf[24];
        std::snprintf(buf, sizeof(buf), "<blank#%u>", static_cast<unsigned>(index));
        return buf;
    }

    std::string out;
    out.reserve(24);
    for (int i = 0; i < 8 && sec.Name[i] != 0; ++i) {
        const unsigned char c = sec.Name[i];
        if (c >= 0x20 && c < 0x7F) {
            out += static_cast<char>(c);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", static_cast<unsigned>(c));
            out += buf;
        }
    }
    return out;
}

// Writes JSON and reports whether it landed. Used for the manifest, which is
// rewritten after every section so that an interrupted dump still leaves a
// description of what made it into the image.
bool writeJsonFile(const std::string& path, const nlohmann::json& value) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;

    // replace: never throw on a byte sequence JSON cannot represent. Throwing
    // from here took the whole process down mid-dump.
    f << value.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    return f.good();
}

} // namespace

bool ModuleDump::isEnabled() {
    static const bool enabled = [] {
        char buf[16] = {0};
        const DWORD n = GetEnvironmentVariableA("SOLSTICE_DUMP", buf,
                                                static_cast<DWORD>(sizeof(buf)));
        if (n == 0 || n >= sizeof(buf)) return false;
        std::string v(buf);
        std::transform(v.begin(), v.end(), v.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        return v == "1" || v == "true" || v == "yes";
    }();
    return enabled;
}

double ModuleDump::entropy(const std::vector<unsigned char>& data) {
    if (data.empty()) return 0.0;

    uint64_t counts[256] = {0};
    size_t sampled = 0;
    for (size_t i = 0; i < data.size(); i += kEntropySampleStride) {
        counts[data[i]]++;
        sampled++;
    }
    if (sampled == 0) return 0.0;

    double h = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (!counts[i]) continue;
        const double p = static_cast<double>(counts[i]) / static_cast<double>(sampled);
        h -= p * std::log2(p);
    }
    return h;
}

size_t ModuleDump::copyReadable(uintptr_t base, uint32_t rva, uint32_t size,
                                std::vector<unsigned char>& out, uint32_t* skipped) {
    out.assign(size, 0);
    if (skipped) *skipped = 0;

    uint32_t offset = 0;
    while (offset < size) {
        const uintptr_t addr = base + rva + offset;
        const uint32_t remain = size - offset;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        // Clamp to the current region and to one page, so a region that spans
        // past our section can never over-read.
        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        uint32_t chunk = kPageSize;
        if (regionEnd > addr) {
            const uintptr_t avail = regionEnd - addr;
            if (avail < chunk) chunk = static_cast<uint32_t>(avail);
        }
        if (chunk > remain) chunk = remain;
        if (chunk == 0) break; // defensive: never spin

        if (pageIsReadable(mbi)) {
            memcpy(out.data() + offset, reinterpret_cast<const void*>(addr), chunk);
        } else {
            // Zero-filled; the manifest records the gap so it is not mistaken
            // for genuinely-zero code.
            memset(out.data() + offset, 0, chunk);
            if (skipped) *skipped += chunk;
        }
        offset += chunk;
    }
    return out.size();
}

void ModuleDump::writeOffsetSnapshot(const std::string& path, const std::string& tag) {
    nlohmann::json j;

    const uintptr_t base =
        reinterpret_cast<uintptr_t>(GetModuleHandleA("Minecraft.Windows.exe"));

    // Store module-relative RVAs so snapshots stay diffable across runs with
    // different ASLR bases.
    auto rel = [&](uintptr_t v) -> int64_t {
        return v ? static_cast<int64_t>(v - base) : -1;
    };

    j["tag"] = tag;
    j["moduleBase"] = hex(base);

    j["offsets"] = nlohmann::json::object();
    for (const auto& [name, addr] : OffsetProvider::mSigs)
        j["offsets"][name] = rel(addr);

    j["signatures"] = nlohmann::json::object();
    for (const auto& [name, addr] : SigManager::mSigs)
        j["signatures"][name] = rel(addr);

    std::ofstream f(path);
    if (!f.is_open()) {
        Solstice::console->critical("[dump] failed to write offset snapshot: {}", path);
        return;
    }
    f << j.dump(2);
}

bool ModuleDump::dump(const std::string& moduleName, bool force) {
    if (!force && !isEnabled()) return false;

    // Nothing thrown in here may reach the caller: this runs on the client's init
    // thread and an escaping exception terminates the process. That is exactly what
    // happened when the manifest was serialised -- nlohmann rejects invalid UTF-8
    // by default, the dump threw after writing the 200 MB image but before writing
    // the manifest, and the game died leaving a 0-byte .sections.json behind.
    try {
        return dumpImage(moduleName);
    } catch (const std::exception& e) {
        Solstice::console->critical("[dump] aborted: {}", e.what());
    } catch (...) {
        Solstice::console->critical("[dump] aborted: unknown exception");
    }
    return false;
}

bool ModuleDump::dumpImage(const std::string& moduleName) {
    HMODULE base = GetModuleHandleA(moduleName.c_str());
    if (!base) {
        Solstice::console->critical("[dump] could not find module {}", moduleName);
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        Solstice::console->critical("[dump] bad DOS header");
        return false;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        reinterpret_cast<uint8_t*>(base) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        Solstice::console->critical("[dump] bad NT header");
        return false;
    }

    const uint32_t sizeOfHeaders = nt->OptionalHeader.SizeOfHeaders;
    const std::string version = readImageVersion(base);

    // Output directory. Defaults to the client's own folder, but can be
    // overridden with SOLSTICE_DUMP_DIR so the image can be written somewhere
    // easy to find. This build is a Win32 desktop client rather than the UWP
    // package, so the default RoamingState path may not be where you expect.
    std::string dir;
    {
        char buf[MAX_PATH * 4] = {0};
        const DWORD n = GetEnvironmentVariableA("SOLSTICE_DUMP_DIR", buf,
                                                static_cast<DWORD>(sizeof(buf)));
        if (n > 0 && n < sizeof(buf)) {
            dir = buf;
            if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += '\\';
        } else {
            dir = FileUtils::getSolsticeDir() + "Dump\\";
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::string tag = version + "_" + timestamp();
    const std::string imagePath = dir + "Minecraft.Windows_" + tag + ".image";
    const std::string manifestPath = dir + "Minecraft.Windows_" + tag + ".sections.json";

    Solstice::console->info("[dump] writing decrypted image -> {}", imagePath);

    std::ofstream image(imagePath, std::ios::binary | std::ios::trunc);
    if (!image.is_open()) {
        Solstice::console->critical("[dump] cannot open {} for writing", imagePath);
        return false;
    }

    nlohmann::json manifest;
    manifest["module"] = moduleName;
    manifest["imageVersion"] = version;
    manifest["moduleBase"] = hex(reinterpret_cast<uintptr_t>(base));
    manifest["sizeOfImage"] = hex(nt->OptionalHeader.SizeOfImage);
    manifest["timestamp"] = tag;
    manifest["sections"] = nlohmann::json::array();

    // Written up front and rewritten after every section. It is a few KB while the
    // image is 200+ MB, and it is the only thing that makes the image usable
    // offline, so it must not be the artefact left behind.
    if (!writeJsonFile(manifestPath, manifest))
        Solstice::console->critical("[dump] cannot write {} (disk full?)", manifestPath);

    // Headers first, so the result parses as a PE offline.
    {
        std::vector<unsigned char> hdr;
        copyReadable(reinterpret_cast<uintptr_t>(base), 0, sizeOfHeaders, hdr);
        image.write(reinterpret_cast<const char*>(hdr.data()),
                    static_cast<std::streamsize>(hdr.size()));
    }

    auto* sec = IMAGE_FIRST_SECTION(nt);
    const WORD count = nt->FileHeader.NumberOfSections;

    for (WORD i = 0; i < count; ++i, ++sec) {
        const uint32_t vsize = sec->Misc.VirtualSize;
        if (vsize == 0) continue;

        std::vector<unsigned char> data;
        uint32_t skipped = 0;
        copyReadable(reinterpret_cast<uintptr_t>(base), sec->VirtualAddress, vsize,
                     data, &skipped);

        // Write at the real RVA so the file mirrors virtual layout: byte at
        // file offset X is memory at RVA X.
        image.seekp(static_cast<std::streamoff>(sec->VirtualAddress), std::ios::beg);
        image.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));

        // A full disk leaves a short image and no error: ofstream only sets its
        // fail bit. Say so instead of silently producing a truncated dump that
        // looks complete.
        if (!image) {
            Solstice::console->critical(
                "[dump] write failed at sec[{}] (disk full?); the image is incomplete", i);
            manifest["truncatedAt"] = printableSectionName(*sec, i);
            break;
        }

        const std::string secName = printableSectionName(*sec, i);

        const double ent = entropy(data);

        // Built explicitly rather than via a braced init-list: push_back with a
        // list of pairs relies on nlohmann's init-list -> object conversion,
        // which is easy to get subtly wrong.
        nlohmann::json secJson = nlohmann::json::object();
        secJson["index"] = i;
        secJson["name"] = secName;
        secJson["rva"] = hex(sec->VirtualAddress);
        secJson["virtualSize"] = hex(vsize);
        secJson["rawSize"] = hex(sec->SizeOfRawData);
        secJson["characteristics"] = hex(sec->Characteristics);
        secJson["entropy"] = ent;
        secJson["dumpedBytes"] = vsize - skipped;
        secJson["skippedBytes"] = skipped;
        manifest["sections"].push_back(secJson);
        writeJsonFile(manifestPath, manifest);

        Solstice::console->info(
            "[dump]   sec[{}] {} RVA=0x{:X} vsize=0x{:X} entropy={:.3} skipped=0x{:X}",
            i, secName, sec->VirtualAddress, vsize, ent, skipped);
    }

    image.flush();
    image.close();

    // Logged right after the image is closed: if anything below fails, the log
    // still says how large the image actually got, which is what distinguishes a
    // truncated dump from a complete one.
    std::error_code sizeEc;
    const auto finalSize = std::filesystem::file_size(imagePath, sizeEc);
    Solstice::console->info("[dump] image closed: {} ({} MB)",
                            imagePath, sizeEc ? 0ULL : (finalSize / (1024 * 1024)));

    if (!writeJsonFile(manifestPath, manifest))
        Solstice::console->critical("[dump] failed to finalise {}", manifestPath);

    writeOffsetSnapshot(dir + "Minecraft.Windows_" + tag + ".offsets.json", tag);

    Solstice::console->info("[dump] done. manifest={}", manifestPath);
    Solstice::console->info("[dump] send the .image file back for offline signature analysis.");
    return true;
}
