// Script Editor (ScriptIdeApp.hpp): Format Document, Rename with a preview, the quick-fix menu and the light bulb, Join Lines,
// Expand Selection and signature help. The decisions are the model's (editor/ScriptIde.hpp: ReindentByBraces, PlanRename,
// ApplyEdits, JoinLines, CallAtCaret) and the engine formatters'; this file draws them and applies them as one undo step each.
// Written from general editor behaviour and the OkayUI interface; no code, names, icons or look from any other editor (rule 19a).
#include "ScriptIdeAppImpl.hpp"

#include "okay/Lua/LuaScriptVM.hpp"             // LuaScriptVM::Format
#include "okay/JavaScript/JavaScriptVM.hpp"     // JavaScriptVM::Format

namespace scriptide {

void ScriptIdeApp::Impl::setText(Doc& d, const std::string& text) {
    UI::CodeEdit& e = d.code.Edit();
    if (e.GetText() == text) return;
    const UI::CodePos caret = e.Caret();
    e.SelectAll();
    e.BreakUndoGroup();
    e.InsertText(text.c_str());
    e.BreakUndoGroup();
    e.SetCaret(UI::CodePos{std::min(caret.line, e.LineCount() - 1), 0});
    e.SetCaret(UI::CodePos{e.Caret().line, std::min(caret.col, e.LineLength(e.Caret().line))});
}

// ---- Format Document ----------------------------------------------------------------------------------------------------------

bool ScriptIdeApp::Impl::formatDocument() {
    Doc* d = activeDoc();
    if (!d) return false;
    const std::string text = d->code.Edit().GetText();
    std::string out;
    if (!(cfg.format && cfg.format(d->language, d->path, text, out))) {
        if (d->language == "lua") out = okay::LuaScriptVM::Format(text);
        else if (d->language == "javascript") out = okay::JavaScriptVM::Format(text);
        else out = ReindentByBraces(text);
    }
    if (out == text) { notice = "Already formatted"; return false; }
    setText(*d, out);
    notice = "Formatted " + d->label;
    return true;
}

// ---- Rename --------------------------------------------------------------------------------------------------------------------

RenamePlan ScriptIdeApp::Impl::planRename(const std::string& from, const std::string& to, bool all) {
    if (all) return PlanRename(buildIndex(), from, to);
    SymbolIndex one;
    if (const Doc* d = activeDoc()) one.SetFile(d->key, d->code.Edit().GetText(), d->language);
    return PlanRename(one, from, to);
}

int ScriptIdeApp::Impl::applyRename(const std::string& from, const std::string& to, bool all) {
    const RenamePlan plan = planRename(from, to, all);
    if (!plan.error.empty()) { notice = plan.error; return 0; }
    int files = 0;
    for (const auto& kv : plan.newTexts) {
        if (Doc* d = byKey(kv.first)) { setText(*d, kv.second); continue; }   // open: in its tab, not saved
        bool ok = false;
        const std::string old = FileText(kv.first, ok);
        if (!ok) continue;
        history.Record(kv.first, old, now(), "Before rename");
        if (WriteText(kv.first, kv.second)) ++files;
    }
    notice = "Renamed " + from + " to " + to + ": " + std::to_string(plan.count) + " places" +
             (files ? " (" + std::to_string(files) + " closed files written)" : std::string());
    return plan.count;
}

void ScriptIdeApp::Impl::openRename(const std::string& word) {
    Doc* d = activeDoc();
    if (!d) return;
    renameFrom = word.empty() ? wordAt(*d, d->code.Edit().Caret()) : word;
    if (renameFrom.empty()) { notice = "Put the caret on a name first"; return; }
    std::snprintf(renameBuf, sizeof renameBuf, "%s", renameFrom.c_str());
    renameOpen = true;
    renameFocus = true;
    renamePlanned.clear();
}

void ScriptIdeApp::Impl::drawRename(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    const float bw = std::min(640.0f, W - 40.0f), bh = std::min(400.0f, H - 40.0f);
    if (beginDialog(cfg.idPrefix + "_rename", X, Y, W, H, bw, bh, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted(("Rename " + renameFrom + " to").c_str());
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 2));
        UI::SetNextItemWidth(bw - 28);
        if (renameFocus) { UI::SetKeyboardFocusHere(); renameFocus = false; }
        const bool enter = UI::InputText("##renameto", renameBuf, (int)sizeof renameBuf, UI::InputText_EnterReturnsTrue);
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 2));
        UI::Checkbox("In every script of the project (not only this file)", &renameAll);
        const std::string want = renameFrom + "\n" + renameBuf + "\n" + (renameAll ? "1" : "0");
        if (want != renamePlanned) { renamePlan = planRename(renameFrom, renameBuf, renameAll); renamePlanned = want; }
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 2));
        if (!renamePlan.error.empty()) UI::TextColored(232, 102, 92, renamePlan.error.c_str());
        else UI::TextDisabled((std::to_string(renamePlan.count) + " places in " + std::to_string(renamePlan.newTexts.size()) + " files").c_str());
        UI::SetCursorScreenPos(UI::Vec2(bx - 4, UI::GetCursorScreenPos().y + 2));
        const float listH = std::max(40.0f, by + bh - 60.0f - UI::GetCursorScreenPos().y);
        if (UI::BeginChild((cfg.idPrefix + "_renamelist").c_str(), bw - 20, listH, true)) {
            for (std::size_t i = 0; i < renamePlan.changes.size() && i < 500; ++i) {
                const RenameChange& c = renamePlan.changes[i];
                const Doc* t = byKey(c.file);
                const std::string where = (t && t->path.empty() ? t->label : relative(c.file)) + ":" + std::to_string(c.line);
                UI::TextDisabled(where.c_str());
                UI::TextColored(232, 102, 92, ("- " + Fit(c.before, bw - 60)).c_str());
                UI::TextColored(98, 180, 110, ("+ " + Fit(c.after, bw - 60)).c_str());
            }
        }
        UI::EndChild();
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        const bool ok = renamePlan.error.empty() && renamePlan.count > 0;
        if ((UI::Button("Rename##apply") || enter) && ok) {
            applyRename(renameFrom, renameBuf, renameAll);
            renameOpen = false;
        }
        UI::SameLine();
        if (UI::Button("Cancel##rename") || UI::IsKeyPressed(UI::KeyId_Escape, false)) renameOpen = false;
    }
    UI::End();
}

// ---- quick fixes ----------------------------------------------------------------------------------------------------------------

std::vector<QuickFix> ScriptIdeApp::Impl::fixesAtCaret() {
    std::vector<QuickFix> out;
    Doc* d = activeDoc();
    if (!d) return out;
    const int line = d->code.Edit().Caret().line + 1;
    const std::string text = d->code.Edit().GetText();
    for (const Diagnostic& p : d->problems) {
        if (p.line != line) continue;
        for (const QuickFix& f : p.fixes) if (!f.title.empty()) out.push_back(f);
        if (p.fixes.empty()) {   // a checker's "Did you mean X?" that came without a fix
            const QuickFix f = FixFromSuggestion(p, text);
            if (!f.title.empty()) out.push_back(f);
        }
    }
    return out;
}

bool ScriptIdeApp::Impl::applyFix(const QuickFix& f) {
    Doc* d = activeDoc();
    if (!d || f.edits.empty()) return false;
    setText(*d, ApplyEdits(d->code.Edit().GetText(), f.edits));
    check(*d);
    notice = f.title;
    return true;
}

void ScriptIdeApp::Impl::drawQuickFixes() {
    Doc* d = activeDoc();
    if (!d || fixes.empty()) { fixesOpen = false; return; }
    const UI::Vec2 at = d->code.Edit().CaretScreenPosBelow();
    const float rowH = UI::GetTextLineHeight() + 8.0f;
    float w = 200.0f;
    for (const QuickFix& f : fixes) w = std::max(w, UI::CalcTextWidth(f.title.c_str()) + 30.0f);
    const float h = rowH * (float)fixes.size() + 8.0f;
    const auto& fg = UI::GetForegroundDrawList();
    fg->AddRectFilled(UI::Vec2(at.x, at.y), UI::Vec2(at.x + w, at.y + h), kPanel, 4.0f);
    fg->AddRect(UI::Vec2(at.x, at.y), UI::Vec2(at.x + w, at.y + h), kLine, 4.0f, 1.0f);
    const UI::Vec2 m = UI::GetMousePos();
    int pick = -1;
    for (std::size_t i = 0; i < fixes.size(); ++i) {
        const float ry = at.y + 4 + rowH * (float)i;
        if ((int)i == fixSel) fg->AddRectFilled(UI::Vec2(at.x + 3, ry), UI::Vec2(at.x + w - 3, ry + rowH), kHover, 3.0f);
        fg->AddText(UI::Vec2(at.x + 12, ry + 4), kText, fixes[i].title.c_str());
        if (m.x >= at.x && m.x < at.x + w && m.y >= ry && m.y < ry + rowH && UI::IsMouseClicked(0)) pick = (int)i;
    }
    if (!fixesFresh) {
        if (UI::IsKeyPressed(UI::KeyId_Down)) fixSel = (fixSel + 1) % (int)fixes.size();
        if (UI::IsKeyPressed(UI::KeyId_Up)) fixSel = (fixSel + (int)fixes.size() - 1) % (int)fixes.size();
        if (UI::IsKeyPressed(UI::KeyId_Enter, false)) pick = fixSel;
        if (UI::IsKeyPressed(UI::KeyId_Escape, false)) fixesOpen = false;
    }
    fixesFresh = false;
    if (pick >= 0 && pick < (int)fixes.size()) {
        const QuickFix f = fixes[(std::size_t)pick];
        fixesOpen = false;
        applyFix(f);
    }
}

// A light bulb in the gutter beside the caret's line when it has fixes; a click opens the menu.
void ScriptIdeApp::Impl::drawLightBulb(Doc& d) {
    UI::CodeEdit& e = d.code.Edit();
    const int line = e.Caret().line + 1;
    bool any = false;
    for (const Diagnostic& p : d.problems) if (p.line == line && (!p.fixes.empty() || p.message.find("Did you mean") != std::string::npos)) any = true;
    if (!any) return;
    const UI::Vec2 top = e.PosToScreen(UI::CodePos{line - 1, 0});
    const float lh = e.LineHeight(), r = std::min(6.0f, lh * 0.3f);
    const UI::Vec2 c(e.ItemRect().x + 7.0f, top.y + lh * 0.5f);
    const auto& dl = UI::GetForegroundDrawList();
    dl->AddCircleFilled(c, r, Rgb(240, 200, 80));
    dl->AddRectFilled(UI::Vec2(c.x - r * 0.45f, c.y + r * 0.7f), UI::Vec2(c.x + r * 0.45f, c.y + r * 1.4f), Rgb(170, 170, 170));
    const UI::Vec2 m = UI::GetMousePos();
    if (UI::IsMouseClicked(0) && std::abs(m.x - c.x) <= r + 2 && std::abs(m.y - c.y) <= r + 3) {
        fixes = fixesAtCaret();
        fixSel = 0;
        fixesOpen = !fixes.empty();
        fixesFresh = true;
    }
}

// ---- Join Lines, Expand Selection -------------------------------------------------------------------------------------------------

bool ScriptIdeApp::Impl::joinLines() {
    Doc* d = activeDoc();
    if (!d) return false;
    UI::CodeEdit& e = d->code.Edit();
    const int first = e.SelectionStart().line, last = std::max(first + 1, e.SelectionEnd().line);
    if (first + 1 >= e.LineCount()) return false;
    const std::string out = JoinLines(e.GetText(), first + 1, last - first);
    const int col = (int)e.Line(first).size();
    setText(*d, out);
    e.SetCaret(UI::CodePos{first, col});
    return true;
}

bool ScriptIdeApp::Impl::expandSelection() {
    Doc* d = activeDoc();
    if (!d) return false;
    UI::CodeEdit& e = d->code.Edit();
    const UI::CodePos a = e.SelectionStart(), b = e.SelectionEnd();
    // 1: nothing selected -> the word; 2: part of lines -> the whole lines; 3: whole lines -> the whole document
    if (!e.HasSelection()) {
        e.SelectWordAt(e.Caret());
        if (e.HasSelection()) return true;
    }
    const bool wholeLines = a.col == 0 && (b.col == 0 ? b.line > a.line : b.col == e.LineLength(b.line));
    if (!wholeLines) {
        e.SetSelection(UI::CodePos{a.line, 0}, UI::CodePos{b.line, e.LineLength(b.line)});
        return true;
    }
    e.SelectAll();
    return true;
}

// ---- signature help -------------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::updateSignature(Doc& d) {
    sigText.clear();
    sigDoc.clear();
    UI::CodeEdit& e = d.code.Edit();
    if (!e.Focused() || acOpen) return;
    const std::string text = e.GetText();
    const UI::CodePos c = e.Caret();
    const std::size_t at = OffsetOf(text, c.line + 1, c.col);
    const CaretCall call = CallAtCaret(text, at);
    if (call.name.empty()) return;
    std::string doc;
    std::string sig = cfg.signature ? cfg.signature(d.language, call.name, doc) : std::string();
    if (sig.empty() && !cfg.signature) {   // the language's hover doc, when its first line is the signature
        const scripteditor::LanguageWords& w = wordsFor(d.language);
        const auto it = w.docs.find(call.name);
        if (it != w.docs.end()) {
            const std::string first = it->second.substr(0, it->second.find('\n'));
            if (first.find('(') != std::string::npos) {
                sig = first;
                const std::size_t nl = it->second.find('\n');
                doc = nl == std::string::npos ? std::string() : it->second.substr(nl + 1);
            }
        }
    }
    if (sig.empty()) return;
    sigText = sig;
    sigDoc = doc.substr(0, doc.find('\n'));
    (void)call.argIndex;
}

void ScriptIdeApp::Impl::drawSignature(Doc& d) {
    if (sigText.empty()) return;
    UI::CodeEdit& e = d.code.Edit();
    const UI::Vec2 below = e.CaretScreenPosBelow();
    const float lh = UI::GetTextLineHeight(), rowH = lh + 4.0f;
    const float h = rowH * (sigDoc.empty() ? 1.0f : 2.0f) + 8.0f;
    float w = std::max(UI::CalcTextWidth(sigText.c_str()), UI::CalcTextWidth(sigDoc.c_str())) + 20.0f;
    w = std::min(w, 520.0f);
    const float x = below.x, y = below.y - e.LineHeight() - h - 2.0f;
    const auto& fg = UI::GetForegroundDrawList();
    fg->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + h), kPanel, 4.0f);
    fg->AddRect(UI::Vec2(x, y), UI::Vec2(x + w, y + h), kLine, 4.0f, 1.0f);
    fg->AddText(UI::Vec2(x + 10, y + 4), Rgb(255, 214, 140), Fit(sigText, w - 20).c_str());
    if (!sigDoc.empty()) fg->AddText(UI::Vec2(x + 10, y + 4 + rowH), kDim, Fit(sigDoc, w - 20).c_str());
}

// ---- public ------------------------------------------------------------------------------------------------------------------

bool ScriptIdeApp::FormatDocument() { return m->formatDocument(); }
void ScriptIdeApp::OpenRename(const std::string& word) { m->openRename(word); }
bool ScriptIdeApp::RenameOpen() const { return m->renameOpen; }
RenamePlan ScriptIdeApp::PlanRenameOf(const std::string& from, const std::string& to, bool allScripts) { return m->planRename(from, to, allScripts); }
int ScriptIdeApp::ApplyRename(const std::string& from, const std::string& to, bool allScripts) { return m->applyRename(from, to, allScripts); }
std::vector<QuickFix> ScriptIdeApp::FixesAtCaret() { return m->fixesAtCaret(); }
void ScriptIdeApp::OpenQuickFixes() { m->fixes = m->fixesAtCaret(); m->fixSel = 0; m->fixesOpen = !m->fixes.empty(); m->fixesFresh = true; }
bool ScriptIdeApp::QuickFixesOpen() const { return m->fixesOpen; }
bool ScriptIdeApp::ApplyFix(std::size_t index) {
    const std::vector<QuickFix> f = m->fixesAtCaret();
    return index < f.size() && m->applyFix(f[index]);
}
bool ScriptIdeApp::JoinLinesAtCaret() { return m->joinLines(); }
bool ScriptIdeApp::ExpandSelection() { return m->expandSelection(); }
std::string ScriptIdeApp::SignatureShown() const { return m->sigText; }

} // namespace scriptide
