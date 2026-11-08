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
#include <cstdint>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

#include "StackWalker.hpp"

LONG WINAPI TopLevelExceptionHandler(const PEXCEPTION_POINTERS pExceptionInfo)
{
    // Full Integration / Safe SDK Compatibility (from 3.6.2-D + 3.6.2-E.1 P1-2):
    // this is a top-level filter. Windows normally hands us a fully populated
    // PEXCEPTION_POINTERS, but a null or partially populated one must not turn
    // the crash reporter into a second crash. Guard every dereference below.
    // On the normal path behaviour is unchanged apart from the added fault
    // context block, which only enriches crash.log / spdlog output.
    DWORD exceptionCode = 0;
    const EXCEPTION_RECORD* rec = (pExceptionInfo && pExceptionInfo->ExceptionRecord)
        ? pExceptionInfo->ExceptionRecord : nullptr;
    if (rec)
        exceptionCode = rec->ExceptionCode;

    // 3.6.2-D: capture the RAW fault context. A bare 0xC0000005 says nothing
    // about whether the fault was a read, a write or an execute, nor which
    // address/register set was involved. The handler's own frames (StackWalker,
    // spdlog) are NOT the fault site and must not be mistaken for it, so this
    // is read straight from ExceptionRecord / ContextRecord.
    std::string faultContext;
    if (rec)
    {
        const char* accessType = "unknown";
        if (rec->NumberParameters >= 1)
        {
            switch (rec->ExceptionInformation[0])
            {
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
        if (const auto* ctx = pExceptionInfo->ContextRecord)
        {
            faultContext += fmt::format(
                "RIP=0x{:X} RSP=0x{:X} RBP=0x{:X}\n"
                "RCX=0x{:X} RDX=0x{:X} R8=0x{:X} R9=0x{:X} RAX=0x{:X}\n",
                ctx->Rip, ctx->Rsp, ctx->Rbp,
                ctx->Rcx, ctx->Rdx, ctx->R8, ctx->R9, ctx->Rax);
        }
    }

    std::string text = "An unhandled exception occurred. (0x" + fmt::format("{:X}", exceptionCode) + ")\n"
        + faultContext + "Stack Trace:\n";

    // StackWalker::ShowCallstack() dereferences pContext->Rip / Rbp / Rsp
    // unconditionally (StackWalker.cpp:77-82). The upstream 3.6.2-E.1 guard
    // passed nullptr here and claimed StackWalker would walk the current
    // thread instead -- it would in fact fault AGAIN inside the crash reporter.
    // Reviewed fix: skip the walk when there is no ContextRecord and say so,
    // keeping the raw fault context above as the diagnostic record.
    const PCONTEXT faultCtx = (pExceptionInfo && pExceptionInfo->ContextRecord)
        ? pExceptionInfo->ContextRecord : nullptr;
    if (faultCtx)
    {
        // Scoped on purpose: b32d185 called sw.~StackWalker() explicitly and
        // then let the automatic destructor run a second time. That destructor
        // calls UnloadModuleSymbols(), so the double destruction is removed
        // here without changing normal-path output.
        StackWalker sw;
        const std::vector<std::string> stackTrace = sw.ShowCallstack(GetCurrentThread(), faultCtx);
        for (const auto& line : stackTrace)
        {
            text += line + "\n";
        }
    }
    else
    {
        text += "(unavailable: no ContextRecord in exception pointers)\n";
    }
    spdlog::error(text);

    ExceptionHandler::makeCrashLog(text, exceptionCode);

    const auto result = MessageBoxA(nullptr, LPCSTR(text.c_str()), "Unhandled Exception", MB_ABORTRETRYIGNORE | MB_ICONERROR);

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
