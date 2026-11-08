#pragma once
//
// Created by vastrakai on 7/7/2024.
//


class ExceptionHandler {
public:
    static void init();
    /// Restores the unhandled-exception filter that was in place before init().
    /// MUST run before this DLL unloads (e.g. on the early-abort paths), or the
    /// process keeps a callback into freed memory.
    static void shutdown();
    static void makeCrashLog(const std::string& text, DWORD exceptionCode);
};