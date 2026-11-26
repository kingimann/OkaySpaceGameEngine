// Editor plugins (PluginsWindow.hpp): OkayScript editor extensions, their editor_* builtins, the Plugins menu and the
// Plugin Manager window. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorCommon.hpp"
#include "PluginsWindow.hpp"
#include "MainShared.hpp"          // g_showPluginManager, OkayExeDir, SectionHeader, OpenScriptFileInEditorAt, extide (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "OkayPanels.hpp"          // DrawPluginManagerOkayUI
#include "okay/Platform/Platform.hpp"   // okay::platform
#include "PluginScriptApi.hpp"      // the script_* calls: what a plugin adds to the Script Editor (PluginRegistry)

namespace oked {

// ---- Editor plugins ------------------------------------------------------------
// User-made editor extensions written in OkayScript. A plugin is a .okay file in
// <project>/EditorPlugins/ (that project only) or <editor>/plugins/ (every
// project). Header comments describe it:
//     // @name Align Tools
//     // @description Line up the selection.
//     // @window            (give it a dockable panel that calls on_gui())
// Hooks (all optional): start() once after loading, on_gui() every frame while
// its window is open (draw with ui_text / ui_button / ui_slider / ui_checkbox /
// ui_sameline / ui_separator / ui_progress plus editor_input / editor_heading /
// editor_hint), on_menu() when
// its Plugins-menu entry is clicked (plugins without a window), on_selection()
// when the selection changes and on_tick() every editor frame.
// The editor_* builtins below give plugins the scene, selection, undo and console.
// Files are watched: saving a plugin reloads it.
struct EditorPlugin {
    std::string path, name, description, error;
    bool user = false;          // from <editor>/plugins (all projects) vs the project
    bool window = false;        // declares @window (or defines on_gui)
    bool hasMenu = false;       // defines on_menu() — a one-shot command
    bool open = false;          // its window is showing
    bool enabled = true;
    std::filesystem::file_time_type mtime{};
    std::unique_ptr<okay::OkayScriptVM> vm;
    okay::ScriptHost host;
};
static std::vector<std::unique_ptr<EditorPlugin>> g_plugins;
static std::set<std::string> g_pluginsDisabled;     // paths turned off in the manager (persisted)
static std::string g_pluginsProject = "\x01";       // project the list was scanned for
static EditorState* g_pluginEd = nullptr;           // the editor a running plugin acts on

// ImGui-backed ui_* bridge, swapped in only while a plugin's on_gui() runs.
struct PluginUIBridge : okay::ScriptUIBridge {
    int n = 0;   // per-call widget ids so identical labels don't collide
    std::string id(const char* label) { return std::string(label) + "##pw" + std::to_string(n++); }
    void Text(const char* s) override { ImGui::TextWrapped("%s", s); }
    bool Button(const char* label) override { return ImGui::Button(id(label).c_str()); }
    bool Checkbox(const char* label, bool v) override { ImGui::Checkbox(id(label).c_str(), &v); return v; }
    float Slider(const char* label, float v, float lo, float hi) override {
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize(label).x - 12.0f);
        ImGui::SliderFloat(id(label).c_str(), &v, lo, hi); return v;
    }
    void ProgressBar(float t) override { ImGui::ProgressBar(t, ImVec2(-1, 0)); }
    void SameLine() override { ImGui::SameLine(); }
    void Separator() override { ImGui::Separator(); }
};
static PluginUIBridge g_pluginUI;

static GameObject* PluginFind(const std::string& name) {
    if (!g_pluginEd) return nullptr;
    if (name.empty()) return g_pluginEd->selected();
    for (const auto& up : g_pluginEd->scene().Objects()) if (up->name == name) return up.get();
    return nullptr;
}
// The editor_* API a plugin can call. `nm` = the object's name ("" = selection).
static void RegisterPluginBuiltins(okay::OkayScriptVM& vm) {
    using V = okay::vs::VsValue;
    auto str = [](std::vector<V>& a, std::size_t i) { return i < a.size() ? a[i].AsString() : std::string(); };
    auto num = [](std::vector<V>& a, std::size_t i, float d = 0.0f) { return i < a.size() ? a[i].AsFloat() : d; };
    auto ed = [] { return g_pluginEd; };
    vm.AddBuiltin("editor_log",   [str](std::vector<V>& a) { ConsoleLog("[plugin] " + str(a, 0)); return V{}; });
    vm.AddBuiltin("editor_warn",  [str](std::vector<V>& a) { ConsoleLog("[plugin] " + str(a, 0), 1); return V{}; });
    vm.AddBuiltin("editor_error", [str](std::vector<V>& a) { ConsoleLog("[plugin] " + str(a, 0), 2); return V{}; });
    vm.AddBuiltin("editor_selected", [](std::vector<V>&) { GameObject* g = PluginFind(""); return V{g ? g->name : std::string()}; });
    vm.AddBuiltin("editor_selection_count", [ed](std::vector<V>&) { return V{ed() ? (float)ed()->MultiSelection().size() : 0.0f}; });
    vm.AddBuiltin("editor_selection_name", [ed, num](std::vector<V>& a) {
        int i = (int)num(a, 0); if (!ed()) return V{std::string()};
        const auto& m = ed()->MultiSelection();
        return V{i >= 0 && i < (int)m.size() && m[i] ? m[i]->name : std::string()};
    });
    vm.AddBuiltin("editor_select", [ed, str](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (g && ed()) ed()->Select(g); return V{g ? 1.0f : 0.0f};
    });
    vm.AddBuiltin("editor_object_count", [ed](std::vector<V>&) { return V{ed() ? (float)ed()->scene().Objects().size() : 0.0f}; });
    vm.AddBuiltin("editor_object_name", [ed, num](std::vector<V>& a) {
        int i = (int)num(a, 0); if (!ed()) return V{std::string()};
        const auto& o = ed()->scene().Objects();
        return V{i >= 0 && i < (int)o.size() ? o[i]->name : std::string()};
    });
    vm.AddBuiltin("editor_exists", [str](std::vector<V>& a) { return V{PluginFind(str(a, 0)) ? 1.0f : 0.0f}; });
    vm.AddBuiltin("editor_create", [ed, str](std::vector<V>& a) {
        if (!ed()) return V{std::string()};
        std::string kind = str(a, 0), name = str(a, 1);
        for (auto& c : kind) c = (char)std::tolower((unsigned char)c);
        ed()->PushUndo();
        GameObject* prevSel = ed()->selected();   // creating shouldn't steal the user's selection
        std::vector<GameObject*> prevMulti = ed()->MultiSelection();
        GameObject* g = nullptr;
        if (kind == "camera") g = ed()->CreateCamera();
        else if (kind == "sprite") g = ed()->CreateSprite();
        else if (kind == "cube" || kind == "sphere" || kind == "plane" || kind == "cylinder" || kind == "capsule" || kind == "cone" || kind == "pyramid") {
            std::string m = kind; m[0] = (char)std::toupper((unsigned char)m[0]); g = ed()->CreateMesh(m);
        } else if (kind == "light") { g = ed()->CreateEmpty("Light"); g->AddComponent<Light>(); }
        else g = ed()->CreateEmpty();
        if (g && !name.empty()) g->name = name;
        ed()->Select(nullptr);
        for (GameObject* m : prevMulti) if (m != prevSel) ed()->ToggleSelect(m);
        if (prevSel) ed()->ToggleSelect(prevSel);
        ed()->dirty = true;
        return V{g ? g->name : std::string()};
    });
    vm.AddBuiltin("editor_delete", [ed, str](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g || !ed()) return V{0.0f};
        ed()->PushUndo(); ed()->Deselect(g); ed()->scene().Destroy(g); ed()->scene().FlushDestroyed(); ed()->dirty = true;
        return V{1.0f};
    });
    vm.AddBuiltin("editor_rename", [ed, str](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g) return V{0.0f};
        g->name = str(a, 1); if (ed()) ed()->dirty = true; return V{1.0f};
    });
    vm.AddBuiltin("editor_set_active", [ed, str, num](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g) return V{0.0f};
        g->active = num(a, 1, 1.0f) != 0.0f; if (ed()) ed()->dirty = true; return V{1.0f};
    });
    vm.AddBuiltin("editor_set_parent", [ed, str](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); GameObject* p = str(a, 1).empty() ? nullptr : PluginFind(str(a, 1));
        if (!g || !g->transform) return V{0.0f};
        g->transform->SetParent(p ? p->transform : nullptr, true); if (ed()) ed()->dirty = true; return V{1.0f};
    });
    // Transform get/set: (name, axis "x"/"y"/"z") and (name, x, y, z).
    auto axis = [](const Vec3& v, const std::string& ax) { return ax == "y" ? v.y : ax == "z" ? v.z : v.x; };
    vm.AddBuiltin("editor_get_pos", [str, axis](std::vector<V>& a) { GameObject* g = PluginFind(str(a, 0)); return V{g ? axis(g->transform->localPosition, str(a, 1)) : 0.0f}; });
    vm.AddBuiltin("editor_get_rot", [str, axis](std::vector<V>& a) { GameObject* g = PluginFind(str(a, 0)); return V{g ? axis(g->transform->localRotation.ToEuler(), str(a, 1)) : 0.0f}; });
    vm.AddBuiltin("editor_get_scale", [str, axis](std::vector<V>& a) { GameObject* g = PluginFind(str(a, 0)); return V{g ? axis(g->transform->localScale, str(a, 1)) : 0.0f}; });
    vm.AddBuiltin("editor_set_pos", [ed, str, num](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g) return V{0.0f};
        g->transform->localPosition = {num(a, 1), num(a, 2), num(a, 3)}; if (ed()) ed()->dirty = true; return V{1.0f};
    });
    vm.AddBuiltin("editor_set_rot", [ed, str, num](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g) return V{0.0f};
        g->transform->localRotation = Quat::Euler(num(a, 1), num(a, 2), num(a, 3)); if (ed()) ed()->dirty = true; return V{1.0f};
    });
    vm.AddBuiltin("editor_set_scale", [ed, str, num](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g) return V{0.0f};
        g->transform->localScale = {num(a, 1, 1.0f), num(a, 2, 1.0f), num(a, 3, 1.0f)}; if (ed()) ed()->dirty = true; return V{1.0f};
    });
    vm.AddBuiltin("editor_undo_point", [ed](std::vector<V>&) { if (ed()) ed()->PushUndo(); return V{}; });
    vm.AddBuiltin("editor_frame", [ed, str](std::vector<V>& a) {
        GameObject* g = PluginFind(str(a, 0)); if (!g || !ed()) return V{0.0f};
        Vec3 p = g->transform->Position(); ed()->camTarget = p; ed()->cameraPos = {p.x, p.y}; return V{1.0f};
    });
    vm.AddBuiltin("editor_scene_name", [ed](std::vector<V>&) { return V{ed() ? ed()->scene().Name() : std::string()}; });
    vm.AddBuiltin("editor_project_dir", [ed](std::vector<V>&) { return V{ed() ? ed()->projectDir() : std::string()}; });
    vm.AddBuiltin("editor_is_playing", [ed](std::vector<V>&) { return V{ed() && ed()->isPlaying() ? 1.0f : 0.0f}; });
    // Extra widgets for on_gui(): a text field and a colour picker (returns "r,g,b").
    vm.AddBuiltin("editor_input", [str](std::vector<V>& a) {
        static std::unordered_map<std::string, std::string> bufs;
        std::string label = str(a, 0), val = str(a, 1);
        char b[256]; std::snprintf(b, sizeof(b), "%s", val.c_str());
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize(label.c_str()).x - 12.0f);
        ImGui::InputText(g_pluginUI.id(label.c_str()).c_str(), b, sizeof(b));
        return V{std::string(b)};
    });
    vm.AddBuiltin("editor_heading", [str](std::vector<V>& a) { SectionHeader(str(a, 0).c_str()); return V{}; });
    vm.AddBuiltin("editor_hint", [str](std::vector<V>& a) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", str(a, 0).c_str()); ImGui::PopStyleColor(); return V{};
    });
}
static std::string PluginUserDir() { return (std::filesystem::path(OkayExeDir()) / "plugins").string(); }
static std::string PluginProjectDir(EditorState& ed) {
    return ed.projectDir().empty() ? std::string() : (std::filesystem::path(ed.projectDir()) / "EditorPlugins").string();
}
static void LoadPlugin(EditorPlugin& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    p.mtime = fs::last_write_time(p.path, ec);
    p.error.clear(); p.vm.reset(); p.window = false; p.hasMenu = false;
    p.name = fs::path(p.path).stem().string(); p.description.clear();
    std::string src = extide::ReadFile(p.path);
    // Header metadata from leading // comments.
    std::istringstream ss(src); std::string line;
    while (std::getline(ss, line)) {
        std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        if (line.compare(b, 2, "//") != 0) break;
        std::string t = line.substr(b + 2);
        auto tag = [&](const char* k, std::string& out) {
            std::size_t at = t.find(k); if (at == std::string::npos) return false;
            out = t.substr(at + std::strlen(k));
            while (!out.empty() && (out[0] == ' ' || out[0] == ':')) out.erase(0, 1);
            while (!out.empty() && (out.back() == ' ' || out.back() == '\r')) out.pop_back();
            return true;
        };
        std::string v;
        if (tag("@name", v) && !v.empty()) p.name = v;
        else if (tag("@description", v)) p.description = v;
        else if (t.find("@window") != std::string::npos) p.window = true;
    }
    if (src.find("on_gui") != std::string::npos) p.window = true;
    // What the old VM added to the Script Editor goes with it (a reload adds it again; a plugin turned off adds nothing).
    pluginreg::ScriptEditorPlugins().RemoveOwner(p.path);
    if (!p.enabled) return;
    p.vm = std::make_unique<okay::OkayScriptVM>();
    RegisterPluginBuiltins(*p.vm);
    RegisterScriptEditorApi(*p.vm, pluginreg::ScriptEditorPlugins(), p.path, p.name);
    p.vm->Bind(&p.host);
    std::string err;
    if (!p.vm->Load(src, &err)) {
        p.error = err.empty() ? "failed to load" : err;
        pluginreg::ScriptEditorPlugins().RemoveOwner(p.path);   // nothing may keep pointing at the VM about to go
        p.vm.reset(); ConsoleLog("Plugin '" + p.name + "': " + p.error, 2); return;
    }
    p.hasMenu = p.vm->HasEvent("on_menu");
    p.vm->CallStart();
}
// Rescan both plugin folders: add new files, drop deleted ones, reload edited ones.
static void ScanPlugins(EditorState& ed, bool force = false) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::pair<std::string, bool>> files;
    for (int pass = 0; pass < 2; ++pass) {
        std::string dir = pass == 0 ? PluginUserDir() : PluginProjectDir(ed);
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        for (const auto& de : fs::directory_iterator(dir, ec))
            if (de.is_regular_file(ec) && de.path().extension() == ".okay") files.push_back({de.path().string(), pass == 0});
    }
    std::sort(files.begin(), files.end());
    g_pluginsProject = ed.projectDir();
    // Drop plugins whose file is gone.
    g_plugins.erase(std::remove_if(g_plugins.begin(), g_plugins.end(), [&](const std::unique_ptr<EditorPlugin>& p) {
        const bool gone = std::none_of(files.begin(), files.end(), [&](const auto& f) { return f.first == p->path; });
        if (gone) pluginreg::ScriptEditorPlugins().RemoveOwner(p->path);
        return gone;
    }), g_plugins.end());
    g_pluginEd = &ed;
    for (const auto& f : files) {
        EditorPlugin* p = nullptr;
        for (auto& up : g_plugins) if (up->path == f.first) p = up.get();
        if (!p) {
            g_plugins.push_back(std::make_unique<EditorPlugin>());
            p = g_plugins.back().get(); p->path = f.first; p->user = f.second;
            p->enabled = !g_pluginsDisabled.count(f.first);
            LoadPlugin(*p);
            if (p->vm) ConsoleLog("Loaded editor plugin: " + p->name);
            continue;
        }
        auto mt = fs::last_write_time(p->path, ec);
        if (force || (!ec && mt != p->mtime)) {
            bool wasOpen = p->open;
            LoadPlugin(*p);
            p->open = wasOpen && p->window;
            if (p->vm) ConsoleLog("Reloaded editor plugin: " + p->name);
        }
    }
}
void SavePluginPrefs() {
    std::ofstream f(std::filesystem::path(OkayExeDir()) / "plugins_disabled.txt");
    for (const auto& s : g_pluginsDisabled) f << s << "\n";
}
static void LoadPluginPrefs() {
    std::ifstream f(std::filesystem::path(OkayExeDir()) / "plugins_disabled.txt");
    std::string l; while (std::getline(f, l)) if (!l.empty()) g_pluginsDisabled.insert(l);
}
// Create a starter plugin in the project (or user folder) and open it in the Script Editor.
void NewPluginFromTemplate(EditorState& ed) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string dir = PluginProjectDir(ed);
    if (dir.empty()) dir = PluginUserDir();
    fs::create_directories(dir, ec);
    fs::path p;
    for (int i = 1; ; ++i) { p = fs::path(dir) / (i == 1 ? "MyPlugin.okay" : "MyPlugin" + std::to_string(i) + ".okay"); if (!fs::exists(p, ec)) break; }
    extide::WriteFile(p.string(),
        "// @name My Plugin\n"
        "// @description Scatter copies of the selected object.\n"
        "// @window\n"
        "\n"
        "count = 5\n"
        "spread = 4\n"
        "\n"
        "on_gui() {\n"
        "    sel = editor_selected()\n"
        "    if (sel == \"\") { editor_hint(\"Select an object in the Hierarchy.\") return }\n"
        "    ui_text(\"Selected: \" + sel)\n"
        "    count = ui_slider(\"Copies\", count, 1, 50)\n"
        "    spread = ui_slider(\"Spread\", spread, 0.5, 20)\n"
        "    if (ui_button(\"Scatter\")) scatter(sel)\n"
        "}\n"
        "\n"
        "scatter(name) {\n"
        "    editor_undo_point()\n"
        "    for (i = 0; i < count; i = i + 1) {\n"
        "        n = editor_create(\"cube\", name + \" \" + (i + 1))\n"
        "        editor_set_pos(n, editor_get_pos(name, \"x\") + random_range(-spread, spread),\n"
        "                          editor_get_pos(name, \"y\"),\n"
        "                          editor_get_pos(name, \"z\") + random_range(-spread, spread))\n"
        "    }\n"
        "    editor_log(\"Scattered \" + count + \" copies\")\n"
        "}\n");
    ScanPlugins(ed);
    for (auto& up : g_plugins) if (up->path == p.string()) up->open = true;
    OpenScriptFileInEditorAt(p.string(), 1);
    ConsoleLog("Created plugin " + p.string());
}
// Per frame: watch for changes, fire on_tick / on_selection, draw plugin windows.
static void EnsurePluginPrefs() {
    static bool prefs = false;
    if (!prefs) { LoadPluginPrefs(); prefs = true; }
}
static void SetPluginEnabled(EditorPlugin& p, bool on) {
    p.enabled = on;
    if (on) g_pluginsDisabled.erase(p.path);
    else { g_pluginsDisabled.insert(p.path); p.open = false; }
    SavePluginPrefs();
    LoadPlugin(p);
}
void TickPlugins(EditorState& ed) {
    EnsurePluginPrefs();
    static Uint32 nextScan = 0;
    if (g_pluginsProject != ed.projectDir() || okay::platform::TicksMs32() >= nextScan) { ScanPlugins(ed); nextScan = okay::platform::TicksMs32() + 1000; }
    g_pluginEd = &ed;
    static GameObject* lastSel = nullptr;
    const bool selChanged = ed.selected() != lastSel;
    lastSel = ed.selected();
    okay::ScriptUIBridge* prevUI = okay::GetScriptUI();
    for (auto& up : g_plugins) {
        EditorPlugin& p = *up;
        if (!p.vm) continue;
        if (selChanged) p.vm->CallEvent("on_selection");
        p.vm->CallEvent("on_tick");
        if (!p.window || !p.open) continue;
        {   // first appearance: float near the middle of the main window, cascaded per plugin
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            float off = 28.0f * (float)(&up - &g_plugins[0]);
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.55f + off, vp->WorkPos.y + vp->WorkSize.y * 0.18f + off), ImGuiCond_FirstUseEver);
        }
        ImGui::SetNextWindowSize(ImVec2(320, 260), ImGuiCond_FirstUseEver);
        std::string title = p.name + "###plugin:" + p.path;
        if (ImGui::Begin(title.c_str(), &p.open)) {
            g_pluginUI.n = 0;
            okay::SetScriptUI(&g_pluginUI);
            ImGui::PushID(p.path.c_str());
            p.vm->CallEvent("on_gui");
            ImGui::PopID();
            okay::SetScriptUI(prevUI);
        }
        ImGui::End();
    }
}
void DrawPluginPanelBody(const std::function<void()>& draw) {
    if (!draw) return;
    okay::ScriptUIBridge* prevUI = okay::GetScriptUI();
    g_pluginUI.n = 0;
    okay::SetScriptUI(&g_pluginUI);
    draw();
    okay::SetScriptUI(prevUI);
}
static void PluginMenuTip(const EditorPlugin& p) {
    if (!ImGui::IsItemHovered()) return;
    if (!p.error.empty() && !p.description.empty()) ImGui::SetTooltip("%s\n%s", p.description.c_str(), p.error.c_str());
    else if (!p.error.empty()) ImGui::SetTooltip("%s", p.error.c_str());
    else if (!p.description.empty()) ImGui::SetTooltip("%s", p.description.c_str());
}
static bool PluginMenuMatch(const EditorPlugin& p, const char* filter) {
    if (!filter || !filter[0]) return true;
    auto fold = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    const std::string f = fold(filter);
    auto has = [&](const std::string& s) { return fold(s).find(f) != std::string::npos; };
    return has(p.name) || has(p.description) || has(std::filesystem::path(p.path).stem().string());
}
static void RevealPluginDir(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    extide::RevealInFiles(dir);
}
void DrawPluginsMenu(EditorState& ed) {
    if (!ImGui::BeginMenu("Plugins")) return;
    // The menu bar runs before the per-frame scan, so the first open of a
    // session (and the first open after switching projects) would otherwise
    // list nothing until the next frame.
    EnsurePluginPrefs();
    if (g_pluginsProject != ed.projectDir()) ScanPlugins(ed);

    static char filter[64] = "";
    if (ImGui::IsWindowAppearing()) filter[0] = '\0';
    const bool many = g_plugins.size() > 8;
    if (many) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##pluginfilter", "Filter", filter, sizeof(filter));
        ImGui::Separator();
    }

    int projectN = 0, userN = 0, enabledN = 0, liveN = 0, disabledN = 0;
    for (const auto& up : g_plugins) {
        if (!up->enabled) { ++disabledN; continue; }
        ++liveN;
        if (!PluginMenuMatch(*up, filter)) continue;
        ++enabledN;
        if (up->user) ++userN; else ++projectN;
    }
    const bool grouped = projectN > 0 && userN > 0;
    bool drewGroup = false;

    auto drawOne = [&](EditorPlugin& p, const std::string& label) {
        if (!p.vm) {
            // Click opens the script; the tooltip is the load error.
            if (ImGui::MenuItem((label + "  (error)").c_str())) OpenScriptFileInEditorAt(p.path, 1);
            PluginMenuTip(p);
            return;
        }
        if (p.window && p.hasMenu) {
            if (ImGui::BeginMenu(label.c_str())) {
                if (ImGui::MenuItem("Panel", nullptr, p.open)) p.open = !p.open;
                if (ImGui::MenuItem("Run")) { g_pluginEd = &ed; p.vm->CallEvent("on_menu"); }
                if (ImGui::MenuItem("Edit Script")) OpenScriptFileInEditorAt(p.path, 1);
                ImGui::EndMenu();
            }
            PluginMenuTip(p);
            return;
        }
        if (p.window) {
            if (ImGui::MenuItem(label.c_str(), nullptr, p.open)) p.open = !p.open;
        } else if (ImGui::MenuItem(label.c_str())) {
            g_pluginEd = &ed;
            p.vm->CallEvent("on_menu");
        }
        PluginMenuTip(p);
    };
    auto drawGroup = [&](const char* heading, bool userGroup) {
        std::vector<EditorPlugin*> items;
        for (auto& up : g_plugins) {
            if (!up->enabled || up->user != userGroup || !PluginMenuMatch(*up, filter)) continue;
            items.push_back(up.get());
        }
        if (items.empty()) return;
        if (grouped) {
            if (drewGroup) ImGui::Separator();
            ImGui::TextDisabled("%s", heading);
        }
        drewGroup = true;
        std::sort(items.begin(), items.end(), [](const EditorPlugin* a, const EditorPlugin* b) {
            auto fold = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
            return fold(a->name) < fold(b->name);
        });
        for (EditorPlugin* p : items) {
            int same = 0;
            for (EditorPlugin* q : items) if (q->name == p->name) ++same;
            std::string label = p->name;
            if (same > 1) label += " (" + std::filesystem::path(p->path).stem().string() + ")";
            drawOne(*p, label);
        }
    };
    drawGroup("This project", false);
    drawGroup("All projects", true);

    if (liveN == 0 && disabledN == 0) ImGui::TextDisabled("No plugins yet");
    else if (liveN == 0) ImGui::TextDisabled("Every plugin is turned off");
    else if (enabledN == 0) ImGui::TextDisabled("Nothing matches.");

    if (disabledN > 0 && enabledN > 0) ImGui::Separator();
    if (disabledN > 0 && ImGui::BeginMenu("Turned off")) {
        std::vector<EditorPlugin*> off;
        for (auto& up : g_plugins) if (!up->enabled) off.push_back(up.get());
        std::sort(off.begin(), off.end(), [](const EditorPlugin* a, const EditorPlugin* b) {
            auto fold = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
            return fold(a->name) < fold(b->name);
        });
        for (EditorPlugin* p : off) {
            std::string label = p->name + (p->user ? "  (all projects)" : "");
            if (ImGui::MenuItem(label.c_str())) SetPluginEnabled(*p, true);
            PluginMenuTip(*p);
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("New Plugin...")) NewPluginFromTemplate(ed);
    if (ImGui::MenuItem("Plugin Manager...")) g_showPluginManager = true;
    if (ImGui::MenuItem("Reload All")) ScanPlugins(ed, true);
    if (ImGui::BeginMenu("Open Folder")) {
        const std::string proj = PluginProjectDir(ed);
        if (ImGui::MenuItem("Project Plugins", nullptr, false, !proj.empty())) RevealPluginDir(proj);
        if (ImGui::MenuItem("Editor Plugins")) RevealPluginDir(PluginUserDir());
        ImGui::EndMenu();
    }
    ImGui::EndMenu();
}
// The Plugin Manager: OkayUI draws the window (DrawPluginManagerOkayUI, no Dear ImGui version); what the person clicked is done here.
void DrawPluginManager(EditorState& ed) {
    if (!g_showPluginManager) return;
    okaypanels::PluginManagerModel model;
    model.rows.reserve(g_plugins.size());
    for (const auto& up : g_plugins) {
        const EditorPlugin& p = *up;
        okaypanels::PluginRow r;
        r.name = p.name; r.description = p.description; r.error = p.error;
        r.enabled = p.enabled; r.user = p.user; r.hasWindow = p.window && p.vm; r.windowOpen = p.open;
        model.rows.push_back(std::move(r));
    }
    okaypanels::PluginManagerIntent intent;
    DrawPluginManagerOkayUI(&g_showPluginManager, model, intent);
    if (intent.newPlugin) NewPluginFromTemplate(ed);
    if (intent.reloadAll) ScanPlugins(ed, true);
    if (intent.openFolder) {
        std::string d = PluginProjectDir(ed); if (d.empty()) d = PluginUserDir();
        std::error_code ec; std::filesystem::create_directories(d, ec);
        extide::RevealInFiles(d);
    }
    // the rows' buttons, only while the list is the one drawn (New Plugin and Reload All above may have changed it)
    if (intent.newPlugin || intent.reloadAll || model.rows.size() != g_plugins.size()) return;
    auto at = [&](int i) -> EditorPlugin* { return i >= 0 && (std::size_t)i < g_plugins.size() ? g_plugins[(std::size_t)i].get() : nullptr; };
    if (EditorPlugin* p = at(intent.toggle)) SetPluginEnabled(*p, !p->enabled);
    if (EditorPlugin* p = at(intent.edit)) OpenScriptFileInEditorAt(p->path, 1);
    if (EditorPlugin* p = at(intent.openClose)) p->open = !p->open;
}

} // namespace oked
