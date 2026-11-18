// The window-mode run function (shared/OkayUIHostMode.hpp): the platform backend, window, painter and the frame loop around DrawHostModeUi and the editor's
// game step (EditorFrameTick.hpp). Kept apart from OkayUIHostMode.cpp because it uses the real okay::editor::EditorState (the editor target links it; the
// headless tests of the screen and of the state file do not). Steps 2 to 4 of docs/own_ui_toolkit.md 6.24. Not run on a real window in the session that
// wrote it: only compile-checked and exercised through its parts.
#include "OkayUIHostMode.hpp"

#ifdef OKAY_HAVE_OKAYUI
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "EditorFrameTick.hpp"
#include "EditorState.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/MeshModifiers.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Platform/Painter.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace oked {

int RunOkayUIHostMode(int, char**) {
    okay::platform::Backend* be = okay::platform::Default();
    if (!be || !be->Init(okay::platform::InitFlags())) { std::fprintf(stderr, "[okayui-host] no platform backend: %s\n", be ? be->LastError().c_str() : "none"); return 1; }
    // What the last run left (the window's place and size, the open panels, the dock layout): untrusted text, sanitised against the display.
    HostModeState state;
    okayuihost::PersistedState saved;
    const std::string statePath = HostStatePath(std::getenv("OKAY_UI_HOST_STATE"), be->PrefPath("OkaySpace", "Editor"));
    const bool haveSaved = !statePath.empty() && okayuihost::LoadFile(statePath, saved);
    okay::platform::DisplayInfo di;
    const bool haveDisplay = be->DisplayAt(0, di);
    okayuihost::WindowState ws = saved.window;
    ws = haveDisplay ? okayuihost::Sanitize(ws, di.ux, di.uy, di.uw, di.uh) : okayuihost::Sanitize(ws, 0, 0, 0, 0);
    if (haveSaved) ApplyPersisted(state, saved);
    okay::platform::WindowDesc d; d.title = "OkaySpace Editor (OkayUI window mode)"; d.width = ws.w; d.height = ws.h; d.resizable = true;
    if (ws.x != INT_MIN) { d.x = ws.x; d.y = ws.y; }
    std::unique_ptr<okay::platform::Window> win = be->MakeWindow(d);
    if (!win) { std::fprintf(stderr, "[okayui-host] no window: %s\n", be->LastError().c_str()); be->Shutdown(); return 1; }
    std::string err;
    std::unique_ptr<okay::platform::Painter> painter = okay::platform::CreateSdl2Painter(*win, okay::platform::PainterDesc(), &err);
    if (!painter) { std::fprintf(stderr, "[okayui-host] no painter: %s\n", err.c_str()); be->Shutdown(); return 1; }
    okay::editor::EditorState editor;   // the real editor state: the same scene, Play loop and services the Dear ImGui editor drives
    oked::SimHooks<okay::editor::EditorState> sim;
    sim.tickGame = [](okay::editor::EditorState& e, float d) { e.Tick(d); };
    sim.onRunFramesDone = [] {};
    sim.resumeBreakPause = [] { okay::Game::ClearPause(okay::BehaviorTreeRunner::kBreakPause); };
    unsigned long long lastMs = be->TicksMs();
    OkayUI::UseLauncherLook(true);
    std::fprintf(stderr, "[okayui-host] window mode on (OKAY_UI_HOST=okayui): OkayUI owns the window; Dear ImGui is not started; state file: %s%s\n",
                 statePath.empty() ? "(none)" : statePath.c_str(), haveSaved ? " (restored)" : "");
    okayuihost::platform::FrameLoop loop(*be, *painter);
    okayuihost::StateSaver saver(statePath.empty() ? std::filesystem::path() : std::filesystem::path(statePath));
    auto current = [&] {
        okayuihost::WindowState w; win->GetSize(w.w, w.h); win->GetPosition(w.x, w.y);
        return ToPersisted(state, w);
    };
    while (loop.Frame([&] {
        const unsigned long long now = be->TicksMs();
        float dt = (float)(now - lastMs) / 1000.0f; lastMs = now;
        if (!(dt >= 0.0f) || dt > 1.0f) dt = 1.0f / 60.0f;   // a stall or a clock step is one normal frame, not one huge step
        oked::StepSimulation(editor, oked::SimStep{dt, false, true, 0.0f}, sim);
        oked::FinishFrame(editor, dt, okay::Game::QuitRequested(), nullptr, [&](float) { okay::RefreshModifierStacks(editor.scene()); });
        DrawHostModeUi(state);
    })) {
        if (!statePath.empty() && state.frame > 2) saver.Offer(current(), be->TicksMs());   // saves at most every 2 seconds and only when something changed
    }
    if (!statePath.empty()) saver.Flush(current(), be->TicksMs());
    painter.reset(); win.reset();
    be->Shutdown();
    return 0;
}

} // namespace oked
#else
namespace oked {
int RunOkayUIHostMode(int, char**) { return 1; }   // not compiled in: HostModeRequested is never true without OKAY_HAVE_OKAYUI in main()
}
#endif
