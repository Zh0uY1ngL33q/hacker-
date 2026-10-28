//
// Created by vastrakai on 6/24/2024.
//

#include "ProcUtils.hpp"

#include <Windows.h>
#include <tlhelp32.h>


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
    static HWND window = nullptr;
    DWORD cachedPid = 0;
    if (window) GetWindowThreadProcessId(window, &cachedPid);
    if (!IsWindow(window) || cachedPid != GetCurrentProcessId()) window = nullptr;
    if (!window)
    {
        std::map<HWND, std::string> titles;
        auto callback = [](HWND hwnd, LPARAM lParam) -> BOOL {

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            // A title match in another process (launcher/browser) is not the
            // game's HWND and cannot be used by the ImGui Win32 backend.
            if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || hwnd == GetConsoleWindow())
                return TRUE;

            char title[256];
            GetWindowTextA(hwnd, title, sizeof(title));
            std::string titleStr = title;
            auto *titles = reinterpret_cast<std::map<HWND, std::string> *>(lParam);
            titles->insert({hwnd, titleStr});
            return TRUE;
        };

        EnumWindows(callback, reinterpret_cast<LPARAM>(&titles));

        for (auto& [hwnd, title] : titles)
        {
            if (title.find("Minecraft") != std::string::npos)
            {
                window = hwnd;
                break;
            }
        }

        spdlog::info("Minecraft window handle: 0x{:X}, process {}", reinterpret_cast<uintptr_t>(window), GetCurrentProcessId());
    }


    return window;
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
