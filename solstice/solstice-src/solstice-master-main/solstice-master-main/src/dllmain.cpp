#pragma once

#include <iostream>
#include <Solstice.hpp>
#include <thread>
#include <Windows.h>
#include <Utils/Logger.hpp>
#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>

static DWORD WINAPI initThread(LPVOID lpParam) {
    auto* hModule = static_cast<HMODULE>(lpParam);
    Sleep(100); // wait for the DLL to fully load before init (avoid loader-lock issues)
    Solstice::init(hModule);
    return 0;
}

bool __stdcall DllMain(const HMODULE hModule, const DWORD fdwReason, [[maybe_unused]] LPVOID lpReserved) {
    if(fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Original code used UWP CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(...)
        // which is unavailable in win32 desktop builds (NetEase Haldra): MainView() returns null
        // and throws winrt::hresult_illegal_method_call (0x8000000E), crashing the game.
        // Use standard win32 injection instead: run init on a new thread.
        HANDLE hThread = CreateThread(nullptr, 0, initThread, hModule, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return true;  // Successful DLL_PROCESS_ATTACH
}
