#include "ScriptEditorCodeEdit.hpp"

#include <algorithm>

namespace scripteditor {

ScriptEditorCodeEdit::ScriptEditorCodeEdit() {
    lang_ = OkayUI::OkayScriptLanguage();
    ed_.SetLanguage(lang_);
    ed_.SetTabSize(4);
    ed_.SetLineSpacing(1.55f);          // what the script editor sets on the ImGui editor
    // Enter copies the line's indent only, as ImGuiColorTextEdit does: the script editor adds a level and pushes the `}` down
    // itself when Enter is pressed between `{` and `}` (editor/main.cpp, "braceEnter"); with ours also doing it the body
    // would be opened twice.
    ed_.SetBracketIndent(false);
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
    if (on) ed_.SetLanguage(lang_);
    else ed_.SetHighlighter(nullptr);
}

void ScriptEditorCodeEdit::SetLanguageWords(const std::unordered_set<std::string>& keywords, const std::unordered_set<std::string>& types,
                                            const std::unordered_set<std::string>& known, std::unordered_map<std::string, std::string> info) {
    OkayUI::CodeLanguage l = OkayUI::OkayScriptLanguage();   // keeps OkayScript's comment rules and $"..." strings
    if (!keywords.empty()) l.keywords = keywords;
    if (!types.empty()) l.types = types;
    l.known.clear();
    for (const std::string& k : known) if (!l.keywords.count(k) && !l.types.count(k)) l.known.insert(k);
    lang_ = std::move(l);
    if (colorize_) ed_.SetLanguage(lang_);
    ed_.SetWordInfo(std::move(info));
}

void ScriptEditorCodeEdit::SetPalette(const std::uint32_t* c, int n) {
    if (!c || n < 21) return;
    // TextEditor::PaletteIndex order: Default, Keyword, Number, String, CharLiteral, Punctuation, Preprocessor, Identifier,
    // KnownIdentifier, PreprocIdentifier, Comment, MultiLineComment, Background, Cursor, Selection, ErrorMarker, Breakpoint,
    // LineNumber, CurrentLineFill, CurrentLineFillInactive, CurrentLineEdge, [TypeName, Function, Member]
    OkayUI::CodePalette p = ed_.Palette();
    using namespace OkayUI;
    p.text[CodeColor_Default] = c[0];   p.text[CodeColor_Keyword] = c[1]; p.text[CodeColor_Number] = c[2];
    p.text[CodeColor_String] = c[3];    p.text[CodeColor_Char] = c[4];    p.text[CodeColor_Punct] = c[5];
    p.text[CodeColor_Preproc] = c[6];   p.text[CodeColor_Known] = c[8];   p.text[CodeColor_Comment] = c[10];
    p.background = c[12];
    p.gutter = c[12];                        // the library draws the numbers on the background
    p.caret = c[13];
    p.selection = c[14];
    p.selectionInactive = (c[14] & 0x00ffffffu) | (((c[14] >> 24) / 2u) << 24);
    p.errorLine = c[15];
    p.breakpoint = c[16];                    // a wash over the whole line, as the library draws it
    p.lineNumber = c[17];
    p.currentLine = c[18];
    p.currentLineInactive = c[19];
    p.currentLineEdge = c[20];
    if (n > 21) p.text[CodeColor_Type] = c[21];
    if (n > 22) p.text[CodeColor_Function] = c[22];
    if (n > 23) p.text[CodeColor_Member] = c[23];
    ed_.SetPalette(p);
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
        cm.colStart = 0; cm.colEnd = -1;
        cm.underline = false;                           // like the library: a wash and the message; the script editor draws its
                                                        // own squiggle under the token the diagnostic names
        // "warning: ..." is a warning (amber), as the library reads it; the prefix is not shown
        static const char kWarn[] = "warning: ";
        cm.warning = m.second.compare(0, sizeof(kWarn) - 1, kWarn) == 0;
        cm.message = cm.warning ? m.second.substr(sizeof(kWarn) - 1) : m.second;
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
    return OkayUI::Vec2{p.x - ed_.GutterWidth() + ed_.PaddingX(), p.y};   // inside the padding, like ImGui's child content origin
}

} // namespace scripteditor
