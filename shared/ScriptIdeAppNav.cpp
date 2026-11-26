// Script Editor (ScriptIdeApp.hpp): find and replace in the shown file and in every file, go to line / symbol / definition, the
// references of a word, back and forward, the tab switcher, the bookmarks list, Search Everywhere and the command list. The
// searching is the model's (editor/ScriptIde.hpp: FindAll, ReplaceAll, OutlineOf, SymbolIndex, RankQuickOpen, FuzzyScore); this
// file keeps the places and draws the bars and lists. Written from general editor behaviour and the OkayUI interface; no code,
// names, icons or look from any other editor (rule 19a).
#include "ScriptIdeAppImpl.hpp"

namespace scriptide {

namespace {
bool IsWordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
std::string Trimmed(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a);
}
std::string LineOf(const std::string& text, int line) {   // 1-based
    std::size_t at = 0;
    for (int l = 1; l < line && at != std::string::npos; ++l) { at = text.find('\n', at); if (at != std::string::npos) ++at; }
    if (at == std::string::npos || at > text.size()) return std::string();
    const std::size_t end = text.find('\n', at);
    return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
} // namespace

// ---- places: back and forward ----------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::notePlace() {
    const Doc* d = activeDoc();
    if (!d) return;
    const Place p{d->key, d->code.Edit().Caret().line + 1};
    if (!back.empty() && back.back().key == p.key && back.back().line == p.line) return;
    back.push_back(p);
    if (back.size() > 100) back.erase(back.begin());
    forward.clear();
}

void ScriptIdeApp::Impl::noteShown() {
    const Doc* d = activeDoc();
    if (!d) return;
    if (!mru.empty() && mru.front() == d->key) return;
    mru.erase(std::remove(mru.begin(), mru.end(), d->key), mru.end());
    mru.insert(mru.begin(), d->key);
    // keep only open documents
    mru.erase(std::remove_if(mru.begin(), mru.end(), [this](const std::string& k) { return byKey(k) == nullptr; }), mru.end());
}

bool ScriptIdeApp::Impl::jumpTo(const std::string& keyOrPath, int line, bool record) {
    if (record) notePlace();
    if (showKey(keyOrPath, line)) return true;
    if (showKey(Normal(keyOrPath), line)) return true;
    return openFile(keyOrPath, line);
}

bool ScriptIdeApp::Impl::goBack(bool fwd) {
    std::vector<Place>& from = fwd ? forward : back;
    std::vector<Place>& to = fwd ? back : forward;
    while (!from.empty()) {
        const Place p = from.back();
        from.pop_back();
        if (const Doc* d = activeDoc()) to.push_back(Place{d->key, d->code.Edit().Caret().line + 1});
        if (jumpTo(p.key, p.line, false)) return true;
        to.pop_back();   // that document is gone: try the one before
    }
    return false;
}

// ---- the find bar ----------------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::openFindBar(bool replace) {
    barOpen = true;
    barFocus = true;
    barReplace = barReplace || replace;
    const Doc* d = activeDoc();
    if (d && d->code.Edit().HasSelection()) {   // the selected text (one line) is what to find
        const std::string sel = d->code.Edit().SelectedText();
        if (!sel.empty() && sel.find('\n') == std::string::npos && sel.size() < sizeof barFind) std::snprintf(barFind, sizeof barFind, "%s", sel.c_str());
    }
}

const std::vector<TextMatch>& ScriptIdeApp::Impl::barHits(Doc& d) {
    BarCache& c = barCache;
    const unsigned v = d.code.Edit().Version();
    if (c.key != d.key || c.version != v || c.needle != barFind || c.regex != barOpt.regex || c.matchCase != barOpt.matchCase ||
        c.word != barOpt.wholeWord) {
        c.key = d.key; c.version = v; c.needle = barFind; c.regex = barOpt.regex; c.matchCase = barOpt.matchCase; c.word = barOpt.wholeWord;
        barError.clear();
        c.hits = barFind[0] ? FindAll(d.code.Edit().GetText(), barFind, barOpt, &barError) : std::vector<TextMatch>();
    }
    return c.hits;
}

namespace {
// The match whose text the selection is exactly (-1: none).
int SelectedHit(const OkayUI::CodeEdit& e, const std::vector<TextMatch>& hits) {
    if (!e.HasSelection()) return -1;
    const OkayUI::CodePos a = e.SelectionStart(), b = e.SelectionEnd();
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const TextMatch& h = hits[i];
        if (h.line - 1 != a.line || h.col != a.col) continue;
        const std::size_t endCol = (std::size_t)h.col + h.length;   // matches do not span lines unless the needle has a newline
        if (b.line == a.line && (std::size_t)b.col == endCol) return (int)i;
    }
    return -1;
}
} // namespace

bool ScriptIdeApp::Impl::findStep(bool fwd) {
    Doc* d = activeDoc();
    if (!d || !barFind[0]) return false;
    const std::vector<TextMatch>& hits = barHits(*d);
    if (hits.empty()) { notice = barError.empty() ? std::string("No matches") : "Bad pattern: " + barError; return false; }
    UI::CodeEdit& e = d->code.Edit();
    const UI::CodePos from = fwd ? e.SelectionEnd() : e.SelectionStart();
    int pick = -1;
    if (fwd) {
        for (std::size_t i = 0; i < hits.size(); ++i) {
            const UI::CodePos p{hits[i].line - 1, hits[i].col};
            if (from <= p && !(e.HasSelection() && p == e.SelectionStart())) { pick = (int)i; break; }
        }
        if (pick < 0) pick = 0;   // round the end
    } else {
        for (int i = (int)hits.size() - 1; i >= 0; --i) {
            const UI::CodePos p{hits[(std::size_t)i].line - 1, hits[(std::size_t)i].col};
            if (p < from) { pick = i; break; }
        }
        if (pick < 0) pick = (int)hits.size() - 1;
    }
    const TextMatch& h = hits[(std::size_t)pick];
    // a match that runs over lines ends where its last line's text ends
    const std::string text = e.GetText();
    const std::size_t endOff = h.offset + h.length;
    int endLine = h.line - 1, endCol = h.col + (int)h.length;
    for (std::size_t i = h.offset; i < endOff && i < text.size(); ++i)
        if (text[i] == '\n') { ++endLine; endCol = (int)(endOff - i - 1); }
    e.SetSelection(UI::CodePos{h.line - 1, h.col}, UI::CodePos{endLine, endCol});
    e.ScrollToCaret();
    notice = std::to_string(pick + 1) + " of " + std::to_string(hits.size());
    return true;
}

bool ScriptIdeApp::Impl::replaceOne() {
    Doc* d = activeDoc();
    if (!d || !barFind[0]) return false;
    UI::CodeEdit& e = d->code.Edit();
    const int cur = SelectedHit(e, barHits(*d));
    if (cur < 0) return findStep(true);   // first select one, the next press replaces it
    std::string with = barWith;
    if (barOpt.regex) {   // $1.. and $& in the replacement: what ReplaceAll makes of the selected match alone
        int n = 0;
        with = ReplaceAll(e.SelectedText(), barFind, barWith, barOpt, &n);
    }
    e.BreakUndoGroup();
    e.InsertText(with.c_str());
    e.BreakUndoGroup();
    findStep(true);
    return true;
}

int ScriptIdeApp::Impl::replaceAllInDoc() {
    Doc* d = activeDoc();
    if (!d || !barFind[0]) return 0;
    UI::CodeEdit& e = d->code.Edit();
    int n = 0;
    std::string why;
    const std::string out = ReplaceAll(e.GetText(), barFind, barWith, barOpt, &n, &why);
    if (n > 0) {
        const UI::CodePos caret = e.Caret();
        e.SelectAll();
        e.BreakUndoGroup();
        e.InsertText(out.c_str());   // one undo step
        e.BreakUndoGroup();
        e.SetCaret(caret);
    }
    notice = why.empty() ? "Replaced " + std::to_string(n) : "Bad pattern: " + why;
    return n;
}

std::size_t ScriptIdeApp::Impl::replaceInFiles(const std::string& needle, const std::string& with, const FindOptions& opt) {
    if (needle.empty()) return 0;
    std::size_t total = 0;
    std::string why;
    std::set<std::string> done;
    for (auto& d : docs) {   // open documents: in their tab
        int n = 0;
        UI::CodeEdit& e = d->code.Edit();
        const std::string out = ReplaceAll(e.GetText(), needle, with, opt, &n, &why);
        if (!why.empty()) { notice = "Bad pattern: " + why; return 0; }
        if (!d->path.empty()) done.insert(d->path);
        if (n <= 0) continue;
        e.SelectAll();
        e.BreakUndoGroup();
        e.InsertText(out.c_str());
        e.BreakUndoGroup();
        total += (std::size_t)n;
    }
    int files = 0;
    for (const std::string& s : scripts) {   // the others: on disk, after a Local History copy
        if (done.count(s)) continue;
        bool ok = false;
        const std::string text = FileText(s, ok);
        if (!ok) continue;
        int n = 0;
        const std::string out = ReplaceAll(text, needle, with, opt, &n, &why);
        if (n <= 0) continue;
        history.Record(s, text, now(), "Before replace");
        if (WriteText(s, out)) { total += (std::size_t)n; ++files; }
    }
    notice = "Replaced " + std::to_string(total) + " in the project (" + std::to_string(files) + " files written; open tabs are not saved)";
    if (panel == P_Find) findInFiles(needle, opt);
    return total;
}

void ScriptIdeApp::Impl::drawFindBar(float X, float Y, float W, float h, bool modal) {
    Doc* d = activeDoc();
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_findbar").c_str(), X, Y, W, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kPanel);
        dl->AddLine(UI::Vec2(X, Y + h - 1), UI::Vec2(X + W, Y + h - 1), kLine);
        UI::SetCursorScreenPos(UI::Vec2(X + 8, Y + 5));
        UI::SetNextItemWidth(std::min(300.0f, W * 0.35f));
        if (barFocus) { UI::SetKeyboardFocusHere(); barFocus = false; }
        const bool enter = UI::InputTextWithHint("##barfind", "Find", barFind, (int)sizeof barFind, UI::InputText_EnterReturnsTrue);
        UI::SameLine(); UI::Checkbox("Aa##bar", &barOpt.matchCase);
        UI::SameLine(); UI::Checkbox("Word##bar", &barOpt.wholeWord);
        UI::SameLine(); UI::Checkbox(".*##bar", &barOpt.regex);
        UI::SameLine();
        const bool prev = UI::Button("Previous##bar");
        UI::SameLine();
        const bool next = UI::Button("Next##bar");
        UI::SameLine();
        std::string count;
        if (d && barFind[0]) {
            const std::vector<TextMatch>& hits = barHits(*d);
            const int cur = SelectedHit(d->code.Edit(), hits);
            count = !barError.empty() ? "bad pattern" : hits.empty() ? "no matches"
                  : (cur >= 0 ? std::to_string(cur + 1) + " of " : std::string()) + std::to_string(hits.size());
        }
        UI::TextDisabled(count.c_str());
        UI::SameLine();
        if (UI::Button(barReplace ? "Hide Replace##bar" : "Replace...##bar")) barReplace = !barReplace;
        UI::SameLine();
        const bool closeBar = UI::Button("x##bar");
        bool doOne = false, doAll = false;
        if (barReplace) {
            UI::SetCursorScreenPos(UI::Vec2(X + 8, Y + 35));
            UI::SetNextItemWidth(std::min(300.0f, W * 0.35f));
            const bool enterR = UI::InputTextWithHint("##barwith", "Replace with", barWith, (int)sizeof barWith, UI::InputText_EnterReturnsTrue);
            UI::SameLine();
            doOne = UI::Button("Replace##bar") || enterR;
            UI::SameLine();
            doAll = UI::Button("Replace All##bar");
        }
        if (!modal) {
            const bool shiftHeld = UI::GetFrameInput().shift;
            if (enter) findStep(!shiftHeld);
            if (prev) findStep(false);
            if (next) findStep(true);
            if (doOne) replaceOne();
            if (doAll) replaceAllInDoc();
            if (closeBar) barOpen = false;
        }
    }
    UI::End();
}

// ---- words, definitions and references ------------------------------------------------------------------------------------

std::string ScriptIdeApp::Impl::wordAt(const Doc& d, UI::CodePos p) const {
    const std::string& line = d.code.Edit().Line(p.line);
    int a = std::min(p.col, (int)line.size()), b = a;
    while (a > 0 && IsWordChar(line[(std::size_t)a - 1])) --a;
    while (b < (int)line.size() && IsWordChar(line[(std::size_t)b])) ++b;
    if (b <= a || std::isdigit((unsigned char)line[(std::size_t)a])) return std::string();
    return line.substr((std::size_t)a, (std::size_t)(b - a));
}

SymbolIndex ScriptIdeApp::Impl::buildIndex() const {
    SymbolIndex idx;
    std::set<std::string> open;
    for (const auto& d : docs) {   // open documents with their unsaved text, under their key (a file's key is its path)
        idx.SetFile(d->key, d->code.Edit().GetText(), d->language);
        if (!d->path.empty()) open.insert(d->path);
    }
    std::size_t read = 0;
    for (const std::string& s : scripts) {
        if (open.count(s) || read >= 3000) continue;
        bool ok = false;
        const std::string text = FileText(s, ok);
        if (ok) { idx.SetFile(s, text, EngineLanguageOf(s)); ++read; }
    }
    return idx;
}

bool ScriptIdeApp::Impl::goToDefinition(const std::string& wordIn) {
    Doc* d = activeDoc();
    if (!d) return false;
    const std::string word = wordIn.empty() ? wordAt(*d, d->code.Edit().Caret()) : wordIn;
    if (word.empty()) { notice = "Put the caret on a name first"; return false; }
    // this document first: the declaration nearest above the caret wins a tie, then the first one
    const std::vector<Symbol> here = OutlineOf(d->language, d->code.Edit().GetText(), d->key);
    for (const Symbol& s : here)
        if (s.name == word) { jumpTo(d->key, s.line); notice = word + ": line " + std::to_string(s.line); return true; }
    const SymbolIndex idx = buildIndex();
    const std::vector<Symbol> defs = idx.Definitions(word);
    for (const Symbol& s : defs) {
        if (s.file == d->key) continue;
        jumpTo(s.file, s.line);
        notice = word + ": " + relative(s.file) + ":" + std::to_string(s.line) + (defs.size() > 1 ? " (" + std::to_string(defs.size()) + " places)" : "");
        return true;
    }
    notice = "No definition of " + word + " in the project";
    return false;
}

std::size_t ScriptIdeApp::Impl::findReferences(const std::string& wordIn) {
    refs.clear();
    Doc* d = activeDoc();
    const std::string word = !wordIn.empty() ? wordIn : (d ? wordAt(*d, d->code.Edit().Caret()) : std::string());
    refsWord = word;
    if (word.empty()) { notice = "Put the caret on a name first"; return 0; }
    const SymbolIndex idx = buildIndex();
    for (const Occurrence& o : idx.References(word)) {
        FileMatch m;
        m.file = o.file;
        m.match.line = o.line; m.match.col = o.col; m.match.offset = o.offset; m.match.length = word.size();
        if (const std::string* t = idx.Text(o.file)) m.lineText = LineOf(*t, o.line);
        refs.push_back(std::move(m));
    }
    panel = P_Refs;
    notice = std::to_string(refs.size()) + " uses of " + word;
    return refs.size();
}

// ---- the lists to pick from ---------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::openPicker(int which) {
    picker = which;
    pickBuf[0] = 0;
    pickSel = 0;
    pickFocus = true;
    if (which == K_Everywhere) pickIndex = buildIndex();
    if (which == K_Switch && mru.size() > 1) pickSel = 1;   // the tab shown before this one
}

std::vector<AppCommand> ScriptIdeApp::Impl::commands() {
    std::vector<AppCommand> c;
    auto add = [&](const char* title, const char* keys, std::function<void()> run) { c.push_back(AppCommand{title, keys, std::move(run)}); };
    add("Save", "Ctrl+S", [this] { if (Doc* d = activeDoc()) save(*d); });
    add("Save All", "Ctrl+Shift+S", [this] { saveAll(); });
    add("Close Tab", "Ctrl+W", [this] { requestClose(active); });
    add("Reopen Closed Tab", "Ctrl+Shift+T", [this] { reopenClosed(); });
    add("New Script...", "", [this] { openNewScript(); });
    add("Open Folder...", "Ctrl+O", [this] { folderOpen = true; folderFocus = true; std::snprintf(folderBuf, sizeof folderBuf, "%s", folder.c_str()); });
    add("Go to File...", "Ctrl+Shift+N", [this] { goOpen = true; goBuf[0] = 0; goSel = 0; });
    add("Go to Line...", "Ctrl+G", [this] { openPicker(K_Line); });
    add("Go to Symbol...", "Ctrl+Shift+O", [this] { openPicker(K_Symbol); });
    add("Go to Definition", "F12", [this] { goToDefinition(std::string()); });
    add("Find All References", "Shift+F12", [this] { findReferences(std::string()); });
    add("Back", "Alt+Left", [this] { goBack(false); });
    add("Forward", "Alt+Right", [this] { goBack(true); });
    add("Switch Tab...", "Ctrl+E", [this] { openPicker(K_Switch); });
    add("Bookmarks...", "Shift+F11", [this] { openPicker(K_Bookmarks); });
    add("Toggle Bookmark", "F11", [this] { if (Doc* d = activeDoc()) d->code.Edit().ToggleBreakpoint(d->code.Edit().Caret().line); });
    add("Find...", "Ctrl+F", [this] { openFindBar(false); });
    add("Replace...", "Ctrl+H", [this] { openFindBar(true); });
    add("Find in Files", "Ctrl+Shift+F", [this] { panel = P_Find; findFocus = true; });
    add("Toggle Comment", "Ctrl+/", [this] { if (Doc* d = activeDoc()) d->code.Edit().ToggleLineComment(LineCommentFor(d->language)); });
    add("Check", "F7", [this] { if (Doc* d = activeDoc()) { check(*d); notice = std::to_string(d->problems.size()) + " problems"; } });
    add(cfg.runLabel.c_str(), "F5", [this] { run(); });
    add("Problems", "", [this] { openPanel(P_Problems); });
    add("Local History", "", [this] { openPanel(P_History); });
    add("Project Tree", "Alt+1", [this] { settings.showTree = !settings.showTree; saveSettings(); });
    if (cfg.commands) for (AppCommand& h : cfg.commands()) c.push_back(std::move(h));
    return c;
}

std::vector<ScriptIdeApp::Impl::PickItem> ScriptIdeApp::Impl::pickItems() {
    std::vector<PickItem> out;
    const std::string q = pickBuf;
    Doc* d = activeDoc();
    struct Scored { int score; PickItem item; };
    auto ranked = [](std::vector<Scored>& v, std::size_t maxN) {
        std::stable_sort(v.begin(), v.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
        std::vector<PickItem> r;
        for (std::size_t i = 0; i < v.size() && r.size() < maxN; ++i) r.push_back(std::move(v[i].item));
        return r;
    };
    if (picker == K_Line) {
        int line = 0, col = 0;
        if (std::sscanf(pickBuf, "%d:%d", &line, &col) >= 1 && line > 0 && d) {
            const std::string key = d->key;
            out.push_back(PickItem{"Go to line " + std::to_string(line) + (col > 0 ? ", column " + std::to_string(col) : ""),
                                   "of " + std::to_string(d->code.Edit().LineCount()),
                                   [this, key, line, col] {
                                       notePlace();
                                       if (Doc* t = byKey(key)) { t->code.Edit().GoToLine(line - 1); if (col > 0) t->code.Edit().SetCaret(UI::CodePos{line - 1, col - 1}); t->code.Edit().Focus(); }
                                   }});
        }
    } else if (picker == K_Symbol && d) {
        std::vector<Scored> v;
        const std::string key = d->key;
        for (const Symbol& s : OutlineOf(d->language, d->code.Edit().GetText(), d->key)) {
            const int sc = FuzzyScore(q, s.display);
            if (sc < 0) continue;
            const int line = s.line;
            v.push_back({sc, PickItem{s.display, "line " + std::to_string(line), [this, key, line] { jumpTo(key, line); }}});
        }
        if (q.empty()) for (const Scored& s : v) out.push_back(s.item);   // in the file's order
        else out = ranked(v, 50);
    } else if (picker == K_Switch) {
        noteShown();
        std::vector<Scored> v;
        int order = 1000;
        for (const std::string& k : mru) {
            const Doc* t = byKey(k);
            if (!t) continue;
            const int sc = FuzzyScore(q, t->label);
            if (sc < 0) continue;
            v.push_back({q.empty() ? order-- : sc, PickItem{(dirty(*t) ? "* " : "") + t->label, t->path.empty() ? t->key : relative(t->path),
                                                            [this, k] { jumpTo(k, 0); }}});
        }
        for (const std::string& f : recent.Items()) {   // files opened before, not open now
            if (byKey(f)) continue;
            const std::string name = fs::u8path(f).filename().u8string();
            const int sc = FuzzyScore(q, name);
            if (sc < 0) continue;
            v.push_back({q.empty() ? order-- : sc - 1, PickItem{name, relative(f) + " (closed)", [this, f] { jumpTo(f, 0); }}});
        }
        out = ranked(v, 30);
    } else if (picker == K_Bookmarks) {
        for (const auto& t : docs)
            for (int l : t->code.Edit().Breakpoints()) {
                const std::string key = t->key;
                const int line = l + 1;
                const std::string label = t->label + ":" + std::to_string(line) + "   " + Trimmed(t->code.Edit().Line(l));
                if (FuzzyScore(q, label) < 0) continue;
                out.push_back(PickItem{label, "", [this, key, line] { jumpTo(key, line); }});
            }
    } else if (picker == K_Everywhere || picker == K_Commands) {
        std::vector<Scored> v;
        for (AppCommand& c : commands()) {
            const int sc = FuzzyScore(q, c.title);
            if (sc < 0) continue;
            std::function<void()> run = c.run;
            v.push_back({sc + (picker == K_Commands ? 0 : 5), PickItem{c.title, c.shortcut.empty() ? std::string("command") : c.shortcut, run}});
        }
        if (picker == K_Everywhere && !q.empty()) {
            for (const QuickOpenItem& f : RankQuickOpen(q, scripts, recent.Items(), folder, 15)) {
                const std::string p = f.path;
                v.push_back({f.score + 10, PickItem{f.label, "file", [this, p] { jumpTo(p, 0); }}});
            }
            int n = 0;
            for (const Symbol& s : pickIndex.AllSymbols()) {
                const int sc = FuzzyScore(q, s.display);
                if (sc < 0 || ++n > 400) continue;
                const std::string f = s.file;
                const int line = s.line;
                const Doc* t = byKey(f);
                v.push_back({sc, PickItem{s.display, (t ? t->label : relative(f)) + ":" + std::to_string(line), [this, f, line] { jumpTo(f, line); }}});
            }
        }
        if (q.empty() && picker == K_Commands) for (Scored& s : v) out.push_back(std::move(s.item));   // the list's own order
        else out = ranked(v, 60);
    }
    return out;
}

void ScriptIdeApp::Impl::drawPicker(float X, float Y, float W, float H) {
    static const char* const kTitles[] = {"", "Go to Line (line or line:column)", "Go to Symbol in this file", "Switch Tab",
                                          "Bookmarks", "Search Everywhere: files, symbols, commands", "Commands"};
    float bx = 0, by = 0;
    const float bw = std::min(600.0f, W - 40.0f), bh = std::min(420.0f, H - 40.0f);
    if (beginDialog(cfg.idPrefix + "_picker", X, Y, W, H, bw, bh, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted(kTitles[picker > 0 && picker <= K_Commands ? picker : 0]);
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 2));
        UI::SetNextItemWidth(bw - 28);
        if (pickFocus) { pickFocus = false; }
        UI::SetKeyboardFocusHere();
        const bool enter = UI::InputText("##pickquery", pickBuf, (int)sizeof pickBuf, UI::InputText_EnterReturnsTrue);
        std::vector<PickItem> items = pickItems();
        if (UI::IsKeyPressed(UI::KeyId_Down)) pickSel = std::min(pickSel + 1, (int)items.size() - 1);
        if (UI::IsKeyPressed(UI::KeyId_Up)) pickSel = std::max(pickSel - 1, 0);
        if (pickSel >= (int)items.size()) pickSel = std::max(0, (int)items.size() - 1);
        if (pickSel < 0) pickSel = 0;
        int pick = -1;
        const float rowH = UI::GetTextLineHeight() + 8.0f;
        const int rows = std::max(1, (int)((bh - 70.0f) / rowH));
        const int first = std::max(0, std::min(pickSel - rows + 1, (int)items.size() - rows));
        const auto& dl = UI::GetWindowDrawList();
        const UI::Vec2 m = UI::GetMousePos();
        float y = UI::GetCursorScreenPos().y + 4;
        for (int i = std::max(0, first); i < (int)items.size() && i < first + rows; ++i) {
            const bool hover = m.x >= bx - 6 && m.x < bx + bw - 20 && m.y >= y && m.y < y + rowH;
            if (i == pickSel) dl->AddRectFilled(UI::Vec2(bx - 6, y), UI::Vec2(bx + bw - 22, y + rowH), kHover, 4.0f);
            const float dw = items[(std::size_t)i].detail.empty() ? 0.0f : std::min(220.0f, UI::CalcTextWidth(items[(std::size_t)i].detail.c_str()));
            dl->AddText(UI::Vec2(bx, y + 4), kText, Fit(items[(std::size_t)i].label, bw - dw - 50).c_str());
            if (dw > 0) dl->AddText(UI::Vec2(bx + bw - 30 - dw, y + 4), kDim, Fit(items[(std::size_t)i].detail, 220).c_str());
            if (hover && UI::IsMouseClicked(0)) pick = i;
            y += rowH;
        }
        if (items.empty()) dl->AddText(UI::Vec2(bx, y + 4), kDim, picker == K_Bookmarks ? "No bookmarks: F11 marks the caret's line" : "Nothing matches");
        if (enter && !items.empty()) pick = pickSel;
        if (UI::IsKeyPressed(UI::KeyId_Escape, false)) picker = K_None;
        if (pick >= 0 && pick < (int)items.size()) {
            picker = K_None;
            if (items[(std::size_t)pick].act) items[(std::size_t)pick].act();
        }
    }
    UI::End();
}

// ---- public ------------------------------------------------------------------------------------------------------------------

void ScriptIdeApp::OpenFindBar(const std::string& needle, bool replace, const FindOptions& options, const std::string& with) {
    m->openFindBar(replace);
    std::snprintf(m->barFind, sizeof m->barFind, "%s", needle.c_str());
    std::snprintf(m->barWith, sizeof m->barWith, "%s", with.c_str());
    m->barOpt = options;
}
bool ScriptIdeApp::FindBarOpen() const { return m->barOpen; }
bool ScriptIdeApp::FindNext(bool forward) { return m->findStep(forward); }
std::size_t ScriptIdeApp::FindCount() { Impl::Doc* d = m->activeDoc(); return d ? m->barHits(*d).size() : 0; }
int ScriptIdeApp::FindCurrent() { Impl::Doc* d = m->activeDoc(); return d ? SelectedHit(d->code.Edit(), m->barHits(*d)) : -1; }
bool ScriptIdeApp::ReplaceNext() { return m->replaceOne(); }
int ScriptIdeApp::ReplaceAllInDocument() { return m->replaceAllInDoc(); }
std::size_t ScriptIdeApp::ReplaceInFiles(const std::string& needle, const std::string& with, const FindOptions& options) {
    return m->replaceInFiles(needle, with, options);
}
bool ScriptIdeApp::GoToLine(int line, int col) {
    Impl::Doc* d = m->activeDoc();
    if (!d || line < 1) return false;
    m->notePlace();
    d->code.Edit().GoToLine(line - 1);
    if (col > 0) d->code.Edit().SetCaret(OkayUI::CodePos{line - 1, col - 1});
    return true;
}
bool ScriptIdeApp::GoToDefinition(const std::string& word) { return m->goToDefinition(word); }
std::size_t ScriptIdeApp::FindReferences(const std::string& word) { return m->findReferences(word); }
const std::vector<FileMatch>& ScriptIdeApp::References() const { return m->refs; }
bool ScriptIdeApp::OpenReference(std::size_t index) {
    if (index >= m->refs.size()) return false;
    const FileMatch r = m->refs[index];
    return m->jumpTo(r.file, r.match.line);
}
bool ScriptIdeApp::GoBack() { return m->goBack(false); }
bool ScriptIdeApp::GoForward() { return m->goBack(true); }
void ScriptIdeApp::OpenPicker(int which, const std::string& query) {
    if (which <= 0 || which > Impl::K_Commands) { m->picker = Impl::K_None; return; }
    m->openPicker(which);
    std::snprintf(m->pickBuf, sizeof m->pickBuf, "%s", query.c_str());
}
int ScriptIdeApp::PickerOpen() const { return m->picker; }
std::vector<std::string> ScriptIdeApp::PickerRows() {
    std::vector<std::string> out;
    for (const auto& it : m->pickItems()) out.push_back(it.label);
    return out;
}
bool ScriptIdeApp::PickRow(std::size_t index) {
    std::vector<Impl::PickItem> items = m->pickItems();
    if (index >= items.size()) return false;
    m->picker = Impl::K_None;
    if (items[index].act) items[index].act();
    return true;
}
std::vector<AppCommand> ScriptIdeApp::Commands() { return m->commands(); }
void ScriptIdeApp::ToggleBookmark() {
    if (Impl::Doc* d = m->activeDoc()) d->code.Edit().ToggleBreakpoint(d->code.Edit().Caret().line);
}

} // namespace scriptide
