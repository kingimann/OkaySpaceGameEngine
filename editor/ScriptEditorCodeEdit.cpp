#include "ScriptEditorCodeEdit.hpp"

#include <algorithm>

namespace scripteditor {

ScriptEditorCodeEdit::ScriptEditorCodeEdit() {
    ed_.SetLanguage(OkayUI::OkayScriptLanguage());
    ed_.SetTabSize(4);
    ed_.SetLineSpacing(1.55f);          // what the script editor sets on the ImGui editor
    seenVersion_ = ed_.Version();
}

Coord ScriptEditorCodeEdit::ToCoord(OkayUI::CodePos p) const {
    p = ed_.Clamp(p);
    return Coord(p.line, ed_.ByteToColumn(p.line, p.col));
}

OkayUI::CodePos ScriptEditorCodeEdit::ToPos(Coord c) const {
    const int line = std::min(std::max(c.mLine, 0), ed_.LineCount() - 1);
    return OkayUI::CodePos{line, ed_.ColumnToByte(line, (float)c.mColumn)};
}

void ScriptEditorCodeEdit::SetText(const std::string& text) {
    ed_.SetText(text);
    seenVersion_ = ed_.Version();       // setting the text is not a user edit
}

std::string ScriptEditorCodeEdit::GetText() const { return ed_.GetText() + "\n"; }

std::vector<std::string> ScriptEditorCodeEdit::GetTextLines() const {
    std::vector<std::string> out;
    out.reserve((size_t)ed_.LineCount());
    for (int i = 0; i < ed_.LineCount(); ++i) out.push_back(ed_.Line(i));
    return out;
}

std::string ScriptEditorCodeEdit::GetCurrentLineText() const { return ed_.Line(ed_.Caret().line); }

int ScriptEditorCodeEdit::LineLength(int line) const {
    if (line < 0 || line >= ed_.LineCount()) return 0;
    return ed_.ByteToColumn(line, ed_.LineLength(line));
}

void ScriptEditorCodeEdit::InsertTextUndoable(const std::string& text) {
    ed_.BreakUndoGroup();               // each call is its own undo step, like the library's InsertTextUndoable
    ed_.InsertText(text.c_str());
    ed_.BreakUndoGroup();
}

void ScriptEditorCodeEdit::Delete() { ed_.DeleteForward(false); }

bool ScriptEditorCodeEdit::IsTextChanged() {
    const bool changed = ed_.Version() != seenVersion_;
    seenVersion_ = ed_.Version();
    return changed;
}

void ScriptEditorCodeEdit::SetCursorPosition(Coord c) { ed_.SetCaret(ToPos(c), false); }

void ScriptEditorCodeEdit::SetSelection(Coord start, Coord end) {
    if (end < start) std::swap(start, end);
    ed_.SetSelection(ToPos(start), ToPos(end));
}

void ScriptEditorCodeEdit::SetColorizerEnable(bool on) {
    if (on == colorize_) return;
    colorize_ = on;
    if (on) ed_.SetLanguage(OkayUI::OkayScriptLanguage());
    else ed_.SetHighlighter(nullptr);
}

void ScriptEditorCodeEdit::SetUserTypes(const std::unordered_set<std::string>& types) {
    if (!colorize_) return;
    ed_.SetUserTypes(std::vector<std::string>(types.begin(), types.end()));
}

void ScriptEditorCodeEdit::SetErrorMarkers(const std::map<int, std::string>& markers) {
    std::vector<OkayUI::CodeMarker> out;
    for (const auto& m : markers) {
        if (m.first < 1) continue;
        OkayUI::CodeMarker cm;
        cm.line = m.first - 1;
        cm.colStart = 0; cm.colEnd = -1;                // the whole line's text, like the library's red line
        cm.message = m.second;
        out.push_back(std::move(cm));
    }
    ed_.SetMarkers(out);
}

void ScriptEditorCodeEdit::SetBreakpoints(const std::unordered_set<int>& lines) {
    std::vector<int> out;
    for (int l : lines) if (l >= 1) out.push_back(l - 1);
    std::sort(out.begin(), out.end());
    ed_.SetBreakpoints(out);
}

OkayUI::Vec2 ScriptEditorCodeEdit::ContentOrigin() const {
    const OkayUI::Vec2 p = ed_.PosToScreen(OkayUI::CodePos{0, 0});
    return OkayUI::Vec2{p.x - ed_.GutterWidth(), p.y};
}

} // namespace scripteditor
