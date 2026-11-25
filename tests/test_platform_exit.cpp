// A window owned by a global must not crash the program when it ends through std::exit (or after main() returns).
//
// std::exit skips the end of main() and destroys the globals in reverse order of construction. okay::platform's backends are
// function-local statics, made after the globals, so they are destroyed first; a global that still owns a window (the editor's
// g_glWin, its hidden OpenGL window) is destroyed afterwards. With our own backend that window's destructor called X11 on a display
// the backend had already closed: the editor crashed (exit 139) on every OKAY_UI_QUIT screenshot run with OKAY_PLATFORM_BACKEND=own.
//
//   test_platform_exit [own|sdl2]   a hidden window with an OpenGL context and a shown window, both owned by globals, then
//                                   std::exit(0) with nothing destroyed. The process must end with 0 (not a crash).
//
// Under Xvfb (exit 77 = skipped when there is no display or the backend cannot start). Written from okay/Platform/Platform.hpp's
// contract; no code from elsewhere.
#include "okay/Platform/Platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace plat = okay::platform;

namespace {
// Global on purpose: destroyed during static destruction, after the backend (as the editor's g_glWin is).
std::unique_ptr<plat::Window> g_glWin;
std::unique_ptr<plat::Window> g_shown;
} // namespace

int main(int argc, char** argv) {
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no display (run under Xvfb)\n"); return 77; }
    plat::BackendKind kind = plat::BackendKind::Own;
    if (argc > 1 && !plat::ParseBackendName(argv[1], kind)) { std::printf("usage: %s [own|sdl2]\n", argv[0]); return 2; }
    if (!plat::IsCompiledIn(kind)) { std::printf("SKIP: backend %s not compiled in\n", plat::BackendName(kind)); return 77; }
    plat::Backend* b = plat::Get(kind);
    plat::InitFlags f; f.audio = false; f.gamepad = false;
    if (!b || !b->Init(f)) { std::printf("SKIP: %s backend did not start: %s\n", plat::BackendName(kind), b ? b->LastError().c_str() : "?"); return 77; }
    if (plat::BackendsShutDown()) { std::printf("FAIL: BackendsShutDown() is true while the backend is alive\n"); return 1; }

    plat::WindowDesc d;
    d.title = "exit-test-gl"; d.width = 16; d.height = 16; d.opengl = true; d.hidden = true;
    g_glWin = b->MakeWindow(d);
    if (!g_glWin) { std::printf("FAIL: hidden GL window: %s\n", b->LastError().c_str()); return 1; }
    const bool ctx = g_glWin->CreateGLContext(plat::GLAttributes());
    std::printf("  hidden GL window made, GL context %s\n", ctx ? "made" : "not made (no GL here)");
    plat::WindowDesc d2;
    d2.title = "exit-test"; d2.width = 200; d2.height = 120;
    g_shown = b->MakeWindow(d2);
    if (!g_shown) { std::printf("FAIL: window: %s\n", b->LastError().c_str()); return 1; }
    g_shown->Show();
    for (int i = 0; i < 10; ++i) { plat::Event e; while (b->PollEvent(e)) {} b->DelayMs(10); }

    std::printf("  std::exit(0) with both windows still owned by globals\n");
    std::fflush(stdout);
    std::exit(0);   // no shut-down: static destruction must cope
}
