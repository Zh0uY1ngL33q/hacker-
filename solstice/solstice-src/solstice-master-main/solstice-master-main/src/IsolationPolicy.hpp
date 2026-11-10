#pragma once

// ---------------------------------------------------------------------------
// SwapBuffers-only isolation build (diagnostic A/B variant).
//
// Contract for this build:
//   1. Keep the native OpenGL SwapBuffers detours (gdi32!SwapBuffers and
//      opengl32!wglSwapBuffers), created by D3DHook::installGlSwapHooks().
//      They are still enabled by the single MH_EnableHook(MH_ALL_HOOKS) call.
//   2. Do NOT dispatch RenderEvent to any subscriber.
//   3. Both detour callbacks call the original SwapBuffers and return.
//   4. Do NOT initialize ClickGUI, the HUD or any visual module;
//      ModuleManager registers no modules at all in this build.
//   5. Do NOT install the 219 packet-handler detours, the dynamic
//      Actor::baseTick hook, or any fallback/recovery RVA path (kiero
//      D3D12/D3D11 vtable bindings, signature bytepatches, ...).
//
// Everything else in Solstice::init still runs (logger, mapping scan,
// ExceptionHandler, directory setup), so a failing run stays diagnosable.
// The manual DEBUG/ENTER console waits and the update-check HTTP request are
// skipped here: they are external inputs, not part of the render path, and
// would otherwise contaminate the A/B comparison with a human/network delay.
//
// Build the full client from the SAME tree with:
//     -DSOLSTICE_SWAP_ONLY_ISOLATION=OFF
// ---------------------------------------------------------------------------

#if defined(SOLSTICE_SWAP_ONLY_ISOLATION)
#define SOLSTICE_ISOLATION_SWAP_ONLY 1
#else
#define SOLSTICE_ISOLATION_SWAP_ONLY 0
#endif
