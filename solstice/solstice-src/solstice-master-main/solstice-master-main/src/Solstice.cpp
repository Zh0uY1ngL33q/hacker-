//
// Created by vastrakai on 6/24/2024.
//

#include "Solstice.hpp"


#include <fstream>
#include <filesystem>
#include <Features/FeatureManager.hpp>
#include <Features/Configs/ConfigManager.hpp>
#include <Hook/HookManager.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>

#include <SDK/OffsetProvider.hpp>
#include <SDK/CodeSection.hpp>
#include <SDK/SigFuzz.hpp>

#include <SDK/SigManager.hpp>
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
    console->flush_on(spdlog::level::info);
    spdlog::set_default_logger(console);
    console->info("Build: {} branch {}", SOLSTICE_BUILD_VERSION, SOLSTICE_BUILD_BRANCH);
    console->info("Diagnostic log path: {}", logFile);

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

#ifdef __DEBUG__
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
    CodeSection::waitUntilDecrypted(30000, 100);

    // Snapshot the decrypted image *before* the signature scan runs. On an
    // unsupported game version the scan aborts the process (release) or blocks
    // on stdin (debug), so a dump placed after it would never be produced --
    // which is exactly the situation where it is most needed.
    bool dumpedThisRun = false;
    if (ModuleDump::isEnabled())
        dumpedThisRun = ModuleDump::dump();

    OffsetProvider::initialize();
    SigManager::initialize();
    int64_t send = NOW;

    int failedSigs = 0;

    // Go through all signatures and print any that failed
    for (const auto& [sig, result] : SigManager::mSigs)
    {
        if (result == 0)
        {
            console->critical("[signatures] Failed to find signature: {}", sig);
            failedSigs++;
        }
    }

    for (const auto& [sig, result] : OffsetProvider::mSigs)
    {
        if (result == 0)
        {
            console->critical("[offsets] Failed to find offset: {}", sig);
            failedSigs++;
        }
    }

    if (failedSigs > 0)
    {
        console->critical("Failed to find {} signatures/offsets!", failedSigs);

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
        SigFuzz::writeReport();

#ifdef __DEBUG__
        console->critical("Solstice should not be used in this state.");
        console->info("Type 'DEBUG' to continue, or press ENTER to exit.");
        std::string input;
        std::getline(std::cin, input);
        if (input != "DEBUG")
        {
            SigManager::deinitialize();
            OffsetProvider::deinitialize();
            Logger::deinitialize();

            setTitle("");
            FreeLibraryAndExitThread(hModule, 0);
        }
#else
        ExceptionHandler::makeCrashLog("Failed to find signatures/offsets!", 0xFF01);
        int* p = nullptr;
        *p = 0;
        exit(0);
#endif
    }

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

    // press enter to continue if failed sigs
    if (failedSigs > 0)
    {
        console->info("Press ENTER to continue...");
        std::string input;
        std::getline(std::cin, input);
    }


    gFeatureManager = std::make_shared<FeatureManager>();
    gFeatureManager->init();

    console->info("initializing hooks...");
    HookManager::init(false);

    console->info("initialized in {}ms", NOW - start);



    ClientInstance* readyClient = ClientInstance::get();
    if (readyClient && readyClient->getMinecraftGame())
        readyClient->getMinecraftGame()->playUi("beacon.activate", 1, 1.0f);
    ChatUtils::displayClientMessage("Initialized!");

    console->info("Press END to eject dll.");
    mLastTick = NOW;

    mThread = std::thread(&Solstice::shutdownThread);
    mThread.detach();
}

void Solstice::shutdownThread()
{
    // Wait for the user to press END
    bool firstCall = true;
    bool isLpValid = false;
    while (!mRequestEject)
    {
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

    mRequestEject = true;

    setTitle("");

    patchInHandSlot(false);

    HookManager::shutdown();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    gFeatureManager->shutdown();

    // Shutdown
    console->warn("Shutting down...");

    if (auto* ci = ClientInstance::get(); ci) {
        if (auto* game = ci->getMinecraftGame(); InstanceLocator::isPlausibleObject(game))
            game->playUi("beacon.deactivate", 1, 1.0f);
        // Chat feeds through the queued path which already tolerates missing GuiData.
    }
    ChatUtils::displayClientMessageRaw("§asolstice§7 » §cEjected!");

    mInitialized = false;
    SigManager::deinitialize();
    OffsetProvider::deinitialize();

    // wait for threads to finish
    Sleep(1000); // Give the user time to read the message

    Logger::deinitialize();
    FreeLibrary(mModule); // i don't understand this
}
