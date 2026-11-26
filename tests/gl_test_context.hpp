#pragma once
// A hidden window with a current OpenGL context and the GL entry points loaded, for the tests that draw with
// OpenGL on Xvfb (tests/run_under_xvfb.sh). Shared by test_gl_post_alpha and test_rhi_gl.
#include "okay/Platform/Platform.hpp"
#include "okay/Render/GLRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace gltest {

/// 0 when the window, its context and GLRenderer::LoadGL are ready; otherwise prints why and returns 77
/// (the ctest "skipped" code), so a machine without a display or GL stays green.
inline int OpenContext(const char* title, int w, int h, std::unique_ptr<okay::platform::Window>& win) {
    using namespace okay;
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"); return 77; }
    platform::Backend* b = platform::Default();
    if (!b || !b->Init(platform::InitFlags{true, false, false})) { std::printf("SKIP: no window backend\n"); return 77; }
    platform::WindowDesc d;
    d.title = title; d.width = w; d.height = h; d.opengl = true; d.hidden = true; d.resizable = false;
    win = b->MakeWindow(d);
    if (!win) { std::printf("SKIP: MakeWindow failed: %s\n", b->LastError().c_str()); return 77; }
    if (!win->CreateGLContext(platform::GLAttributes{})) { std::printf("SKIP: no GL context\n"); return 77; }
    if (!GLRenderer::LoadGL([](const char* n) -> void* { return platform::Default()->GLProcAddress(n); })) {
        std::printf("SKIP: GL entry points missing\n"); return 77;
    }
    return 0;
}

} // namespace gltest
