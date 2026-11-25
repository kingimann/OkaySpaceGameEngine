// The window-mode run function (shared/OkayUIHostMode.hpp): the platform backend, window, painter and the frame loop around DrawHostModeUi and the editor's
// game step (EditorFrameTick.hpp). Kept apart from OkayUIHostMode.cpp because it uses the real okay::editor::EditorState (the editor target links it; the
// headless tests of the screen and of the state file do not). Steps 2 to 4 of docs/ui/own_ui_toolkit.md 6.24. Not run on a real window in the session that
// wrote it: only compile-checked and exercised through its parts.
#include "OkayUIHostMode.hpp"

#ifdef OKAY_HAVE_OKAYUI
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostPanels.hpp"
#include "OkayUIHostViews.hpp"
#include "OkayUIHostObjects.hpp"
#include "OkayUIHostMenu.hpp"
#include "OkayUIHostDialogs.hpp"
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
    std::unique_ptr<okay::platform::Window> win;
    std::unique_ptr<okay::platform::Painter> painter;
    std::string err;
    if (!okayuihost::platform::OpenWindow(*be, d, win, painter, err)) { std::fprintf(stderr, "[okayui-host] %s\n", err.c_str()); be->Shutdown(); return 1; }
    okay::editor::EditorState editor;   // the real editor state: the same scene, Play loop and services the Dear ImGui editor drives
    {   // the real panels: Console, Scene Manager and the Project window over the project folder (OKAY_UI_HOST_PROJECT, else ./Assets's folder, else none)
        const char* pe = std::getenv("OKAY_UI_HOST_PROJECT");
        std::error_code pec; std::string projectDir = pe && pe[0] ? pe : std::string();
        if (projectDir.empty() && std::filesystem::is_directory("Assets", pec)) projectDir = std::filesystem::current_path(pec).generic_string();
        if (!projectDir.empty()) editor.setProjectDir(projectDir);
        state.panels = wm::MakeHooks(&editor, projectDir, &state);
    }
    {   // the Scene and Game views: the engine's software renderer into a painter texture (OkayUIHostViews.cpp); the orbit camera is the editor's own
        wm::ViewSource vs; vs.scene = [&editor] { return &editor.scene(); };
        vs.selected = [&editor] { return editor.selected(); };
        vs.select = [&editor](okay::GameObject* g, bool toggle) { if (toggle && g) editor.ToggleSelect(g); else editor.Select(g); };
        vs.beginEdit = [&editor](const char* what) { editor.PushUndo(what); };
        vs.edited = [&editor] { editor.dirty = true; };
        vs.is2D = [&editor] { return !editor.view3D; };
        vs.cam2dPos = &editor.cameraPos; vs.cam2dZoom = &editor.cameraZoom;
        vs.yaw = &editor.camYaw; vs.pitch = &editor.camPitch; vs.dist = &editor.camDist; vs.target = &editor.camTarget;
        wm::AddSceneViews(state.panels, &state, painter.get(), vs);
        wm::AddObjectPanels(state.panels, &state, &editor);   // the Hierarchy and the Inspector
        wm::AddMenuBar(state.panels, &state, &editor);        // File / Edit / GameObject / View / Help and the shortcuts
    }
    oked::wm::OkayUIDialogs dialogs;   // the error modal and the "Save changes?" question as OkayUI popups; the panels reach them through oked::CurrentDialogs()
    {
        namespace fs = std::filesystem;
        okay::editor::EditorState* ed = &editor;
        dialogs.ctx.inPrefabMode = [ed] { return ed->InPrefabMode(); };
        dialogs.ctx.hasUnsavedChanges = [ed] { return ed->HasUnsavedChanges(); };
        dialogs.ctx.sceneLabel = [ed] { return ed->path().empty() ? std::string("this new scene") : fs::path(ed->path()).filename().string(); };
        dialogs.ctx.isPlaying = [ed] { return ed->isPlaying(); };
        dialogs.ctx.stopPlay = [ed] { ed->Stop(); };
        dialogs.ctx.save = [ed](std::string& err) {   // the open scene's path, else <project>/Assets/Scenes/<name>.okayscene (as the Dear ImGui editor does)
            std::string p = ed->path();
            if (p.empty()) {
                std::string name = ed->scene().Name();
                for (char& c : name) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) c = '_';
                if (name.empty() || name == "Scene") name = "Main";
                p = ((ed->projectDir().empty() ? fs::path("Scenes") : fs::path(ed->projectDir()) / "Assets" / "Scenes") / (name + ".okayscene")).string();
            }
            std::error_code ec;
            if (fs::path(p).has_parent_path()) fs::create_directories(fs::path(p).parent_path(), ec);
            const bool ok = ed->Save(p, &err);
            wm::ConsoleLog(ok ? "Saved scene: " + p : "Couldn't save the scene to " + p + ": " + err, ok ? 0 : 2);
            return ok;
        };
        dialogs.log = [](const std::string& line, int level) { wm::ConsoleLog(line, level); };
        state.panels.dialogs = [&dialogs] { dialogs.Draw(); };
        oked::SetDialogs(&dialogs);
    }
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
    while (!state.quit && loop.Frame([&] {
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
    oked::SetDialogs(nullptr);
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
