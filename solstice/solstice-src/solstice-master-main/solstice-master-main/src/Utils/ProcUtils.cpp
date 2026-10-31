//
// Created by vastrakai on 6/24/2024.
//

#include "ProcUtils.hpp"

#include <Windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>

namespace {
    // getMinecraftWindow() is called from the presentation path, i.e. once per
    // frame. Everything here is therefore written to be cheap and, above all,
    // to log only when the answer changes -- the previous implementation printed
    // one line per failed call, which buried the whole log under thousands of
    // "Minecraft window handle: 0x0" lines.
    std::mutex g_windowMutex;
    HWND g_adoptedWindow = nullptr;   // proven by a live render target
    HWND g_cachedWindow = nullptr;
    HWND g_loggedWindow = reinterpret_cast<HWND>(static_cast<uintptr_t>(~0ull));
    ULONGLONG g_lastScanTick = 0;

    constexpr ULONGLONG kRescanIntervalMs = 500;

    bool isHostWindow(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd) || hwnd == GetConsoleWindow()) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        return pid == GetCurrentProcessId();
    }

    std::string windowText(HWND hwnd)
    {
        char buf[256]{};
        GetWindowTextA(hwnd, buf, sizeof(buf));
        return std::string(buf);
    }

    std::string windowClass(HWND hwnd)
    {
        char buf[256]{};
        GetClassNameA(hwnd, buf, sizeof(buf));
        return std::string(buf);
    }

    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // A title match is only a hint now: injected builds rename the window to the
    // Solstice title, and localised clients do not contain "Minecraft" at all.
    bool looksLikeGameWindow(HWND hwnd)
    {
        const std::string title = toLower(windowText(hwnd));
        const std::string cls = toLower(windowClass(hwnd));
        return title.find("minecraft") != std::string::npos ||
               title.find("solstice") != std::string::npos ||
               cls.find("corewindow") != std::string::npos ||
               cls.find("inputsite") != std::string::npos ||
               cls.find("bedrock") != std::string::npos;
    }

    struct WindowCandidate {
        HWND hwnd;
        long long area;
        bool visible;
        bool named;
    };

    void collectCandidate(HWND hwnd, std::vector<WindowCandidate>* out)
    {
        if (!isHostWindow(hwnd)) return;
        RECT client{};
        long long area = 0;
        if (GetClientRect(hwnd, &client))
            area = static_cast<long long>(client.right - client.left) * (client.bottom - client.top);
        out->push_back({ hwnd, area, IsWindowVisible(hwnd) != FALSE, looksLikeGameWindow(hwnd) });
    }

    BOOL CALLBACK enumChildProc(HWND hwnd, LPARAM lParam)
    {
        collectCandidate(hwnd, reinterpret_cast<std::vector<WindowCandidate>*>(lParam));
        return TRUE;
    }

    BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM lParam)
    {
        auto* out = reinterpret_cast<std::vector<WindowCandidate>*>(lParam);
        collectCandidate(hwnd, out);
        // UWP / XAML hosts put the real render window in a child of a top-level
        // window that may belong to another process, so always look one level in.
        EnumChildWindows(hwnd, enumChildProc, lParam);
        return TRUE;
    }

    HWND pickGameWindow(const std::vector<WindowCandidate>& candidates)
    {
        HWND best = nullptr;
        long long bestArea = -1;
        int bestRank = -1;
        for (const auto& candidate : candidates) {
            if (candidate.area <= 0) continue; // tool/hidden shells without a client area
            const int rank = (candidate.named ? 2 : 0) + (candidate.visible ? 1 : 0);
            if (rank > bestRank || (rank == bestRank && candidate.area > bestArea)) {
                bestRank = rank;
                bestArea = candidate.area;
                best = candidate.hwnd;
            }
        }
        return best;
    }

    void dumpCandidatesOnce(const std::vector<WindowCandidate>& candidates)
    {
        static bool dumped = false;
        if (dumped) return;
        dumped = true;
        spdlog::warn("[window] no game window resolved; {} host window(s) visible to EnumWindows:", candidates.size());
        size_t shown = 0;
        for (const auto& candidate : candidates) {
            if (shown++ >= 12) break;
            spdlog::warn("[window]   hwnd=0x{:X} class='{}' title='{}' visible={} area={}",
                reinterpret_cast<uintptr_t>(candidate.hwnd), windowClass(candidate.hwnd),
                windowText(candidate.hwnd), candidate.visible, candidate.area);
        }
    }
}

void ProcUtils::setMinecraftWindow(HWND hwnd)
{
    if (!isHostWindow(hwnd)) return;
    // Prefer the top-level owner when it is ours (XAML hosts hand out child
    // CoreWindows); a root owned by another process must be ignored.
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    if (isHostWindow(root)) hwnd = root;
    std::lock_guard<std::mutex> lock(g_windowMutex);
    g_adoptedWindow = hwnd;
}

int ProcUtils::getModuleCount()
{
    HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

    if (hModuleSnap == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    MODULEENTRY32 me32;
    me32.dwSize = sizeof(MODULEENTRY32);

    if (!Module32First(hModuleSnap, &me32))
    {
        CloseHandle(hModuleSnap);
        return 0;
    }

    int count = 1;
    while (Module32Next(hModuleSnap, &me32))
    {
        count++;
    }

    CloseHandle(hModuleSnap);
    return count;
}

HWND ProcUtils::getMinecraftWindow()
{
    std::lock_guard<std::mutex> lock(g_windowMutex);

    // 1. A HWND handed over by a live render target is authoritative: it is the
    //    window the game is actually presenting into.
    if (isHostWindow(g_adoptedWindow))
        g_cachedWindow = g_adoptedWindow;
    else if (g_cachedWindow && !isHostWindow(g_cachedWindow))
        g_cachedWindow = nullptr;

    // 2. Otherwise discover it, but at most twice a second. Called every frame,
    //    an unconditional EnumWindows + per-frame log line is what flooded the
    //    log on builds where the title heuristic missed.
    if (!g_cachedWindow)
    {
        const ULONGLONG now = GetTickCount64();
        if (now - g_lastScanTick >= kRescanIntervalMs)
        {
            g_lastScanTick = now;
            std::vector<WindowCandidate> candidates;
            EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&candidates));
            g_cachedWindow = pickGameWindow(candidates);
            if (!g_cachedWindow) dumpCandidatesOnce(candidates);
        }
    }

    if (g_cachedWindow != g_loggedWindow)
    {
        g_loggedWindow = g_cachedWindow;
        if (g_cachedWindow)
            spdlog::info("Minecraft window handle: 0x{:X}, process {} (class='{}', title='{}')",
                reinterpret_cast<uintptr_t>(g_cachedWindow), GetCurrentProcessId(),
                windowClass(g_cachedWindow), windowText(g_cachedWindow));
        else
            spdlog::warn("Minecraft window handle: unresolved in process {} (retrying quietly)",
                GetCurrentProcessId());
    }

    return g_cachedWindow;
}

std::vector<std::wstring> ProcUtils::getModulePaths()
{
    HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

    if (hModuleSnap == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    MODULEENTRY32 me32;
    me32.dwSize = sizeof(MODULEENTRY32);

    if (!Module32First(hModuleSnap, &me32))
    {
        CloseHandle(hModuleSnap);
        return {};
    }

    std::vector<std::wstring> modulePaths;

    do
    {
        char* path = me32.szExePath;
        // Convert the path to a wide string
        std::wstring wpath(path, path + strlen(path));
        modulePaths.push_back(wpath);
    } while (Module32Next(hModuleSnap, &me32));

    CloseHandle(hModuleSnap);

    return modulePaths;
}

// gets the version from the current process's file version info
std::string ProcUtils::getVersion()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);

    DWORD dwDummy;
    DWORD dwFVISize = GetFileVersionInfoSizeA(path, &dwDummy);
    if (dwFVISize == 0)
    {
        return "Unknown";
    }

    std::vector<char> versionInfo(dwFVISize);
    if (!GetFileVersionInfoA(path, 0, dwFVISize, versionInfo.data()))
    {
        return "Unknown";
    }

    VS_FIXEDFILEINFO* pFileInfo;
    UINT uLen;
    if (!VerQueryValueA(versionInfo.data(), "\\", reinterpret_cast<void**>(&pFileInfo), &uLen))
    {
        return "Unknown";
    }

    return fmt::format("{}.{}.{}.{}", HIWORD(pFileInfo->dwFileVersionMS), LOWORD(pFileInfo->dwFileVersionMS),
                       HIWORD(pFileInfo->dwFileVersionLS), LOWORD(pFileInfo->dwFileVersionLS));
}
