// The Script Editor app inside the editor (ScriptEditorAppHost.hpp): one shared ScriptIdeApp, fed with the editor's data through
// its Config hooks. No second script editor is written here; this file only connects the shared one to the editor.
#include "ScriptEditorAppHost.hpp"
#include "ScriptAssist.hpp"        // LanguageWordsFor, OkayScriptPalette: the editor's words, hover docs and colours (imgui.h before OkayUI's headers)
#include "ScriptIdeApp.hpp"
#include "OkayPanels.hpp"          // DrawAppWindowOkayUI: a whole OkayUI app in an editor window
#include "PluginRegistry.hpp"      // ScriptEditorPlugins, ScriptProblems: the editor plugins' lint
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "okay/Platform/Platform.hpp"   // PrefPath: the settings folder the Script Editor program uses too
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

namespace oked {

bool g_showScriptEditorApp = false;

namespace {
namespace fs = std::filesystem;

okay::editor::EditorState* g_ed = nullptr;   // the editor whose scene the hooks read (set every frame the window draws)

// A component's file, absolute ('/' separators); "" when it has none.
std::string FileOf(const okay::ScriptComponent& sc) {
    if (sc.Path().empty()) return std::string();
    fs::path p = fs::u8path(sc.Path());
    if (p.is_relative() && g_ed && !g_ed->projectDir().empty()) p = fs::u8path(g_ed->projectDir()) / p;
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(p, ec);
    return (ec ? p : a).generic_u8string();
}

// The document key of a component: its file when it has one (the file is the document, wherever it is attached), else
// "scene:<object guid>/<n>" (the n-th Script component of the object).
std::string KeyOf(const okay::GameObject& go, const okay::ScriptComponent& sc, int n) {
    const std::string f = FileOf(sc);
    return f.empty() ? "scene:" + go.Guid() + "/" + std::to_string(n) : f;
}

template <class Fn> void ForEachScript(Fn fn) {   // fn(object, component, n); stops when fn returns true
    if (!g_ed) return;
    for (const auto& up : g_ed->scene().Objects()) {
        int n = 0;
        for (okay::ScriptComponent* sc : up->GetComponents<okay::ScriptComponent>())
            if (fn(*up, *sc, n++)) return;
    }
}

std::string ReadAll(const std::string& path, bool& ok) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    ok = (bool)f;
    return ok ? std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) : std::string();
}

// The document of a key: a file (and the components using it follow its saves: cfg.saved), or a scene-only script whose save
// writes it to Assets/Scripts/<object name>.<ext> and gives the component that file.
bool DocumentFor(const std::string& key, scriptide::DocumentSpec& out) {
    bool found = false;
    ForEachScript([&](okay::GameObject& go, okay::ScriptComponent& sc, int n) {
        if (KeyOf(go, sc, n) != key) return false;
        found = true;
        out = scriptide::DocumentSpec();
        out.key = key;
        out.language = sc.Language();
        const std::string file = FileOf(sc);
        if (!file.empty()) {
            bool ok = false;
            const std::string text = ReadAll(file, ok);
            out.path = file;
            out.text = ok ? text : sc.Source();
            return true;
        }
        out.label = go.name + " (no file)";
        out.text = sc.Source();
        const std::string guid = go.Guid();
        out.save = [guid, n](const std::string& text, std::string& why) {
            bool done = false;
            ForEachScript([&](okay::GameObject& o, okay::ScriptComponent& c, int i) {
                if (o.Guid() != guid || i != n) return false;
                const std::string path = scriptide::DefaultScriptPath(g_ed ? g_ed->projectDir() : std::string(),
                                                                      scriptide::ScriptStem(o.name), c.Language());
                std::error_code ec;
                fs::create_directories(fs::u8path(path).parent_path(), ec);
                std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
                f << text;
                if (!f) { why = "cannot write " + path; return true; }
                c.SetPath(path);
                c.SetSource(text);
                if (g_ed) g_ed->dirty = true;
                ConsoleLog("[Script Editor] saved " + o.name + "'s script as " + path);
                done = true;
                return true;
            });
            if (!done && why.empty()) why = "the object is not in the scene any more";
            return done;
        };
        return true;
    });
    return found;
}

std::vector<scriptide::SceneScript> SceneScripts() {
    std::vector<scriptide::SceneScript> out;
    ForEachScript([&](okay::GameObject& go, okay::ScriptComponent& sc, int n) {
        scriptide::SceneScript s;
        s.key = KeyOf(go, sc, n);
        s.path = FileOf(sc);
        s.object = go.name;
        s.label = s.path.empty() ? std::string("(no file)") : fs::u8path(s.path).filename().u8string();
        out.push_back(std::move(s));
        return out.size() >= 500;
    });
    return out;
}

scriptide::ScriptIdeApp& App() {
    static std::unique_ptr<scriptide::ScriptIdeApp> app;
    if (!app) {
        scriptide::ScriptIdeApp::Config cfg;
        if (okay::platform::Backend* be = okay::platform::Default()) {   // the program's settings file: one view for both hosts
            const std::string data = be->PrefPath("OkaySpace", "ScriptEditor");
            if (!data.empty()) cfg.settingsPath = data + "script_editor.json";
        }
        cfg.idPrefix = "##editor_script_editor_app";
        cfg.wordsFor = [](const std::string& language) { return &LanguageWordsFor(language); };
        cfg.palette = &OkayScriptPalette();
        cfg.moreProblems = [](const std::string& language, const std::string& path, const std::string& text,
                              std::vector<scriptide::Diagnostic> own) {
            pluginreg::DocContext ctx;
            ctx.language = language; ctx.path = path; ctx.text = text;
            return pluginreg::ScriptProblems(pluginreg::ScriptEditorPlugins(), ctx, std::move(own));
        };
        cfg.hostName = "Scene Script Editor";
        cfg.showInHost = [](const std::string& path, int line) { OpenScriptFileInEditorAt(path, line); };
        cfg.saved = [](const std::string& path, const std::string& text) {
            // every component using this file runs the saved text
            ForEachScript([&](okay::GameObject&, okay::ScriptComponent& sc, int) {
                if (FileOf(sc) == path) { sc.SetSource(text); if (g_ed) g_ed->dirty = true; }
                return false;
            });
            ConsoleLog("[Script Editor] saved " + path);
        };
        cfg.sceneScripts = [] { return SceneScripts(); };
        cfg.documentFor = [](const std::string& key, scriptide::DocumentSpec& out) { return DocumentFor(key, out); };
        cfg.languageChanged = [](const std::string& key, const std::string& language) {
            ForEachScript([&](okay::GameObject& go, okay::ScriptComponent& sc, int n) {
                if (KeyOf(go, sc, n) == key) { sc.SetLanguage(language); if (g_ed) g_ed->dirty = true; }
                return false;
            });
        };
        cfg.attachTarget = [] { return g_ed && g_ed->selected() ? g_ed->selected()->name : std::string(); };
        cfg.attachScript = [](const std::string& path, const std::string&, std::string& key) {
            okay::GameObject* go = g_ed ? g_ed->selected() : nullptr;
            if (!go || !AttachScriptAsset(*g_ed, go, path)) return false;
            key = path;   // the file is the document
            return true;
        };
        cfg.commands = [] {   // the editor plugins' commands
            std::vector<scriptide::AppCommand> out;
            for (const pluginreg::Command* c : pluginreg::ScriptEditorPlugins().Commands()) {
                if (!c || !c->run) continue;
                out.push_back(scriptide::AppCommand{(c->category.empty() ? std::string() : c->category + ": ") + c->title, c->shortcut, c->run});
            }
            return out;
        };
        app.reset(new scriptide::ScriptIdeApp(cfg));
    }
    return *app;
}
} // namespace

void DrawScriptEditorApp(okay::editor::EditorState& ed) {
    if (!g_showScriptEditorApp) return;
    g_ed = &ed;
    scriptide::ScriptIdeApp& app = App();
    // selecting an object (or one that just gained a script) opens its scripts and shows the first
    static okay::GameObject* lastSel = nullptr;
    static std::size_t lastCount = 0;
    okay::GameObject* sel = ed.selected();
    const std::size_t count = sel ? sel->GetComponents<okay::ScriptComponent>().size() : 0;
    if (sel && count > 0 && (sel != lastSel || count != lastCount)) {
        std::vector<scriptide::DocumentSpec> specs;
        int n = 0;
        for (okay::ScriptComponent* sc : sel->GetComponents<okay::ScriptComponent>()) {
            scriptide::DocumentSpec spec;
            if (DocumentFor(KeyOf(*sel, *sc, n++), spec)) specs.push_back(std::move(spec));
        }
        app.ShowDocuments(specs);
    }
    lastSel = sel;
    lastCount = count;
    // the open project is the app's folder (a project opened later moves it along; a folder opened in the app stays until then)
    static std::string followed;
    if (ed.projectDir() != followed) {
        followed = ed.projectDir();
        if (!followed.empty()) app.OpenFolder(followed);
    }
    DrawAppWindowOkayUI("Script Editor (Files)", &g_showScriptEditorApp, "##okayui_script_editor_app",
                        [&app](float x, float y, float w, float h) { app.Frame(x, y, w, h); }, 980.0f, 640.0f);
}

} // namespace oked
