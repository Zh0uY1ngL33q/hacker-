#include "OpenGLGui.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <thread>
#include <gl/GL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>
#include <Solstice.hpp>
#include <Features/FeatureManager.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Events/WindowResizeEvent.hpp>
#include <Features/Modules/Visual/ClickGui.hpp>
#include <Features/Modules/Visual/HudEditor.hpp>
#include <Features/Modules/Visual/TargetHUD.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/Keyboard.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/ProcUtils.hpp>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    std::recursive_mutex glGuiMutex;
    std::atomic<bool> glGuiActive{false};
    std::atomic<bool> glGuiCapture{false};
    std::atomic<long long> glGuiLastDrawMs{0};
    bool glGuiReportedOpenDraw = false;
    bool glGuiReportedMismatch = false;
    long long glNowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    std::atomic<bool> glGuiStop{false};
    std::atomic<unsigned> glGuiWindowCalls{0};
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
    const HGLRC current = wglGetCurrentContext();
    const HWND window = WindowFromDC(dc);
    if (!current || !window) return;

    if (glGuiContext && (glGuiStop.load() || Solstice::mRequestEject)) {
        if (current == glGuiOwner) glCloseOnRenderThread();
        return;
    }
    if (glGuiStop.load() || Solstice::mRequestEject || glGuiFailed || !glClickGui()) return;

    if (!glGuiContext) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId() || window != ProcUtils::getMinecraftWindow()) return;
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
    const bool capturing = gui->mWantedState && GetForegroundWindow() == window;
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
    ImGui::NewFrame();
    RenderEvent event;
    gui->onRenderEvent(event); // original UI
    // Once this GL path is active, D3DHook::present() returns the original
    // present immediately, so the D3D RenderEvent dispatch never runs. Draw the
    // ImGui-only HUD modules here; modules that need D3D resources degrade
    // themselves via OpenGLGui::isActive(). ImGui::NewFrame leaves no current
    // window and this ImGui fork's PushFont writes to it, so give the
    // dispatchers a fullscreen click-through window first.
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
            spdlog::info("[GUI probe] GL HUD dispatch active (HudEditor/TargetHUD)");
        }
        if (auto* hudEditor = gFeatureManager->mModuleManager->getModule<HudEditor>()) {
            hudEditor->onRenderEvent(event);
            hudEditor->onCustomRenderEvent(event);
        }
        if (auto* targetHud = gFeatureManager->mModuleManager->getModule<TargetHUD>())
            targetHud->onRenderEvent(event);
        ImGui::End();
        ImGui::PopStyleVar();
    }
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
    ImDrawData* drawData = ImGui::GetDrawData();
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
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
