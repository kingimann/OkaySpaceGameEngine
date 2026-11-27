// The Visual Script app inside the editor (VisualScriptAppHost.hpp): one shared VisualScriptApp, fed with the editor's data through
// its Config hooks. No second Visual Script editor is written here; this file only connects the shared one to the editor.
#include "VisualScriptAppHost.hpp"
#include "ScriptEditorAppHost.hpp"   // g_scriptCodeOwnsKeys: a hosted OkayUI app has the keyboard
#include "VisualScriptApp.hpp"
#include "VisualScriptModel.hpp"     // PublishOpenGraph, the editor's open graphs
#include "OkayPanels.hpp"            // DrawAppWindowOkayUI: a whole OkayUI app in an editor window
#include "ConsolePanel.hpp"          // ConsoleLog
#include "okay/Platform/Platform.hpp"   // PrefPath: the settings folder the Visual Script program uses too
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/UI/OkayUI.hpp"

#include <filesystem>
#include <memory>

namespace oked {

bool g_showVisualScriptApp = false;

namespace {
okay::editor::EditorState* g_ed = nullptr;   // the editor whose scene the hooks read (set every frame the window draws)

vsapp::VisualScriptApp& App() {
    static std::unique_ptr<vsapp::VisualScriptApp> app;
    if (!app) {
        vsapp::VisualScriptApp::Config cfg;
        cfg.idPrefix = "##editor_vsapp";
        if (okay::platform::Backend* be = okay::platform::Default()) {
            const std::string data = be->PrefPath("OkaySpace", "VisualScript");
            if (!data.empty()) cfg.settingsPath = data + "visual_script.json";   // the program's settings: one look for both
        }
        // Save: write the file and reload every object that runs this graph (the same as the Visual Script window's Save)
        cfg.save = [](okay::editor::vsed::VsEditState&, std::string& why) {
            if (!g_ed) { why = "no scene"; return false; }
            if (!okay::editor::vsed::PublishOpenGraph(*g_ed, false)) { why = "the file could not be written"; return false; }
            return true;
        };
        cfg.log = [](const std::string& line, int level) { ConsoleLog("Visual Script: " + line, level); };
        // while playing: the selected object's running copy of this graph file, else the first object's that runs it
        cfg.liveGraph = [](const std::string& path) -> const okay::vs::NodeGraph* {
            if (!g_ed || !g_ed->isPlaying() || path.empty()) return nullptr;
            const okay::editor::vsed::VsDebugView& dv = okay::editor::vsed::DebugView();
            if (dv.stopped && dv.graph && dv.key == path) return dv.graph;
            auto usesThis = [&](okay::GameObject* o) -> okay::vs::NodeGraph* {
                if (!o) return nullptr;
                for (auto* vc : o->GetComponents<okay::VisualScriptComponent>())
                    if (okay::editor::vsed::VsSamePath(vc->Path(), path) && vc->Graph()) return vc->Graph();
                return nullptr;
            };
            if (okay::vs::NodeGraph* g = usesThis(g_ed->selected())) return g;
            for (const auto& up : g_ed->scene().Objects()) if (okay::vs::NodeGraph* g = usesThis(up.get())) return g;
            return nullptr;
        };
        cfg.stopPlay = [] { if (g_ed) g_ed->Stop(); };
        app.reset(new vsapp::VisualScriptApp(cfg));
    }
    return *app;
}
} // namespace

void DrawVisualScriptApp(okay::editor::EditorState& ed) {
    g_ed = &ed;
    if (!g_showVisualScriptApp) return;
    vsapp::VisualScriptApp& app = App();
    // the open project is the app's folder (File > Open lists its graphs, Find References searches it)
    static std::string followed;
    if (ed.projectDir() != followed) {
        followed = ed.projectDir();
        if (!followed.empty()) app.OpenFolder(followed);
    }
    DrawAppWindowOkayUI("Visual Script (App)", &g_showVisualScriptApp, "##okayui_visual_script_app",
                        [&app](float x, float y, float w, float h) { app.Frame(x, y, w, h); }, 1080.0f, 680.0f,
                        [&app](const std::string& dropped) {
                            if (std::filesystem::u8path(dropped).extension().u8string() == ".okayvs") app.OpenFile(dropped);
                        });
    if (app.WantsKeyboard()) g_scriptCodeOwnsKeys = true;   // the editor routes every key to the hosted app this frame
}

} // namespace oked
