#pragma once
// Script Editor (ScriptIdeApp.hpp): the inside of the app, shared by its source files (ScriptIdeApp.cpp: documents, checks, the
// main layout; ScriptIdeAppScene.cpp: the host's scripts, New Script, the close prompt, reopening tabs, the language; ScriptIdeAppNav.cpp:
// find and replace, go to line / symbol / definition, references, back and forward, the switcher, bookmarks, Search Everywhere and
// the command list). Not a public header: only these files include it.
//
// Written from general editor behaviour and the OkayUI / OkayUI::CodeEdit interfaces; no code, names, icons or look from any other
// editor (rule 19a).
#include "okay/UI/OkayUIDesign.hpp"
#include "ScriptIdeApp.hpp"
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

std::string EngineLanguageOf(const std::string& path);   // ScriptIdeApp.cpp

namespace appdetail {
inline UI::Color32 Rgb(int r, int g, int b, int a = 255) { return UI::RGBA((unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a); }
// The Script Editor's chrome colours: the design in force (okay/UI/OkayUIDesign.hpp), taken from OkayUI's theme at the start of
// every frame (FollowDesignColours), so the app matches the launcher and the editor in every design and scheme.
inline UI::Color32 kBack = Rgb(30, 32, 37), kPanel = Rgb(37, 40, 46), kStrip = Rgb(26, 28, 32), kLine = Rgb(56, 60, 69),
                   kText = Rgb(214, 218, 226), kDim = Rgb(140, 146, 158), kAccent = Rgb(86, 156, 214), kTabOn = Rgb(44, 48, 56),
                   kHover = Rgb(52, 57, 66), kError = Rgb(232, 102, 92), kWarn = Rgb(226, 182, 76), kAdd = Rgb(98, 180, 110);
inline void FollowDesignColours() {
    const UI::Theme& t = UI::Style();
    auto c = [](const unsigned char* v) { return OkayUI::design::Packed(v); };
    auto mix = [](const unsigned char* a, const unsigned char* b, float k) { return OkayUI::design::PackedMix(a, b, k); };
    kBack = c(t.panel); kPanel = c(t.raised); kStrip = c(t.track); kLine = mix(t.panel, t.text, 0.14f);
    kText = c(t.text); kDim = c(t.textDim); kAccent = c(t.accent); kTabOn = c(t.bg); kHover = c(t.bgHover);
    kError = c(t.danger); kWarn = c(t.warning); kAdd = c(t.success);
}

inline std::string FileText(const std::string& path, bool& ok, std::string* why = nullptr) {
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

inline bool WriteText(const std::string& path, const std::string& text) {
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

inline std::string Fit(const std::string& text, float maxW) {
    if (UI::CalcTextWidth(text.c_str()) <= maxW) return text;
    std::string t = text;
    while (!t.empty() && UI::CalcTextWidth((t + "...").c_str()) > maxW) t.pop_back();
    return t + "...";
}

inline std::string TimeText(std::int64_t ms) {
    const std::time_t t = (std::time_t)(ms / 1000);
    char buf[40] = "";
    if (const std::tm* tmv = std::localtime(&t)) std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tmv);   // the UI thread only
    return buf;
}

inline std::string Normal(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    fs::path a = fs::weakly_canonical(p, ec);
    if (ec) a = fs::absolute(p, ec);
    return (ec ? p : a).generic_u8string();
}
} // namespace appdetail

using namespace appdetail;

struct ScriptIdeApp::Impl {
    struct Doc {
        std::string key, path, language, label;
        std::function<bool(const std::string&, std::string&)> saveFn;   // the host saves it (DocumentSpec::save)
        std::map<int, std::string> marks;   // the checks' line marks (1-based)
        int execLine = 0;                   // the paused script's line (1-based), 0 = none
        std::vector<int> bpSeen;            // the breakpoints the host was last told about (0-based)
        scripteditor::ScriptEditorCodeEdit code;
        std::string saved;              // the file's text as the widget holds it (newlines as '\n')
        bool crlf = false;              // the file used "\r\n": written back that way
        std::vector<Diagnostic> problems;
        unsigned seenVersion = 0, checkedVersion = ~0u;
        double editedAt = 0.0;
        int gotoLine = 0;
        fs::file_time_type mtime{};     // the file's time when it was last read or written here
        bool hasMtime = false, conflict = false;   // conflict: changed on disk while the tab has edits
        unsigned marksVersion = ~0u;    // the changed-line marks below are of this text version
        std::vector<char> lineMarks;    // per line: 1 changed or added since the save, 2 lines deleted above it
        unsigned outlineVersion = ~0u;
        std::vector<Symbol> outline;    // the file's symbols, for the sticky header and the breadcrumb
        std::string shownFind;          // what the widget highlights (the find bar's text; "" none)
        bool shownFindCase = false;
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
    enum Panel { P_None, P_Find, P_Problems, P_History, P_Refs };
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
    // completion
    bool acOpen = false;
    std::vector<CompletionItem> acItems;
    int acSel = 0;
    std::string acPrefix;
    // Open Folder
    bool folderOpen = false, folderFocus = false;
    char folderBuf[1024] = "";
    // New Script
    bool newOpen = false, newFocus = false, newAttach = false;
    char newName[96] = "NewScript";
    int newLang = 0, newTpl = 0;
    // "Save changes?" (closing a changed tab, or quitting with changed tabs)
    bool promptOpen = false, promptQuit = false;
    std::string promptKey;
    // closed tabs, newest last (Ctrl+Shift+T)
    struct Closed { std::string key, path; int line = 0; };
    std::vector<Closed> closed;

    // ---- find and navigation (ScriptIdeAppNav.cpp) ----
    // the find bar over the code (Ctrl+F, Ctrl+H with Replace)
    bool barOpen = false, barFocus = false, barReplace = false;
    char barFind[256] = "", barWith[256] = "";
    FindOptions barOpt;
    std::string barError;
    struct BarCache { std::string key, needle; unsigned version = ~0u; bool regex = false, matchCase = false, word = false; std::vector<TextMatch> hits; } barCache;
    char fifWith[256] = "";   // Find in Files: Replace with
    // a list to pick from (Go to Line, Go to Symbol, the switcher, the bookmarks, Search Everywhere, the commands)
    enum Picker { K_None, K_Line, K_Symbol, K_Switch, K_Bookmarks, K_Everywhere, K_Commands, K_Snippets, K_Api };
    int picker = K_None;
    char pickBuf[160] = "";
    int pickSel = 0;
    bool pickFocus = false;
    SymbolIndex pickIndex;   // built when Search Everywhere opens
    struct PickItem { std::string label, detail; std::function<void()> act; };
    // the references of a word (Shift+F12)
    std::vector<FileMatch> refs;
    std::string refsWord;
    // back and forward: the places jumped from
    struct Place { std::string key; int line = 0; };
    std::vector<Place> back, forward;
    std::vector<std::string> mru;   // open documents, the shown one first (Ctrl+E)
    double shiftAt = -10.0;         // Search Everywhere on Shift pressed twice
    int shiftCount = 0;

    // ---- edits (ScriptIdeAppEdit.cpp) ----
    bool renameOpen = false, renameFocus = false, renameAll = true;
    char renameBuf[128] = "";
    std::string renameFrom;
    RenamePlan renamePlan;
    std::string renamePlanned;          // "<from>\n<to>\n<all>" the preview was made for
    bool fixesOpen = false, fixesFresh = false;   // fresh: opened this frame (the key that opened it is not a pick)
    std::vector<QuickFix> fixes;
    int fixSel = 0;
    std::map<std::string, int> taken;   // completion items taken this session (taken before: first)
    std::string sigText, sigDoc;        // the signature help shown this frame
    bool keysTaken = false;             // a shortcut of the app used this frame's keys: the code widget does not see them

    bool formatDocument();
    RenamePlan planRename(const std::string& from, const std::string& to, bool all);
    int applyRename(const std::string& from, const std::string& to, bool all);
    void openRename(const std::string& word);
    void drawRename(float X, float Y, float W, float H);
    std::vector<QuickFix> fixesAtCaret();
    bool applyFix(const QuickFix& f);
    void drawQuickFixes();
    void drawLightBulb(Doc& d);
    bool joinLines();
    bool expandSelection();
    void updateSignature(Doc& d);
    void drawSignature(Doc& d);
    void setText(Doc& d, const std::string& text);   // the whole text, one undo step, the caret kept on its line

    // ---- the view, the disk and Insert (ScriptIdeAppView.cpp) ----
    double nextDiskCheck = 0.0;
    bool refOpen = false;   // Help > C# Quick Reference
    void noteDiskTime(Doc& d);
    void checkDisk();
    bool reloadFromDisk(Doc& d);
    const std::vector<char>& changedMarks(Doc& d);
    const std::vector<Symbol>& outlineOf(Doc& d);
    const Symbol* enclosing(Doc& d, int line0);
    void drawOverlays(Doc& d);
    float minimapWidth() const { return settings.showMinimap ? 72.0f : 0.0f; }
    void drawMinimap(Doc& d, float X, float Y, float W, float H, bool modal);
    float conflictHeight() const { const Doc* d = activeDoc(); return d && d->conflict ? 34.0f : 0.0f; }
    void drawConflict(float X, float Y, float W, float h, bool modal);
    void drawQuickReference(float X, float Y, float W, float H);
    void drawInsertMenu(Doc* d);
    void drawHelpMenu();
    void drawViewExtras(Doc* d, bool& changed);
    std::vector<Snippet> snippetsFor(const std::string& language);
    std::vector<ApiEntry> apiFor(const std::string& language);
    void insertAtCaret(Doc& d, const std::string& text);

    bool modalOpen() const { return goOpen || folderOpen || newOpen || promptOpen || picker != K_None || renameOpen || refOpen; }
    void openFindBar(bool replace);
    const std::vector<TextMatch>& barHits(Doc& d);
    bool findStep(bool forward);
    bool replaceOne();
    int replaceAllInDoc();
    std::size_t replaceInFiles(const std::string& needle, const std::string& with, const FindOptions& opt);
    void drawFindBar(float X, float Y, float W, float h, bool modal);
    float findBarHeight() const { return barOpen ? (barReplace ? 66.0f : 36.0f) : 0.0f; }
    std::string wordAt(const Doc& d, UI::CodePos p) const;
    SymbolIndex buildIndex() const;
    bool jumpTo(const std::string& keyOrPath, int line, bool record = true);
    void notePlace();
    bool goBack(bool fwd);
    bool goToDefinition(const std::string& word);
    std::size_t findReferences(const std::string& word);
    void openPicker(int which);
    std::vector<PickItem> pickItems();
    void drawPicker(float X, float Y, float W, float H);
    std::vector<AppCommand> commands();
    void noteShown();
    // The languages the language menu and New Script offer.
    std::vector<std::string> languages() const;
    // ScriptIdeAppScene.cpp
    void requestClose(int index);
    void requestQuit();
    void answerPrompt(int answer);
    bool reopenClosed();
    bool setLanguage(Doc& d, const std::string& language);
    bool createScript(const std::string& typed, const std::string& language, std::size_t tpl, bool attach, std::string* outPath);
    void openNewScript();
    void drawSceneScripts(float X, float W, bool modal);
    void drawNewScript(float X, float Y, float W, float H);
    void drawClosePrompt(float X, float Y, float W, float H);
    void drawLanguageMenu(Doc* d);
    // A dimmed cover over the app with a box of `bw` x `bh` in the middle; (bx, by) is the box's inside top-left. Between Begin
    // and End of the caller's window (named `id`).
    bool beginDialog(const std::string& id, float X, float Y, float W, float H, float bw, float bh, float& bx, float& by);

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
        d.code.SetColorizerEnable(settings.syntaxColors);
        d.code.SetAutoClose(settings.autoClose);
        static const float kSpacing[3] = {1.3f, 1.55f, 1.85f};   // compact, comfortable, spacious
        d.code.SetLineSpacing(kSpacing[std::min(std::max(settings.density, 0), 2)]);
        if (cfg.useCodeFont) d.code.UseCodeFont(settings.codeFontPx);
    }

    bool openFile(const std::string& rawPath, int line) {
        const std::string path = Normal(rawPath);
        if (showKey(path, line)) return true;
        bool ok = false;
        std::string why;
        const std::string text = FileText(path, ok, &why);
        if (!ok) { notice = fs::u8path(path).filename().u8string() + " " + why + "."; return false; }
        addDoc(path, path, fs::u8path(path).filename().u8string(), EngineLanguageOf(path), text, nullptr, line);
        recent.Push(path);
        // Local History starts with the file as it was opened, so the first edits can always be undone from there (and the
        // first keystroke does not count as five minutes of edits)
        if (history.Versions(path).empty()) history.Record(path, text, now(), "Opened");
        else history.MaybeRecord(path, text, now());
        check(*docs.back());
        notice.clear();
        return true;
    }

    // Show the tab with this key (at a line); false when none is open.
    bool showKey(const std::string& key, int line) {
        for (std::size_t i = 0; i < docs.size(); ++i)
            if (docs[i]->key == key) { active = (int)i; if (line > 0) docs[i]->gotoLine = line; return true; }
        return false;
    }

    Doc& addDoc(const std::string& key, const std::string& path, const std::string& label, const std::string& language,
                const std::string& text, std::function<bool(const std::string&, std::string&)> saveFn, int line) {
        std::unique_ptr<Doc> d(new Doc());
        d->key = key;
        d->path = path;
        d->label = label;
        d->language = language;
        d->saveFn = std::move(saveFn);
        d->crlf = text.find("\r\n") != std::string::npos;
        d->code.SetLanguage(wordsFor(d->language));
        if (cfg.palette) d->code.SetPalette(*cfg.palette);
        else d->code.Edit().SetPalette(UI::DesignCodePalette());   // the design in force (frame() follows a change)
        d->code.Edit().SetFolding(true);
        applyView(*d);
        d->code.SetText(text);
        d->saved = d->code.Edit().GetText();
        d->seenVersion = d->code.Edit().Version();
        d->gotoLine = line;
        if (!d->path.empty()) noteDiskTime(*d);
        docs.push_back(std::move(d));
        active = (int)docs.size() - 1;
        return *docs.back();
    }

    bool openDocument(const DocumentSpec& spec, int line) {
        if (spec.key.empty()) return false;
        if (showKey(spec.key, line)) return true;
        const std::string path = spec.path.empty() ? std::string() : Normal(spec.path);
        const std::string label = !spec.label.empty() ? spec.label : (path.empty() ? spec.key : fs::u8path(path).filename().u8string());
        const std::string language = !spec.language.empty() ? spec.language : EngineLanguageOf(path);
        Doc& d = addDoc(spec.key, path, label, language, spec.text, spec.save, line);
        if (!path.empty()) {
            recent.Push(path);
            if (history.Versions(path).empty()) history.Record(path, spec.text, now(), "Opened");
            else history.MaybeRecord(path, spec.text, now());
        }
        check(d);
        notice.clear();
        return true;
    }

    Doc* byKey(const std::string& key) {
        for (auto& d : docs) if (d->key == key) return d.get();
        return nullptr;
    }
    const Doc* byKey(const std::string& key) const {
        for (const auto& d : docs) if (d->key == key) return d.get();
        return nullptr;
    }

    // The checks' marks, then the paused line on top (a wash and a note in the gutter's tooltip).
    void applyMarks(Doc& d) {
        d.code.SetErrorMarkers(d.marks);
        if (d.execLine <= 0) return;
        std::vector<UI::CodeMarker> m = d.code.Edit().Markers();
        UI::CodeMarker x;
        x.line = d.execLine - 1;
        x.message = "Paused here (line " + std::to_string(d.execLine) + ")";
        x.color = Rgb(240, 200, 80);
        x.lineColor = Rgb(240, 200, 80, 60);
        m.push_back(x);
        d.code.Edit().SetMarkers(m);
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
        if (d.saveFn) {
            std::string why;
            if (!d.saveFn(text, why)) { notice = "Could not save " + d.label + (why.empty() ? std::string() : ": " + why); return false; }
        } else if (d.path.empty() || !WriteText(d.path, text)) { notice = "Could not save " + (d.path.empty() ? d.label : d.path); return false; }
        d.saved = d.code.Edit().GetText();
        d.conflict = false;
        if (!d.path.empty()) { history.Record(d.path, text, now(), "Saved"); noteDiskTime(d); }
        check(d);
        notice = "Saved " + (d.path.empty() ? d.label : relative(d.path));
        if (cfg.saved) cfg.saved(d.path.empty() ? d.key : d.path, text);
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
        const Doc& gone = *docs[(std::size_t)index];
        closed.push_back(Closed{gone.key, gone.path, gone.code.Edit().Caret().line + 1});
        if (closed.size() > 30) closed.erase(closed.begin());
        if (promptKey == gone.key) { promptOpen = false; promptKey.clear(); }
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
        std::map<int, std::string> marks;   // one per line, an error over a warning
        for (const Diagnostic& p : d.problems) {
            if (p.line < 1) continue;
            auto it = marks.find(p.line);
            const std::string msg = (p.severity == 0 ? "" : "warning: ") + p.message;
            if (it == marks.end()) marks[p.line] = msg;
            else if (p.severity == 0 && it->second.rfind("warning: ", 0) == 0) it->second = msg;   // an error wins over a warning
        }
        d.marks = std::move(marks);
        applyMarks(d);
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
        for (const auto& d : docs) if (!d->path.empty()) texts[d->path] = d->code.Edit().GetText();   // open files: what the tab holds
        findResults = FindInTexts(texts, needle, opt, 2000, &findError);
        notice = findError.empty() ? std::to_string(findResults.size()) + " matches in the project" : "Bad pattern: " + findError;
        return findResults.size();
    }

    void loadVersions() {
        versions.clear(); diff.clear(); versionSel = -1;
        if (const Doc* d = activeDoc(); d && !d->path.empty()) versions = history.Versions(d->path);
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
        if (cfg.run) { notice = cfg.run(d->path.empty() ? d->key : d->path); return; }
        notice = TestRunScript(d->language, d->code.Edit().GetText()).message;
    }

    // The word before the caret (letters, digits, '_') and where it starts.
    std::string wordBeforeCaret(const Doc& d, int& startCol) const {
        const UI::CodePos c = d.code.Edit().Caret();
        const std::string& line = d.code.Edit().Line(c.line);
        int a = std::min(c.col, (int)line.size());
        while (a > 0 && (std::isalnum((unsigned char)line[(std::size_t)a - 1]) || line[(std::size_t)a - 1] == '_')) --a;
        startCol = a;
        return line.substr((std::size_t)a, (std::size_t)(std::min(c.col, (int)line.size()) - a));
    }

    // Open (or refresh) the list for the word before the caret. A typed word needs 2 letters; Ctrl+Space asks with any. Never
    // inside a comment or a string.
    void startCompletion(Doc& d, bool asked) {
        int start = 0;
        const std::string prefix = wordBeforeCaret(d, start);
        const UI::CodePos c = d.code.Edit().Caret();
        const std::string text = d.code.Edit().GetText();
        const std::size_t at = OffsetOf(text, c.line + 1, c.col);
        // after a '.' or ':' (members) any prefix will do
        const std::size_t wordStart = OffsetOf(text, c.line + 1, start);
        const bool member = wordStart > 0 && (text[wordStart - 1] == '.' || text[wordStart - 1] == ':');
        if (!asked && !member && prefix.size() < 2) { acOpen = false; return; }
        if (!prefix.empty() && std::isdigit((unsigned char)prefix[0])) { acOpen = false; return; }
        if (at > 0) {
            const std::vector<char> mask = CodeMaskFor(d.language, text.substr(0, at));
            if (!mask.empty() && !mask.back() && text[at - 1] != '\n') { acOpen = false; return; }
        }
        acItems = cfg.complete ? cfg.complete(d.language, text, c.line + 1, c.col, prefix)
                               : DefaultCompletions(wordsFor(d.language), text, prefix);
        // items taken before come first, in the order the provider gave
        std::stable_sort(acItems.begin(), acItems.end(), [this](const CompletionItem& x, const CompletionItem& y) {
            const auto ix = taken.find(x.label), iy = taken.find(y.label);
            return (ix == taken.end() ? 0 : ix->second) > (iy == taken.end() ? 0 : iy->second);
        });
        acPrefix = prefix;
        acSel = 0;
        acOpen = !acItems.empty();
    }

    // Replace the word before the caret with the item's text: one undo step.
    void acceptCompletion(Doc& d, std::size_t i) {
        if (i >= acItems.size()) return;
        const CompletionItem item = acItems[i];
        UI::CodeEdit& e = d.code.Edit();
        int start = 0;
        wordBeforeCaret(d, start);
        const UI::CodePos c = e.Caret();
        e.BreakUndoGroup();
        e.SetSelection(UI::CodePos{c.line, start}, c);
        e.InsertText((item.insert.empty() ? item.label : item.insert).c_str());
        e.BreakUndoGroup();
        if (item.caretBack > 0) {
            const UI::CodePos p = e.Caret();
            e.SetCaret(UI::CodePos{p.line, std::max(0, p.col - item.caretBack)});
        }
        ++taken[item.label];
        d.seenVersion = e.Version();
        d.editedAt = UI::GetTime();
        acOpen = false;
    }

    // Up / Down / Enter / Tab / Esc this frame: handled here (true), so the code widget does not see them.
    bool completionKeys(Doc& d) {
        auto key = [](UI::KeyId k) { return UI::IsKeyPressed(k, true); };
        const int n = (int)acItems.size();
        if (key(UI::KeyId_Escape)) { acOpen = false; return true; }
        if (key(UI::KeyId_Down)) { acSel = n ? (acSel + 1) % n : 0; return true; }
        if (key(UI::KeyId_Up)) { acSel = n ? (acSel + n - 1) % n : 0; return true; }
        if (key(UI::KeyId_Enter) || key(UI::KeyId_Tab)) { acceptCompletion(d, (std::size_t)acSel); return true; }
        return false;
    }

    // A box drawn over everything (lists and notes beside the caret): its fill and edge; the draw list to draw its inside on.
    static const UI::DrawListHandle& PopupBox(float x, float y, float w, float h) {
        const auto& fg = UI::GetForegroundDrawList();
        fg->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + h), kPanel, 4.0f);
        fg->AddRect(UI::Vec2(x, y), UI::Vec2(x + w, y + h), kLine, 4.0f, 1.0f);
        return fg;
    }

    static UI::Color32 KindColor(char k) {
        switch (k) {
        case 'k': return Rgb(204, 120, 50);
        case 'T': return Rgb(78, 201, 176);
        case 'f': case 'm': return Rgb(255, 198, 109);
        case 'p': return Rgb(150, 190, 230);
        default: return Rgb(160, 160, 170);
        }
    }

    // The list under the caret, each row with its kind letter and detail; the selected item's doc and example beside it.
    void drawCompletion(Doc& d) {
        const UI::Vec2 at = d.code.Edit().CaretScreenPosBelow();
        const float lh = UI::GetTextLineHeight(), rowH = lh + 6.0f;
        const int shown = std::min((int)acItems.size(), 10);
        const int first = std::max(0, std::min(acSel - shown + 1, (int)acItems.size() - shown));
        float w = 220.0f;
        for (int i = first; i < first + shown; ++i)
            w = std::max(w, UI::CalcTextWidth(acItems[(std::size_t)i].label.c_str()) + UI::CalcTextWidth(acItems[(std::size_t)i].detail.c_str()) + 60.0f);
        w = std::min(w, 520.0f);
        const float h = rowH * (float)shown + 8.0f;
        const auto& fg = PopupBox(at.x, at.y, w, h);
        const UI::Vec2 m = UI::GetMousePos();
        int click = -1;
        for (int i = first; i < first + shown; ++i) {
            const CompletionItem& it = acItems[(std::size_t)i];
            const float ry = at.y + 4.0f + rowH * (float)(i - first);
            const bool hover = m.x >= at.x && m.x < at.x + w && m.y >= ry && m.y < ry + rowH;
            if (i == acSel) fg->AddRectFilled(UI::Vec2(at.x + 3, ry), UI::Vec2(at.x + w - 3, ry + rowH), kHover, 3.0f);
            float tx = at.x + 10;
            if (it.kind) {
                const char letter[2] = {it.kind, 0};
                fg->AddText(UI::Vec2(tx, ry + 3), KindColor(it.kind), letter);
                tx += 16;
            }
            fg->AddText(UI::Vec2(tx, ry + 3), kText, it.label.c_str());
            if (!it.detail.empty()) {
                const float lw = UI::CalcTextWidth(it.label.c_str());
                fg->AddText(UI::Vec2(tx + 14 + lw, ry + 3), kDim, Fit(it.detail, std::max(20.0f, at.x + w - tx - lw - 24)).c_str());
            }
            if (hover && UI::IsMouseClicked(0)) click = i;
        }
        // the details pane
        if (acSel >= 0 && acSel < (int)acItems.size()) {
            const CompletionItem& it = acItems[(std::size_t)acSel];
            if (!it.doc.empty() || !it.example.empty()) {
                const float pw = 340.0f, dispW = UI::GetFrameInput().displayW;
                float px = at.x + w + 6;
                if (dispW > 0 && px + pw > dispW) px = at.x - pw - 6 >= 0 ? at.x - pw - 6 : std::max(0.0f, dispW - pw);   // keep it on screen
                std::vector<std::string> lines;   // the doc wrapped to the pane, then the example's lines
                auto wrap = [&](const std::string& text) {
                    std::istringstream in(text);
                    std::string para;
                    while (std::getline(in, para)) {
                        std::string cur;
                        std::istringstream words(para);
                        std::string wd;
                        while (words >> wd) {
                            const std::string next = cur.empty() ? wd : cur + " " + wd;
                            if (!cur.empty() && UI::CalcTextWidth(next.c_str()) > pw - 20) { lines.push_back(cur); cur = wd; }
                            else cur = next;
                        }
                        lines.push_back(cur);
                    }
                };
                wrap(it.doc);
                const std::size_t docLines = lines.size();
                std::vector<std::string> ex;
                if (!it.example.empty()) { std::istringstream in(it.example); std::string l; while (std::getline(in, l)) ex.push_back(l); }
                const float ph = 12 + rowH * (float)(docLines + (ex.empty() ? 0 : ex.size() + 1));
                PopupBox(px, at.y, pw, ph);
                float y = at.y + 6;
                for (std::size_t i = 0; i < docLines; ++i, y += rowH) fg->AddText(UI::Vec2(px + 10, y), i == 0 ? kText : kDim, lines[i].c_str());
                if (!ex.empty()) {
                    fg->AddText(UI::Vec2(px + 10, y), kDim, "Example");
                    y += rowH;
                    fg->AddRectFilled(UI::Vec2(px + 8, y - 2), UI::Vec2(px + pw - 8, y + rowH * (float)ex.size()), kBack, 3.0f);
                    for (const std::string& l : ex) { fg->AddText(UI::Vec2(px + 14, y), Rgb(206, 214, 160), l.c_str()); y += rowH; }
                }
            }
        }
        if (click >= 0) acceptCompletion(d, (std::size_t)click);
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


} // namespace scriptide
