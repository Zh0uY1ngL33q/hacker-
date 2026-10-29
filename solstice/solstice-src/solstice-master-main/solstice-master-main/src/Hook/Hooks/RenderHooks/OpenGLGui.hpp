#pragma once
#include <Windows.h>

// OpenGL presentation adapter for the existing Modern ClickGUI. It does not
// dispatch unrelated render modules whose resources are still Direct3D-only.
namespace OpenGLGui {
    bool isActive();
    bool capturesInput();
    void render(HDC dc);
    void shutdown();
}
