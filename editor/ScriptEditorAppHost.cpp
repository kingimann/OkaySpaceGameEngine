// The Script Editor app inside the editor (ScriptEditorAppHost.hpp): one shared ScriptIdeApp, fed with the editor's data through
// its Config hooks. No second script editor is written here; this file only connects the shared one to the editor.
#include "ScriptEditorAppHost.hpp"
#include "ScriptAssist.hpp"        // LanguageWordsFor, OkayScriptPalette: the editor's words, hover docs and colours (imgui.h before OkayUI's headers)
#include "ScriptIdeApp.hpp"
#include "OkayPanels.hpp"          // DrawAppWindowOkayUI: a whole OkayUI app in an editor window
#include "PluginRegistry.hpp"      // ScriptEditorPlugins, ScriptProblems: the editor plugins' lint
#include "PluginScriptApi.hpp"     // ScriptEditorHostApi: what plugins' script_* calls reach
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "okay/Platform/Platform.hpp"   // PrefPath: the settings folder the Script Editor program uses too
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Lua/LuaScriptVM.hpp"          // the bound names and members for Lua scripts' completion
#include "okay/JavaScript/JavaScriptVM.hpp"   // the same for JavaScript

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include <cstring>
#include <filesystem>
#include <map>
#include <fstream>
#include <memory>

// The editable text of a script component that Play compiles and the Inspector reads (main.cpp, keyed by the component).
void SetCodeBuffer(void* key, const std::string& text);

namespace oked {

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

// The completion list from the editor's script assist (ScriptAssist.hpp): after `receiver.` (or Lua's `receiver:`) the members
// of what the receiver is (C#-aware for OkayScript: GetComponent<T>(), a variable holding a component), else the language's
// words, the file's own names and the editor plugins' items. Names that start with what was typed first (as typed, then in any
// case), fuzzy matches only when there are none. Methods and known functions are inserted with "()" and the caret inside when
// they take arguments. The details pane gets the help card's description and example.
std::vector<scriptide::CompletionItem> CompleteAt(const std::string& language, const std::string& text, int line, int col,
                                                  const std::string& prefix) {
    const std::size_t caret = scriptide::OffsetOf(text, line, col);
    const std::size_t ws = caret >= prefix.size() ? caret - prefix.size() : 0;
    auto isWord = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    const bool lua = language == okay::LuaScriptVM::kLanguage, js = language == okay::JavaScriptVM::kLanguage;
    // the receiver: the plain name before the dot and the whole expression (a.b.c, GetComponent<T>(), xs[0])
    std::string receiver, expr;
    if (ws > 0 && (text[ws - 1] == '.' || (lua && text[ws - 1] == ':'))) {
        std::size_t a = ws - 1;
        while (a > 0 && isWord(text[a - 1])) --a;
        receiver = text.substr(a, ws - 1 - a);
        std::size_t e = ws - 1;
        while (e > 0) {
            const char c = text[e - 1];
            if (isWord(c) || c == '.') { --e; continue; }
            if (c == ')' || c == ']' || c == '>') {   // a balanced (..), [..] or <..>
                const char open = c == ')' ? '(' : c == ']' ? '[' : '<';
                int depth = 0;
                std::size_t k = e;
                bool found = false;
                while (k > 0) {
                    --k;
                    if (text[k] == c) ++depth;
                    else if (text[k] == open && --depth == 0) { found = true; break; }
                }
                if (!found) break;
                e = k;
                continue;
            }
            break;
        }
        expr = text.substr(e, ws - 1 - e);
    }
    std::vector<std::string> members;
    bool memberMode = false;
    if (!expr.empty() || !receiver.empty()) {
        if (lua) members = okay::LuaScriptVM::CompletionMembers(expr);
        else if (js) members = okay::JavaScriptVM::CompletionMembers(expr);
        else {
            members = ScriptMembers(receiver);
            if (members.empty() || expr.find_first_of(".<") != std::string::npos) {
                const std::vector<std::string> cs = CsMembersFor(text, expr);
                if (!cs.empty()) members = cs;
            }
        }
        memberMode = !members.empty();
    }
    static const std::unordered_set<std::string> kConstants = {"PI", "Deg2Rad", "Rad2Deg", "Epsilon", "Infinity"};
    std::vector<std::string> pool;
    if (!memberMode) {
        pool = lua ? okay::LuaScriptVM::CompletionNames() : js ? okay::JavaScriptVM::CompletionNames() : ScriptCompletions();
        AddBufferIdentifiers(text.c_str(), pool);
        pluginreg::DocContext ctx;
        ctx.language = language; ctx.text = text; ctx.line = line; ctx.col = col; ctx.word = prefix;
        for (const auto& it : pluginreg::ScriptEditorPlugins().Complete(ctx)) pool.push_back(it.label);
    }
    const std::vector<std::string>& names = memberMode ? members : pool;
    std::string low = prefix;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    struct Hit { int tier, score; const std::string* name; };
    std::vector<Hit> hits;
    std::unordered_set<std::string> seen;
    for (const std::string& n : names) {
        if (n.empty() || n == prefix || !seen.insert(n).second) continue;
        if (n.compare(0, prefix.size(), prefix) == 0) { hits.push_back({0, 0, &n}); continue; }
        std::string ln = n.substr(0, prefix.size());
        for (char& c : ln) c = (char)std::tolower((unsigned char)c);
        if (ln == low) { hits.push_back({1, 0, &n}); continue; }
        const int s = scriptide::FuzzyScore(prefix, n);
        if (s >= 0) hits.push_back({2, -s, &n});
    }
    if (std::any_of(hits.begin(), hits.end(), [](const Hit& h) { return h.tier < 2; }))
        hits.erase(std::remove_if(hits.begin(), hits.end(), [](const Hit& h) { return h.tier == 2; }), hits.end());
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.score != b.score) return a.score < b.score;
        return a.name->size() < b.name->size();
    });
    std::vector<scriptide::CompletionItem> out;
    for (const Hit& h : hits) {
        if (out.size() >= 40) break;
        const std::string& n = *h.name;
        scriptide::CompletionItem it;
        it.label = n;
        it.insert = n;
        const ScriptHelp* help = (!receiver.empty() ? ScriptHelpFor(receiver + "." + n) : nullptr);
        if (!help) help = ScriptHelpFor(n);
        if (memberMode) {
            const bool method = !lua && !js && std::isupper((unsigned char)n[0]) && !kConstants.count(n);
            it.kind = method ? 'm' : 'p';
            if (method) { it.insert = n + "()"; it.caretBack = 1; it.detail = "()"; }
        } else {
            switch (ClassifyCompletion(n)) {
            case AcKind::Keyword: it.kind = 'k'; break;
            case AcKind::Type: it.kind = 'T'; break;
            case AcKind::Function: it.kind = 'f'; break;
            default: it.kind = 'v'; break;
            }
            if (const std::string* sg = ScriptSignature(n)) {
                const std::size_t paren = sg->find('(');
                it.detail = paren == std::string::npos ? *sg : sg->substr(paren);
                const bool noArgs = sg->find("()") != std::string::npos;
                it.insert = n + "()";
                it.caretBack = noArgs ? 0 : 1;
                it.example = ScriptExampleFromSignature(*sg);
            }
            if (const std::string* dc = ScriptDoc(n)) it.doc = *dc;
        }
        if (help && help->doc) it.doc = help->doc;
        if (help && help->example) it.example = help->example;
        if (lua) { const std::string d = okay::LuaScriptVM::CompletionDoc(n); if (!d.empty() && it.doc.empty()) it.doc = d; }
        if (js) { const std::string d = okay::JavaScriptVM::CompletionDoc(n); if (!d.empty() && it.doc.empty()) it.doc = d; }
        out.push_back(std::move(it));
    }
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
        cfg.complete = CompleteAt;
        cfg.signature = [](const std::string& language, const std::string& name, std::string& doc) {
            if (language == okay::LuaScriptVM::kLanguage || language == okay::JavaScriptVM::kLanguage) {
                const std::string d = language == okay::LuaScriptVM::kLanguage ? okay::LuaScriptVM::CompletionDoc(name)
                                                                              : okay::JavaScriptVM::CompletionDoc(name);
                const std::size_t nl = d.find('\n');
                const std::string first = d.substr(0, nl);
                if (first.find('(') == std::string::npos) return std::string();
                doc = nl == std::string::npos ? std::string() : d.substr(nl + 1);
                return first;
            }
            const std::string* sg = ScriptSignature(name);
            if (!sg) return std::string();
            if (const std::string* dc = ScriptDoc(name)) doc = *dc;
            return *sg;
        };
        cfg.snippets = [](const std::string& language) {   // the built-in ones, then the editor plugins' snippets
            std::vector<scriptide::Snippet> out;
            for (const scriptide::Snippet& s : scriptide::BuiltinSnippets())
                if (s.language == language || s.group == "C#") out.push_back(s);
            for (const auto& p : pluginreg::ScriptEditorPlugins().Snippets(language))
                out.push_back(scriptide::Snippet{"Plugins", p.title.empty() ? p.prefix : p.title, p.prefix, p.body, p.language});
            return out;
        };
        cfg.api = [](const std::string& language) {   // OkayScript: the signature table; Lua and JavaScript: the app's own list
            std::vector<scriptide::ApiEntry> out;
            if (language == okay::LuaScriptVM::kLanguage || language == okay::JavaScriptVM::kLanguage) {
                const bool lua = language == okay::LuaScriptVM::kLanguage;
                for (const std::string& n : lua ? okay::LuaScriptVM::CompletionNames() : okay::JavaScriptVM::CompletionNames()) {
                    scriptide::ApiEntry a;
                    a.name = n;
                    a.doc = lua ? okay::LuaScriptVM::CompletionDoc(n) : okay::JavaScriptVM::CompletionDoc(n);
                    const std::string first = a.doc.substr(0, a.doc.find('\n'));
                    a.signature = first.find('(') != std::string::npos ? first : n;
                    out.push_back(std::move(a));
                }
            } else {
                for (const auto& kv : ScriptSignatureMap()) {
                    scriptide::ApiEntry a;
                    a.name = kv.first;
                    a.signature = kv.second;
                    if (const std::string* d = ScriptDoc(kv.first)) a.doc = *d;
                    out.push_back(std::move(a));
                }
            }
            std::sort(out.begin(), out.end(), [](const scriptide::ApiEntry& x, const scriptide::ApiEntry& y) { return x.name < y.name; });
            return out;
        };
        cfg.showDocs = [] { g_showScriptDocs = true; };
        cfg.openExternal = [](const std::string& path) { extide::OpenExternal(path); ConsoleLog("Opened " + path + " in an outside editor"); };
        cfg.windowCommands = [] {
            return std::vector<scriptide::AppCommand>{{"Float Window", "", [] { g_scriptDockReq = 1; }},
                                                      {"Dock Window", "", [] { g_scriptDockReq = 2; }}};
        };
        cfg.format = [](const std::string& language, const std::string& path, const std::string& text, std::string& out) {
            pluginreg::DocContext ctx;   // an editor plugin's formatter for the language first; else the app's own
            ctx.language = language; ctx.path = path; ctx.text = text;
            return pluginreg::ScriptEditorPlugins().Format(ctx, out);
        };
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

// Play compiles what is on screen and the Inspector shows the fields of the unsaved text: each changed tab's text goes into the
// code buffer of every component that uses it.
void SyncCodeBuffers(scriptide::ScriptIdeApp& app) {
    static std::map<std::string, unsigned> seen;
    for (const scriptide::ScriptIdeApp::DocumentInfo& d : app.Documents()) {
        auto it = seen.find(d.key);
        if (it != seen.end() && it->second == d.version) continue;
        seen[d.key] = d.version;
        const std::string text = app.DocumentText(d.key);
        ForEachScript([&](okay::GameObject& go, okay::ScriptComponent& sc, int n) {
            if (KeyOf(go, sc, n) == d.key) SetCodeBuffer(&sc, text);
            return false;
        });
    }
}
} // namespace

void ApplyScriptDockRequest() {
    if (g_scriptDockReq == 1) {   // float: a large window in the middle of the editor
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::max(760.0f, screen.x * 0.62f), std::max(560.0f, screen.y * 0.78f)), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        g_scriptDockReq = 0;
    } else if (g_scriptDockReq == 2 && g_dockspaceId) {   // dock: a tab of the main area
        ImGui::SetNextWindowDockID(g_dockspaceId, ImGuiCond_Always);
        g_scriptDockReq = 0;
    }
}

void DrawScriptEditorApp(okay::editor::EditorState& ed) {
    g_ed = &ed;
    if (!g_showScriptEditor) return;
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
    ApplyScriptDockRequest();
    // what plugins' script_* calls reach while the window is drawn
    ScriptEditorHost& host = ScriptEditorHostApi();
    host.text = [&app] { return app.ActiveText(); };
    host.path = [&app] { return app.ActivePath(); };
    host.language = [&app] { return app.ActiveLanguage(); };
    host.caretLine = [&app] { OkayUI::CodeEdit* e = app.ActiveEdit(); return e ? e->Caret().line + 1 : 0; };
    host.insert = [&app](const std::string& t) { app.InsertText(t); };
    host.setText = [&app](const std::string& t) { app.SetActiveText(t); };
    host.open = [&app](const std::string& p, int line) { app.OpenFile(p, line); };
    struct HostReset { ~HostReset() { ScriptEditorHostApi() = ScriptEditorHost{}; } } hostReset;
    // the window keeps the old one's title and flag, so saved layouts and the "Scripting" layout still place it
    DrawAppWindowOkayUI("Script Editor", &g_showScriptEditor, "##okayui_script_editor_app",
                        [&app](float x, float y, float w, float h) { app.Frame(x, y, w, h); }, 980.0f, 640.0f,
                        [&app](const std::string& dropped) { if (!scriptide::LanguageForPath(dropped).empty()) app.OpenFile(dropped); });
    SyncCodeBuffers(app);
    {   // the code or a field of the app has the keyboard: the editor routes every key to it
        OkayUI::CodeEdit* e = app.ActiveEdit();
        g_scriptCodeOwnsKeys = (e && e->Focused()) || OkayUI::WantsTextInput();
    }
}

void ScriptEditorOpenFile(const std::string& path, int line) {
    g_showScriptEditor = true;
    App().OpenFile(path, line);
}

void ScriptEditorShowComponent(okay::editor::EditorState& ed, okay::ScriptComponent* sc) {
    g_ed = &ed;
    g_showScriptEditor = true;
    if (!sc) return;
    ForEachScript([&](okay::GameObject& go, okay::ScriptComponent& c, int n) {
        if (&c != sc) return false;
        scriptide::DocumentSpec spec;
        if (DocumentFor(KeyOf(go, c, n), spec)) App().ShowDocuments({spec});
        return true;
    });
}

std::vector<std::string> ScriptEditorUnsavedTitles() {
    std::vector<std::string> out;
    for (const auto& d : App().Documents())
        if (d.dirty) out.push_back("Script: " + (d.path.empty() ? d.label : d.path));
    return out;
}

bool ScriptEditorHasUnsaved() { return !ScriptEditorUnsavedTitles().empty(); }

int ScriptEditorSaveAll(bool withFileOnly, std::string* failed) {
    int n = 0;
    for (const auto& d : App().Documents()) {
        if (!d.dirty || (withFileOnly && d.path.empty())) continue;
        if (App().SaveDocument(d.key)) ++n;
        else if (failed && failed->empty()) *failed = d.path.empty() ? d.label : d.path;
    }
    if (n) SyncCodeBuffers(App());
    return n;
}

std::vector<std::pair<std::string, std::string>> ScriptEditorUnsavedTexts() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& d : App().Documents())
        if (d.dirty) out.emplace_back(d.path.empty() ? std::string("untitled-script") : d.path, App().DocumentText(d.key));
    return out;
}

void ScriptEditorRestore(const std::string& key, const std::string& text) {
    g_showScriptEditor = true;
    scriptide::ScriptIdeApp& app = App();
    if (key.rfind("untitled", 0) == 0) {   // a script that had no file: a tab that saves into Assets/Scripts
        static int count = 0;
        scriptide::DocumentSpec spec;
        spec.key = "untitled:" + std::to_string(++count);
        spec.label = "Untitled";
        spec.language = "okayscript";
        spec.save = [](const std::string& t, std::string& why) {
            const std::string path = scriptide::DefaultScriptPath(g_ed ? g_ed->projectDir() : std::string(), "Untitled", "okayscript");
            std::error_code ec;
            fs::create_directories(fs::u8path(path).parent_path(), ec);
            std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
            f << t;
            if (!f) { why = "cannot write " + path; return false; }
            ConsoleLog("[Script Editor] saved the recovered script as " + path);
            return true;
        };
        if (app.OpenDocument(spec)) app.SetActiveText(text);
        return;
    }
    if (app.OpenFile(key)) app.SetActiveText(text);   // the recovered text, unsaved (one undo step back to the file)
}

bool ScriptEditorHasDocument() { return !App().Documents().empty(); }

void ScriptEditorDemo(const std::string& what) {
    g_showScriptEditor = true;
    scriptide::ScriptIdeApp& app = App();
    if (what == "new") app.OpenNewScript();
    else if (what == "quickopen") app.OpenPicker(5);
    else if (what == "palette") app.OpenPicker(6);
    else if (what == "search") app.OpenPicker(5);
    else if (what == "rename") app.OpenRename("Fire");
    else if (what == "quickfix") app.OpenQuickFixes();
}

} // namespace oked
