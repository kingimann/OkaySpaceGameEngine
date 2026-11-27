// Script Editor (ScriptIdeApp.hpp): the view's helpers over the code (the minimap, the pinned header of the block in view, the bars
// of changed lines, the column-120 ruler, a line's problem written after it, the breadcrumb), the disk (Reload from Disk, open
// files changed outside loaded again, "changed on disk" for a tab with edits), and Insert / Help (behaviour templates, API
// commands, C# snippets, the C# quick reference). The decisions are the model's (LineDiff, OutlineOf, BuiltinSnippets,
// CallTemplate); this file draws them. Written from general editor behaviour and the OkayUI interface; no code, names, icons or
// look from any other editor (rule 19a).
#include "ScriptIdeAppImpl.hpp"

#include "okay/Lua/LuaScriptVM.hpp"             // the bound names and docs for Insert > API Command
#include "okay/JavaScript/JavaScriptVM.hpp"

namespace scriptide {

// ---- the disk --------------------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::noteDiskTime(Doc& d) {
    std::error_code ec;
    const fs::file_time_type t = fs::last_write_time(fs::u8path(d.path), ec);
    d.hasMtime = !ec;
    if (!ec) d.mtime = t;
}

bool ScriptIdeApp::Impl::reloadFromDisk(Doc& d) {
    if (d.path.empty()) return false;
    bool ok = false;
    const std::string text = FileText(d.path, ok);
    if (!ok) { notice = "Could not read " + d.path; return false; }
    d.crlf = text.find("\r\n") != std::string::npos;
    std::string clean = text;
    clean.erase(std::remove(clean.begin(), clean.end(), '\r'), clean.end());
    setText(d, clean);   // one undo step: Ctrl+Z brings the tab's text back
    d.saved = d.code.Edit().GetText();
    d.seenVersion = d.code.Edit().Version();
    d.conflict = false;
    noteDiskTime(d);
    check(d);
    return true;
}

void ScriptIdeApp::Impl::checkDisk() {
    for (auto& up : docs) {
        Doc& d = *up;
        if (d.path.empty() || !d.hasMtime) continue;
        std::error_code ec;
        const fs::file_time_type t = fs::last_write_time(fs::u8path(d.path), ec);
        if (ec || t == d.mtime) continue;
        bool ok = false;
        std::string text = FileText(d.path, ok);
        if (!ok) continue;
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        if (text == d.saved) { d.mtime = t; continue; }   // touched, not changed
        if (!dirty(d)) {
            reloadFromDisk(d);
            notice = "Loaded the outside change to " + d.label;
        } else {
            d.conflict = true;   // the tab has edits: the person picks
            d.mtime = t;
        }
    }
}

void ScriptIdeApp::Impl::drawConflict(float X, float Y, float W, float h, bool modal) {
    Doc* d = activeDoc();
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_conflict").c_str(), X, Y, W, h) && d) {
        UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), Rgb(90, 66, 30));
        UI::SetCursorScreenPos(UI::Vec2(X + 10, Y + 5));
        UI::TextUnformatted((d->label + " changed on disk, and this tab has edits.").c_str());
        UI::SameLine();
        const bool load = UI::Button("Load External##conflict");
        UI::SameLine();
        const bool keep = UI::Button("Keep Mine##conflict");
        if (!modal && load) reloadFromDisk(*d);
        if (!modal && keep) d->conflict = false;   // the next save writes over the outside change
    }
    UI::End();
}

// ---- marks over the code -----------------------------------------------------------------------------------------------------

const std::vector<char>& ScriptIdeApp::Impl::changedMarks(Doc& d) {
    UI::CodeEdit& e = d.code.Edit();
    if (d.marksVersion == e.Version()) return d.lineMarks;
    d.marksVersion = e.Version();
    d.lineMarks.assign((std::size_t)e.LineCount(), 0);
    if (e.GetText() == d.saved) return d.lineMarks;
    int line = 0;   // the current text's line the walk is at
    for (const DiffLine& l : LineDiff(d.saved, e.GetText())) {
        if (l.op == ' ') ++line;
        else if (l.op == '+') { if (line < (int)d.lineMarks.size()) d.lineMarks[(std::size_t)line] = 1; ++line; }
        else if (line < (int)d.lineMarks.size() && d.lineMarks[(std::size_t)line] == 0) d.lineMarks[(std::size_t)line] = 2;   // deleted above
    }
    return d.lineMarks;
}

const std::vector<Symbol>& ScriptIdeApp::Impl::outlineOf(Doc& d) {
    if (d.outlineVersion != d.code.Edit().Version()) {
        d.outlineVersion = d.code.Edit().Version();
        d.outline = OutlineOf(d.language, d.code.Edit().GetText(), d.key);
    }
    return d.outline;
}

// The innermost symbol whose block holds line `line0` (0-based), or null.
const Symbol* ScriptIdeApp::Impl::enclosing(Doc& d, int line0) {
    const Symbol* best = nullptr;
    UI::CodeEdit& e = d.code.Edit();
    for (const Symbol& s : outlineOf(d)) {
        const int head = s.line - 1;
        if (head > line0) continue;
        const int end = e.FoldEnd(head);
        if (head == line0 || (end >= line0)) if (!best || head > best->line - 1) best = &s;
    }
    return best;
}

void ScriptIdeApp::Impl::drawOverlays(Doc& d) {
    UI::CodeEdit& e = d.code.Edit();
    const UI::Rect v = e.ViewRect();
    if (v.w <= 0 || v.h <= 0) return;
    const auto& dl = UI::GetWindowDrawList();
    const int first = e.FirstVisibleLine(), count = e.VisibleLineCount() + 1;
    const float lh = e.LineHeight();
    UI::PushClipRect(e.ItemRect().x, v.y, e.ItemRect().w, v.h);
    // the bars of changed lines, at the left edge of the gutter
    const std::vector<char>& marks = changedMarks(d);
    for (int l = first; l < first + count && l < (int)marks.size(); ++l) {
        if (!marks[(std::size_t)l] || e.IsLineHidden(l)) continue;
        const float y = e.PosToScreen(UI::CodePos{l, 0}).y;
        if (marks[(std::size_t)l] == 1) dl->AddRectFilled(UI::Vec2(e.ItemRect().x + 1, y), UI::Vec2(e.ItemRect().x + 4, y + lh), Rgb(86, 156, 214));
        else dl->AddRectFilled(UI::Vec2(e.ItemRect().x + 1, y - 2), UI::Vec2(e.ItemRect().x + 6, y + 2), Rgb(232, 102, 92));
    }
    // the ruler at column 120
    if (settings.showRuler) {
        const float x = e.PosToScreen(UI::CodePos{first, 0}).x + 120.0f * e.CharWidth();
        if (x > v.x && x < v.x + v.w) dl->AddLine(UI::Vec2(x, v.y), UI::Vec2(x, v.y + v.h), Rgb(255, 255, 255, 22));
    }
    // a line's problem after its code
    if (settings.inlineProblems) {
        std::set<int> done;
        for (const Diagnostic& p : d.problems) {
            const int l = p.line - 1;
            if (l < first || l >= first + count || l >= e.LineCount() || !done.insert(l).second || e.IsLineHidden(l)) continue;
            const UI::Vec2 end = e.PosToScreen(UI::CodePos{l, e.LineLength(l)});
            const float x = end.x + 3.0f * e.CharWidth();
            if (x > v.x + v.w - 40) continue;
            dl->AddText(UI::Vec2(x, end.y + (lh - UI::GetTextLineHeight()) * 0.5f), p.severity == 0 ? Rgb(232, 102, 92, 170) : Rgb(226, 182, 76, 170),
                        Fit(p.message, v.x + v.w - x - 8).c_str());
        }
    }
    UI::PopClipRect();
    // the header of the block the view's top line is in, pinned at the top
    if (first > 0) {
        const Symbol* s = enclosing(d, first);
        if (s && s->line - 1 < first) {
            const std::string text = e.Line(s->line - 1);
            dl->AddRectFilled(UI::Vec2(e.ItemRect().x, v.y), UI::Vec2(v.x + v.w, v.y + lh), (kPanel & 0x00ffffffu) | 0xf5000000u);
            dl->AddLine(UI::Vec2(e.ItemRect().x, v.y + lh), UI::Vec2(v.x + v.w, v.y + lh), kLine);
            const std::string num = std::to_string(s->line);
            dl->AddText(UI::Vec2(e.ItemRect().x + 6, v.y + (lh - UI::GetTextLineHeight()) * 0.5f), kDim, num.c_str());
            dl->AddText(UI::Vec2(v.x, v.y + (lh - UI::GetTextLineHeight()) * 0.5f), kText, Fit(text, v.w - 10).c_str());
        }
    }
}

void ScriptIdeApp::Impl::drawMinimap(Doc& d, float X, float Y, float W, float H, bool modal) {
    UI::CodeEdit& e = d.code.Edit();
    const auto& dl = UI::GetWindowDrawList();
    dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kStrip);
    dl->AddLine(UI::Vec2(X, Y), UI::Vec2(X, Y + H), kLine);
    const int n = std::max(1, e.LineCount());
    const float step = std::min(3.0f, (H - 8.0f) / (float)n);   // one line: at most 3 px
    const std::vector<char>& marks = changedMarks(d);
    for (int l = 0; l < n; ++l) {
        const std::string& line = e.Line(l);
        const std::size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        const float y = Y + 4 + step * (float)l;
        const float x0 = X + 6 + std::min(30.0f, (float)a * 1.2f), x1 = std::min(X + W - 4, x0 + (float)(line.size() - a) * 1.2f);
        const bool comment = line.compare(a, 2, "//") == 0 || line.compare(a, 2, "--") == 0 || line[a] == '#';
        dl->AddRectFilled(UI::Vec2(x0, y), UI::Vec2(x1, y + std::max(1.0f, step - 1.0f)),
                          l < (int)marks.size() && marks[(std::size_t)l] == 1 ? Rgb(86, 156, 214, 200) : comment ? Rgb(110, 120, 110, 160) : Rgb(170, 176, 190, 150));
    }
    // the part in view, and a click (or drag) there scrolls to it
    const float top = Y + 4 + step * (float)e.FirstVisibleLine(), bot = top + step * (float)std::max(1, e.VisibleLineCount());
    dl->AddRectFilled(UI::Vec2(X + 1, top), UI::Vec2(X + W, bot), Rgb(255, 255, 255, 26));
    const UI::Vec2 m = UI::GetMousePos();
    if (!modal && UI::IsMouseDown(0) && m.x >= X && m.x < X + W && m.y >= Y && m.y < Y + H) {
        const int line = std::min(n - 1, std::max(0, (int)((m.y - Y - 4) / std::max(0.5f, step))));
        const int half = std::max(1, e.VisibleLineCount()) / 2;
        e.SetScroll(e.ScrollX(), (float)std::max(0, line - half) * e.LineHeight());
    }
}

// ---- Insert and Help ---------------------------------------------------------------------------------------------------------

std::vector<Snippet> ScriptIdeApp::Impl::snippetsFor(const std::string& language) {
    if (cfg.snippets) return cfg.snippets(language);
    std::vector<Snippet> out;
    for (const Snippet& s : BuiltinSnippets())
        if (s.language == language || (language.empty() && s.language == "okayscript")) out.push_back(s);
    return out;
}

std::vector<ApiEntry> ScriptIdeApp::Impl::apiFor(const std::string& language) {
    if (cfg.api) return cfg.api(language);
    std::vector<ApiEntry> out;
    std::vector<std::string> names;
    std::string (*doc)(const std::string&) = nullptr;
    if (language == "lua") { names = okay::LuaScriptVM::CompletionNames(); doc = okay::LuaScriptVM::CompletionDoc; }
    else if (language == "javascript") { names = okay::JavaScriptVM::CompletionNames(); doc = okay::JavaScriptVM::CompletionDoc; }
    else names = wordsFor(language).known;
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    for (const std::string& n : names) {
        ApiEntry a;
        a.name = n;
        a.doc = doc ? doc(n) : std::string();
        const std::string first = a.doc.substr(0, a.doc.find('\n'));
        a.signature = first.find('(') != std::string::npos ? first : n;
        out.push_back(std::move(a));
    }
    return out;
}

void ScriptIdeApp::Impl::insertAtCaret(Doc& d, const std::string& text) {
    UI::CodeEdit& e = d.code.Edit();
    e.BreakUndoGroup();
    e.InsertText(text.c_str());
    e.BreakUndoGroup();
    e.Focus();
    notice.clear();
}

void ScriptIdeApp::Impl::drawInsertMenu(Doc* d) {
    if (!UI::BeginMenu("Insert")) return;
    if (UI::MenuItem("Behaviour Template...", nullptr, false, d != nullptr)) openPicker(K_Snippets);
    if (UI::MenuItem("API Command...", nullptr, false, d != nullptr)) openPicker(K_Api);
    if (UI::BeginMenu("C# Snippets")) {
        for (const Snippet& s : BuiltinSnippets())
            if (s.group == "C#" && UI::MenuItem(s.title.c_str(), nullptr, false, d != nullptr) && d) insertAtCaret(*d, s.body);
        UI::EndMenu();
    }
    if (cfg.snippets && d) {   // the host's own (the editor plugins' snippets) not in the lists above
        std::vector<Snippet> more;
        for (const Snippet& s : cfg.snippets(d->language)) if (s.group != "C#" && s.language != "okayscript") more.push_back(s);
        if (!more.empty() && UI::BeginMenu("More Snippets")) {
            for (const Snippet& s : more) if (UI::MenuItem(s.title.c_str())) insertAtCaret(*d, s.body);
            UI::EndMenu();
        }
    }
    UI::EndMenu();
}

void ScriptIdeApp::Impl::drawHelpMenu() {
    if (!UI::BeginMenu("Help")) return;
    if (UI::MenuItem("C# Quick Reference")) refOpen = true;
    if (cfg.showDocs && UI::MenuItem("Scripting Docs")) cfg.showDocs();
    if (UI::MenuItem("Commands...", "Ctrl+Shift+P")) openPicker(K_Commands);
    UI::EndMenu();
}

void ScriptIdeApp::Impl::drawViewExtras(Doc* d, bool& changed) {
    auto toggle = [&](const char* label, bool& v) { if (UI::MenuItem(label, nullptr, v)) { v = !v; changed = true; } };
    toggle("Minimap", settings.showMinimap);
    toggle("Ruler at Column 120", settings.showRuler);
    toggle("Problems After the Code", settings.inlineProblems);
    toggle("Syntax Colors", settings.syntaxColors);
    toggle("Auto-close Brackets", settings.autoClose);
    if (UI::BeginMenu("Tab Width")) {
        for (int tw : {2, 4, 8})
            if (UI::MenuItem(std::to_string(tw).c_str(), nullptr, settings.tabSize == tw)) { settings.tabSize = tw; changed = true; }
        UI::EndMenu();
    }
    if (UI::BeginMenu("Line Spacing")) {
        static const char* kNames[3] = {"Compact", "Comfortable", "Spacious"};
        for (int i = 0; i < 3; ++i)
            if (UI::MenuItem(kNames[i], nullptr, settings.density == i)) { settings.density = i; changed = true; }
        UI::EndMenu();
    }
    if (UI::MenuItem("Fold All", "Ctrl+Alt+[", false, d != nullptr)) d->code.Edit().FoldAll();
    if (UI::MenuItem("Unfold All", "Ctrl+Alt+]", false, d != nullptr)) d->code.Edit().UnfoldAll();
    if (cfg.windowCommands) {
        const std::vector<AppCommand> items = cfg.windowCommands();
        if (!items.empty() && UI::BeginMenu("Window")) {
            for (const AppCommand& c : items) if (UI::MenuItem(c.title.c_str()) && c.run) c.run();
            UI::EndMenu();
        }
    }
}

void ScriptIdeApp::Impl::drawQuickReference(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    const float bw = std::min(760.0f, W - 40.0f), bh = std::min(480.0f, H - 40.0f);
    if (beginDialog(cfg.idPrefix + "_csref", X, Y, W, H, bw, bh, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("C# Quick Reference: each of these works in a script");
        UI::SetCursorScreenPos(UI::Vec2(bx - 4, UI::GetCursorScreenPos().y + 4));
        if (UI::BeginChild((cfg.idPrefix + "_csreflist").c_str(), bw - 20, bh - 90, false)) {
            for (const auto& r : CSharpQuickReference()) {
                UI::TextColored(206, 214, 250, r.first.c_str());
                UI::TextDisabled(("    " + r.second).c_str());
            }
        }
        UI::EndChild();
        UI::SetCursorScreenPos(UI::Vec2(bx, by + bh - 52));
        if (UI::Button("Close##csref") || UI::IsKeyPressed(UI::KeyId_Escape, false)) refOpen = false;
    }
    UI::End();
}

// ---- public ------------------------------------------------------------------------------------------------------------------

bool ScriptIdeApp::ReloadFromDisk() { Impl::Doc* d = m->activeDoc(); return d && m->reloadFromDisk(*d); }
void ScriptIdeApp::CheckDiskNow() { m->checkDisk(); }
bool ScriptIdeApp::ConflictOpen() const { const Impl::Doc* d = m->activeDoc(); return d && d->conflict; }
void ScriptIdeApp::ResolveConflict(bool loadExternal) {
    Impl::Doc* d = m->activeDoc();
    if (!d || !d->conflict) return;
    if (loadExternal) m->reloadFromDisk(*d);
    else d->conflict = false;
}
std::vector<int> ScriptIdeApp::ChangedLines() {
    std::vector<int> out;
    Impl::Doc* d = m->activeDoc();
    if (!d) return out;
    const std::vector<char>& marks = m->changedMarks(*d);
    for (std::size_t i = 0; i < marks.size(); ++i) if (marks[i] == 1) out.push_back((int)i + 1);
    return out;
}
std::string ScriptIdeApp::StickyHeader() {
    Impl::Doc* d = m->activeDoc();
    if (!d) return std::string();
    const int first = d->code.Edit().FirstVisibleLine();
    if (first <= 0) return std::string();
    const Symbol* s = m->enclosing(*d, first);
    return s && s->line - 1 < first ? d->code.Edit().Line(s->line - 1) : std::string();
}
std::string ScriptIdeApp::Breadcrumb() {
    Impl::Doc* d = m->activeDoc();
    if (!d) return std::string();
    const Symbol* s = m->enclosing(*d, d->code.Edit().Caret().line);
    return s ? s->display : std::string();
}
void ScriptIdeApp::ShowQuickReference() { m->refOpen = true; }
std::vector<ScriptIdeApp::DocumentInfo> ScriptIdeApp::Documents() const {
    std::vector<DocumentInfo> out;
    for (const auto& d : m->docs)
        out.push_back(DocumentInfo{d->key, d->path, d->label, d->language, m->dirty(*d), d->code.Edit().Version()});
    return out;
}
bool ScriptIdeApp::SaveDocument(const std::string& key) { Impl::Doc* d = m->byKey(key); return d && m->save(*d); }
bool ScriptIdeApp::InsertText(const std::string& text) { Impl::Doc* d = m->activeDoc(); if (!d) return false; m->insertAtCaret(*d, text); return true; }
bool ScriptIdeApp::SetActiveText(const std::string& text) { Impl::Doc* d = m->activeDoc(); if (!d) return false; m->setText(*d, text); return true; }
bool ScriptIdeApp::QuickReferenceOpen() const { return m->refOpen; }

} // namespace scriptide
