// See OkayUIHostMenu.hpp. Built like OkayUIHostObjects.cpp: its own object library with okayui/probe first on the include path, so the editor's own
// MenuEntriesUI.hpp (which includes <imgui.h>) draws the real MenuBarModel entries through OkayUI's compatibility layer. No Dear ImGui.
#include "OkayUIHostMenu.hpp"
#include "DialogService.hpp"
#include "EditorState.hpp"
#include "ConsolePanel.hpp"
#include "MenuBarModel.hpp"
#include "MenuEntriesUI.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace oked {
namespace wm {

namespace {
MenuProbe g_probe;
bool g_open[8] = {};   // unused spare
const float kBarH = 24.0f;

menubar::State MakeState(okay::editor::EditorState& ed, bool darkMode) {
    menubar::State s;
    s.playing = ed.isPlaying();
    s.inPrefabMode = ed.InPrefabMode();
    s.scenePath = ed.path();
    s.selectedCount = (int)ed.MultiSelection().size();
    if (s.selectedCount == 0 && ed.selected()) s.selectedCount = 1;
    s.objectCount = (int)ed.scene().Objects().size();
    s.canUndo = ed.CanUndo(); s.canRedo = ed.CanRedo();
    s.undoName = ed.UndoName(); s.redoName = ed.RedoName();
    s.darkMode = darkMode;
    return s;
}

std::string g_clipboard;   // the copied object's text (SceneSerializer), as main.cpp keeps it

bool SaveScene(okay::editor::EditorState& ed, std::string& err) {
    namespace fs = std::filesystem;
    if (ed.isPlaying()) { err = "Stop Play first: saving now would save the running game, not your scene."; return false; }
    std::string p = ed.path();
    if (p.empty()) {
        std::string name = ed.scene().Name();
        for (char& c : name) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) c = '_';
        if (name.empty() || name == "Scene") name = "Main";
        p = ((ed.projectDir().empty() ? fs::path("Scenes") : fs::path(ed.projectDir()) / "Assets" / "Scenes") / (name + ".okayscene")).string();
    }
    std::error_code ec;
    if (fs::path(p).has_parent_path()) fs::create_directories(fs::path(p).parent_path(), ec);
    if (!ed.Save(p, &err)) return false;
    ConsoleLog("Saved scene: " + p);
    return true;
}

bool* FlagFor(HostModeState* st, const std::string& id, bool& scratch) {
    static const struct { const char* id; const char* panel; } map[] = {
        {"panel.hierarchy", kPanelHierarchy}, {"panel.inspector", kPanelInspector}, {"panel.project", kPanelProject}, {"panel.console", kPanelConsole},
        {"panel.game", kPanelGame}, {"panel.scenemanager", kPanelSceneManager}, {"panel.audiomixer", kPanelMixer}};
    for (const auto& m : map) if (id == m.id) return st ? &st->OpenRef(m.panel) : &scratch;
    return nullptr;   // not a panel window mode has: drawn greyed out
}
}   // namespace

float MenuBarHeight() { return kBarH; }
const MenuProbe& GetMenuProbe() { return g_probe; }

bool RunMenuAction(okay::editor::EditorState& ed, HostModeState* st, const std::string& id) {
    g_probe.lastAction = id;
    auto prompt = [&](const char* doing, std::function<void()> act) { if (DialogService* d = CurrentDialogs()) d->RunAfterSavePrompt(doing, std::move(act), {}); else act(); };
    if (id == "file.new2d") prompt("starting a new scene", [&ed] { ed.NewScene2D(); ConsoleLog("New 2D scene"); });
    else if (id == "file.new3d") prompt("starting a new scene", [&ed] { ed.NewScene3D(); ConsoleLog("New 3D scene"); });
    else if (id == "file.save") { std::string err; if (!SaveScene(ed, err)) { if (DialogService* d = CurrentDialogs()) d->ShowError("The last save did not work", err + "\n\nYour changes are still open in the editor. Fix the problem and save again (Ctrl+S)."); else ConsoleLog("Couldn't save: " + err, 2); } }
    else if (id == "file.exit") { if (st) st->quit = true; }
    else if (id == "edit.undo" || id == "edit.redo") {
        const bool redo = id == "edit.redo";
        const std::string nm = redo ? ed.RedoName() : ed.UndoName();
        if (redo ? ed.Redo() : ed.Undo()) ConsoleLog(std::string(redo ? "Redo " : "Undo ") + nm);
        else ConsoleLog(std::string("Can't ") + (redo ? "redo " : "undo ") + nm + ": " + ed.UndoError(), 1);
    }
    else if (id == "edit.cut" && ed.selected()) { g_clipboard = okay::SceneSerializer::SerializeObject(*ed.selected()); ConsoleLog("Cut " + ed.selected()->name + " (Ctrl+V to paste)"); ed.DeleteSelected(); }
    else if (id == "edit.copy" && ed.selected()) { g_clipboard = okay::SceneSerializer::SerializeObject(*ed.selected()); ConsoleLog("Copied " + ed.selected()->name); }
    else if (id == "edit.paste" && !g_clipboard.empty()) {
        ed.PushUndo();
        if (okay::GameObject* go = okay::SceneSerializer::InstantiateFromText(ed.scene(), g_clipboard)) { go->transform->localPosition += okay::Vec3{0.5f, 0.5f, 0.0f}; ed.Select(go); ed.dirty = true; ConsoleLog("Pasted " + go->name); }
    }
    else if (id == "edit.duplicate" && ed.selected()) { ed.DuplicateSelected(); ConsoleLog("Duplicated selection"); }
    else if (id == "edit.delete" && ed.selected()) { ed.DeleteSelected(); ConsoleLog("Deleted selection"); }
    else if (id == "edit.selectall") { std::vector<okay::GameObject*> all; for (const auto& up : ed.scene().Objects()) all.push_back(up.get()); ed.Select(nullptr); for (okay::GameObject* g : all) ed.ToggleSelect(g); }
    else if (id == "edit.deselect") ed.Select(nullptr);
    else if (id == "go.empty") { ed.Select(ed.CreateEmpty()); ConsoleLog("Created empty GameObject"); }
    else if (id == "go.emptychild" && ed.selected()) {
        okay::GameObject* parent = ed.selected(); okay::GameObject* child = ed.CreateEmpty("Child");
        child->transform->SetParent(parent->transform, false); ed.Select(child); ed.dirty = true; ConsoleLog("Created an empty child of " + parent->name);
    }
    else if (id == "go.emptyparent" && ed.selected()) { if (ed.MultiSelection().size() > 1) ed.GroupSelected(); ConsoleLog("Created an empty parent"); }
    else if (id == "create.cube") ed.Select(ed.CreateCube());
    else if (id == "create.pyramid") ed.Select(ed.CreatePyramid());
    else if (id == "create.sprite") ed.Select(ed.CreateSprite());
    else if (id == "create.camera") ed.Select(ed.CreateCamera());
    else if (id == "view.frame" && ed.selected() && ed.selected()->transform) ed.camTarget = ed.selected()->transform->Position();
    else if (id.rfind("panel.", 0) == 0) { bool scratch = false; if (bool* f = FlagFor(st, id, scratch)) *f = !*f; else { ConsoleLog("Not available in window mode yet: " + id, 1); return false; } }
    else if (id == "help.shortcuts") ConsoleLog("Shortcuts: Ctrl+S save, Ctrl+Z undo, Ctrl+Y redo, Ctrl+X/C/V cut/copy/paste, Ctrl+D duplicate, Del delete, Ctrl+A select all, F frame the selection.");
    else { ConsoleLog("Not available in window mode yet: " + id, 1); return false; }
    return true;
}

std::string HandleShortcuts(okay::editor::EditorState& ed, HostModeState* st, const OkayUI::Input& in) {
    using namespace OkayUI;
    if (GetIO().WantTextInput || in.blocked) return std::string();
    struct K { KeyId key; bool ctrl, shift; const char* id; };
    static const K keys[] = {
        {KeyId_S, true, false, "file.save"}, {KeyId_Z, true, false, "edit.undo"}, {KeyId_Y, true, false, "edit.redo"},
        {KeyId_X, true, false, "edit.cut"}, {KeyId_C, true, false, "edit.copy"}, {KeyId_V, true, false, "edit.paste"},
        {KeyId_D, true, false, "edit.duplicate"}, {KeyId_A, true, false, "edit.selectall"}, {KeyId_Delete, false, false, "edit.delete"},
        {KeyId_F, false, false, "view.frame"}};
    for (const K& k : keys) {
        if (!IsKeyPressed(k.key, false)) continue;
        if (in.ctrl != k.ctrl) continue;
        if (in.shift != k.shift) continue;
        // the menu's own greying decides whether it applies: an action that does not apply is simply not run
        const std::string id = k.id;
        const menubar::State ms = MakeState(ed, true);
        const menubar::Entry* e = nullptr; std::vector<menubar::Entry> top;
        top = menubar::BuildMenu(id.rfind("file.", 0) == 0 ? "file" : id.rfind("view.", 0) == 0 ? "view" : "edit", ms);
        e = menubar::FindId(top, id);
        if (e && !e->enabled) continue;
        if (RunMenuAction(ed, st, id)) return id;
    }
    return std::string();
}

void AddMenuBar(HostModeState::Panels& panels, HostModeState* st, okay::editor::EditorState* ed) {
    if (!ed) return;
    panels.menu = [ed, st] {
        using namespace OkayUI;
        g_probe.itemsDrawn = 0;
        const Input& in = GetFrameInput();
        const std::string ran = HandleShortcuts(*ed, st, in);
        (void)ran;
        BareNextWindow();   // pinned at the top, no title bar or frame of its own: just the strip the menus live in
        if (Begin("##menubar", 0.0f, 0.0f, in.displayW, kBarH)) {
            const Vec2 bar0 = GetCursorScreenPos(); float barAcc = 0.0f;   // the bar lays its menus out left to right: each is its label's width + 16
            if (BeginMenuBar()) {
                menubar::Host host;
                bool scratch = false;
                host.flag = [st, &scratch](const menubar::Entry& e) { return FlagFor(st, e.id, scratch); };
                host.run = [ed, st](const menubar::Entry& e) { RunMenuAction(*ed, st, e.id); };
                host.slot = [ed, st](const menubar::Entry& e) {
                    auto item = [&](const char* label, const char* id) { if (MenuItem(label)) RunMenuAction(*ed, st, id); ++g_probe.itemsDrawn; };
                    if (e.id == "create.3d") { item("Cube", "create.cube"); item("Pyramid", "create.pyramid"); }
                    else if (e.id == "create.2d") item("Sprite", "create.sprite");
                    else TextDisabled("Not available in window mode yet.");
                };
                int i = 0;
                for (const menubar::Top& t : menubar::TopMenus()) {
                    const float bw = CalcTextSize(t.label.c_str()).x + 16.0f;
                    if (i < 6) g_probe.topMenu[i] = Rect{bar0.x + barAcc, bar0.y, bw, GetFrameHeight()};
                    barAcc += bw;
                    if (t.hostDrawn) { if (BeginMenu(t.label.c_str())) { TextDisabled("Plugins are not available in window mode yet."); OkayUI::EndMenu(); } ++i; continue; }
                    const bool open = BeginMenu(t.label.c_str());
                    ++i;
                    if (!open) continue;
                    menubar::DrawEntries(menubar::BuildMenu(t.id, MakeState(*ed, true)), host);
                    g_probe.itemsDrawn += (int)menubar::BuildMenu(t.id, MakeState(*ed, true)).size();
                    g_probe.lastItem = GetItemRect();
                    OkayUI::EndMenu();
                }
                EndMenuBar();
            }
        }
        End();
    };
}

} // namespace wm
} // namespace oked
