#include "ScriptEditorCodeEdit.hpp"

#include <algorithm>

namespace scripteditor {

namespace {
const char kBom[] = "\xEF\xBB\xBF";

// "\r\n" to "\n": the one change to a script's bytes that loading is allowed to make (the old editor did the same).
std::string CrlfToLf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out.push_back(s[i]);
    }
    return out;
}
}

ScriptEditorCodeEdit::ScriptEditorCodeEdit() {
    ed_.SetLanguage(OkayUI::OkayScriptLanguage());
    ed_.SetTabSize(4);
    ed_.SetLineSpacing(1.55f);          // what the script editor sets on the ImGui editor
    ed_.SetInlineMessages(true);        // the script editor shows each line's first error or warning next to the code
    ed_.SetBreakpointCallback([this](int line, bool) { gutterClicks_.push_back(line + 1); }, false);   // report the click, toggle nothing
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
    bom_ = text.compare(0, 3, kBom) == 0;
    const std::string body = bom_ ? text.substr(3) : text;
    // Would showing this change more than the CRLF line ends? Then the editor must not be the one to write it back.
    const std::string clean = ed_.Model().Repair(body);
    const bool damaged = clean != CrlfToLf(body);
    keepOriginal_ = false; notice_.clear(); original_.clear();
    ed_.SetText(body);
    if (damaged || ed_.Truncated()) {
        keepOriginal_ = true;
        original_ = text;
        if (ed_.Truncated()) notice_ = "This file is over the editor's size limit; only the start is shown, and it is open read-only so nothing is lost.";
        else if (!okay::text::Buffer::IsValidUtf8(body)) notice_ = "This file is not valid UTF-8; it is open read-only so nothing is lost.";
        else notice_ = "This file has control characters or lone carriage returns this editor would change; it is open read-only so nothing is lost.";
    }
    ed_.SetReadOnly(hostReadOnly_ || keepOriginal_);
    seenVersion_ = ed_.Version();       // setting the text is not a user edit
}

std::string ScriptEditorCodeEdit::Notice() const {
    if (keepOriginal_) return notice_;
    if (!insertNotice_.empty() && insertNoticeVersion_ == ed_.Version()) return insertNotice_;
    return ed_.Status();
}

std::string ScriptEditorCodeEdit::GetText() const {
    if (keepOriginal_) return original_ + "\n";             // nothing could be edited, so what goes back is what came in
    return (bom_ ? std::string(kBom) : std::string()) + ed_.GetText() + "\n";
}

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
    if (ed_.ReadOnly()) return;                     // nothing to do (and nothing to say: the notice already tells why)
    std::string t = text;
    if (t.compare(0, 3, kBom) == 0 && ed_.SelectionStart() == OkayUI::CodePos{0, 0}) {   // the script's own mark, not a character
        t.erase(0, 3);
        bom_ = true;
    }
    if (ed_.Model().Repair(t) != CrlfToLf(t)) {     // e.g. "Reload from disk" of a file that is not valid UTF-8: do not repair it into the script
        insertNotice_ = "Not inserted: that text has bytes this editor cannot keep (not valid UTF-8, or control characters); the script was not changed.";
        insertNoticeVersion_ = ed_.Version();
        return;
    }
    ed_.BreakUndoGroup();               // each call is its own undo step, like the library's InsertTextUndoable
    ed_.InsertText(t.c_str());
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
        if (m.second.compare(0, 7, "warning") == 0) cm.color = OkayUI::RGBA(230, 195, 90);   // the script editor prefixes warnings with "warning: "
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

std::vector<int> ScriptEditorCodeEdit::TakeGutterClicks() {
    std::vector<int> out;
    out.swap(gutterClicks_);
    return out;
}

OkayUI::Vec2 ScriptEditorCodeEdit::ContentOrigin() const {
    const OkayUI::Vec2 p = ed_.PosToScreen(OkayUI::CodePos{0, 0});
    return OkayUI::Vec2{p.x - ed_.GutterWidth(), p.y};
}

} // namespace scripteditor
