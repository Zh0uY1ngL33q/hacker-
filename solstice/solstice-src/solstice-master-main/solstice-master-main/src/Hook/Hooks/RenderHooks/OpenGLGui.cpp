#include "OpenGLGui.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <gl/GL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>
#include <Solstice.hpp>
#include <Features/FeatureManager.hpp>
#include <Features/Modules/Module.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Events/WindowResizeEvent.hpp>
#include <Features/Modules/Visual/ClickGui.hpp>
#include <Features/Modules/Visual/HudEditor.hpp>
#include <Features/Modules/Visual/TargetHUD.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Rendering/LevelRenderer.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <cmath>
#include <Utils/FontHelper.hpp>
#include <Utils/Keyboard.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/ProcUtils.hpp>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    std::recursive_mutex glGuiMutex;
    std::atomic<bool> glGuiActive{false};
    std::atomic<bool> glGuiCapture{false};
    std::atomic<long long> glGuiLastDrawMs{0};
    bool glGuiReportedOpenDraw = false;
    bool glGuiReportedMismatch = false;
    long long glNowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    // Minecraft yaw: 0 faces +Z and increasing yaw turns clockwise seen from
    // above; pitch is positive when looking down. Both in degrees, matching
    // ActorRotationComponent.
    //
    // This exists because the matrix read at ClientInstance+0x368 does not always
    // carry a view-space Z axis. The ESP probe measured its third column as
    // (0, 0, 0, 1), which makes every projection's z constant, so OWorldToScreen's
    // `z > 0` guard rejected every single point -- ESP, Tracers and Nametags all
    // drew nothing while the camera origin and fov were correct.
    glmatrixf viewMatrixFromRotation(float yawDeg, float pitchDeg)
    {
        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;
        const float yaw = yawDeg * kDeg2Rad;
        const float pitch = pitchDeg * kDeg2Rad;
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float cy = std::cos(yaw), sy = std::sin(yaw);

        // Minecraft's basis is right-handed with the camera looking down its own
        // -Z: with yaw = 0 the forward axis is +Z and the screen-right axis is
        // -X (stand facing +Z with +Y up and your right hand points at -X). The
        // camera's own X axis is therefore cross(worldUp, -forward), which works
        // out to (-cos yaw, 0, -sin yaw). Using +cos/+sin here inverted it, and
        // because up is derived from right the vertical axis flipped too: every
        // box landed mirrored through the centre of the screen.
        const glm::vec3 forward{ -sy * cp, -sp, cy * cp };
        const glm::vec3 right{ -cy, 0.f, -sy };
        const glm::vec3 up = glm::cross(right, forward);

        // OWorldToScreen treats (*this)[n] as a row and dots it with the world
        // offset. A glm::mat4 is column-major, so the columns assigned below are
        // the rows it will read.
        glmatrixf m{ glm::mat4(1.0f) };
        m[0] = glm::vec4(right, 0.f);
        m[1] = glm::vec4(up, 0.f);
        m[2] = glm::vec4(-forward, 0.f);
        m[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
        return m;
    }

    struct ViewCandidate {
        uint32_t offset = 0;
        glm::mat4 matrix{ 1.f };
        float forwardDot = 0.f;
    };

    // The camera basis identifies a view matrix without knowing any convention:
    // three orthonormal columns and a (0,0,0,1) fourth column. Every such block
    // inside an object is returned, sorted by how well its -Z axis matches the
    // direction the player is looking, which is what makes the right one
    // unambiguous instead of a guess. Translation is dropped: OWorldToScreen
    // subtracts the origin itself, so the matrix it consumes is rotation only.
    std::vector<ViewCandidate> scanForViewMatrix(const void* object, size_t bytes, const glm::vec3& forward)
    {
        std::vector<ViewCandidate> hits;
        if (!object) return hits;
        auto* base = reinterpret_cast<const uint8_t*>(object);
        const auto unit = [](const glm::vec3& v) { return std::fabs(glm::length(v) - 1.f) < 2e-2f; };
        for (size_t page = 0; page < bytes; page += 0x1000) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(base + page, &mbi, sizeof(mbi))) break;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) continue;
            const size_t span = std::min<size_t>(0x1000, bytes - page);
            for (size_t off = 0; off + 64 <= span; off += 4) {
                const float* f = reinterpret_cast<const float*>(base + page + off);
                // Only the rotation is used: OWorldToScreen subtracts the origin
                // itself, so any translation in the stored block is dropped below.
                if (std::fabs(f[15] - 1.f) > 1e-6f) continue;
                const glm::vec3 c0(f[0], f[1], f[2]);
                const glm::vec3 c1(f[4], f[5], f[6]);
                const glm::vec3 c2(f[8], f[9], f[10]);
                if (!unit(c0) || !unit(c1) || !unit(c2)) continue;
                if (std::fabs(glm::dot(c0, c1)) > 2e-2f) continue;
                if (std::fabs(glm::dot(c0, c2)) > 2e-2f) continue;
                if (std::fabs(glm::dot(c1, c2)) > 2e-2f) continue;
                // Right-handed only: cross(right, up) has to land on the back
                // axis, otherwise the block is a reflection and every projection
                // through it comes out mirrored.
                if (glm::dot(glm::cross(c0, c1), c2) < 0.9f) continue;
                ViewCandidate hit;
                hit.offset = static_cast<uint32_t>(page + off);
                hit.matrix[0] = glm::vec4(c0, 0.f);
                hit.matrix[1] = glm::vec4(c1, 0.f);
                hit.matrix[2] = glm::vec4(c2, 0.f);
                hit.matrix[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
                hit.forwardDot = glm::dot(-c2, forward);
                hits.push_back(hit);
            }
        }
        std::sort(hits.begin(), hits.end(), [](const ViewCandidate& a, const ViewCandidate& b) {
            return a.forwardDot > b.forwardDot;
        });
        return hits;
    }
    std::atomic<bool> glGuiStop{false};
    std::atomic<unsigned> glGuiWindowCalls{0};
    // Batch 3.6.2-C GL-hang localization: a monotonically increasing frame id
    // and one-shot structured stage markers. The markers are throttled so they
    // never spam every frame, but a hang is always bracketed by the last stage
    // that started, which narrows the freeze to HUD_DISPATCH / IMGUI_RENDER /
    // GL_SUBMIT without needing a full process dump first.
    unsigned glGuiFrameCounter = 0;
    void glHangMarker(int stage, const char* phase, uintptr_t ctx, DWORD tid)
    {
        // First ~120 frames: full per-boundary trace. After that: one marker
        // every 30 frames so a late hang is still bracketed but we do not log
        // on every present.
        if (glGuiFrameCounter < 120 || (glGuiFrameCounter % 30 == 0)) {
            spdlog::info("[GL-HANG] stage={} phase={} tid={:#x} frame={} ctx={:#x}",
                stage, phase, static_cast<unsigned long>(tid), glGuiFrameCounter, ctx);
        }
    }
    // 3.6.2-F subscriber-level fault localization diagnostics.
    // A dedicated, low-overhead diagnostic file (flush-on-write) records one
    // BEGIN/END line per RenderEvent subscriber on the GL presentation thread for
    // the first kRenderDiagMaxFrames frames. Every record is flushed immediately,
    // so a hang or AV inside a subscriber leaves the last BEGIN without a matching
    // END on disk. The diagnostic path never goes through spdlog: render() already
    // holds glGuiMutex in this scope, so re-entering the spdlog sink (which takes
    // its own lock) from inside a dispatcher callback is deliberately avoided.
    // Module names come from an address table built once at open time. listen()
    // stores the most-derived object pointer as void* in listener.mInstance, and
    // dynamic_cast<void*>(Module*) reproduces exactly that address, so an unknown
    // or non-module listener pointer is never dereferenced as a Module*.
#ifdef SOLSTICE_RENDER_DIAG
    constexpr unsigned kRenderDiagMaxFrames = 8;
    HANDLE gRenderDiagFile = INVALID_HANDLE_VALUE;
    unsigned gRenderDiagFramesRemaining = kRenderDiagMaxFrames;
    bool gRenderDiagOpened = false;
    std::unordered_map<void*, std::string> gRenderDiagNames;

    void renderDiagWrite(const char* data, DWORD len) {
        if (gRenderDiagFile == INVALID_HANDLE_VALUE || len == 0) return;
        DWORD written = 0;
        WriteFile(gRenderDiagFile, data, len, &written, nullptr);
        FlushFileBuffers(gRenderDiagFile);
    }

    void renderDiagOpen() {
        if (gRenderDiagOpened) return;
        gRenderDiagOpened = true;
        char path[MAX_PATH] = {0};
        DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
        std::string full;
        if (n > 0 && n < MAX_PATH) full = std::string(path) + "\\solstice_render_diag.log";
        else full = "solstice_render_diag.log";
        gRenderDiagFile = CreateFileA(full.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        // Build the address -> name table once, outside the per-callback path.
        if (gFeatureManager && gFeatureManager->mModuleManager) {
            for (const auto& mod : gFeatureManager->mModuleManager->getModules()) {
                if (!mod) continue;
                void* mostDerived = dynamic_cast<void*>(mod.get());
                gRenderDiagNames.emplace(mostDerived, mod->mName);
                gRenderDiagNames.emplace(static_cast<void*>(mod.get()), mod->mName);
            }
        }
        char hdr[512];
        int len = snprintf(hdr, sizeof(hdr),
            "[RENDER-DIAG] opened maxFrames=%u modules=%zu file=%s handle=%s\n",
            kRenderDiagMaxFrames, gRenderDiagNames.size(), full.c_str(),
            gRenderDiagFile == INVALID_HANDLE_VALUE ? "INVALID" : "OK");
        if (len > 0) {
            const int capped = len < (int)sizeof(hdr) - 1 ? len : (int)sizeof(hdr) - 1;
            renderDiagWrite(hdr, static_cast<DWORD>(capped));
        }
    }

    const char* renderDiagName(void* inst) {
        auto it = gRenderDiagNames.find(inst);
        return it != gRenderDiagNames.end() ? it->second.c_str() : "(non-module)";
    }

    void renderDiagRecord(unsigned frame, unsigned index, const char* module, int phase) {
        char buf[256];
        int len = snprintf(buf, sizeof(buf),
            "[RENDER-DIAG] frame=%u index=%u module=%s phase=%s tid=%#x\n",
            frame, index, module, phase == 0 ? "BEGIN" : "END",
            static_cast<unsigned>(GetCurrentThreadId()));
        if (len < 0) return;
        const int capped = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
        renderDiagWrite(buf, static_cast<DWORD>(capped));
    }
#endif

    ImGuiContext* glGuiContext = nullptr;
    HGLRC glGuiOwner = nullptr;
    HWND glGuiWindow = nullptr;
    WNDPROC glGuiPreviousProc = nullptr;
    bool glGuiFailed = false;
    bool glGuiClipped = false;
    RECT glGuiPreviousClip{};
    int glGuiWidth = 0, glGuiHeight = 0;
    using GlBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
    GlBindFramebuffer glGuiBindFramebuffer = nullptr;
    GLenum glGuiDrawBuffer = GL_BACK;
    LRESULT CALLBACK glGuiWindowProc(HWND, UINT, WPARAM, LPARAM);

    ClickGui* glClickGui()
    {
        return gFeatureManager && gFeatureManager->mModuleManager
            ? gFeatureManager->mModuleManager->getModule<ClickGui>() : nullptr;
    }

    void glRestoreClip()
    {
        if (!glGuiClipped) return;
        ClipCursor(GetForegroundWindow() == glGuiWindow ? &glGuiPreviousClip : nullptr);
        glGuiClipped = false;
    }

    void glRestoreWindow()
    {
        if (glGuiPreviousProc && IsWindow(glGuiWindow)) {
            const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtr(glGuiWindow, GWLP_WNDPROC));
            // Do not remove another component's subclass installed after ours.
            // Our normal cleanup occurs on the presentation thread first.
            if (current == &glGuiWindowProc) {
                SetWindowLongPtr(glGuiWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(glGuiPreviousProc));
            } else if (current != glGuiPreviousProc) {
                // A later subclass may still call our proc. Do not overwrite
                // its chain or unload code it references. A restart releases
                // the pinned module in this exceptional teardown case.
                HMODULE retained = nullptr;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&glGuiWindowProc), &retained);
                spdlog::warn("OpenGL window subclass chain changed; module retained for safety until game restart");
            }
        }
    }

    LRESULT CALLBACK glGuiWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        struct WindowCall {
            WindowCall() { glGuiWindowCalls.fetch_add(1); }
            ~WindowCall() { glGuiWindowCalls.fetch_sub(1); }
        } windowCall;
        WNDPROC original = nullptr;
        bool consume = false;
        {
            std::lock_guard<std::recursive_mutex> lock(glGuiMutex);
            original = glGuiPreviousProc;
            if (glGuiContext && !glGuiStop.load() && !Solstice::mRequestEject) {
                ImGui::SetCurrentContext(glGuiContext);
                ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
                auto* gui = glClickGui();
                const bool openAtEntry = gui && (gui->mEnabled || gui->mWantedState);
                const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
                if ((down || up) && wParam < 256) {
                    const auto key = static_cast<int>(wParam);
                    Keyboard::mPressedKeys[key] = down;
                    if (down && key == VK_END && !ImGui::GetIO().WantTextInput)
                        Solstice::mRequestEject = true;
                    const bool repeated = (lParam & (1LL << 30)) != 0;
                    if (gui && down && !repeated && key == gui->mKey &&
                        !ImGui::GetIO().WantTextInput) {
                        gui->toggle();
                        if (!gui->mWantedState) glGuiCapture.store(false);
                        consume = true;
                    } else if (gui && (gui->mEnabled || gui->mWantedState)) {
                        KeyEvent event(key, down);
                        gui->onKeyEvent(event); // preserves Escape/binding/Shift behavior
                        if (!gui->mWantedState) glGuiCapture.store(false);
                    }
                }
                if (message == WM_KILLFOCUS) {
                    for (auto& key : Keyboard::mPressedKeys) key.second = false;
                    if (gui) gui->mWantedState = false;
                    glGuiCapture.store(false);
                    glRestoreClip();
                    ClipCursor(nullptr);
                }
                // Closing Escape must not fall through and open the game's
                // pause menu on the same key-down that closed the GUI.
                if (gui && (OpenGLGui::capturesInput() || (openAtEntry && !gui->mWantedState))) {
                    switch (message) {
                    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
                    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
                    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
                    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
                    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
                    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_INPUT:
                        consume = true;
                    }
                }
            }
        }
        // Foreground raw input requires DefWindowProc cleanup even when the UI
        // consumes it. Unrelated window/system messages always reach the game.
        if (consume) return message == WM_INPUT ? DefWindowProc(window, message, wParam, lParam) : 0;
        return original ? CallWindowProc(original, window, message, wParam, lParam)
                        : DefWindowProc(window, message, wParam, lParam);
    }

    void glCloseOnRenderThread()
    {
        glGuiCapture.store(false);
        glRestoreClip();
        glRestoreWindow();
        ImGui::SetCurrentContext(glGuiContext);
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        FontHelper::Fonts.clear();
        ImGui::DestroyContext(glGuiContext);
        glGuiContext = nullptr;
        glGuiActive.store(false);
    }
}

bool OpenGLGui::isActive() { return glGuiActive.load(); }
bool OpenGLGui::capturesInput() {
    const auto last = glGuiLastDrawMs.load();
    return glGuiCapture.load() && last != 0 && glNowMs() - last < 500;
}

void OpenGLGui::render(HDC dc) try
{
    std::lock_guard<std::recursive_mutex> lock(glGuiMutex);
    ++glGuiFrameCounter;
    const HGLRC current = wglGetCurrentContext();
    const HWND window = WindowFromDC(dc);
    // One-shot proof of which presentation path is live. Without this the log
    // cannot distinguish "DX Present never reached" from "GL swap reached but
    // the window was rejected".
    static bool loggedSwapEntry = false;
    if (!loggedSwapEntry) {
        loggedSwapEntry = true;
        spdlog::info("[GUI probe] GL swap entry: context=0x{:X} dcWindow=0x{:X} clickGui={}",
            reinterpret_cast<uintptr_t>(current), reinterpret_cast<uintptr_t>(window),
            glClickGui() != nullptr);
    }
    if (!current || !window) return;

    if (glGuiContext && (glGuiStop.load() || Solstice::mRequestEject)) {
        if (current == glGuiOwner) glCloseOnRenderThread();
        return;
    }
    if (glGuiStop.load() || Solstice::mRequestEject || glGuiFailed || !glClickGui()) return;

    if (!glGuiContext) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId()) return;
        // The window behind this DC is the target we are drawing into, so it is
        // the game window by definition. Requiring it to equal the
        // title-derived ProcUtils::getMinecraftWindow() silently disabled the
        // whole overlay whenever that heuristic missed (localised / repackaged
        // clients), which also produced one log line per frame.
        ProcUtils::setMinecraftWindow(window);
        // Never replace an ImGui context already owned by the DX renderer.
        if (ImGui::GetCurrentContext()) return;
        glGuiContext = ImGui::CreateContext();
        glGuiOwner = current;
        glGuiWindow = window;
        glGuiActive.store(true); // own the context while backend setup runs too
        PIXELFORMATDESCRIPTOR format{};
        if (DescribePixelFormat(dc, GetPixelFormat(dc), sizeof(format), &format))
            glGuiDrawBuffer = (format.dwFlags & PFD_DOUBLEBUFFER) ? GL_BACK : GL_FRONT;
        ImGui::GetIO().IniFilename = nullptr;
        FontHelper::load();
        const bool platformReady = ImGui_ImplWin32_InitForOpenGL(window);
        const bool rendererReady = platformReady && ImGui_ImplOpenGL3_Init();
        const auto framebufferProc = reinterpret_cast<uintptr_t>(wglGetProcAddress("glBindFramebuffer"));
        glGuiBindFramebuffer = framebufferProc > 3 && framebufferProc != static_cast<uintptr_t>(-1)
            ? reinterpret_cast<GlBindFramebuffer>(framebufferProc) : nullptr;
        if (!rendererReady || !glGuiBindFramebuffer || !ImGui_ImplOpenGL3_CreateDeviceObjects()) {
            if (rendererReady) ImGui_ImplOpenGL3_Shutdown();
            if (platformReady) ImGui_ImplWin32_Shutdown();
            FontHelper::Fonts.clear();
            ImGui::DestroyContext(glGuiContext);
            glGuiContext = nullptr;
            glGuiActive.store(false);
            glGuiFailed = true;
            spdlog::error("OpenGL ClickGUI backend initialization failed; native frame left untouched");
            return;
        }
        SetLastError(0);
        glGuiPreviousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&glGuiWindowProc)));
        if (!glGuiPreviousProc) {
            spdlog::error("OpenGL ClickGUI cannot subclass the game window: {}", GetLastError());
            glCloseOnRenderThread();
            glGuiFailed = true;
            return;
        }
        glGuiActive.store(true);
        spdlog::info("OpenGL Modern ClickGUI initialized on the game window; TAB opens, ESC closes");
    }
    if (current != glGuiOwner || window != glGuiWindow) {
        if (!glGuiReportedMismatch) {
            glGuiReportedMismatch = true;
            spdlog::warn("OpenGL GUI swap context/window mismatch: context={} owner={} window={} expected={}",
                (void*)current, (void*)glGuiOwner, (void*)window, (void*)glGuiWindow);
        }
        return;
    }
    ImGui::SetCurrentContext(glGuiContext);
    auto* gui = glClickGui();
    if (!gui) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    // This project's Win32 backend deliberately omits its usual DisplaySize
    // update. Use the actual client area rather than the unadapted GuiData.
    RECT clientRect{};
    if (!GetClientRect(window, &clientRect) || clientRect.right <= clientRect.left ||
        clientRect.bottom <= clientRect.top) {
        glGuiCapture.store(false);
        glRestoreClip();
        return;
    }
    io.DisplaySize = ImVec2(static_cast<float>(clientRect.right - clientRect.left),
                            static_cast<float>(clientRect.bottom - clientRect.top));
    // HudEditor owns its own GL overlay; when it is enabled we must capture
    // input (software cursor + swallow events via MouseHook + ClipCursor) just
    // like ClickGUI, otherwise the mouse still rotates the camera while editing
    // the HUD. The previous condition only engaged capture for ClickGUI's
    // mWantedState, so an open HudEditor never set capturing and the camera
    // kept tracking the pointer.
    const bool hudEditorActive = HudEditor::gInstance && HudEditor::gInstance->mEnabled;
    const bool capturing = (gui->mWantedState || hudEditorActive) && GetForegroundWindow() == window;
    // The local Win32 backend also omits its mouse-position fallback. Poll in
    // the foreground so the software cursor has a position even before a
    // WM_MOUSEMOVE is delivered after opening the menu.
    if (capturing && !io.WantSetMousePos) {
        POINT cursor{};
        if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor))
            io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    }
    if (!capturing) { glGuiCapture.store(false); glGuiReportedOpenDraw = false; }
    io.MouseDrawCursor = capturing;
    if (capturing) {
        if (!glGuiClipped) {
            GetClipCursor(&glGuiPreviousClip);
            glGuiClipped = true;
        }
        ClipCursor(nullptr);
    } else glRestoreClip();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    if (glGuiWidth != static_cast<int>(io.DisplaySize.x) || glGuiHeight != static_cast<int>(io.DisplaySize.y)) {
        glGuiWidth = static_cast<int>(io.DisplaySize.x);
        glGuiHeight = static_cast<int>(io.DisplaySize.y);
        WindowResizeEvent event(glGuiWidth, glGuiHeight);
        gui->onWindowResizeEvent(event);
    }
    MathUtils::displaySize = {io.DisplaySize.x, io.DisplaySize.y};
    // RenderUtils::transform is only ever assigned from D3DHook::present(), and
    // that present returns the original immediately once this GL path is active.
    // It is not enough that SetupAndRenderHook also pushes into FrameTransforms:
    // nothing drains that queue here either. So every world-to-screen consumer --
    // ESP, Tracers, Nametags, ChinaHat, JumpCircles, ItemESP -- projected through
    // a zero matrix and drew nothing, whatever their symbols resolved to.
    // Build the same struct here from the same inputs SetupAndRenderHook uses.
    if (auto* ci = ClientInstance::get()) {
        if (auto* player = ci->getLocalPlayer()) {
            if (auto* lr = ci->getLevelRenderer()) {
                if (auto* rp = lr->getRendererPlayer()) {
                    if (auto* cam = rp->getCameraPos()) {
                    if (auto* rpc = player->getRenderPositionComponent()) {
                        glmatrixf view = ci->getViewMatrix();
                        // ClientInstance+0x358 now holds the game's own view
                        // matrix, so normally nothing else is needed here. The
                        // checks below exist for the two ways that can fail: the
                        // field moved to another offset, or it holds something
                        // that is not a camera basis (a zeroed frame, a stale
                        // object). A matrix that is orthonormal is trusted; a
                        // located-but-moved one is re-read from its address every
                        // frame, never cached, because a stored copy freezes the
                        // projection at whatever the camera did during the search.
                        const auto isCameraBasis = [](const glmatrixf& m) {
                            const glm::vec3 c0{ m[0] }, c1{ m[1] }, c2{ m[2] };
                            const auto unit = [](const glm::vec3& v) { return std::fabs(glm::length(v) - 1.f) < 5e-2f; };
                            return unit(c0) && unit(c1) && unit(c2)
                                && std::fabs(glm::dot(c0, c1)) < 5e-2f
                                && std::fabs(glm::dot(c0, c2)) < 5e-2f
                                && std::fabs(glm::dot(c1, c2)) < 5e-2f;
                        };
                        static bool sScanned = false;
                        static bool sViewFound = false;
                        static void* sViewObject = nullptr;
                        static uint32_t sViewOffset = 0;
                        int viewSource = isCameraBasis(view) ? 0 : 3;
                        glm::vec3 forward{ 0.f, 0.f, 1.f };
                        float yawDeg = 0.f, pitchDeg = 0.f;
                        if (auto* rot = player->getActorRotationComponent()) {
                            yawDeg = rot->mYaw;
                            pitchDeg = rot->mPitch;
                            constexpr float kD2R = 3.14159265358979323846f / 180.f;
                            const float y = yawDeg * kD2R, p = pitchDeg * kD2R;
                            forward = { -std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p) };
                            if (viewSource != 0 && sViewFound && sViewObject) {
                                std::memcpy(&view, static_cast<const uint8_t*>(sViewObject) + sViewOffset, sizeof(view));
                                if (isCameraBasis(view)) viewSource = 1;
                            }
                            if (viewSource != 0 && !sScanned) {
                                sScanned = true;
                                const struct { const void* object; size_t bytes; const char* name; } targets[] = {
                                    { ci, 0x1800, "ClientInstance" },
                                    { lr, 0x1800, "LevelRenderer" },
                                    { rp, 0x1800, "LevelRendererPlayer" },
                                };
                                float best = 0.999f;
                                for (const auto& target : targets) {
                                    int logged = 0;
                                    for (const auto& candidate : scanForViewMatrix(target.object, target.bytes, forward)) {
                                        if (logged++ < 3)
                                            spdlog::info("[view] {} +0x{:X} forward-dot {:.4f}", target.name, candidate.offset, candidate.forwardDot);
                                        if (candidate.forwardDot > best) {
                                            best = candidate.forwardDot;
                                            sViewObject = const_cast<void*>(target.object);
                                            sViewOffset = candidate.offset;
                                            sViewFound = true;
                                        }
                                    }
                                }
                                spdlog::info("[view] search done found={} offset=+0x{:X} dot={:.4f}", sViewFound, sViewOffset, best);
                                if (sViewFound) {
                                    std::memcpy(&view, static_cast<const uint8_t*>(sViewObject) + sViewOffset, sizeof(view));
                                    if (isCameraBasis(view)) viewSource = 1;
                                }
                            }
                            if (viewSource != 0) {
                                view = viewMatrixFromRotation(yawDeg, pitchDeg);
                                viewSource = 2;
                            }
                        }
                        RenderUtils::transform = FrameTransform{
                            view, *cam, rpc->mPosition, ci->getFov(), viewSource };
                        // getBoxPoints() and getImBoxPoints() read these three
                        // statics, not the transform members, and they are
                        // otherwise only filled by D3DHook::igBeginFrame -- i.e.
                        // never on this path. With fov and origin at zero
                        // OWorldToScreen returns false and every ESP box, tracer
                        // and hat collapses to nothing, which is exactly what a
                        // working transform alone did not fix.
                        MathUtils::fov = RenderUtils::transform.mFov;
                        MathUtils::origin = RenderUtils::transform.mOrigin;

                    }
                    }
                }
            }
        }
    }
    ImGui::NewFrame();
    // RenderEvent used to be dispatched only from D3DHook::present(), and that
    // present returns the original immediately once this GL path is active. All
    // 39 files that subscribe to RenderEvent -- the whole Visual group (Arraylist,
    // ESP, Nametags, Watermark, Notifications, Keystrokes, Tracers, ...) plus the
    // combat and movement modules that hook it -- therefore never rendered here;
    // only the three modules called by hand drew anything. The dispatch now
    // happens further down, inside the fullscreen click-through window, because
    // ImGui::NewFrame leaves no current window and this ImGui fork's PushFont
    // writes to it.
    if (gFeatureManager && gFeatureManager->mModuleManager) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##solstice_hud", nullptr, hudFlags);
        static bool loggedHudDispatch = false;
        if (!loggedHudDispatch) {
            loggedHudDispatch = true;
            spdlog::info("[GUI probe] GL HUD dispatch active (RenderEvent -> all subscribers)");
        }
        // Interface::onRenderEvent dereferences ClientInstance::get() without a
        // null check, and this path can run before the client is fully up.
        // TargetHUD subscribes too (TargetHUD.cpp), so nothing is called by hand
        // here any more -- doing that would draw it twice.
        if (ClientInstance::get()) {
            glHangMarker(1, "HUD_DISPATCH_BEGIN", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
            auto holder = nes::make_holder<RenderEvent>();
#ifdef SOLSTICE_RENDER_DIAG
            // 3.6.2-F: per-subscriber BEGIN/END fault localization. The diagnostic
            // path mirrors the production trigger() exactly (same lock + ordering);
            // it only adds flush-on-write BEGIN/END markers. After the capped window
            // of frames it falls back to the unchanged production trigger().
            renderDiagOpen();
            if (gRenderDiagFramesRemaining > 0) {
                --gRenderDiagFramesRemaining;
                const unsigned frame = glGuiFrameCounter;
                auto& disp = gFeatureManager->mDispatcher->get<RenderEvent>();
                disp.diagnostic_trigger(
                    holder,
                    [](void* inst) -> const char* {
                        return renderDiagName(inst);
                    },
                    [frame](unsigned index, const char* name, int phase) {
                        renderDiagRecord(frame, index, name, phase);
                    });
            } else {
                gFeatureManager->mDispatcher->trigger(holder);
            }
#else
            gFeatureManager->mDispatcher->trigger(holder);
#endif
            glHangMarker(1, "HUD_DISPATCH_END", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    glHangMarker(2, "IMGUI_RENDER_BEGIN", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
    ImGui::Render();
    // SwapBuffers may arrive with an offscreen draw FBO still bound. Draw on
    // the window's default framebuffer, then restore the game's FBO selection.
    GLint previousFramebuffer = 0;
    glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &previousFramebuffer);
    glGuiBindFramebuffer(0x8CA9 /* GL_DRAW_FRAMEBUFFER */, 0);
    GLint previousDrawBuffer = 0;
    GLboolean previousColorMask[4]{};
    glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    glDrawBuffer(glGuiDrawBuffer);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glHangMarker(2, "IMGUI_RENDER_END", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
    ImDrawData* drawData = ImGui::GetDrawData();
    glHangMarker(3, "GL_SUBMIT_BEGIN", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
    if (drawData && drawData->CmdListsCount > 0) {
        ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }
    if (capturing && drawData && drawData->TotalVtxCount > 0) {
        glGuiLastDrawMs.store(glNowMs());
        glGuiCapture.store(true);
        if (!glGuiReportedOpenDraw) {
            glGuiReportedOpenDraw = true;
            spdlog::info("OpenGL GUI submitted: vertices={} lists={} display={}x{} framebuffer={} drawBuffer={}",
                drawData->TotalVtxCount, drawData->CmdListsCount, io.DisplaySize.x, io.DisplaySize.y,
                previousFramebuffer, static_cast<unsigned>(glGuiDrawBuffer));
        }
    } else if (capturing) {
        glGuiCapture.store(false);
        if (!glGuiReportedOpenDraw) {
            glGuiReportedOpenDraw = true;
            spdlog::warn("OpenGL GUI open frame has no vertices; display={}x{}", io.DisplaySize.x, io.DisplaySize.y);
        }
    }
    glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
    glDrawBuffer(static_cast<GLenum>(previousDrawBuffer));
    glGuiBindFramebuffer(0x8CA9, static_cast<GLuint>(previousFramebuffer));
    glHangMarker(3, "GL_SUBMIT_END", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
}
catch (const std::exception& ex)
{
    std::lock_guard<std::recursive_mutex> lock(glGuiMutex);
    glGuiFailed = true;
    glGuiCapture.store(false);
    glRestoreClip();
    if (auto* gui = glClickGui()) gui->mWantedState = false;
    if (glGuiContext) glCloseOnRenderThread();
    spdlog::error("OpenGL ClickGUI disabled after UI exception: {}", ex.what());
}
catch (...)
{
    std::lock_guard<std::recursive_mutex> lock(glGuiMutex);
    glGuiFailed = true;
    glGuiCapture.store(false);
    glRestoreClip();
    if (auto* gui = glClickGui()) gui->mWantedState = false;
    if (glGuiContext) glCloseOnRenderThread();
    spdlog::error("OpenGL ClickGUI disabled after unknown UI exception");
}

void OpenGLGui::shutdown()
{
    glGuiStop.store(true);
    // Let SwapBuffers destroy GPU resources with the owning GL context current.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (isActive() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        std::lock_guard<std::recursive_mutex> lock(glGuiMutex);
        glGuiCapture.store(false);
        glRestoreClip();
        glRestoreWindow();
        if (glGuiContext) {
            // Minimized/no-present fallback: never steal a context from another
            // thread or issue GL deletion without it. Detach callbacks, abandon
            // allocations until the game's context teardown reclaims them.
            spdlog::warn("OpenGL cleanup had no owning frame; callbacks detached, session resources abandoned");
            ImGui::SetCurrentContext(nullptr);
        }
        glGuiActive.store(false);
    }
    // A callback can still be inside the original game WndProc, outside our
    // mutex. Do not unload its return address while it is in flight.
    const auto callbackDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (glGuiWindowCalls.load() && std::chrono::steady_clock::now() < callbackDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (glGuiWindowCalls.load()) {
        HMODULE retained = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&glGuiWindowProc), &retained);
        spdlog::warn("Game window callback is still in flight; module retained until game restart");
    }
}
