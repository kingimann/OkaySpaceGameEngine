// Script Editor (ScriptIdeApp.hpp): the script editor drawn with OkayUI inside a rectangle, shared by the Script Editor program
// (scripteditor/main.cpp) and the editor's window (editor/ScriptEditorAppHost.cpp). The decisions are the headless model's
// (editor/ScriptIde.hpp) and the engine checkers'; this file keeps the open files and draws them.
//
// Written from general editor behaviour (a file tree beside tabs, a search panel, a status line, a history of saved copies) and
// the OkayUI and OkayUI::CodeEdit interfaces; no code, names, icons or look from any other editor (rule 19a).
#include "ScriptIdeApp.hpp"

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
                    ",\n  \"codeFontPx\": " + std::to_string((int)codeFontPx) + ",\n  \"lastFolder\": ";
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

namespace {
// The language id the engine's VMs use for a file ("okayscript", "lua", "javascript"); "" when the engine has none.
std::string EngineLanguageOf(const std::string& path) {
    const std::string l = LanguageForPath(path);
    if (!l.empty()) return l;
    std::string ext = fs::u8path(path).extension().u8string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".js" ? "javascript" : std::string();
}

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
    return made.emplace(language, std::move(w)).first->second;
}

// ---- the app ----------------------------------------------------------------------------------------------------------------

namespace {
UI::Color32 Rgb(int r, int g, int b, int a = 255) { return UI::RGBA((unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a); }
// The Script Editor's own colours: a plain dark scheme.
const UI::Color32 kBack = Rgb(30, 32, 37), kPanel = Rgb(37, 40, 46), kStrip = Rgb(26, 28, 32), kLine = Rgb(56, 60, 69),
                  kText = Rgb(214, 218, 226), kDim = Rgb(140, 146, 158), kAccent = Rgb(86, 156, 214), kTabOn = Rgb(44, 48, 56),
                  kHover = Rgb(52, 57, 66), kError = Rgb(232, 102, 92), kWarn = Rgb(226, 182, 76), kAdd = Rgb(98, 180, 110);

std::string FileText(const std::string& path, bool& ok, std::string* why = nullptr) {
    ok = false;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (!fs::is_regular_file(p, ec)) { if (why) *why = "not a file"; return std::string(); }
    const auto size = fs::file_size(p, ec);
    if (ec || size > (8u << 20)) { if (why) *why = "too big to open here (over 8 MB)"; return std::string(); }   // checked before reading
    std::ifstream f(p, std::ios::binary);
    if (!f) { if (why) *why = "cannot be read"; return std::string(); }
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.find('\0') != std::string::npos) { if (why) *why = "is not a text file"; return std::string(); }
    ok = true;
    return s;
}

bool WriteText(const std::string& path, const std::string& text) {
    const fs::path p = fs::u8path(path), tmp = fs::u8path(path + ".saving");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), (std::streamsize)text.size());
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);   // the old file is only replaced by a complete new one
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

std::string Fit(const std::string& text, float maxW) {
    if (UI::CalcTextWidth(text.c_str()) <= maxW) return text;
    std::string t = text;
    while (!t.empty() && UI::CalcTextWidth((t + "...").c_str()) > maxW) t.pop_back();
    return t + "...";
}

std::string TimeText(std::int64_t ms) {
    const std::time_t t = (std::time_t)(ms / 1000);
    char buf[40] = "";
    if (const std::tm* tmv = std::localtime(&t)) std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tmv);   // the UI thread only
    return buf;
}

std::string Normal(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    fs::path a = fs::weakly_canonical(p, ec);
    if (ec) a = fs::absolute(p, ec);
    return (ec ? p : a).generic_u8string();
}
} // namespace

struct ScriptIdeApp::Impl {
    struct Doc {
        std::string path, language, label;
        scripteditor::ScriptEditorCodeEdit code;
        std::string saved;              // the file's text as the widget holds it (newlines as '\n')
        bool crlf = false;              // the file used "\r\n": written back that way
        std::vector<Diagnostic> problems;
        unsigned seenVersion = 0, checkedVersion = ~0u;
        double editedAt = 0.0;
        int gotoLine = 0;
    };

    Config cfg;
    AppSettings settings;
    std::string settingsWritten;
    std::string folder;
    std::vector<std::string> scripts;
    double nextScan = 0.0;
    LocalHistory history;
    std::vector<std::unique_ptr<Doc>> docs;
    int active = -1;
    std::string notice;
    bool quit = false;

    // the bottom panel
    enum Panel { P_None, P_Find, P_Problems, P_History };
    int panel = P_None;
    char findBuf[256] = "";
    FindOptions findOpt;
    std::vector<FileMatch> findResults;
    std::string findError;
    bool findFocus = false;
    std::vector<HistoryVersion> versions;
    int versionSel = -1;
    std::vector<DiffLine> diff;
    // the tree
    char treeFilter[96] = "";
    std::set<std::string> collapsed;
    // Go to File
    bool goOpen = false;
    char goBuf[160] = "";
    int goSel = 0;
    RecentFiles recent{30};
    // Open Folder
    bool folderOpen = false, folderFocus = false;
    char folderBuf[1024] = "";

    explicit Impl(Config c) : cfg(std::move(c)) {
        if (!cfg.settingsPath.empty()) {
            bool ok = false;
            const std::string text = FileText(cfg.settingsPath, ok);
            if (ok) settings = AppSettings::Parse(text);
        }
        settingsWritten = settings.Serialize();
    }

    std::int64_t now() const {
        if (cfg.nowMs) return cfg.nowMs();
        return (std::int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void saveSettings() {
        const std::string text = settings.Serialize();
        if (cfg.settingsPath.empty() || text == settingsWritten) return;
        std::error_code ec;
        fs::create_directories(fs::u8path(cfg.settingsPath).parent_path(), ec);
        if (WriteText(cfg.settingsPath, text)) settingsWritten = text;
    }

    Doc* activeDoc() { return active >= 0 && active < (int)docs.size() ? docs[(std::size_t)active].get() : nullptr; }
    const Doc* activeDoc() const { return active >= 0 && active < (int)docs.size() ? docs[(std::size_t)active].get() : nullptr; }

    std::string relative(const std::string& path) const {
        if (!folder.empty() && path.size() > folder.size() + 1 && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/')
            return path.substr(folder.size() + 1);
        return fs::u8path(path).filename().u8string();
    }

    void scan() {
        scripts = folder.empty() ? std::vector<std::string>() : ListProjectScripts(folder);
        for (std::string& s : scripts) s = fs::u8path(s).generic_u8string();
        nextScan = UI::GetTime() + 3.0;
    }

    bool openFolder(const std::string& path) {
        std::error_code ec;
        if (path.empty() || !fs::is_directory(fs::u8path(path), ec)) { notice = "Not a folder: " + path; return false; }
        folder = Normal(path);
        history.SetProject(folder);
        scan();
        settings.NoteFolder(folder);
        saveSettings();
        notice = "Opened " + folder + " (" + std::to_string(scripts.size()) + " scripts)";
        return true;
    }

    const scripteditor::LanguageWords& wordsFor(const std::string& language) {
        if (cfg.wordsFor) if (const scripteditor::LanguageWords* w = cfg.wordsFor(language)) return *w;
        return DefaultWordsFor(language);
    }

    void applyView(Doc& d) {
        d.code.SetTabSize(settings.tabSize);
        d.code.SetWordWrap(settings.wordWrap);
        d.code.SetShowWhitespace(settings.showWhitespace);
        if (cfg.useCodeFont) d.code.UseCodeFont(settings.codeFontPx);
    }

    bool openFile(const std::string& rawPath, int line) {
        const std::string path = Normal(rawPath);
        for (std::size_t i = 0; i < docs.size(); ++i)
            if (docs[i]->path == path) { active = (int)i; if (line > 0) docs[i]->gotoLine = line; return true; }
        bool ok = false;
        std::string why;
        const std::string text = FileText(path, ok, &why);
        if (!ok) { notice = fs::u8path(path).filename().u8string() + " " + why + "."; return false; }
        std::unique_ptr<Doc> d(new Doc());
        d->path = path;
        d->language = EngineLanguageOf(path);
        d->label = fs::u8path(path).filename().u8string();
        d->crlf = text.find("\r\n") != std::string::npos;
        d->code.SetLanguage(wordsFor(d->language));
        if (cfg.palette) d->code.SetPalette(*cfg.palette);
        d->code.Edit().SetFolding(true);
        applyView(*d);
        d->code.SetText(text);
        d->saved = d->code.Edit().GetText();
        d->seenVersion = d->code.Edit().Version();
        d->gotoLine = line;
        docs.push_back(std::move(d));
        active = (int)docs.size() - 1;
        recent.Push(path);
        // Local History starts with the file as it was opened, so the first edits can always be undone from there (and the
        // first keystroke does not count as five minutes of edits)
        if (history.Versions(path).empty()) history.Record(path, text, now(), "Opened");
        else history.MaybeRecord(path, text, now());
        check(*docs.back());
        notice.clear();
        return true;
    }

    bool openPath(const std::string& path) {
        std::error_code ec;
        const fs::path p = fs::u8path(path);
        if (fs::is_directory(p, ec)) return openFolder(path);
        if (fs::is_regular_file(p, ec)) {
            if (folder.empty()) openFolder(Normal(path).substr(0, Normal(path).find_last_of('/')));
            return openFile(path, 0);
        }
        notice = "Not found: " + path;
        return false;
    }

    std::string diskText(const Doc& d) const {
        std::string t = d.code.Edit().GetText();
        if (!d.crlf) return t;
        std::string out;
        out.reserve(t.size() + t.size() / 16);
        for (char c : t) { if (c == '\n') out += '\r'; out += c; }
        return out;
    }

    bool dirty(const Doc& d) const { return d.code.Edit().GetText() != d.saved; }

    bool save(Doc& d) {
        const std::string text = diskText(d);
        if (!WriteText(d.path, text)) { notice = "Could not save " + d.path; return false; }
        d.saved = d.code.Edit().GetText();
        history.Record(d.path, text, now(), "Saved");
        check(d);
        notice = "Saved " + relative(d.path);
        if (cfg.saved) cfg.saved(d.path, text);
        if (panel == P_History && activeDoc() == &d) loadVersions();
        return true;
    }

    bool saveAll() {
        bool ok = true;
        int n = 0;
        for (auto& d : docs) if (dirty(*d)) { ok = save(*d) && ok; ++n; }
        if (ok) notice = n ? "Saved " + std::to_string(n) + (n == 1 ? " file" : " files") : "Nothing to save";
        return ok;
    }

    void close(int index) {
        if (index < 0 || index >= (int)docs.size()) return;
        docs.erase(docs.begin() + index);
        if (active >= (int)docs.size()) active = (int)docs.size() - 1;
        else if (active > index) --active;
        if (panel == P_History) loadVersions();
    }

    void check(Doc& d) {
        const std::string text = d.code.Edit().GetText();
        d.problems = cfg.check ? cfg.check(d.language, d.path, text) : CheckScript(d.language, text);
        if (cfg.moreProblems) d.problems = cfg.moreProblems(d.language, d.path, text, std::move(d.problems));
        d.checkedVersion = d.code.Edit().Version();
        std::map<int, std::string> marks;
        for (const Diagnostic& p : d.problems) {
            if (p.line < 1) continue;
            auto it = marks.find(p.line);
            const std::string msg = (p.severity == 0 ? "" : "warning: ") + p.message;
            if (it == marks.end()) marks[p.line] = msg;
            else if (p.severity == 0 && it->second.rfind("warning: ", 0) == 0) it->second = msg;   // an error wins over a warning
        }
        d.code.SetErrorMarkers(marks);
    }

    std::size_t findInFiles(const std::string& needle, const FindOptions& opt) {
        findResults.clear();
        findError.clear();
        if (needle.empty()) return 0;
        std::map<std::string, std::string> texts;
        for (const std::string& s : scripts) {
            bool ok = false;
            std::string t = FileText(s, ok);
            if (ok) texts[s] = std::move(t);
        }
        for (const auto& d : docs) texts[d->path] = d->code.Edit().GetText();   // open files: what the tab holds
        findResults = FindInTexts(texts, needle, opt, 2000, &findError);
        notice = findError.empty() ? std::to_string(findResults.size()) + " matches in the project" : "Bad pattern: " + findError;
        return findResults.size();
    }

    void loadVersions() {
        versions.clear(); diff.clear(); versionSel = -1;
        if (const Doc* d = activeDoc()) versions = history.Versions(d->path);
    }

    void showVersion(int i) {
        versionSel = i;
        diff.clear();
        const Doc* d = activeDoc();
        if (!d || i < 0 || i >= (int)versions.size()) return;
        diff = LineDiff(history.Load(versions[(std::size_t)i]), d->code.Edit().GetText());
    }

    bool restore(const HistoryVersion& v) {
        Doc* d = activeDoc();
        if (!d || v.file.empty()) return false;
        std::string text = history.Load(v);
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        UI::CodeEdit& e = d->code.Edit();
        e.SelectAll();
        e.BreakUndoGroup();
        e.InsertText(text.c_str());   // one undo step: Ctrl+Z brings the text before the restore back
        e.BreakUndoGroup();
        notice = "Restored the copy of " + TimeText(v.time) + " (not saved yet)";
        return true;
    }

    void openPanel(int p) {
        panel = panel == p ? P_None : p;
        if (panel == P_History) loadVersions();
        if (panel == P_Find) findFocus = true;
    }

    void run() {
        const Doc* d = activeDoc();
        if (!d) return;
        if (!cfg.run) { notice = "This program edits and checks scripts; open the project in the editor to run them."; return; }
        notice = cfg.run(d->path);
    }

    void frame(float X, float Y, float W, float H);
    void drawMenu(float X, float Y, float W, float h);
    void drawTree(float X, float Y, float W, float H, bool modal);
    void drawTabs(float X, float Y, float W, float h, bool modal);
    void drawCode(float X, float Y, float W, float H, bool modal);
    void drawPanel(float X, float Y, float W, float H, bool modal);
    void drawStatus(float X, float Y, float W, float h);
    void drawGoToFile(float X, float Y, float W, float H);
    void drawOpenFolder(float X, float Y, float W, float H);
};

void ScriptIdeApp::Impl::drawMenu(float X, float Y, float W, float h) {
    const std::string pre = cfg.idPrefix;
    UI::BareNextWindow();
    if (UI::Begin((pre + "_menu").c_str(), X, Y, W, h)) {
        UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        Doc* d = activeDoc();
        if (UI::BeginMenuBar()) {
            if (UI::BeginMenu("File")) {
                if (UI::MenuItem("Open Folder...", "Ctrl+Shift+O")) { folderOpen = true; folderFocus = true; std::snprintf(folderBuf, sizeof folderBuf, "%s", folder.c_str()); }
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
                if (UI::MenuItem("Close Tab", "Ctrl+W", false, d != nullptr)) close(active);
                if (cfg.quitItem) { UI::MenuSeparator(); if (UI::MenuItem("Quit", "Ctrl+Q")) quit = true; }
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
                if (UI::MenuItem("Find in Files", "Ctrl+Shift+F", panel == P_Find)) openPanel(P_Find);
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
                if (changed) { for (auto& doc : docs) applyView(*doc); saveSettings(); }
                UI::EndMenu();
            }
            if (UI::BeginMenu("Run")) {
                if (UI::MenuItem("Check", "F7", false, d != nullptr)) { if (d) { check(*d); notice = std::to_string(d->problems.size()) + " problems"; } }
                if (UI::MenuItem(cfg.runLabel.c_str(), "F5", false, d != nullptr && (bool)cfg.run)) run();
                if (cfg.showInHost && UI::MenuItem(("Show in " + cfg.hostName).c_str(), nullptr, false, d != nullptr))
                    cfg.showInHost(d->path, d->code.Edit().Caret().line + 1);
                UI::EndMenu();
            }
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
        } else {
            UI::SetNextItemWidth(W - 16);
            UI::InputTextWithHint("##treefilter", "Filter", treeFilter, (int)sizeof treeFilter);
            UI::SetCursorScreenPos(UI::Vec2(X + 4, UI::GetCursorScreenPos().y + 2));
            if (UI::BeginChild((pre + "_treelist").c_str(), W - 8, std::max(20.0f, Y + H - UI::GetCursorScreenPos().y - 4), false)) {
                const std::vector<TreeRow> rows = BuildFileTree(scripts, folder, treeFilter);
                const std::string activePath = activeDoc() ? activeDoc()->path : std::string();
                int hiddenBelow = -1;   // rows deeper than this are inside a closed folder
                for (const TreeRow& r : rows) {
                    if (hiddenBelow >= 0 && r.depth > hiddenBelow) continue;
                    hiddenBelow = -1;
                    const bool shut = r.folder && treeFilter[0] == 0 && collapsed.count(r.path) != 0;
                    UI::Indent((float)r.depth * 12.0f);
                    const std::string label = (r.folder ? (shut ? "+ " : "- ") : "  ") + r.name + "##" + r.path;
                    if (UI::Selectable(label.c_str(), !r.folder && r.path == activePath) && !modal) {
                        if (r.folder) { if (shut) collapsed.erase(r.path); else collapsed.insert(r.path); }
                        else openFile(r.path, 0);
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
        if (toClose >= 0) close(toClose);
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
            line(folder.empty() ? "Open a folder: File > Open Folder (Ctrl+Shift+O)" : "Pick a script in the tree", kText);
            line("Go to File: Ctrl+Shift+N    Find in Files: Ctrl+Shift+F", kDim);
            line("Save: Ctrl+S    Close tab: Ctrl+W    Project tree: Alt+1", kDim);
        } else {
            UI::CodeEdit& e = d->code.Edit();
            if (d->gotoLine > 0) { e.GoToLine(d->gotoLine - 1); e.Focus(); d->gotoLine = 0; }
            e.SetReadOnly(modal);
            UI::SetCursorScreenPos(UI::Vec2(X, Y));
            e.Draw((pre + "_edit").c_str(), W, H);
            e.SetReadOnly(false);
            if (e.Version() != d->seenVersion) {
                d->seenVersion = e.Version();
                d->editedAt = UI::GetTime();
                history.MaybeRecord(d->path, diskText(*d), now());
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
        const std::string left = notice.empty() ? (d ? relative(d->path) : folder) : notice;
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
    const UI::Input& in = UI::GetFrameInput();
    const bool ctrl = in.ctrl, shift = in.shift, alt = UI::IsKeyDown(UI::KeyId_Alt);
    if (ctrl && !alt) UI::GetIO().InputQueueCharacters.resize(0);   // Ctrl+<key> is a shortcut, never typing
    const bool modal = goOpen || folderOpen;
    auto key = [](UI::KeyId k) { return UI::IsKeyPressed(k, false); };
    Doc* d = activeDoc();
    if (!modal) {
        if (ctrl && shift && key(UI::KeyId_S)) saveAll();
        else if (ctrl && key(UI::KeyId_S)) { if (d) save(*d); }
        else if (ctrl && shift && key(UI::KeyId_N) && !folder.empty()) { goOpen = true; goBuf[0] = 0; goSel = 0; }
        else if (ctrl && shift && key(UI::KeyId_F)) { panel = P_Find; findFocus = true; }
        else if (ctrl && shift && key(UI::KeyId_O)) { folderOpen = true; folderFocus = true; std::snprintf(folderBuf, sizeof folderBuf, "%s", folder.c_str()); }
        else if (ctrl && (key(UI::KeyId_W) || key(UI::KeyId_F4))) close(active);
        else if (ctrl && key(UI::KeyId_Tab) && !docs.empty()) active = (active + (shift ? (int)docs.size() - 1 : 1)) % (int)docs.size();
        else if (ctrl && key(UI::KeyId_Q) && cfg.quitItem) quit = true;
        else if (ctrl && key(UI::KeyId_Equal)) { settings.codeFontPx = std::min(32.0f, settings.codeFontPx + 1.0f); for (auto& doc : docs) applyView(*doc); saveSettings(); }
        else if (ctrl && key(UI::KeyId_Minus)) { settings.codeFontPx = std::max(10.0f, settings.codeFontPx - 1.0f); for (auto& doc : docs) applyView(*doc); saveSettings(); }
        else if (alt && key(UI::KeyId_1)) { settings.showTree = !settings.showTree; saveSettings(); }
        else if (key(UI::KeyId_F7) && d) { check(*d); notice = std::to_string(d->problems.size()) + " problems"; }
        else if (key(UI::KeyId_F5)) run();
        else if (key(UI::KeyId_Escape) && panel != P_None && !UI::WantsTextInput()) panel = P_None;
    }

    // the script list follows the disk now and then, and a file is checked a short while after its last edit
    if (!folder.empty() && UI::GetTime() >= nextScan) scan();
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
    drawCode(cx, bodyY + tabH, cw, std::max(20.0f, bodyH - tabH - panelH), modal);
    if (panelH > 0) drawPanel(cx, bodyY + bodyH - panelH, cw, panelH, modal);
    drawStatus(X, Y + H - statusH, W, statusH);
    if (goOpen) drawGoToFile(X, Y, W, H);
    if (folderOpen) drawOpenFolder(X, Y, W, H);
}

// ---- ScriptIdeApp -----------------------------------------------------------------------------------------------------------

ScriptIdeApp::ScriptIdeApp(Config config) : m(new Impl(std::move(config))) {}
ScriptIdeApp::~ScriptIdeApp() { m->saveSettings(); }

bool ScriptIdeApp::OpenFolder(const std::string& folder) { return m->openFolder(folder); }
bool ScriptIdeApp::OpenFile(const std::string& path, int line) { return m->openFile(path, line); }
bool ScriptIdeApp::OpenPath(const std::string& path) { return m->openPath(path); }
void ScriptIdeApp::Frame(float x, float y, float w, float h) { m->frame(x, y, w, h); }
bool ScriptIdeApp::Save() { Impl::Doc* d = m->activeDoc(); return d && m->save(*d); }
bool ScriptIdeApp::SaveAll() { return m->saveAll(); }
void ScriptIdeApp::Close(const std::string& path) {
    const std::string p = Normal(path);
    for (std::size_t i = 0; i < m->docs.size(); ++i) if (m->docs[i]->path == p) { m->close((int)i); return; }
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
    return d ? m->history.Versions(d->path) : std::vector<HistoryVersion>();
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
    for (const auto& d : m->docs) out.push_back(d->path);
    return out;
}
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

} // namespace scriptide
