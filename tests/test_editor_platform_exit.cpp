// The editor's own platform side (editor/EditorPlatform.cpp) and the shared ImGui glue (shared/ImGuiOwnPlatform.*) must not crash
// when the program ends through std::exit without shutting them down first. std::exit skips the end of main() and destroys the
// globals instead, in reverse order of construction; okay::platform's backends are function-local statics made after the globals,
// so they are destroyed first, and a global that still owns the editor platform (the editor's g_edPlat) then reaches a backend that
// is gone. This was the editor's crash on the OKAY_UI_QUIT screenshot hook (rc 139 right after the PNG was saved).
//
//   test_editor_platform_exit exit        a global own editor platform with its main window, the views' GL window, ImGui on our
//                                          glue and one panel window (an ImGui viewport outside the main window); then std::exit(0)
//                                          with nothing shut down. The process must end with 0.
//   test_editor_platform_exit nocontext   the ImGui context is destroyed before the editor platform (and so the glue): the glue's
//                                          destructor must not touch ImGui. Then the platform is destroyed normally. Ends with 0.
//
// Under Xvfb (exit 77 = skipped when there is no display or our own backend cannot start). Written from the okay::platform and
// EditorPlatform interfaces and imgui.h; no code from elsewhere.
#include "EditorPlatform.hpp"
#include "okay/Platform/Platform.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace ep = okay::editorplat;
namespace plat = okay::platform;

namespace {

// Global on purpose: it is destroyed during static destruction, after okay::platform's backends (as the editor's g_edPlat is).
std::unique_ptr<ep::EditorPlatform> g_plat;

int Fail(const char* what) {
    std::printf("FAIL: %s (%s)\n", what, g_plat ? g_plat->LastError().c_str() : "no platform");
    std::fflush(stdout);
    return 1;
}

// Pump events and run one ImGui frame with a small window placed at `pos` (screen coordinates).
void Frame(ImVec2 pos) {
    if (plat::Backend* b = g_plat->MainWindowBackend()) {
        plat::Event e;
        while (b->PollEvent(e)) g_plat->ProcessEvent(e);
    }
    g_plat->NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(160, 120), ImGuiCond_Always);
    ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("panel");
    ImGui::End();
    ImGui::Render();
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) ImGui::UpdatePlatformWindows();
}

// Start-up as the editor does on its own side: our backend, the OpenGL main window, the views' GL context, ImGui on the glue.
// Returns 0 when ready, 77 to skip, 1 on failure.
int StartUp() {
    setenv("OKAY_PLATFORM_BACKEND", "own", 1);
    unsetenv("OKAY_THIRD_PARTY");
    unsetenv("OKAY_EDITOR_OWN_VIEWPORTS");
    plat::ReconsiderDefault();
    if (!plat::IsCompiledIn(plat::BackendKind::Own)) { std::printf("SKIP: our own backend is not compiled in\n"); return 77; }
    std::string err;
    g_plat = ep::CreateOwnEditorPlatform(&err);
    if (!g_plat) { std::printf("FAIL: CreateOwnEditorPlatform: %s\n", err.c_str()); return 1; }
    if (!g_plat->CreateMainWindow("exit-test", 320, 240, true)) {
        std::printf("SKIP: no main window: %s\n", g_plat->LastError().c_str());
        return 77;
    }
    g_plat->MainWindow()->Show();
    if (!g_plat->CreateViewportGL()) std::printf("  note: no views' GL context: %s\n", g_plat->LastError().c_str());

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    unsigned char* px = nullptr; int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);   // no renderer here: build the font atlas by hand
    if (!g_plat->InitImGui(nullptr)) return Fail("InitImGui");
    if (g_plat->SupportsImGuiViewports()) {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;   // no renderer: ImGui only needs to make the windows here
    }
    for (int i = 0; i < 3; ++i) Frame(ImVec2(10, 10));
    return 0;
}

int CountPanelWindows() {
    int n = 0;
    for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports)
        if (vp != ImGui::GetMainViewport() && vp->PlatformUserData) ++n;
    return n;
}

int RunExit() {
    if (const int rc = StartUp()) return rc;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        // A panel well outside the main window becomes its own OS window (the glue's Platform_CreateWindow).
        const ImGuiViewport* mv = ImGui::GetMainViewport();
        const ImVec2 out(mv->Pos.x + mv->Size.x + 80, mv->Pos.y + 20);
        for (int i = 0; i < 5; ++i) Frame(out);
        const int panels = CountPanelWindows();
        std::printf("  panel windows open: %d\n", panels);
        if (panels < 1) return Fail("the panel did not become its own window");
    } else {
        std::printf("  note: panel windows are off on this build; exiting with the main window and the glue only\n");
    }
    std::printf("  std::exit(0) with the editor platform, its windows and ImGui still alive\n");
    std::fflush(stdout);
    std::exit(0);   // no shut-down: static destruction must cope (the process must end with 0, not a crash)
}

int RunNoContext() {
    if (const int rc = StartUp()) return rc;
    ImGui::DestroyContext();   // before the glue: its destructor must not touch ImGui now
    if (ImGui::GetCurrentContext() != nullptr) return Fail("the context is still current");
    g_plat->DestroyViewportGL();
    g_plat.reset();
    std::printf("  the glue was destroyed after the ImGui context without touching it\n");
    std::printf("PASS\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no display (run under Xvfb)\n"); return 77; }
    const char* mode = argc > 1 ? argv[1] : "exit";
    if (std::strcmp(mode, "exit") == 0) return RunExit();
    if (std::strcmp(mode, "nocontext") == 0) return RunNoContext();
    std::printf("usage: %s [exit|nocontext]\n", argv[0]);
    return 2;
}
