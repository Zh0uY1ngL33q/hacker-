"""Source/build contracts only; not an OpenGL or MSVC integration test."""
import hashlib
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'


def source(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')


class OpenGLGuiContracts(unittest.TestCase):
    def test_backend_sources_are_pinned_upstream(self):
        hashes = {
            'imgui_impl_opengl3.cpp': '9e84bf5461781d4a2de84a47eb5698c0634e096b0b738bed0aad28c2f8bf53a9',
            'imgui_impl_opengl3.h': 'a0b244dc8b4623696afc682526a620cd67e9843f04fea9b57bb1407fe56a12b9',
            'imgui_impl_opengl3_loader.h': 'cc7a7fece270e4a546ff49d38fda36a42abdc0621df24ccc33d956a7d147783a',
        }
        for name, expected in hashes.items():
            data = (ROOT / 'include' / 'ImGui' / name).read_bytes().replace(b'\r\n', b'\n')
            self.assertEqual(hashlib.sha256(data).hexdigest(), expected, name)
        self.assertIn('MIT License', source('include/ImGui/OPENGL_BACKEND_LICENSE.txt'))

    def test_gl_loader_isolated_and_linked(self):
        build = source('include/ImGui/CMakeLists.txt')
        self.assertIn('"imgui_impl_opengl3.cpp"', build)
        self.assertIn('SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON', build)
        self.assertIn('opengl32.lib', build)

    def test_existing_modern_gui_not_a_debug_panel(self):
        gl = source('src/Hook/Hooks/RenderHooks/OpenGLGui.cpp')
        self.assertIn('gui->onRenderEvent(event)', gl)
        self.assertNotIn('mDispatcher->trigger', gl)
        self.assertNotIn('ImGui::Begin(', gl)
        self.assertNotIn('getGuiData', gl)
        self.assertIn('modernGui.render(', source('src/Features/Modules/Visual/ClickGui.cpp'))

    def test_nested_swap_is_rendered_once(self):
        swap = source('src/Hook/Hooks/RenderHooks/D3DHook.cpp')
        self.assertIn('outermost(glSwapDepth++ == 0)', swap)
        self.assertEqual(swap.count('GlSwapScope scope;'), 2)
        self.assertEqual(swap.count('if (scope.outermost) OpenGLGui::render(dc);'), 2)
        self.assertIn('~GlSwapScope() { --glSwapDepth;', swap)

    def test_framebuffer_is_restored(self):
        gl = source('src/Hook/Hooks/RenderHooks/OpenGLGui.cpp')
        save = gl.index('GL_DRAW_FRAMEBUFFER_BINDING')
        draw = gl.index('ImGui_ImplOpenGL3_RenderDrawData')
        restore = gl.index('static_cast<GLuint>(previousFramebuffer)')
        self.assertLess(save, draw)
        self.assertLess(draw, restore)

    def test_closing_escape_is_consumed_and_raw_input_cleaned(self):
        gl = source('src/Hook/Hooks/RenderHooks/OpenGLGui.cpp')
        self.assertIn('const bool openAtEntry', gl)
        self.assertIn('OpenGLGui::capturesInput() || (openAtEntry && !gui->mWantedState)', gl)
        self.assertIn('glNowMs() - last < 500', gl)
        self.assertIn('drawData->TotalVtxCount > 0', gl)
        self.assertIn('message == WM_INPUT ? DefWindowProc', gl)
        self.assertIn('key == gui->mKey', gl)
        self.assertIn('WM_KILLFOCUS', gl)

    def test_no_duplicate_native_key_or_mouse_ui_events(self):
        for file, marker in [('KeyHook.cpp', 'void KeyHook::onKey'),
                             ('MouseHook.cpp', 'void MouseHook::onMouse')]:
            code = source('src/Hook/Hooks/MiscHooks/' + file).split(marker, 1)[1]
            self.assertLess(code.index('OpenGLGui::isActive()'), code.index('nes::make_holder'))

    def test_embedded_font_ownership_and_dx_only_effects(self):
        for file in ['CMakeLists.txt', 'src/Utils/Resources.cpp']:
            self.assertIn('font_config.FontDataOwnedByAtlas = false', source(file))
        util = source('src/Utils/MiscUtils/ImRenderUtils.cpp')
        self.assertIn('if (OpenGLGui::isActive()) return;', util)
        self.assertIn('if (OpenGLGui::isActive()) return ImGui::GetIO().DisplaySize;', util)
        modern = source('src/Features/GUI/ModernDropdown.cpp')
        self.assertNotIn('ClientInstance::get()->playUi', modern)

    def test_changed_native_sources_have_balanced_braces(self):
        files = ['src/Hook/Hooks/RenderHooks/OpenGLGui.cpp',
                 'src/Hook/Hooks/RenderHooks/OpenGLGui.hpp',
                 'src/Hook/Hooks/RenderHooks/D3DHook.cpp']
        token = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
        for file in files:
            clean = token.sub('', source(file))
            depth = 0
            for char in clean:
                depth += (char == '{') - (char == '}')
                self.assertGreaterEqual(depth, 0, file)
            self.assertEqual(depth, 0, file)


if __name__ == '__main__':
    unittest.main()
