#include "ScriptEditorCodeEdit.hpp"
#include "okay/UI/OkayUICompat.hpp"   // AddFontTtf
#include "OkfDecode.hpp"
#include "JetBrainsMonoFontOwn.h"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace scripteditor {

ScriptEditorCodeEdit::ScriptEditorCodeEdit() {
    language_ = OkayUI::OkayScriptLanguage();
    ed_.SetLanguage(language_);
    ed_.SetTabSize(4);
    ed_.SetLineSpacing(1.55f);          // what the script editor sets
    ed_.SetFolding(true);               // the fold column in the gutter, Ctrl+Shift+[ and ]
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
    if (on) { ed_.SetLanguage(language_); if (!userTypes_.empty()) ed_.SetUserTypes(userTypes_); }
    else ed_.SetHighlighter(nullptr);
}

void ScriptEditorCodeEdit::SetUserTypes(const std::unordered_set<std::string>& types) {
    userTypes_.assign(types.begin(), types.end());
    std::sort(userTypes_.begin(), userTypes_.end());
    if (!colorize_) return;
    ed_.SetUserTypes(userTypes_);
}

void ScriptEditorCodeEdit::SetErrorMarkers(const std::map<int, std::string>& markers) {
    std::vector<OkayUI::CodeMarker> out;
    for (const auto& m : markers) {
        if (m.first < 1) continue;
        OkayUI::CodeMarker cm;
        cm.line = m.first - 1;
        cm.colStart = 0; cm.colEnd = -1;                // the whole line's text, like the library's red line
        // A message that starts with "warning: " is a warning (an amber wash), and the tooltip names the line.
        static const char kWarn[] = "warning: ";
        const bool warn = m.second.compare(0, sizeof(kWarn) - 1, kWarn) == 0;
        const std::string msg = warn ? m.second.substr(sizeof(kWarn) - 1) : m.second;
        cm.message = (warn ? "Warning at line " : "Error at line ") + std::to_string(m.first) + ": " + msg;
        if (warn) { cm.lineColor = OkayUI::RGBA(200, 150, 40, 55); cm.color = OkayUI::RGBA(230, 190, 70); }
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
    return OkayUI::Vec2{p.x - ed_.GutterWidth() + offX_, p.y + offY_};
}

void ScriptEditorCodeEdit::SetLanguage(const LanguageWords& w) {
    OkayUI::CodeLanguage l;
    for (const std::string& k : w.keywords) l.keywords.insert(k);
    for (const std::string& t : w.types) l.types.insert(t);
    for (const std::string& k : w.known) l.known.insert(k);
    l.lineComments.clear();
    for (const std::string& c : w.lineComments) if (!c.empty()) l.lineComments.push_back(c);
    l.blockStart = w.blockEnd.empty() ? std::string() : w.blockStart;
    l.blockEnd = w.blockEnd;
    l.callBeforeKnown = true;            // the script editor's order: print( is a call
    l.blockOpenWords = w.blockOpenWords; l.blockCloseWords = w.blockCloseWords; l.dedentWords = w.dedentWords;
    l.foldByIndent = w.foldByIndent;
    // A language with Lua's comments and no block words of its own gets Lua's (folding by `end`, the indent after `then`,
    // `end` dedenting): the words of LuaLanguageWords need no second copy of them.
    const bool luaComments = std::find(w.lineComments.begin(), w.lineComments.end(), "--") != w.lineComments.end();
    if (luaComments && l.blockOpenWords.empty()) {
        const LanguageWords lw = LuaBlockWords();
        l.blockOpenWords = lw.blockOpenWords; l.blockCloseWords = lw.blockCloseWords; l.dedentWords = lw.dedentWords;
        l.foldByIndent = true;
    }
    language_ = l;
    auto docs = std::make_shared<const std::map<std::string, std::string>>(w.docs);
    ed_.SetHoverInfo([docs](const std::string& word) {
        const auto it = docs->find(word);
        std::string out = it == docs->end() ? std::string() : it->second;
        if (const auto& extra = ExtraHoverInfo()) {
            const std::string more = extra(word);
            if (!more.empty()) out += (out.empty() ? "" : "\n\n") + more;
        }
        return out;
    });
    if (colorize_) { ed_.SetLanguage(language_); if (!userTypes_.empty()) ed_.SetUserTypes(userTypes_); }
}

std::function<std::string(const std::string&)>& ExtraHoverInfo() {
    static std::function<std::string(const std::string&)> f;
    return f;
}

LanguageWords LuaBlockWords() {
    LanguageWords w;
    w.blockOpenWords = {"function", "do", "then", "repeat", "else"};
    w.blockCloseWords = {"end", "until", "elseif", "else"};
    w.dedentWords = {"end", "else", "elseif", "until"};
    w.foldByIndent = true;
    return w;
}

void ScriptEditorCodeEdit::SetPaletteColors(const unsigned* c, int count) {
    if (!c || count < kScriptPaletteSize) return;
    OkayUI::CodePalette p = ed_.Palette();
    // PaletteIndex order (see the header)
    p.text[OkayUI::CodeColor_Default] = c[0];  p.text[OkayUI::CodeColor_Keyword] = c[1];  p.text[OkayUI::CodeColor_Number] = c[2];
    p.text[OkayUI::CodeColor_String] = c[3];   p.text[OkayUI::CodeColor_Char] = c[4];     p.text[OkayUI::CodeColor_Punct] = c[5];
    p.text[OkayUI::CodeColor_Preproc] = c[6];  p.text[OkayUI::CodeColor_Known] = c[8];    p.text[OkayUI::CodeColor_Comment] = c[10];
    p.text[OkayUI::CodeColor_Type] = c[21];    p.text[OkayUI::CodeColor_Function] = c[22]; p.text[OkayUI::CodeColor_Member] = c[23];
    p.background = c[12]; p.gutter = c[12];   // one background inside the frame, line numbers included
    p.caret = c[13]; p.selection = c[14]; p.selectionInactive = c[14];
    p.errorLine = c[15]; p.breakpointLine = c[16];
    p.lineNumber = c[17]; p.lineNumberCurrent = c[17];
    p.currentLine = c[18]; p.currentLineInactive = c[19]; p.currentLineEdge = c[20];
    // drawn from the colours above: the other places of the word at the caret (the selection, fainter), the indent guides and
    // the fold marks (the line numbers, fainter), the whitespace marks (the comments, fainter)
    auto alpha = [](unsigned col, unsigned a) { return (col & 0x00FFFFFFu) | (a << 24); };
    p.wordMatch = alpha(c[14], std::min(0x60u, (c[14] >> 24) / 2 + 0x18u));
    p.indentGuide = alpha(c[17], 0x38u);
    p.whitespace = alpha(c[10], 0x70u);
    p.fold = c[17];
    ed_.SetPalette(p);
}

const OkayUI::Font* ScriptEditorCodeEdit::CodeFont() {
    // Decoded once: the bytes must outlive the font (AddFontTtf keys its glyph cache on them). 16 px is the size the Dear ImGui
    // script editor shows at 100%; UseCodeFont scales it to the size asked for.
    static std::vector<std::uint8_t> bytes;
    static const OkayUI::Font* font = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (okf::DecodeLines(okf_data::kJetBrainsMonoOkf, okf_data::kJetBrainsMonoOkfLines, bytes) && bytes.size() == okf_data::kJetBrainsMonoOkfRawBytes)
            font = OkayUI::AddFontTtf(bytes.data(), bytes.size(), 16.0f, "JetBrains Mono");
    }
    return font;
}

bool ScriptEditorCodeEdit::UseCodeFont(float px) {
    const OkayUI::Font* f = CodeFont();
    if (!f) return false;
    ed_.SetFont(f, px);
    return true;
}

} // namespace scripteditor
