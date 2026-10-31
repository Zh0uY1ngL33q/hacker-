#pragma once
#include <Hook/Hooks/ActorHooks/BaseTickHook.hpp>
//
// Created by vastrakai on 6/24/2024.
//


class ProcUtils {
public:
    static int getModuleCount();
    static HWND getMinecraftWindow();
    // Adopt a HWND that a live render path (swap chain / swap-buffer DC) proved
    // to be the game window. Preferred over title matching, which is not stable
    // across Store / NetEase / localised clients.
    static void setMinecraftWindow(HWND hwnd);
    static std::vector<std::wstring> getModulePaths();
    static std::string getVersion();
};