//
// Created by vastrakai on 6/24/2024.
//

#include "Solstice.hpp"


#include <fstream>
#include <filesystem>
#include <IsolationPolicy.hpp>
#include <Features/FeatureManager.hpp>
#include <Features/Configs/ConfigManager.hpp>
#include <Hook/HookManager.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>

#include <SDK/OffsetProvider.hpp>
#include <SDK/CodeSection.hpp>
#include <SDK/SigFuzz.hpp>

#include <SDK/SigManager.hpp>
#include <SDK/ResolveState.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/MinecraftGame.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

#include <Features/Auth/Authorization.hpp>

#include "spdlog/sinks/stdout_color_sinks-inl.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <build_info.h>

// Older build_info.h (a build directory that was configured before the
// fingerprint existed) must still compile.
#ifndef SOLSTICE_SOURCE_FINGERPRINT_SHORT
#define SOLSTICE_SOURCE_FINGERPRINT_SHORT "unknown"
#endif
#ifndef SOLSTICE_BUILD_TIMESTAMP
#define SOLSTICE_BUILD_TIMESTAMP "unknown"
#endif
#include <Features/IRC/IrcClient.hpp>
#include <Features/Modules/Misc/IRC.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <Utils/OAuthUtils.hpp>
#include <Utils/SysUtils/xorstr.hpp>
#include <Utils/SysUtils/ModuleDump.hpp>

#include <wininet.h>
#pragma comment(lib, "wininet.lib")

#ifdef __DEBUG__
std::string title = "[" + std::string(SOLSTICE_BUILD_VERSION_SHORT) + "-" + std::string(SOLSTICE_BUILD_BRANCH) + "] [debug]";
#elif __PRIVATE_BUILD__
std::string title = "[" + std::string(SOLSTICE_BUILD_VERSION_SHORT) + "-" + std::string(SOLSTICE_BUILD_BRANCH) + "] [private]";
#else
std::string title = "[" + std::string(SOLSTICE_BUILD_VERSION_SHORT) + "-" + std::string(SOLSTICE_BUILD_BRANCH) + "]";
#endif


void setTitle(std::string title)
{
    HWND hwnd = ProcUtils::getMinecraftWindow();
    if (hwnd)
        SetWindowTextA(hwnd, title.c_str());
}

std::vector<unsigned char> gBpBytes = {0x1c}; // Defines the new offset for mInHandSlot
DEFINE_PATCH_FUNC(patchInHandSlot, SigManager::ItemInHandRenderer_renderItem_bytepatch2+2, gBpBytes);

// called using winrt::Windows::ApplicationModel::Core::CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [&]()
void Solstice::init(HMODULE hModule)
{
    // Wait for the host to finish loading its modules. The previous threshold
    // was the literal 130, which is one specific build's module count: on a
    // build that loads fewer modules this spins forever, which would also stop
    // the optional dump below from ever running. Wait for the count to stop
    // changing instead, with a hard timeout so it can never block startup.
    int moduleCount = 0;
    {
        constexpr int kMaxWaitMs    = 30000;
        constexpr int kStableChecks = 20; // ~200ms without a change
        int last   = -1;
        int stable = 0;
        int waited = 0;
        while (waited < kMaxWaitMs) {
            const int count = ProcUtils::getModuleCount();
            if (count == last) {
                if (++stable >= kStableChecks) break;
            } else {
                last   = count;
                stable = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }
        moduleCount = last;
    }

    int64_t start = NOW;

    mModule = hModule;
    mInitialized = true;

#ifdef __DEBUG__
    Logger::initialize();
#endif

    console = spdlog::stdout_color_mt(CC(21, 207, 148) + "solstice" + ANSI_COLOR_RESET, spdlog::color_mode::automatic);

    // Persist both console-> and spdlog:: messages for remote diagnostics.
    std::string logFile = FileUtils::getSolsticeDir() + xorstr_("solstice.log");
    console->set_level(spdlog::level::trace);
    console->set_pattern("[" + CC(255, 135, 0) + "%H:%M:%S.%e" + ANSI_COLOR_RESET + "] [%n] [%^%l%$] %v");
    try {
        std::filesystem::create_directories(FileUtils::getSolsticeDir());
        // A fresh file per injection prevents old sessions being mistaken for
        // the current build. Upload it before the next injection.
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        console->sinks().push_back(fileSink);
    } catch (const std::exception& ex) {
        console->error("Cannot create diagnostic log {}: {}", logFile, ex.what());
    }
    // Do NOT flush on every info: that forces a synchronous disk write on the
    // calling (game main) thread for EVERY log message. Under any hot-path
    // logging (e.g. Aura's per-attack packet) this stalls the render loop ->
    // FPS drop -> server timeout -> disconnect. Flush periodically on a
    // background thread instead, and only force an immediate flush for real
    // errors so crash diagnostics are still persisted without per-message stalls.
    spdlog::flush_every(std::chrono::seconds(3));
    spdlog::flush_on(spdlog::level::err);
    spdlog::set_default_logger(console);
    // The commit hash alone is not an identity: it only reports what HEAD was on
    // the build machine, and it stays stale (or wrong) when the tree was dirty
    // or the checkout lagged. The source fingerprint is computed from the files
    // that were actually compiled, so it pins the running binary to its code.
    // "built" is when build_info.h was written, "compiled" is when this
    // translation unit was compiled. They are the same generation only when the
    // header was actually regenerated before this file was built, so a large gap
    // means the binary was compiled against a stale commit hash -- which is
    // exactly the failure that kept showing a version the running code was not.
    console->info("Build: {} branch {} | source {} | header {} | compiled {} | uncommitted {}",
                  SOLSTICE_BUILD_VERSION, SOLSTICE_BUILD_BRANCH,
                  SOLSTICE_SOURCE_FINGERPRINT_SHORT, SOLSTICE_BUILD_TIMESTAMP,
                  __DATE__ " " __TIME__, SOLSTICE_FILES_CHANGED_COUNT);
    console->info("Diagnostic log path: {}", logFile);
    char hostPath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, hostPath, MAX_PATH);
    console->info("[GUI probe] host process={} path={}", GetCurrentProcessId(), hostPath);

    console->info("Welcome to " + CC(0, 255, 0) + "Solstice" + ANSI_COLOR_RESET + "!"
#ifdef __DEBUG__
        + CC(255, 0, 0) + " [Debug] " + ANSI_COLOR_RESET
#endif
);

    spdlog::info("Minecraft version: {}", ProcUtils::getVersion());
    console->info("host loaded {} modules", moduleCount);

    ExceptionHandler::init();

    FileUtils::validateDirectories();

    // Change the window title


    setTitle(title);


    sHWID = GET_HWID().toString();
    spdlog::info("HWID: {}", sHWID);

    if (MH_Initialize() != MH_OK)
    {
        console->critical("Failed to initialize MinHook!");
    }

    Prefs = PreferenceManager::load();

    HWND hwnd = ProcUtils::getMinecraftWindow(); // Cache the window handle

// A persisted EnforceDebugging=true in preferences.json would spin in a
// MessageBoxA loop before any hook exists -- exactly the external residue this
// isolation build is meant to exclude from the A/B comparison.
#if defined(__DEBUG__) && !SOLSTICE_ISOLATION_SWAP_ONLY
    if (Prefs->mEnforceDebugging)
    {
        while (!IsDebuggerPresent())
        {
            MessageBoxA(nullptr, "Please attach a debugger to continue.\nThis message is being shown because of your preference settings.", "Solstice", MB_OK | MB_ICONERROR);
            spdlog::info("Waiting for debugger...");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
#endif

#ifdef __PRIVATE_BUILD__
#ifndef __PRIVATE_DEBUG__
#pragma message("Note: This is a private build. Please do not share this build with anyone.")
    Auth auth;
    auth.init();
    if(!auth.isPrivateUser())
    {
        auth.exit();
    }
#endif
#endif




    console->info("initializing signatures...");
    int64_t sstart = NOW;

    // NetEase decrypts its code lazily after startup, so scanning too early
    // resolves most signatures to 0. Readiness is judged from the bytes
    // themselves (entropy + x64 prologue density) rather than from a signature:
    // a probe lifted from one game version can never match another, which made
    // this loop wait out its full 30s timeout on every update.
    const int decryptWait = CodeSection::waitUntilDecrypted(30000, 100);
    if (decryptWait < 0)
    {
        // Encrypted bytes would produce a complete-looking table of garbage.
        // There is no mapping to hand downstream, so stop BEFORE either
        // initialize() runs and before any hook is installed.
        console->critical("[init] target image was not decrypted within 30s; aborting before any signature scan");
        ExceptionHandler::makeCrashLog("Target image was not decrypted before the signature scan", 0xFF03);
        SigFuzz::stopAsync();
        // The filter installed by ExceptionHandler::init() lives in this DLL;
        // restore the previous one before unloading or the process keeps a
        // callback into freed memory.
        ExceptionHandler::shutdown();
        spdlog::shutdown();
        Logger::deinitialize();
        FreeLibraryAndExitThread(hModule, 0);
    }

    // Snapshot the decrypted image *before* the signature scan runs. On an
    // unsupported game version the scan aborts the process (release) or blocks
    // on stdin (debug), so a dump placed after it would never be produced --
    // which is exactly the situation where it is most needed.
    bool dumpedThisRun = false;
    if (ModuleDump::isEnabled())
        dumpedThisRun = ModuleDump::dump();

    // Both initializers now return a verdict. It is the upstream's job to act on
    // it: previously they always set mIsInitialized = true and the caller had
    // no way to know the SDK was unusable.
    const bool offsetsOk = OffsetProvider::initialize();
    const bool sigsOk    = SigManager::initialize();
    const bool mappingOk = offsetsOk && sigsOk;
    int64_t send = NOW;

    // Count from the authoritative status tables, never from "mSigs != 0".
    // Both initializers already logged each rejected entry with its reason, so
    // this is only the aggregate that decides whether startup may continue.
    const auto& sigTable = SigManager::statusTable();
    const auto& offTable = OffsetProvider::statusTable();
    const int badSigs    = static_cast<int>(sigTable.size() - sigTable.count(ResolveStatus::Ok));
    const int badOffsets = static_cast<int>(offTable.size() - offTable.count(ResolveStatus::Ok));
    const int failedSigs = badSigs + badOffsets;

    if (!mappingOk || failedSigs > 0)
    {
        console->critical("Failed to find {} signatures/offsets! (signatures={} offsets={}; ok={}/{})",
                          failedSigs, badSigs, badOffsets,
                          sigTable.count(ResolveStatus::Ok) + offTable.count(ResolveStatus::Ok),
                          sigTable.size() + offTable.size());
        // The verdict above is about SCANNED mappings only. Hardcoded constants
        // are unverified by definition and must not be folded into an optimistic
        // "everything resolved" conclusion.
        console->critical("[init] scanned verdict only; {} hardcoded DEFINE_FIELD constants are UNVERIFIED "
                          "(CLASS_FIELD and literal offsets elsewhere are not even in that list)",
                          OffsetProvider::hardcodedFields().size());

        // Both artefacts are produced here:
        //
        //   * sigreport.json: what failed, and for each pattern that survived with
        //     a few bytes changed, the new bytes. Small, and the thing that makes
        //     the next iteration possible.
        //   * the decrypted image: 200 MB of file I/O, so it only happens when
        //     explicitly requested via SOLSTICE_DUMP=1 -- the stable gallery of
        //     unresolved entries is small and the runtime tables keep reproducing
        //     the same hits, so a fresh image is only needed when dissecting a
        //     specific new MISSING/SUSPECT entry offline.
        // The report itself (failed + ambiguous signatures) is cheap, so it is
        // written synchronously. The near-miss search behind it is a serial
        // full-section scan and stays opt-in (SOLSTICE_FUZZ=1); it is started
        // further down, AFTER the debug prompt below - otherwise its output
        // would write over the "press ENTER" line while it waits for input.
        SigFuzz::writeReport();

// Solstice Full Integration: restore the b32d185 ORIGINAL manual gate.
//
// The mapping-trust state machine upstream replaced this with
// `#ifdef SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` (a CMake option defaulting to
// OFF). That would make a default build refuse to start WITHOUT ever offering
// the operator a choice, which contradicts the reconstruction requirement:
// in a NOT TRUSTWORTHY state the original manual interaction MUST still be
// present, verbatim, requiring a human to type DEBUG (ENTER exits).
//
// __DEBUG__ here only controls whether the CHOICE is offered; it is NOT an
// automatic bypass. The scan results are unchanged, the authoritative
// ResolveStatus tables still classify every entry, and a human override is
// explicitly recorded as an override (below) rather than as a resolution --
// so an operator's decision can never turn an unusable mapping into an OK one.
#ifdef __DEBUG__

// In the isolation build the manual gate is an external human input, not part
// of the render path. Skipping it keeps the A/B comparison free of a console
// wait (and of a possible hang when no console is attached).
#if !SOLSTICE_ISOLATION_SWAP_ONLY
        console->critical("Solstice should not be used in this state.");
        console->info("Type 'DEBUG' to continue, or press ENTER to exit.");
        std::string input;
        std::getline(std::cin, input);
        if (input != "DEBUG")
        {
            SigManager::deinitialize();
            OffsetProvider::deinitialize();
            SigFuzz::stopAsync();
            ExceptionHandler::shutdown();
            spdlog::shutdown();
            Logger::deinitialize();

            setTitle("");
            FreeLibraryAndExitThread(hModule, 0);
        }
        else
        {
            // The override is a deliberate human decision, not a resolution.
            // Keep it visible in the log so a later crash is not attributed to
            // a "working" mapping table.
            console->critical("[init] OPERATOR OVERRIDE: continuing with {} unusable mappings; "
                              "every SDK result in this session is untrusted", failedSigs);
        }
// isolation build: report the state but never block on stdin
#else
        console->warn("[isolation] {} unusable mappings reported; manual DEBUG gate skipped, "
                      "continuing without console input", failedSigs);
#endif

#else
        console->critical("[init] mapping table is not trustworthy and no override was enabled; "
                          "refusing to start. The classification report above is complete.");
        ExceptionHandler::makeCrashLog("Failed to find signatures/offsets!", 0xFF01);
        // The unhandled-exception filter points into this DLL. Restore the
        // previous one before the module is unloaded so no dangling callback
        // remains in the process.
        SigFuzz::stopAsync();
        ExceptionHandler::shutdown();
        spdlog::shutdown();
        Logger::deinitialize();
        FreeLibraryAndExitThread(hModule, 0);
#endif
    }

    // Now that the debug prompt is out of the way, the near-miss analysis may
    // run in the background (opt-in via SOLSTICE_FUZZ=1).
    SigFuzz::analyzeAsync();

    console->info("initialized signatures in {}ms", send - sstart);

    // ClientInstance is the entry point for everything below. It can still be
    // null when the build has unresolved signatures -- print what we have and keep
    // going, so hooks/GUI can be tested independently of the SDK being complete.
    auto* clientInstance = ClientInstance::get();
    console->info("clientinstance addr @ 0x{:X}", reinterpret_cast<uintptr_t>(clientInstance));
    if (clientInstance)
    {
        auto* mcGame = clientInstance->getMinecraftGame();
        console->info("mcgame from clientinstance addr @ 0x{:X}", reinterpret_cast<uintptr_t>(mcGame));
        console->info("localplayer addr @ 0x{:X}", reinterpret_cast<uintptr_t>(clientInstance->getLocalPlayer()));
    }
    else
    {
        console->error("clientinstance not found -- SDK-dependent features will be skipped");
    }

#if !SOLSTICE_ISOLATION_SWAP_ONLY
    // press enter to continue if failed sigs
    if (failedSigs > 0)
    {
        console->info("Press ENTER to continue...");
        std::string input;
        std::getline(std::cin, input);
    }
#else
    if (failedSigs > 0)
        console->info("[isolation] {} failed mappings recorded; second console wait skipped", failedSigs);
#endif

    gFeatureManager = std::make_shared<FeatureManager>();
    gFeatureManager->init();

    console->info("initializing hooks...");
    HookManager::init(false);

    console->info("initialized in {}ms", NOW - start);



#if !SOLSTICE_ISOLATION_SWAP_ONLY
    ClientInstance* readyClient = ClientInstance::get();
    if (readyClient && readyClient->getMinecraftGame())
        readyClient->getMinecraftGame()->playUi("beacon.activate", 1, 1.0f);
    ChatUtils::displayClientMessage("Initialized!");
#else
    console->info("[isolation] init complete: only the native SwapBuffers detours are active");
#endif

    console->info("Press END to eject dll.");
    mLastTick = NOW;

    mThread = std::thread(&Solstice::shutdownThread);
    mThread.detach();
}

void Solstice::shutdownThread()
{
#if SOLSTICE_ISOLATION_SWAP_ONLY
    // Isolation build: no network update check, no IRC auto-enable, no
    // ConfigManager::loadConfig (persisted config), no signature bytepatch and
    // no module ticks. KeyHook is not installed here, so END is polled in this
    // loop to keep the DLL unloadable.
    console->info("[isolation] shutdown thread active; hold END to eject");
    while (!mRequestEject)
    {
        if (GetAsyncKeyState(VK_END) & 0x8000) mRequestEject = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    // Wait for the user to press END
    bool firstCall = true;
    bool isLpValid = false;
    ULONGLONG lastKeyProbe = 0;
    bool lastTabProbe = false;
    while (!mRequestEject)
    {
        // Diagnostic only: no toggling or event injection. This tells whether
        // Windows sees TAB even when the signature-based keyboard hook is idle.
        const ULONGLONG probeNow = GetTickCount64();
        if (probeNow - lastKeyProbe >= 50) {
            lastKeyProbe = probeNow;
            const bool tabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
            if (tabDown && !lastTabProbe) {
                DWORD foregroundPid = 0;
                const HWND foreground = GetForegroundWindow();
                GetWindowThreadProcessId(foreground, &foregroundPid);
                console->info("[GUI probe] Windows TAB; foreground=0x{:X} pid={} hostPid={}",
                    reinterpret_cast<uintptr_t>(foreground), foregroundPid, GetCurrentProcessId());
            }
            lastTabProbe = tabDown;
        }
        if (firstCall)
        {
            NotifyUtils::notify("Solstice initialized!", 5.0f, Notification::Type::Info);
            firstCall = false;

            std::string latestHash;
            latestHash = OAuthUtils::getLatestCommitHash();

            if(latestHash == xorstr_("403"))
            {

            }
            else if (!latestHash.empty())
            {
                if (latestHash != SOLSTICE_BUILD_VERSION)
                {
                    console->warn("Solstice is out of date! Latest commit: {}", latestHash);
                    NotifyUtils::notify("There is a new version of Solstice available!\nIt is recommended to update.", 10.0f, Notification::Type::Warning);
                    ChatUtils::displayClientMessage("§aThere is a new version of Solstice available! Download it from the Discord server.");
                } else {
                    console->info("Solstice is up to date!");
                }
            }
            else if(latestHash.empty())
            {

            }
        }

        if (!isLpValid)
        {
            auto* lp = ClientInstance::get()
                ? ClientInstance::get()->getLocalPlayer()
                : nullptr;
            if (InstanceLocator::isPlausibleObject(lp))
            {
                isLpValid = true;
                HookManager::init(true); // Initialize the base tick hook

                auto ircModule = gFeatureManager->mModuleManager->getModule<IRC>();
                if (ircModule && !ircModule->mEnabled) ircModule->toggle();

                if (!Prefs->mDefaultConfigName.empty())
                {
                    if (ConfigManager::configExists(Prefs->mDefaultConfigName))
                    {
                        ConfigManager::loadConfig(Prefs->mDefaultConfigName);
                    }
                    else
                    {
                        console->warn("Default config does not exist! Clearing default config...");
                        NotifyUtils::notify("Default config does not exist! Clearing default config...", 10.0f, Notification::Type::Warning);
                        Prefs->mDefaultConfigName = "";
                        PreferenceManager::save(Prefs);
                    }
                } else {
                    console->warn("No default config set!");
                }
            }
        }

        {
            auto* ci = ClientInstance::get();
            patchInHandSlot(ci && ci->getLocalPlayer() != nullptr);
        }
        mLastTick = NOW;
        gFeatureManager->mModuleManager->onClientTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#endif

    mRequestEject = true;

    setTitle("");

#if !SOLSTICE_ISOLATION_SWAP_ONLY
    patchInHandSlot(false);
#endif

    HookManager::shutdown();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    gFeatureManager->shutdown();

    // Shutdown
    console->warn("Shutting down...");

#if !SOLSTICE_ISOLATION_SWAP_ONLY
    if (auto* ci = ClientInstance::get(); ci) {
        if (auto* game = ci->getMinecraftGame(); InstanceLocator::isPlausibleGame(game))
            game->playUi("beacon.deactivate", 1, 1.0f);
        // Chat feeds through the queued path which already tolerates missing GuiData.
    }
#endif
    ChatUtils::displayClientMessageRaw("§asolstice§7 » §cEjected!");

    mInitialized = false;
    SigManager::deinitialize();
    OffsetProvider::deinitialize();

    // wait for threads to finish
    Sleep(1000); // Give the user time to read the message

    // The near-miss analysis thread logs through spdlog, so it has to be gone
    // before the logger is torn down. Logger::deinitialize() closes stdout and
    // frees the console, and any log line written after that lands in a closed
    // FILE* -> access violation.
    SigFuzz::stopAsync();

    // Release the spdlog sinks while the console is still alive. After this,
    // any further log call is a no-op instead of a write into a dead stdout.
    spdlog::shutdown();

    Logger::deinitialize();

    // NOT freeing the library on purpose: the game may still be executing
    // inside one of our hooks (PacketReceiveHook::onPacketSend kept firing
    // after the unload), and unmapping the DLL under it is an instant
    // access violation. The hooks are already removed and the features are
    // shut down, so leaving the module mapped is harmless.
}
