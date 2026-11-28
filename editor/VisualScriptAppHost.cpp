// The editor's Visual Script window (VisualScriptAppHost.hpp): one shared VisualScriptApp, fed with the editor's data through its Config
// hooks. No second Visual Script editor is written here; this file only connects the shared one to the editor.
#include "VisualScriptAppHost.hpp"
#include "ScriptEditorAppHost.hpp"   // g_scriptCodeOwnsKeys: a hosted OkayUI app has the keyboard
#include "VisualScriptApp.hpp"
#include "VisualScriptModel.hpp"     // PublishOpenGraph, the editor's open graphs
#include "OkayPanels.hpp"            // DrawAppWindowOkayUI: a whole OkayUI app in an editor window
#include "ConsolePanel.hpp"          // ConsoleLog
#include "imgui.h"                   // the editor's own drags (Project and Hierarchy panels), handed on to OkayUI
#include "okay/Platform/Platform.hpp"   // PrefPath: the settings folder the Visual Script program uses too
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/VisualScript/VsRefs.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>

namespace oked {

extern bool g_showVisualScript;   // main.cpp (namespace oked): View > Editors & Tools > Visual Script

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
        // Save: write the file and reload every object that runs this graph (including ones whose path was stored differently and
        // embedded copies); the Console says how many objects were updated
        cfg.save = [](okay::editor::vsed::VsEditState& tab, std::string& why) {
            if (!g_ed) { why = "no scene"; return false; }
            if (!okay::editor::vsed::PublishOpenGraph(*g_ed, false)) { why = "the file could not be written"; return false; }
            int n = 0;
            for (const auto& up : g_ed->scene().Objects())
                for (auto* vc : up->GetComponents<okay::VisualScriptComponent>())
                    if (!tab.g.path.empty() && okay::editor::vsed::VsSamePath(vc->Path(), tab.g.path)) ++n;
            if (n) ConsoleLog("Visual Script: updated " + std::to_string(n) + " object(s) that run " + tab.g.path);
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
        cfg.stopPlay = [] { if (g_ed) { g_ed->Stop(); ConsoleLog("Stop"); } };
        cfg.editor = []() { return g_ed; };   // the scene: object lists, Attach to Selected Object, Add Component, ...
        // a custom node's Edit node and Make a custom version: the script opens in the Script Editor at the node's definition
        cfg.openScript = [](const std::string& path, int line) { ScriptEditorOpenFile(path, line); };
        app.reset(new vsapp::VisualScriptApp(cfg));
    }
    return *app;
}

// A drag that began in one of the editor's Dear ImGui panels, handed to OkayUI so the app's slots and canvas take it: a file from the
// Project panel (ASSET_PATH, its path), an object from the Hierarchy (OBJECT_REF: what a slot stores for it, its @guid: reference or
// its name), a property from the Inspector (VS_INSPECTOR).
void HandDragToOkayUI() {
    const ImGuiPayload* pl = ImGui::GetDragDropPayload();
    if (!pl || !pl->Data) return;
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (pl->IsDataType("ASSET_PATH") || pl->IsDataType("VS_INSPECTOR")) {
        OkayUI::SetExternalDragDrop(pl->IsDataType("ASSET_PATH") ? "ASSET_PATH" : "VS_INSPECTOR", pl->Data,
                                    std::min(pl->DataSize, OkayUI::kDragPayloadMax), released);
    } else if (pl->IsDataType("GO_PTR")) {
        okay::GameObject* go = *(okay::GameObject* const*)pl->Data;
        if (!go) return;
        const std::string ref = go->Guid().empty() ? go->name : okay::vs::VsGuidRef(go->Guid());
        OkayUI::SetExternalDragDrop("OBJECT_REF", ref.c_str(), (int)std::min<std::size_t>(ref.size() + 1, OkayUI::kDragPayloadMax), released);
    }
}
} // namespace

void DrawVisualScriptApp(okay::editor::EditorState& ed) {
    g_ed = &ed;
    okay::editor::vsed::VsRestoreTabs();   // the graphs that were open last time (once)
    if (!g_showVisualScript) return;
    vsapp::VisualScriptApp& app = App();
    // the open project is the app's folder (File > Open lists its graphs, Find References searches it)
    static std::string followed;
    if (ed.projectDir() != followed) {
        followed = ed.projectDir();
        if (!followed.empty()) app.OpenFolder(followed);
    }
    // "Visual Script": the old window's name, so saved layouts put it where it was docked
    DrawAppWindowOkayUI("Visual Script", &g_showVisualScript, "##okayui_visual_script_app",
                        [&app](float x, float y, float w, float h) { HandDragToOkayUI(); app.Frame(x, y, w, h); }, 1180.0f, 760.0f,
                        [&app](const std::string& dropped) {
                            if (std::filesystem::u8path(dropped).extension().u8string() == ".okayvs") app.OpenFile(dropped);
                        });
    if (app.WantsKeyboard()) g_scriptCodeOwnsKeys = true;   // the editor routes every key to the hosted app this frame
}

} // namespace oked
