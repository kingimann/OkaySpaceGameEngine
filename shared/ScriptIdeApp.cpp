// Script Editor (ScriptIdeApp.hpp): the script editor drawn with OkayUI inside a rectangle, shared by the Script Editor program
// (scripteditor/main.cpp) and the editor's window (editor/ScriptEditorAppHost.cpp). The decisions are the headless model's
// (editor/ScriptIde.hpp) and the engine checkers'; this file keeps the open files and draws them.
//
// Written from general editor behaviour (a file tree beside tabs, a search panel, a status line, a history of saved copies) and
// the OkayUI and OkayUI::CodeEdit interfaces; no code, names, icons or look from any other editor (rule 19a).
#include "okay/UI/OkayUIDesign.hpp"
#include "ScriptIdeAppImpl.hpp"

#include "okay/Components/ScriptComponent.hpp"   // TestRunScript: the script on a sandbox object
#include "okay/Scene/Scene.hpp"
#include "okay/Scripting/ScriptVM.hpp"          // okay::CreateScriptVM: the engine's checker and built-in names per language
#include "okay/Lua/Lua.hpp"                     // okay::lua::kKeywords
#include "okay/Lua/LuaScriptVM.hpp"             // LuaScriptVM::Lint
#include "okay/JavaScript/JavaScript.hpp"       // okay::js::kKeywords
#include "okay/JavaScript/JavaScriptVM.hpp"     // JavaScriptVM::Lint
#include "okay/Core/Json.hpp"            // okay::json::Parse / Escape: the settings file
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace scriptide {

namespace fs = std::filesystem;
namespace UI = OkayUI;

// ---- settings ----------------------------------------------------------------------------------------------------------

std::string AppSettings::Serialize() const {
    std::string o = "{\n  \"tabSize\": " + std::to_string(tabSize) + ",\n  \"wordWrap\": " + (wordWrap ? "true" : "false") +
                    ",\n  \"showWhitespace\": " + (showWhitespace ? "true" : "false") + ",\n  \"showTree\": " + (showTree ? "true" : "false") +
                    ",\n  \"codeFontPx\": " + std::to_string((int)codeFontPx) + ",\n  \"showMinimap\": " + (showMinimap ? "true" : "false") +
                    ",\n  \"showRuler\": " + (showRuler ? "true" : "false") + ",\n  \"inlineProblems\": " + (inlineProblems ? "true" : "false") +
                    ",\n  \"syntaxColors\": " + (syntaxColors ? "true" : "false") + ",\n  \"autoClose\": " + (autoClose ? "true" : "false") +
                    ",\n  \"liveSync\": " + (liveSync ? "true" : "false") + ",\n  \"density\": " + std::to_string(density) +
                    ",\n  \"lastFolder\": ";
    okay::json::Escape(lastFolder, o);
    o += ",\n  \"recentFolders\": [";
    for (std::size_t i = 0; i < recentFolders.size(); ++i) {
        o += i ? ", " : "";
        okay::json::Escape(recentFolders[i], o);
    }
    return o + "]\n}\n";
}

AppSettings AppSettings::Parse(const std::string& text) {
    AppSettings s;
    okay::json::Value v;
    if (!okay::json::Parse(text, v) || !v.IsObject()) return s;
    auto whole = [&](const char* key, int lo, int hi, int fallback) {   // a whole number in range, else the default
        const okay::json::Value* n = v.Get(key);
        std::uint64_t u = 0;
        return n && n->AsIndex(u) ? (int)std::min<std::uint64_t>(std::max<std::uint64_t>(u, (std::uint64_t)lo), (std::uint64_t)hi) : fallback;
    };
    auto flag = [&](const char* key, bool fallback) { const okay::json::Value* b = v.Get(key); return b && b->IsBool() ? b->boolean : fallback; };
    s.tabSize = whole("tabSize", 1, 16, s.tabSize);
    s.codeFontPx = (float)whole("codeFontPx", 10, 32, (int)s.codeFontPx);
    s.wordWrap = flag("wordWrap", s.wordWrap);
    s.showWhitespace = flag("showWhitespace", s.showWhitespace);
    s.showTree = flag("showTree", s.showTree);
    s.showMinimap = flag("showMinimap", s.showMinimap);
    s.showRuler = flag("showRuler", s.showRuler);
    s.inlineProblems = flag("inlineProblems", s.inlineProblems);
    s.syntaxColors = flag("syntaxColors", s.syntaxColors);
    s.autoClose = flag("autoClose", s.autoClose);
    s.liveSync = flag("liveSync", s.liveSync);
    s.density = whole("density", 0, 2, s.density);
    if (const okay::json::Value* f = v.Get("lastFolder"); f && f->IsString()) s.lastFolder = f->string;
    if (const okay::json::Value* r = v.Get("recentFolders"); r && r->IsArray())
        for (const okay::json::Value& f : r->array)
            if (f.IsString() && !f.string.empty() && s.recentFolders.size() < 10 &&
                std::find(s.recentFolders.begin(), s.recentFolders.end(), f.string) == s.recentFolders.end())
                s.recentFolders.push_back(f.string);
    return s;
}

void AppSettings::NoteFolder(const std::string& folder) {
    if (folder.empty()) return;
    recentFolders.erase(std::remove(recentFolders.begin(), recentFolders.end(), folder), recentFolders.end());
    recentFolders.insert(recentFolders.begin(), folder);
    if (recentFolders.size() > 10) recentFolders.resize(10);
    lastFolder = folder;
}

// ---- checks and words without the editor ------------------------------------------------------------------------------------

// The language id the engine's VMs use for a file ("okayscript", "lua", "javascript"); "" when the engine has none.
std::string EngineLanguageOf(const std::string& path) {
    const std::string l = LanguageForPath(path);
    if (!l.empty()) return l;
    std::string ext = fs::u8path(path).extension().u8string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".js" ? "javascript" : std::string();
}

namespace {
std::vector<std::string> BuiltinNamesOf(const std::string& language) {
    if (language.empty() || language == "csharp") return {};
    const std::unique_ptr<okay::IScriptVM> vm = okay::CreateScriptVM(language);
    return vm ? vm->BuiltinNames() : std::vector<std::string>();
}
} // namespace

std::vector<Diagnostic> CheckScript(const std::string& language, const std::string& text) {
    std::vector<Diagnostic> out;
    if (language.empty() || language == "csharp") return out;   // no checker for C# in the engine (the editor's hosts may add one)
    std::vector<okay::ScriptDiagnostic> found;
    if (std::unique_ptr<okay::IScriptVM> vm = okay::CreateScriptVM(language)) found = vm->ValidateAll(text);
    if (language == "lua") { const auto more = okay::LuaScriptVM::Lint(text); found.insert(found.end(), more.begin(), more.end()); }
    if (language == "javascript") { const auto more = okay::JavaScriptVM::Lint(text); found.insert(found.end(), more.begin(), more.end()); }
    for (const okay::ScriptDiagnostic& f : found) {
        Diagnostic d;
        d.line = f.line; d.col = f.column; d.severity = f.severity == 0 ? 0 : 1; d.message = f.message; d.source = "checker";
        d.fixes.push_back(FixFromSuggestion(d, text));
        if (d.fixes.back().title.empty()) d.fixes.clear();
        out.push_back(std::move(d));
    }
    std::vector<Diagnostic> lint = BasicLint(language, text, BuiltinNamesOf(language));
    for (Diagnostic& d : lint) {
        // the checker said it already (same line, same message start): keep one
        const bool dup = std::any_of(out.begin(), out.end(), [&](const Diagnostic& o) { return o.line == d.line && o.message == d.message; });
        if (!dup) out.push_back(std::move(d));
    }
    std::stable_sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
    return out;
}

TestRunResult TestRunScript(const std::string& language, const std::string& text,
                            const std::function<void(okay::ScriptComponent&)>& prepare) {
    TestRunResult r;
    try {
        okay::Scene sandbox;
        sandbox.physicsEnabled = false;
        okay::GameObject* go = sandbox.CreateGameObject("ScriptTest");
        auto* sc = go->AddComponent<okay::ScriptComponent>(language.empty() ? std::string("okayscript") : language);
        if (prepare) prepare(*sc);
        std::string error;
        if (!sc->LoadSource(text, &error)) { r.message = "Compile error: " + error; return r; }
        sandbox.Start();
        for (int i = 0; i < 3; ++i) sandbox.Update(1.0f / 60.0f);
        r.ok = true;
        r.message = "Test run OK: start() + 3 update() frames (sandbox).";
    } catch (const std::exception& e) {
        r.message = std::string("Runtime error: ") + e.what();
    } catch (...) {
        r.message = "Runtime error during test run.";
    }
    return r;
}

const scripteditor::LanguageWords& DefaultWordsFor(const std::string& language) {
    static std::map<std::string, scripteditor::LanguageWords> made;
    const auto it = made.find(language);
    if (it != made.end()) return it->second;
    scripteditor::LanguageWords w;
    if (language == "lua") {
        w.keywords.assign(std::begin(okay::lua::kKeywords), std::end(okay::lua::kKeywords));
        w.lineComments = {"--"};
        w.blockStart = "--[["; w.blockEnd = "]]";
        const scripteditor::LanguageWords blocks = scripteditor::LuaBlockWords();
        w.blockOpenWords = blocks.blockOpenWords; w.blockCloseWords = blocks.blockCloseWords; w.dedentWords = blocks.dedentWords;
        w.foldByIndent = blocks.foldByIndent;
    } else if (language == "javascript") {
        w.keywords.assign(okay::js::kKeywords, okay::js::kKeywords + okay::js::kKeywordCount);
        w.lineComments = {"//"};
        w.blockStart = "/*"; w.blockEnd = "*/";
    } else {
        const UI::CodeLanguage l = language == "okayscript" ? UI::OkayScriptLanguage() : UI::CLikeLanguage();
        w.keywords.assign(l.keywords.begin(), l.keywords.end());
        w.types.assign(l.types.begin(), l.types.end());
        w.lineComments = l.lineComments;
        w.blockStart = l.blockStart; w.blockEnd = l.blockEnd;
        std::sort(w.keywords.begin(), w.keywords.end());
        std::sort(w.types.begin(), w.types.end());
    }
    w.known = BuiltinNamesOf(language);
    // Lua and JavaScript: the engine's bound names too, with their docs (hover, the completion's details, signature help)
    std::vector<std::string> bound;
    std::string (*doc)(const std::string&) = nullptr;
    if (language == "lua") { bound = okay::LuaScriptVM::CompletionNames(); doc = okay::LuaScriptVM::CompletionDoc; }
    if (language == "javascript") { bound = okay::JavaScriptVM::CompletionNames(); doc = okay::JavaScriptVM::CompletionDoc; }
    for (const std::string& n : bound) {
        if (std::find(w.known.begin(), w.known.end(), n) == w.known.end()) w.known.push_back(n);
        const std::string d = doc ? doc(n) : std::string();
        if (!d.empty()) w.docs.emplace(n, d);
    }
    return made.emplace(language, std::move(w)).first->second;
}

std::vector<CompletionItem> DefaultCompletions(const scripteditor::LanguageWords& words, const std::string& text,
                                               const std::string& prefix, std::size_t maxItems) {
    // Three tiers: names that start with the prefix as typed, then ones that start with it in any case, then (only when there is
    // nothing in the first two) fuzzy matches. Shorter names first within a prefix tier, the better fuzzy score first in the last.
    struct Scored { int tier, score; CompletionItem item; };
    std::vector<Scored> out;
    std::set<std::string> seen{prefix};
    std::string lowPrefix = prefix;
    for (char& c : lowPrefix) c = (char)std::tolower((unsigned char)c);
    auto docOf = [&](const std::string& name) {
        const auto it = words.docs.find(name);
        return it == words.docs.end() ? std::string() : it->second;
    };
    auto offer = [&](const std::string& name, char kind, const std::string& detail) {
        if (name.empty() || !seen.insert(name).second) return;
        int tier = 2, score = 0;
        if (name.compare(0, prefix.size(), prefix) == 0) tier = 0;
        else {
            std::string low = name.substr(0, prefix.size());
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if (low == lowPrefix) tier = 1;
            else if ((score = FuzzyScore(prefix, name)) < 0) return;
        }
        CompletionItem it;
        it.label = it.insert = name;
        it.kind = kind;
        it.doc = docOf(name);
        it.detail = detail.empty() ? it.doc.substr(0, it.doc.find('\n')) : detail;
        out.push_back({tier, -score, std::move(it)});
    };
    for (const std::string& k : words.keywords) offer(k, 'k', "keyword");
    for (const std::string& t : words.types) offer(t, 'T', std::string());
    for (const std::string& k : words.known) offer(k, 'f', std::string());
    // the words of the text (identifiers), one pass
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = (unsigned char)text[i];
        if (std::isalpha(c) || c == '_') {
            std::size_t j = i + 1;
            while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_')) ++j;
            if (j - i <= 64) offer(text.substr(i, j - i), 'v', "in this file");
            i = j;
        } else if (std::isdigit(c)) {
            while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '_')) ++i;
        } else ++i;
    }
    const bool anyPrefix = std::any_of(out.begin(), out.end(), [](const Scored& s) { return s.tier < 2; });
    if (anyPrefix) out.erase(std::remove_if(out.begin(), out.end(), [](const Scored& s) { return s.tier == 2; }), out.end());
    std::stable_sort(out.begin(), out.end(), [](const Scored& a, const Scored& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.tier == 2 && a.score != b.score) return a.score < b.score;
        if (a.item.label.size() != b.item.label.size()) return a.item.label.size() < b.item.label.size();
        return a.item.label < b.item.label;
    });
    std::vector<CompletionItem> items;
    for (std::size_t i = 0; i < out.size() && items.size() < maxItems; ++i) items.push_back(std::move(out[i].item));
    return items;
}

// ---- the app ----------------------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::drawMenu(float X, float Y, float W, float h) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_menu").c_str(), X, Y, W, h)) {
        UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        Doc* d = activeDoc();
        if (UI::BeginMenuBar()) {
            if (UI::BeginMenu("File")) {
                if (UI::MenuItem("New Script...", nullptr, false, !folder.empty())) openNewScript();
                if (UI::MenuItem("Open Folder...", "Ctrl+O")) { folderOpen = true; folderFocus = true; std::snprintf(folderBuf, sizeof folderBuf, "%s", folder.c_str()); }
                if (UI::MenuItem("Go to File...", "Ctrl+Shift+N", false, !folder.empty())) { goOpen = true; goBuf[0] = 0; goSel = 0; }
                if (UI::BeginMenu("Open Recent Folder")) {
                    if (settings.recentFolders.empty()) UI::MenuItem("(none yet)", nullptr, false, false);
                    for (const std::string& f : settings.recentFolders)
                        if (UI::MenuItem(f.c_str())) openFolder(f);
                    UI::EndMenu();
                }
                UI::MenuSeparator();
                if (UI::MenuItem("Save", "Ctrl+S", false, d != nullptr)) { if (d) save(*d); }
                if (UI::MenuItem("Save All", "Ctrl+Shift+S", false, !docs.empty())) saveAll();
                if (UI::MenuItem("Close Tab", "Ctrl+W", false, d != nullptr)) requestClose(active);
                if (UI::MenuItem("Reopen Closed Tab", "Ctrl+Shift+T", false, !closed.empty())) reopenClosed();
                if (UI::MenuItem("Reload from Disk", nullptr, false, d && !d->path.empty())) reloadFromDisk(*d);
                if (cfg.openExternal && UI::MenuItem("Open in External Editor", nullptr, false, d && !d->path.empty())) cfg.openExternal(d->path);
                if (UI::MenuItem("Load Outside Changes", nullptr, settings.liveSync)) { settings.liveSync = !settings.liveSync; saveSettings(); }
                if (cfg.quitItem) { UI::MenuSeparator(); if (UI::MenuItem("Quit", "Ctrl+Q")) requestQuit(); }
                UI::EndMenu();
            }
            if (UI::BeginMenu("Edit")) {
                UI::CodeEdit* e = d ? &d->code.Edit() : nullptr;
                if (UI::MenuItem("Undo", "Ctrl+Z", false, e && e->CanUndo())) e->Undo();
                if (UI::MenuItem("Redo", "Ctrl+Y", false, e && e->CanRedo())) e->Redo();
                UI::MenuSeparator();
                if (UI::MenuItem("Toggle Comment", "Ctrl+/", false, e != nullptr)) e->ToggleLineComment(LineCommentFor(d->language));
                if (UI::MenuItem("Duplicate Lines", "Ctrl+Shift+D", false, e != nullptr)) e->DuplicateLines();
                UI::MenuSeparator();
                drawLanguageMenu(d);
                UI::MenuSeparator();
                if (UI::MenuItem("Format Document", "Ctrl+Alt+L", false, d != nullptr)) formatDocument();
                if (UI::MenuItem("Rename Symbol...", "F2", false, d != nullptr)) openRename(std::string());
                if (UI::MenuItem("Quick Fixes...", "Alt+Enter", false, d != nullptr)) { fixes = fixesAtCaret(); fixSel = 0; fixesOpen = !fixes.empty(); fixesFresh = true; if (!fixesOpen) notice = "No quick fixes on this line"; }
                if (UI::MenuItem("Join Lines", "Ctrl+Shift+J", false, d != nullptr)) joinLines();
                if (UI::MenuItem("Expand Selection", "Alt+Shift+Up", false, d != nullptr)) expandSelection();
                UI::MenuSeparator();
                if (UI::MenuItem("Find...", "Ctrl+F", false, d != nullptr)) openFindBar(false);
                if (UI::MenuItem("Replace...", "Ctrl+H", false, d != nullptr)) openFindBar(true);
                if (UI::MenuItem("Find in Files", "Ctrl+Shift+F", panel == P_Find)) openPanel(P_Find);
                UI::EndMenu();
            }
            if (UI::BeginMenu("Go")) {
                if (UI::MenuItem("Search Everywhere", "Shift Shift")) openPicker(K_Everywhere);
                if (UI::MenuItem("Commands...", "Ctrl+Shift+P")) openPicker(K_Commands);
                UI::MenuSeparator();
                if (UI::MenuItem("Go to File...", "Ctrl+Shift+N", false, !folder.empty())) { goOpen = true; goBuf[0] = 0; goSel = 0; }
                if (UI::MenuItem("Go to Line...", "Ctrl+G", false, d != nullptr)) openPicker(K_Line);
                if (UI::MenuItem("Go to Symbol...", "Ctrl+Shift+O", false, d != nullptr)) openPicker(K_Symbol);
                if (UI::MenuItem("Go to Definition", "F12", false, d != nullptr)) goToDefinition(std::string());
                if (UI::MenuItem("Find All References", "Shift+F12", false, d != nullptr)) findReferences(std::string());
                UI::MenuSeparator();
                if (UI::MenuItem("Back", "Alt+Left", false, !back.empty())) goBack(false);
                if (UI::MenuItem("Forward", "Alt+Right", false, !forward.empty())) goBack(true);
                if (UI::MenuItem("Switch Tab...", "Ctrl+E", false, !docs.empty())) openPicker(K_Switch);
                if (UI::MenuItem("Toggle Bookmark", "F11", false, d != nullptr)) d->code.Edit().ToggleBreakpoint(d->code.Edit().Caret().line);
                if (UI::MenuItem("Bookmarks...", "Shift+F11")) openPicker(K_Bookmarks);
                UI::EndMenu();
            }
            if (UI::BeginMenu("View")) {
                bool changed = false;
                if (UI::MenuItem("Project Tree", "Alt+1", settings.showTree)) { settings.showTree = !settings.showTree; changed = true; }
                if (UI::MenuItem("Problems", nullptr, panel == P_Problems)) openPanel(P_Problems);
                if (UI::MenuItem("Local History", nullptr, panel == P_History, d != nullptr)) openPanel(P_History);
                UI::MenuSeparator();
                if (UI::MenuItem("Word Wrap", nullptr, settings.wordWrap)) { settings.wordWrap = !settings.wordWrap; changed = true; }
                if (UI::MenuItem("Show Whitespace", nullptr, settings.showWhitespace)) { settings.showWhitespace = !settings.showWhitespace; changed = true; }
                if (UI::MenuItem("Larger Code Text", "Ctrl+=")) { settings.codeFontPx = std::min(32.0f, settings.codeFontPx + 1.0f); changed = true; }
                if (UI::MenuItem("Smaller Code Text", "Ctrl+-")) { settings.codeFontPx = std::max(10.0f, settings.codeFontPx - 1.0f); changed = true; }
                UI::MenuSeparator();
                drawViewExtras(d, changed);
                if (changed) { for (auto& doc : docs) applyView(*doc); saveSettings(); }
                UI::EndMenu();
            }
            if (UI::BeginMenu("Run")) {
                if (UI::MenuItem("Check", "F7", false, d != nullptr)) { if (d) { check(*d); notice = std::to_string(d->problems.size()) + " problems"; } }
                if (UI::MenuItem(cfg.runLabel.c_str(), "F5", false, d != nullptr)) run();
                const bool paused = d && d->execLine > 0;
                if (d && UI::MenuItem("Toggle Breakpoint", "F9")) d->code.Edit().ToggleBreakpoint(d->code.Edit().Caret().line);
                if (cfg.debug && UI::MenuItem("Continue", "F8", false, paused)) cfg.debug("continue");
                if (cfg.debug && UI::MenuItem("Step Over", "F10", false, paused)) cfg.debug("step");
                if (cfg.showInHost && UI::MenuItem(("Show in " + cfg.hostName).c_str(), nullptr, false, d != nullptr))
                    cfg.showInHost(d->path.empty() ? d->key : d->path, d->code.Edit().Caret().line + 1);
                UI::EndMenu();
            }
            drawInsertMenu(d);
            drawHelpMenu();
            UI::EndMenuBar();
        }
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawTree(float X, float Y, float W, float H, bool modal) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_tree").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kPanel);
        dl->AddLine(UI::Vec2(X + W - 1, Y), UI::Vec2(X + W - 1, Y + H), kLine);
        UI::SetCursorScreenPos(UI::Vec2(X + 8, Y + 6));
        const std::string head = folder.empty() ? std::string("No folder open") : fs::u8path(folder).filename().u8string();
        UI::TextUnformatted(Fit(head, W - 16).c_str());
        UI::SetCursorScreenPos(UI::Vec2(X + 8, UI::GetCursorScreenPos().y + 2));
        if (folder.empty()) {
            UI::TextDisabled("File > Open Folder");
            UI::SetCursorScreenPos(UI::Vec2(X + 8, UI::GetCursorScreenPos().y + 6));
            drawSceneScripts(X, W, modal);
        } else {
            UI::SetNextItemWidth(W - 16);
            UI::InputTextWithHint("##treefilter", "Filter", treeFilter, (int)sizeof treeFilter);
            UI::SetCursorScreenPos(UI::Vec2(X + 4, UI::GetCursorScreenPos().y + 2));
            if (UI::BeginChild((pre + "_treelist").c_str(), W - 8, std::max(20.0f, Y + H - UI::GetCursorScreenPos().y - 4), false)) {
                drawSceneScripts(X, W, modal);
                const std::vector<TreeRow> rows = BuildFileTree(scripts, folder, treeFilter);
                const std::string activePath = activeDoc() ? activeDoc()->path : std::string();
                int hiddenBelow = -1;   // rows deeper than this are inside a closed folder
                for (const TreeRow& r : rows) {
                    if (hiddenBelow >= 0 && r.depth > hiddenBelow) continue;
                    hiddenBelow = -1;
                    const bool shut = r.folder && treeFilter[0] == 0 && collapsed.count(r.path) != 0;
                    UI::Indent((float)r.depth * 12.0f);
                    // Room for a chevron before the name; folders get the design's chevron (down when open) and every row
                    // faint indent guides under its parents' chevrons.
                    const std::string label = "      " + r.name + "##" + r.path;
                    if (UI::Selectable(label.c_str(), !r.folder && r.path == activePath) && !modal) {
                        if (r.folder) { if (shut) collapsed.erase(r.path); else collapsed.insert(r.path); }
                        else openFile(r.path, 0);
                    }
                    {
                        const UI::Rect ir = UI::GetItemRect();
                        const auto& tdl = UI::GetWindowDrawList();
                        const float cy = ir.y + ir.h * 0.5f;
                        if (r.folder) UI::DrawChevron(tdl, UI::Vec2(ir.x + 15.0f, cy), 8.0f, shut ? 0 : 1, kDim);
                        for (int d = 0; d < r.depth; ++d) {
                            const float gx = ir.x + 15.0f - 12.0f * (float)(r.depth - d);
                            tdl->AddLine(UI::Vec2(gx, ir.y), UI::Vec2(gx, ir.y + ir.h), (kLine & 0x00ffffffu) | 0xc0000000u, 1.0f);
                        }
                    }
                    UI::Unindent((float)r.depth * 12.0f);
                    if (shut) hiddenBelow = r.depth;
                }
                if (rows.empty()) UI::TextDisabled(treeFilter[0] ? "Nothing matches" : "No scripts in this folder");
            }
            UI::EndChild();
        }
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawTabs(float X, float Y, float W, float h, bool modal) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_tabs").c_str(), X, Y, W, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        const UI::Vec2 m = UI::GetMousePos();
        float x = X + 4;
        int toClose = -1;
        const float lh = UI::GetTextLineHeight();
        for (std::size_t i = 0; i < docs.size() && x < X + W - 40; ++i) {
            const Doc& d = *docs[i];
            const std::string label = (dirty(d) ? "* " : "") + d.label;
            const float tw = std::min(220.0f, UI::CalcTextWidth(label.c_str()) + 40.0f);
            const bool on = (int)i == active;
            const bool hover = m.x >= x && m.x < x + tw && m.y >= Y && m.y < Y + h;
            dl->AddRectFilled(UI::Vec2(x, Y + 3), UI::Vec2(x + tw, Y + h), on ? kTabOn : (hover ? kHover : kStrip), 4.0f);
            if (on) dl->AddRectFilled(UI::Vec2(x, Y + h - 2), UI::Vec2(x + tw, Y + h), kAccent);
            dl->AddText(UI::Vec2(x + 10, Y + (h - lh) * 0.5f + 1), on ? kText : kDim, Fit(label, tw - 34).c_str());
            const float cx = x + tw - 18;
            const bool overClose = m.x >= cx && m.x < cx + 14 && m.y >= Y + 6 && m.y < Y + h - 4;
            dl->AddText(UI::Vec2(cx + 3, Y + (h - lh) * 0.5f + 1), overClose ? kText : kDim, "x");
            if (hover && !modal && UI::IsMouseClicked(0)) { if (overClose) toClose = (int)i; else active = (int)i; }
            if (hover && !modal && UI::IsMouseClicked(2)) toClose = (int)i;
            x += tw + 2;
        }
        if (toClose >= 0) requestClose(toClose);
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawCode(float X, float Y, float W, float H, bool modal) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_code").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kBack);
        Doc* d = activeDoc();
        if (!d) {
            float y = Y + std::max(30.0f, H * 0.3f);
            auto line = [&](const char* t, UI::Color32 c) {
                dl->AddText(UI::Vec2(X + std::max(16.0f, (W - UI::CalcTextWidth(t)) * 0.5f), y), c, t);
                y += UI::GetTextLineHeight() + 8;
            };
            line(folder.empty() ? "Open a folder: File > Open Folder (Ctrl+O)" : "Pick a script in the tree", kText);
            line("Go to File: Ctrl+Shift+N    Find in Files: Ctrl+Shift+F", kDim);
            line("Save: Ctrl+S    Close tab: Ctrl+W    Project tree: Alt+1", kDim);
        } else {
            UI::CodeEdit& e = d->code.Edit();
            if (d->gotoLine > 0) { e.GoToLine(d->gotoLine - 1); e.Focus(); d->gotoLine = 0; }
            // the completion list owns Up, Down, Enter, Tab and Esc while it is open (typing still goes to the code)
            const bool acKeys = acOpen && !modal && completionKeys(*d);
            {   // the find bar's matches are highlighted (plain text; a regex is not)
                const std::string want = barOpen && !barOpt.regex ? std::string(barFind) : std::string();
                if (want != d->shownFind || barOpt.matchCase != d->shownFindCase) {
                    d->shownFind = want;
                    d->shownFindCase = barOpt.matchCase;
                    e.SetFind(want, barOpt.matchCase);
                }
            }
            e.SetReadOnly(modal);
            e.SetHandleKeyboardInput(!acKeys && !fixesOpen && !keysTaken);
            UI::SetCursorScreenPos(UI::Vec2(X, Y));
            const float mmW = W > 300 ? minimapWidth() : 0.0f;
            e.Draw((pre + "_edit").c_str(), W - mmW, H);
            e.SetReadOnly(false);
            e.SetHandleKeyboardInput(true);
            drawOverlays(*d);
            if (mmW > 0) drawMinimap(*d, X + W - mmW, Y, mmW, H, modal);
            {   // Ctrl+wheel over the code: bigger or smaller code text
                const UI::Input& zi = UI::GetFrameInput();
                const UI::Vec2 zp = UI::GetMousePos();
                if (!modal && zi.ctrl && zi.wheel != 0 && zp.x >= X && zp.x < X + W && zp.y >= Y && zp.y < Y + H) {
                    settings.codeFontPx = std::min(32.0f, std::max(10.0f, settings.codeFontPx + (zi.wheel > 0 ? 1.0f : -1.0f)));
                    for (auto& doc : docs) applyView(*doc);
                    saveSettings();
                }
            }
            {   // Ctrl+click on a name: go to where it is defined (a name defined nowhere keeps the widget's extra caret)
                const UI::Input& ci = UI::GetFrameInput();
                const UI::Rect v = e.ViewRect();
                const UI::Vec2 mp = UI::GetMousePos();
                if (!modal && ci.ctrl && !ci.shift && UI::IsMouseClicked(0) && mp.x >= v.x && mp.x < v.x + v.w && mp.y >= v.y && mp.y < v.y + v.h) {
                    const std::string w = wordAt(*d, e.ScreenToPos(mp.x, mp.y));
                    const std::string before = notice;
                    UI::CodeEdit* from = &e;
                    if (!w.empty() && goToDefinition(w)) {
                        from->ClearExtraCarets();
                        if (Doc* to = activeDoc()) to->code.Edit().ClearExtraCarets();
                        d = activeDoc();
                    } else notice = before;
                }
            }
            if (d != activeDoc() || !d) { UI::End(); return; }
            const bool typed = e.Version() != d->seenVersion;
            if (typed) {
                d->seenVersion = e.Version();
                d->editedAt = UI::GetTime();
                if (!d->path.empty()) history.MaybeRecord(d->path, diskText(*d), now());
            }
            if (e.Breakpoints() != d->bpSeen) {   // a click in the gutter
                d->bpSeen = e.Breakpoints();
                if (cfg.breakpointsChanged) {
                    std::vector<int> lines;
                    for (int l : d->bpSeen) lines.push_back(l + 1);
                    cfg.breakpointsChanged(d->key, lines);
                }
            }
            if (!modal && e.Focused()) {
                const UI::Input& in = UI::GetFrameInput();
                if (in.ctrl && UI::IsKeyPressed(UI::KeyId_Space, false)) startCompletion(*d, true);
                else if (typed && acOpen) startCompletion(*d, false);
                else if (typed && in.text && in.text[0] && !in.ctrl) {
                    const char c = in.text[std::strlen(in.text) - 1];
                    if (std::isalnum((unsigned char)c) || c == '_') startCompletion(*d, false);
                }
            } else if (!e.Focused()) acOpen = false;
            if (acOpen) drawCompletion(*d);
            if (!modal) {
                drawLightBulb(*d);
                if (fixesOpen) drawQuickFixes();
                updateSignature(*d);
                drawSignature(*d);
            }
        }
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawPanel(float X, float Y, float W, float H, bool modal) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_panel").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kPanel);
        dl->AddLine(UI::Vec2(X, Y), UI::Vec2(X + W, Y), kLine);
        UI::SetCursorScreenPos(UI::Vec2(X + 8, Y + 6));
        if (panel == P_Find) {
            UI::TextUnformatted("Find in Files");
            UI::SameLine();
            UI::SetNextItemWidth(std::min(360.0f, W * 0.4f));
            if (findFocus) { UI::SetKeyboardFocusHere(); findFocus = false; }
            const bool enter = UI::InputText("##fif", findBuf, (int)sizeof findBuf, UI::InputText_EnterReturnsTrue);
            UI::SameLine(); UI::Checkbox("Match case", &findOpt.matchCase);
            UI::SameLine(); UI::Checkbox("Whole word", &findOpt.wholeWord);
            UI::SameLine(); UI::Checkbox("Regex", &findOpt.regex);
            UI::SameLine();
            if ((UI::Button("Search") || enter) && !modal) findInFiles(findBuf, findOpt);
            UI::SetCursorScreenPos(UI::Vec2(X + 8, UI::GetCursorScreenPos().y + 2));
            UI::TextDisabled("Replace with");
            UI::SameLine();
            UI::SetNextItemWidth(std::min(360.0f, W * 0.4f));
            UI::InputText("##fifwith", fifWith, (int)sizeof fifWith);
            UI::SameLine();
            if (UI::Button("Replace in All Files") && !modal) replaceInFiles(findBuf, fifWith, findOpt);
            UI::SetCursorScreenPos(UI::Vec2(X + 4, UI::GetCursorScreenPos().y + 2));
            if (UI::BeginChild((pre + "_fiflist").c_str(), W - 8, std::max(20.0f, Y + H - UI::GetCursorScreenPos().y - 4), false)) {
                if (!findError.empty()) UI::TextColored(232, 102, 92, ("Bad pattern: " + findError).c_str());
                for (std::size_t i = 0; i < findResults.size(); ++i) {
                    const FileMatch& r = findResults[i];
                    std::string lineText = r.lineText;
                    const std::size_t a = lineText.find_first_not_of(" \t");
                    lineText = a == std::string::npos ? std::string() : lineText.substr(a);
                    const std::string label = relative(r.file) + ":" + std::to_string(r.match.line) + "   " + lineText + "##fr" + std::to_string(i);
                    if (UI::Selectable(label.c_str(), false) && !modal) {
                        const std::string f = r.file;
                        const int ln = r.match.line;
                        openFile(f, ln);
                    }
                }
            }
            UI::EndChild();
        } else if (panel == P_Refs) {
            UI::TextUnformatted(("Uses of " + refsWord + " (" + std::to_string(refs.size()) + ")").c_str());
            UI::SetCursorScreenPos(UI::Vec2(X + 4, UI::GetCursorScreenPos().y + 2));
            if (UI::BeginChild((pre + "_refslist").c_str(), W - 8, std::max(20.0f, Y + H - UI::GetCursorScreenPos().y - 4), false)) {
                int pick = -1;
                for (std::size_t i = 0; i < refs.size(); ++i) {
                    const FileMatch& r = refs[i];
                    const Doc* t = byKey(r.file);
                    std::string lineText = r.lineText;
                    const std::size_t a = lineText.find_first_not_of(" \t");
                    lineText = a == std::string::npos ? std::string() : lineText.substr(a);
                    const std::string where = t && t->path.empty() ? t->label : relative(r.file);
                    if (UI::Selectable((where + ":" + std::to_string(r.match.line) + "   " + lineText + "##rf" + std::to_string(i)).c_str(), false) && !modal) pick = (int)i;
                }
                if (pick >= 0) { const FileMatch r = refs[(std::size_t)pick]; jumpTo(r.file, r.match.line); }
            }
            UI::EndChild();
        } else if (panel == P_Problems) {
            const Doc* d = activeDoc();
            UI::TextUnformatted(d ? ("Problems in " + d->label).c_str() : "Problems");
            UI::SetCursorScreenPos(UI::Vec2(X + 4, UI::GetCursorScreenPos().y + 2));
            if (UI::BeginChild((pre + "_problist").c_str(), W - 8, std::max(20.0f, Y + H - UI::GetCursorScreenPos().y - 4), false)) {
                if (!d || d->problems.empty()) UI::TextDisabled("No problems");
                int go = 0;
                if (d)
                    for (std::size_t i = 0; i < d->problems.size(); ++i) {
                        const Diagnostic& p = d->problems[i];
                        const std::string label = std::string(p.severity == 0 ? "error   " : "warning ") + "line " + std::to_string(p.line) + ": " +
                                                  p.message + "##pr" + std::to_string(i);
                        if (UI::Selectable(label.c_str(), false) && !modal) go = std::max(1, p.line);
                    }
                if (go > 0 && activeDoc()) activeDoc()->gotoLine = go;
            }
            UI::EndChild();
        } else if (panel == P_History) {
            const Doc* d = activeDoc();
            UI::TextUnformatted(d ? ("Local History of " + d->label + " (a copy on every save and every 5 minutes of edits)").c_str() : "Local History");
            const float top = UI::GetCursorScreenPos().y + 2;
            const float listW = std::min(260.0f, W * 0.35f);
            UI::SetCursorScreenPos(UI::Vec2(X + 4, top));
            if (UI::BeginChild((pre + "_histlist").c_str(), listW, std::max(20.0f, Y + H - top - 4), false)) {
                if (versions.empty()) UI::TextDisabled(folder.empty() ? "Open a folder to keep copies" : "No copies yet: save the file");
                int pick = -1;
                for (std::size_t i = 0; i < versions.size(); ++i) {
                    const std::string label = TimeText(versions[i].time) + "  " + versions[i].label + "##hv" + std::to_string(i);
                    if (UI::Selectable(label.c_str(), (int)i == versionSel) && !modal) pick = (int)i;
                }
                if (pick >= 0) showVersion(pick);
            }
            UI::EndChild();
            UI::SetCursorScreenPos(UI::Vec2(X + listW + 12, top));
            if (UI::BeginChild((pre + "_histdiff").c_str(), W - listW - 20, std::max(20.0f, Y + H - top - 4), false)) {
                if (versionSel >= 0 && versionSel < (int)versions.size()) {
                    if (UI::Button("Restore this copy") && !modal) restore(versions[(std::size_t)versionSel]);
                    UI::SameLine();
                    UI::TextDisabled("lines with - are only in the copy, + only in the tab");
                    for (const DiffLine& l : diff) {
                        const std::string t = std::string(1, l.op) + " " + l.text;
                        if (l.op == '-') UI::TextColored(232, 102, 92, t.c_str());
                        else if (l.op == '+') UI::TextColored(98, 180, 110, t.c_str());
                        else UI::TextDisabled(t.c_str());
                    }
                } else UI::TextDisabled("Pick a copy to compare it with the tab");
            }
            UI::EndChild();
        }
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawStatus(float X, float Y, float W, float h) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_status").c_str(), X, Y, W, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        dl->AddLine(UI::Vec2(X, Y), UI::Vec2(X + W, Y), kLine);
        const float ty = Y + (h - UI::GetTextLineHeight()) * 0.5f;
        const Doc* d = activeDoc();
        float rx = X + W - 10;
        auto right = [&](const std::string& t, UI::Color32 c) {
            rx -= UI::CalcTextWidth(t.c_str());
            dl->AddText(UI::Vec2(rx, ty), c, t.c_str());
            rx -= 18;
        };
        if (d) {
            const UI::CodePos c = d->code.Edit().Caret();
            right("Ln " + std::to_string(c.line + 1) + ", Col " + std::to_string(d->code.Edit().ByteToColumn(c.line, c.col) + 1), kDim);
            right(d->language.empty() ? std::string("text") : d->language, kDim);
            int errors = 0, warnings = 0;
            for (const Diagnostic& p : d->problems) (p.severity == 0 ? errors : warnings)++;
            if (warnings) right(std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings"), kWarn);
            if (errors) right(std::to_string(errors) + (errors == 1 ? " error" : " errors"), kError);
            if (!errors && !warnings) right("no problems", kAdd);
        }
        std::string left = notice.empty() ? (d ? (d->path.empty() ? d->label : relative(d->path)) : folder) : notice;
        if (notice.empty() && d) {   // the breadcrumb: the block the caret is in
            if (const Symbol* s = enclosing(*const_cast<Doc*>(d), d->code.Edit().Caret().line)) left += "  >  " + s->display;
        }
        dl->AddText(UI::Vec2(X + 10, ty), notice.empty() ? kDim : kText, Fit(left, std::max(40.0f, rx - X - 20)).c_str());
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawGoToFile(float X, float Y, float W, float H) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_goto").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), Rgb(0, 0, 0, 90));
        const float bw = std::min(560.0f, W - 40), bh = std::min(360.0f, H - 60);
        const float bx = X + (W - bw) * 0.5f, by = Y + 40;
        dl->AddRectFilled(UI::Vec2(bx, by), UI::Vec2(bx + bw, by + bh), kPanel, 6.0f);
        dl->AddRect(UI::Vec2(bx, by), UI::Vec2(bx + bw, by + bh), kLine, 6.0f, 1.0f);
        UI::SetCursorScreenPos(UI::Vec2(bx + 12, by + 10));
        UI::TextUnformatted("Go to File (name, or name:line)");
        UI::SetCursorScreenPos(UI::Vec2(bx + 12, UI::GetCursorScreenPos().y + 2));
        UI::SetNextItemWidth(bw - 24);
        UI::SetKeyboardFocusHere();
        const bool enter = UI::InputText("##gotofile", goBuf, (int)sizeof goBuf, UI::InputText_EnterReturnsTrue);
        const QuickOpenQuery q = ParseQuickOpenQuery(goBuf);
        const std::vector<QuickOpenItem> items = RankQuickOpen(q.text, scripts, recent.Items(), folder, 12);
        if (UI::IsKeyPressed(UI::KeyId_Down)) goSel = std::min(goSel + 1, (int)items.size() - 1);
        if (UI::IsKeyPressed(UI::KeyId_Up)) goSel = std::max(goSel - 1, 0);
        if (goSel >= (int)items.size()) goSel = std::max(0, (int)items.size() - 1);
        int pick = -1;
        UI::SetCursorScreenPos(UI::Vec2(bx + 8, UI::GetCursorScreenPos().y + 4));
        for (std::size_t i = 0; i < items.size(); ++i) {
            UI::SetCursorScreenPos(UI::Vec2(bx + 8, UI::GetCursorScreenPos().y));
            const std::string label = items[i].label + (items[i].recent ? "   (recent)" : "") + "##go" + std::to_string(i);
            if (UI::Selectable(label.c_str(), (int)i == goSel)) pick = (int)i;
        }
        if (items.empty()) { UI::SetCursorScreenPos(UI::Vec2(bx + 12, UI::GetCursorScreenPos().y)); UI::TextDisabled("No script matches"); }
        if (enter && !items.empty()) pick = goSel;
        if (pick >= 0 && pick < (int)items.size()) { openFile(items[(std::size_t)pick].path, q.line); goOpen = false; }
        if (UI::IsKeyPressed(UI::KeyId_Escape, false)) goOpen = false;
    }
    UI::End();
}

void ScriptIdeApp::Impl::drawOpenFolder(float X, float Y, float W, float H) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_openfolder").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), Rgb(0, 0, 0, 90));
        const float bw = std::min(620.0f, W - 40), bh = 120.0f;
        const float bx = X + (W - bw) * 0.5f, by = Y + 60;
        dl->AddRectFilled(UI::Vec2(bx, by), UI::Vec2(bx + bw, by + bh), kPanel, 6.0f);
        dl->AddRect(UI::Vec2(bx, by), UI::Vec2(bx + bw, by + bh), kLine, 6.0f, 1.0f);
        UI::SetCursorScreenPos(UI::Vec2(bx + 12, by + 10));
        UI::TextUnformatted("Open Folder: the folder with your scripts (a project folder)");
        UI::SetCursorScreenPos(UI::Vec2(bx + 12, UI::GetCursorScreenPos().y + 4));
        UI::SetNextItemWidth(bw - 24);
        if (folderFocus) { UI::SetKeyboardFocusHere(); folderFocus = false; }
        const bool enter = UI::InputText("##folderpath", folderBuf, (int)sizeof folderBuf, UI::InputText_EnterReturnsTrue);
        UI::SetCursorScreenPos(UI::Vec2(bx + 12, UI::GetCursorScreenPos().y + 6));
        if (UI::Button("Open") || enter) { if (openFolder(folderBuf)) folderOpen = false; }
        UI::SameLine();
        if (cfg.pickFolder && UI::Button("Browse...")) {
            Impl* self = this;
            cfg.pickFolder(folderBuf, [self](const std::string& chosen) {
                std::snprintf(self->folderBuf, sizeof self->folderBuf, "%s", chosen.c_str());
                if (self->openFolder(chosen)) self->folderOpen = false;
            });
        }
        UI::SameLine();
        if (UI::Button("Cancel") || UI::IsKeyPressed(UI::KeyId_Escape, false)) folderOpen = false;
    }
    UI::End();
}

void ScriptIdeApp::Impl::frame(float X, float Y, float W, float H) {
    {   // the design changed (Settings, Edit > Appearance or another program): the code widgets follow
        static unsigned s_rev = 0;
        if (!cfg.palette && s_rev != OkayUI::design::Revision()) {
            s_rev = OkayUI::design::Revision();
            for (auto& d : docs) d->code.Edit().SetPalette(UI::DesignCodePalette());
        }
    }
    const UI::Input& in = UI::GetFrameInput();
    const bool ctrl = in.ctrl, shift = in.shift, alt = UI::IsKeyDown(UI::KeyId_Alt);
    if (ctrl && !alt) UI::GetIO().InputQueueCharacters.resize(0);   // Ctrl+<key> is a shortcut, never typing
    const bool modal = modalOpen();
    keysTaken = false;
    auto key = [](UI::KeyId k) { return UI::IsKeyPressed(k, false); };
    Doc* d = activeDoc();
    if (!modal) {
        if (ctrl && shift && key(UI::KeyId_S)) saveAll();
        else if (ctrl && key(UI::KeyId_S)) { if (d) save(*d); }
        else if (ctrl && shift && key(UI::KeyId_N) && !folder.empty()) { goOpen = true; goBuf[0] = 0; goSel = 0; }
        else if (ctrl && shift && key(UI::KeyId_F)) { panel = P_Find; findFocus = true; }
        else if (ctrl && shift && key(UI::KeyId_T)) reopenClosed();
        else if (ctrl && shift && key(UI::KeyId_O) && d) openPicker(K_Symbol);
        else if (ctrl && alt && key(UI::KeyId_L) && d) formatDocument();
        else if (ctrl && shift && key(UI::KeyId_J) && d) joinLines();
        else if (alt && shift && key(UI::KeyId_Up) && d) { expandSelection(); keysTaken = true; }
        else if (!ctrl && !shift && key(UI::KeyId_F2) && d) openRename(std::string());
        else if (alt && key(UI::KeyId_Enter) && d) { keysTaken = true; fixes = fixesAtCaret(); fixSel = 0; fixesOpen = !fixes.empty(); fixesFresh = true; if (!fixesOpen) notice = "No quick fixes on this line"; }
        else if (ctrl && shift && key(UI::KeyId_P)) openPicker(K_Commands);
        else if (ctrl && !shift && key(UI::KeyId_F) && d) openFindBar(false);
        else if (ctrl && !shift && key(UI::KeyId_H) && d) openFindBar(true);
        else if (ctrl && !shift && key(UI::KeyId_G) && d) openPicker(K_Line);
        else if (ctrl && !shift && key(UI::KeyId_E) && !docs.empty()) openPicker(K_Switch);
        else if (!ctrl && key(UI::KeyId_F3) && d) findStep(!shift);
        else if (!ctrl && shift && key(UI::KeyId_F12) && d) findReferences(std::string());
        else if (!ctrl && !shift && key(UI::KeyId_F12) && d) goToDefinition(std::string());
        else if (alt && key(UI::KeyId_Left)) { goBack(false); keysTaken = true; }
        else if (alt && key(UI::KeyId_Right)) { goBack(true); keysTaken = true; }
        else if (shift && key(UI::KeyId_F11)) openPicker(K_Bookmarks);
        else if (key(UI::KeyId_F11) && d) d->code.Edit().ToggleBreakpoint(d->code.Edit().Caret().line);
        else if (ctrl && !shift && key(UI::KeyId_O)) { folderOpen = true; folderFocus = true; std::snprintf(folderBuf, sizeof folderBuf, "%s", folder.c_str()); }
        else if (ctrl && (key(UI::KeyId_W) || key(UI::KeyId_F4))) requestClose(active);
        else if (ctrl && key(UI::KeyId_Tab) && !docs.empty()) active = (active + (shift ? (int)docs.size() - 1 : 1)) % (int)docs.size();
        else if (ctrl && key(UI::KeyId_Q) && cfg.quitItem) requestQuit();
        else if (ctrl && key(UI::KeyId_Equal)) { settings.codeFontPx = std::min(32.0f, settings.codeFontPx + 1.0f); for (auto& doc : docs) applyView(*doc); saveSettings(); }
        else if (ctrl && key(UI::KeyId_Minus)) { settings.codeFontPx = std::max(10.0f, settings.codeFontPx - 1.0f); for (auto& doc : docs) applyView(*doc); saveSettings(); }
        else if (alt && key(UI::KeyId_1)) { settings.showTree = !settings.showTree; saveSettings(); }
        else if (key(UI::KeyId_F8) && cfg.debug && d && d->execLine > 0) cfg.debug("continue");
        else if (key(UI::KeyId_F10) && cfg.debug && d && d->execLine > 0) cfg.debug("step");
        else if (key(UI::KeyId_F7) && d) { check(*d); notice = std::to_string(d->problems.size()) + " problems"; }
        else if (key(UI::KeyId_F5)) run();
        else if (key(UI::KeyId_F9) && d) {   // a breakpoint on the caret's line
            UI::CodeEdit& e = d->code.Edit();
            e.ToggleBreakpoint(e.Caret().line);
        }
        else if (key(UI::KeyId_Escape) && barOpen) barOpen = false;
        else if (key(UI::KeyId_Escape) && panel != P_None && !UI::WantsTextInput()) panel = P_None;
        // Shift pressed twice, with nothing else between: Search Everywhere
        if (!ctrl && !alt && UI::IsKeyPressed(UI::KeyId_Shift, false)) {
            const double t = UI::GetTime();
            shiftCount = (t - shiftAt < 0.45 && shiftCount == 1) ? 2 : 1;
            shiftAt = t;
            if (shiftCount == 2) { shiftCount = 0; openPicker(K_Everywhere); }
        } else if (ctrl || alt || (in.text && in.text[0])) shiftCount = 0;
    }
    noteShown();

    // the script list follows the disk now and then, and a file is checked a short while after its last edit
    if (!folder.empty() && UI::GetTime() >= nextScan) scan();
    if (settings.liveSync && UI::GetTime() >= nextDiskCheck) { nextDiskCheck = UI::GetTime() + 1.0; checkDisk(); }
    d = activeDoc();
    if (d && d->checkedVersion != d->code.Edit().Version() && UI::GetTime() - d->editedAt > 0.4) check(*d);

    const float menuH = 26.0f, tabH = 30.0f, statusH = 24.0f;
    const float panelH = panel == P_None ? 0.0f : std::min(240.0f, std::max(120.0f, H * 0.3f));
    const float treeW = settings.showTree ? std::min(260.0f, std::max(160.0f, W * 0.22f)) : 0.0f;
    const float bodyY = Y + menuH, bodyH = std::max(40.0f, H - menuH - statusH);
    drawMenu(X, Y, W, menuH);
    if (treeW > 0) drawTree(X, bodyY, treeW, bodyH, modal);
    const float cx = X + treeW, cw = std::max(40.0f, W - treeW);
    drawTabs(cx, bodyY, cw, tabH, modal);
    const float fbH = activeDoc() ? findBarHeight() : 0.0f, cfH = conflictHeight();
    drawCode(cx, bodyY + tabH + fbH + cfH, cw, std::max(20.0f, bodyH - tabH - panelH - fbH - cfH), modal);
    if (fbH > 0) drawFindBar(cx, bodyY + tabH, cw, fbH, modal);   // after the code, so its field can take the keyboard from it
    if (cfH > 0) drawConflict(cx, bodyY + tabH + fbH, cw, cfH, modal);
    if (panelH > 0) drawPanel(cx, bodyY + bodyH - panelH, cw, panelH, modal);
    drawStatus(X, Y + H - statusH, W, statusH);
    if (goOpen) drawGoToFile(X, Y, W, H);
    if (folderOpen) drawOpenFolder(X, Y, W, H);
    if (newOpen) drawNewScript(X, Y, W, H);
    if (promptOpen) drawClosePrompt(X, Y, W, H);
    if (picker != K_None) drawPicker(X, Y, W, H);
    if (renameOpen) drawRename(X, Y, W, H);
    if (refOpen) drawQuickReference(X, Y, W, H);
}

// ---- ScriptIdeApp -----------------------------------------------------------------------------------------------------------

ScriptIdeApp::ScriptIdeApp(Config config) : m(new Impl(std::move(config))) {}
ScriptIdeApp::~ScriptIdeApp() { m->saveSettings(); }

bool ScriptIdeApp::OpenFolder(const std::string& folder) { return m->openFolder(folder); }
bool ScriptIdeApp::OpenFile(const std::string& path, int line) { return m->openFile(path, line); }
bool ScriptIdeApp::OpenPath(const std::string& path) { return m->openPath(path); }
bool ScriptIdeApp::OpenDocument(const DocumentSpec& spec, int line) { return m->openDocument(spec, line); }
bool ScriptIdeApp::HasDocument(const std::string& key) const { return m->byKey(key) != nullptr; }
std::string ScriptIdeApp::DocumentText(const std::string& key) const { const Impl::Doc* d = m->byKey(key); return d ? d->code.Edit().GetText() : std::string(); }
bool ScriptIdeApp::DocumentDirty(const std::string& key) const { const Impl::Doc* d = m->byKey(key); return d && m->dirty(*d); }
bool ScriptIdeApp::ReloadDocument(const std::string& key, const std::string& text) {
    Impl::Doc* d = m->byKey(key);
    if (!d) return false;
    d->crlf = text.find("\r\n") != std::string::npos;
    d->code.SetText(text);
    d->saved = d->code.Edit().GetText();
    d->seenVersion = d->code.Edit().Version();
    m->check(*d);
    return true;
}
bool ScriptIdeApp::SetBreakpoints(const std::string& key, const std::vector<int>& lines) {
    Impl::Doc* d = m->byKey(key);
    if (!d) return false;
    std::vector<int> z;
    for (int l : lines) if (l >= 1) z.push_back(l - 1);
    d->code.Edit().SetBreakpoints(z);
    d->bpSeen = d->code.Edit().Breakpoints();   // the host set them: nothing to report back
    return true;
}
std::vector<int> ScriptIdeApp::Breakpoints(const std::string& key) const {
    std::vector<int> out;
    if (const Impl::Doc* d = m->byKey(key)) for (int l : d->code.Edit().Breakpoints()) out.push_back(l + 1);
    return out;
}
bool ScriptIdeApp::SetExecutionLine(const std::string& key, int line) {
    for (std::size_t i = 0; i < m->docs.size(); ++i) {
        Impl::Doc& d = *m->docs[i];
        if (d.key != key) continue;
        d.execLine = std::max(0, line);
        m->applyMarks(d);
        if (line > 0) { m->active = (int)i; d.gotoLine = line; }
        return true;
    }
    return false;
}
bool ScriptIdeApp::CompletionOpen() const { return m->acOpen; }
const std::vector<CompletionItem>& ScriptIdeApp::CompletionList() const { return m->acItems; }
void ScriptIdeApp::Frame(float x, float y, float w, float h) { appdetail::FollowDesignColours(); m->frame(x, y, w, h); }
bool ScriptIdeApp::Save() { Impl::Doc* d = m->activeDoc(); return d && m->save(*d); }
bool ScriptIdeApp::SaveAll() { return m->saveAll(); }
void ScriptIdeApp::Close(const std::string& key) {
    for (std::size_t i = 0; i < m->docs.size(); ++i) if (m->docs[i]->key == key) { m->close((int)i); return; }
    const std::string p = Normal(key);   // a file's path as the caller wrote it
    for (std::size_t i = 0; i < m->docs.size(); ++i) if (m->docs[i]->key == p) { m->close((int)i); return; }
}
std::size_t ScriptIdeApp::FindInFiles(const std::string& needle, const FindOptions& options) {
    std::snprintf(m->findBuf, sizeof m->findBuf, "%s", needle.c_str());
    m->findOpt = options;
    m->panel = Impl::P_Find;
    return m->findInFiles(needle, options);
}
const std::vector<FileMatch>& ScriptIdeApp::FindResults() const { return m->findResults; }
bool ScriptIdeApp::OpenResult(std::size_t index) {
    if (index >= m->findResults.size()) return false;
    const FileMatch r = m->findResults[index];
    return m->openFile(r.file, r.match.line);
}
std::vector<HistoryVersion> ScriptIdeApp::HistoryOfActive() const {
    const Impl::Doc* d = m->activeDoc();
    return d && !d->path.empty() ? m->history.Versions(d->path) : std::vector<HistoryVersion>();
}
bool ScriptIdeApp::RestoreVersion(const HistoryVersion& v) { return m->restore(v); }
bool ScriptIdeApp::WantsQuit() const { return m->quit; }
std::string ScriptIdeApp::WindowTitle() const {
    std::string t;
    if (const Impl::Doc* d = m->activeDoc()) t = (m->dirty(*d) ? "*" : "") + d->label + " - ";
    if (!m->folder.empty()) t += fs::u8path(m->folder).filename().u8string() + " - ";
    return t + "Script Editor";
}
void ScriptIdeApp::SaveSettings() { m->saveSettings(); }
const std::string& ScriptIdeApp::Folder() const { return m->folder; }
const std::vector<std::string>& ScriptIdeApp::ProjectScripts() const { return m->scripts; }
std::vector<std::string> ScriptIdeApp::OpenFiles() const {
    std::vector<std::string> out;
    for (const auto& d : m->docs) out.push_back(d->path.empty() ? d->key : d->path);
    return out;
}
std::string ScriptIdeApp::ActiveKey() const { const Impl::Doc* d = m->activeDoc(); return d ? d->key : std::string(); }
std::string ScriptIdeApp::ActivePath() const { const Impl::Doc* d = m->activeDoc(); return d ? d->path : std::string(); }
std::string ScriptIdeApp::ActiveText() const { const Impl::Doc* d = m->activeDoc(); return d ? d->code.Edit().GetText() : std::string(); }
bool ScriptIdeApp::ActiveDirty() const { const Impl::Doc* d = m->activeDoc(); return d && m->dirty(*d); }
OkayUI::CodeEdit* ScriptIdeApp::ActiveEdit() { Impl::Doc* d = m->activeDoc(); return d ? &d->code.Edit() : nullptr; }
const std::vector<Diagnostic>& ScriptIdeApp::ActiveProblems() const {
    static const std::vector<Diagnostic> none;
    const Impl::Doc* d = m->activeDoc();
    return d ? d->problems : none;
}
void ScriptIdeApp::CheckActiveNow() { if (Impl::Doc* d = m->activeDoc()) m->check(*d); }
const std::string& ScriptIdeApp::Notice() const { return m->notice; }
AppSettings& ScriptIdeApp::Settings() { return m->settings; }
void ScriptIdeApp::ApplySettings() { for (auto& doc : m->docs) m->applyView(*doc); }

} // namespace scriptide
