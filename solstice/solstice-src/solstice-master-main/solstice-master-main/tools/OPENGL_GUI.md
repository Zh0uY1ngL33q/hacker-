# OpenGL Modern ClickGUI adapter

## Why this path exists

The 2026-10-29 12:54 runtime log shows both GDI SwapBuffers and WGL
wglSwapBuffers called by the selected game HWND, with a nonzero current GL
context. The D3D12 dummy-device hooks installed successfully but did not receive
the game's Present. Windows received TAB while the native keyboard feed hook
did not. The adapter therefore uses the proven GL presentation and Win32 window
message paths rather than assuming another SDK offset fixes GUI presentation.

## Implementation boundary

- Existing `ClickGui::onRenderEvent` -> existing `ModernGui::render`; no debug
  window, substitute module list, or separate GUI design.
- Original backend code from Dear ImGui **v1.89.8**, MIT licensed:
  https://github.com/ocornut/imgui/tree/v1.89.8/backends
  Vendored cpp/header are unmodified. The internal loader has only one trailing
  comment-space removed for `git diff --check`; no implementation changes.
  File hashes are checked in `test_opengl_gui.py`. No new runtime download or
  package-manager dependency.
- ImGui Win32 backend owns keyboard/mouse events for GL; native input hooks
  pass through when closed and do not enqueue duplicate ImGui events.
- Default TAB toggles, ESC closes (except the existing key-binding interaction),
  click/drag/wheel use the original Modern UI. Focus loss closes/releases UI.
- Window dimensions come from the platform backend, not unverified GuiData.
- Nested GDI -> WGL swaps render only once. The renderer targets the default
  draw framebuffer and restores the previous framebuffer and GL backend state.
- Only ClickGUI is rendered on this GL path. Other HUD/world render modules
  still need their own adaptation and are NOT declared working by this change.
- Direct2D blur is omitted. Game UI sounds and native grab/releaseMouse calls
  are bypassed on GL because their SDK bindings are not yet verified.
- Embedded TTF resources are not heap allocations: the resource generator and
  generated source set `FontDataOwnedByAtlas=false`, including for teardown.

## Lifecycle limits

GPU cleanup is requested on the next owning-context swap. Shutdown waits up to
two seconds. If minimized/no frame is available, it detaches callbacks and
abandons the session resources rather than stealing another thread's context;
GPU allocation reclamation then waits for the game's GL context teardown.

If another component has inserted a window subclass above ours, the adapter
does not overwrite that component's chain. It retains this DLL until game
restart so the chain cannot jump into unloaded code; this is explicitly logged.
Normal shutdown restores the previous window procedure and pointer clipping.
Window and swap callbacks are drained before unload; if a native callback is
still in flight after the bounded wait, the DLL is likewise retained until
restart rather than unloading its return address or live trampoline.
Context replacement/loss, multi-window ownership, and repeated injection are
not yet runtime-tested. Do not call the implementation a fully verified backend.

## Validation

`test_opengl_gui.py` covers **source/build contracts**, upstream-file hashes,
source scope, nesting, framebuffer restoration order, input routing, font
ownership, and lexical braces. These are NOT a C++ build or a GPU test.

MSVC and the game are on the user's other machine. Required tests there:

1. Build latest HEAD using `cmake --build out/build/x64-Release`.
2. Restart game, inject, complete the existing DEBUG/ENTER initialization gates.
3. Look for `OpenGL Modern ClickGUI initialized ...` in solstice.log.
4. TAB opens the original category UI; ESC closes without opening pause menu.
5. Test category drag, right-click settings, wheel scrolling and editing a UI
   setting. **Do not use unadapted gameplay modules as a GUI acceptance test.**
6. Verify normal game input returns after close, then test focus loss, resize,
   fullscreen changes and END unload. Attach solstice.log/build errors if any.
