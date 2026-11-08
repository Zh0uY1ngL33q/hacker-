//
// Created by vastrakai on 7/7/2024.
//

#include "ExceptionHandler.hpp"

#include <build_info.h>

// Older build_info.h must still compile.
#ifndef SOLSTICE_SOURCE_FINGERPRINT_SHORT
#define SOLSTICE_SOURCE_FINGERPRINT_SHORT "unknown"
#endif
#ifndef SOLSTICE_BUILD_TIMESTAMP
#define SOLSTICE_BUILD_TIMESTAMP "unknown"
#endif
#include <Windows.h>
#include <string>
#include <spdlog/spdlog.h>

#include "StackWalker.hpp"

LONG WINAPI TopLevelExceptionHandler(const PEXCEPTION_POINTERS pExceptionInfo)
{
    // Get the exception code
    auto exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;

    // Capture the raw fault context. This turns a bare "0xC0000005" into a
    // read/write/execute classification plus the faulting address and the
    // register set at the moment of the fault -- without a debugger. The
    // handler's own stack frames (StackWalker, memcpy, spdlog) are NOT the
    // fault site and must not be mistaken for it.
    std::string faultContext;
    if (const auto* rec = pExceptionInfo->ExceptionRecord) {
        const char* accessType = "unknown";
        if (rec->NumberParameters >= 1) {
            switch (rec->ExceptionInformation[0]) {
                case 0: accessType = "read"; break;
                case 1: accessType = "write"; break;
                case 8: accessType = "execute"; break;
                default: accessType = "unknown"; break;
            }
        }
        const uintptr_t faultAddress = (rec->NumberParameters >= 2)
            ? static_cast<uintptr_t>(rec->ExceptionInformation[1]) : 0;
        faultContext = fmt::format(
            "ExceptionAddress: 0x{:X}\n"
            "AccessType: {}\n"
            "FaultAddress: 0x{:X}\n"
            "NumberParameters: {}\n",
            reinterpret_cast<uintptr_t>(rec->ExceptionAddress),
            accessType, faultAddress, rec->NumberParameters);
        if (const auto* ctx = pExceptionInfo->ContextRecord) {
            faultContext += fmt::format(
                "RIP=0x{:X} RSP=0x{:X} RBP=0x{:X}\n"
                "RCX=0x{:X} RDX=0x{:X} R8=0x{:X} R9=0x{:X} RAX=0x{:X}\n",
                ctx->Rip, ctx->Rsp, ctx->Rbp,
                ctx->Rcx, ctx->Rdx, ctx->R8, ctx->R9, ctx->Rax);
        }
    }

    std::string text = "An unhandled exception occurred. (0x" + fmt::format("{:X}", exceptionCode) + ")\n"
        + faultContext + "Stack Trace:\n";

    // Get the stack trace using stackwalker
    StackWalker sw;
    std::vector<std::string> stackTrace = sw.ShowCallstack(GetCurrentThread(), pExceptionInfo->ContextRecord);
    for (const auto& line : stackTrace)
    {
        text += line + "\n";
    }
    spdlog::error(text);

    ExceptionHandler::makeCrashLog(text, exceptionCode);

    auto result = MessageBoxA(nullptr, LPCSTR(text.c_str()), "Unhandled Exception", MB_ABORTRETRYIGNORE | MB_ICONERROR);

    // deconstruct stack trace
    stackTrace.clear();
    sw.~StackWalker();


    switch (result)
    {
    case IDABORT:   // Terminates the process
        exit(1);
    case IDRETRY:
        return EXCEPTION_CONTINUE_EXECUTION;
    case IDIGNORE:
        return EXCEPTION_CONTINUE_SEARCH;
    default:
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

namespace {
    // SetUnhandledExceptionFilter returns the previous filter. Keep it so an
    // early abort can restore it before FreeLibraryAndExitThread unloads the
    // module that contains TopLevelExceptionHandler.
    LPTOP_LEVEL_EXCEPTION_FILTER gPreviousFilter = nullptr;
}

void ExceptionHandler::init()
{
    gPreviousFilter = SetUnhandledExceptionFilter(TopLevelExceptionHandler);
}

void ExceptionHandler::shutdown()
{
    SetUnhandledExceptionFilter(gPreviousFilter);
    gPreviousFilter = nullptr;
}

void ExceptionHandler::makeCrashLog(const std::string& text, DWORD exceptionCode)
{
    std::string excPath = FileUtils::getSolsticeDir() + xorstr_("crash.log");

    std::ofstream excFile(excPath, std::ios::app);

    if (excFile.is_open())
    {
        // Prepend the date and time to the crash log
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        excFile << xorstr_("----------------- Crash at ") << std::put_time(std::localtime(&nowTime), xorstr_("%Y-%m-%d %X")) << xorstr_("\n");
        excFile << text << xorstr_("\n\n");


        auto modules = gFeatureManager ? gFeatureManager->mModuleManager->getModules() : std::vector<std::shared_ptr<Module>>();
        for (const auto& module : modules)
        {
            // Append modules and the mEnabled state
            excFile << module->mName << xorstr_(" - ") << (module->mEnabled ? xorstr_("Enabled") : xorstr_("Disabled")) << xorstr_("\n");
        }
        if (gFeatureManager)
        {
            // Append the module count
            excFile << xorstr_("Module count: ") << modules.size() << xorstr_("\n");
        } else
        {
            excFile << xorstr_("(FeatureManager not initialized)\n");
        }

        // Append the exception code then divider
        excFile << xorstr_("Exception Code: 0x") << fmt::format(xorstr_("{:X}"), exceptionCode) << xorstr_("\n");
        excFile << xorstr_("Solstice commit: ") << SOLSTICE_BUILD_VERSION << xorstr_("\n");
        excFile << xorstr_("Solstice source: ") << SOLSTICE_SOURCE_FINGERPRINT_SHORT << xorstr_("\n");
        excFile << xorstr_("Solstice built: ") << SOLSTICE_BUILD_TIMESTAMP << xorstr_("\n");
        excFile << xorstr_("Solstice branch: ") << SOLSTICE_BUILD_BRANCH << xorstr_("\n");
        excFile << xorstr_("Solstice commit msg: ") << SOLSTICE_BUILD_COMMIT_MESSAGE << xorstr_("\n");
        excFile << xorstr_("Minecraft version: ") << ProcUtils::getVersion() << xorstr_("\n");
        excFile << xorstr_("----------------------------------------\n");
        excFile.flush();
        excFile.close();
    }
}
