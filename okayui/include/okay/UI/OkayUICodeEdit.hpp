#pragma once
// OkayUI::CodeEdit: a multi-line code editor widget (own code; replaces the vendored ImGuiColorTextEdit).
// Line-number gutter with breakpoint marks, caret and selection (mouse and keyboard), typing, auto-indent, clipboard,
// undo/redo with grouping, find, bracket matching, scrolling, a pluggable syntax highlighter (plus a built-in C-like and
// OkayScript one), read-only mode and error markers. See docs/own_ui_code_editor.md.
//
// A CodeEdit is a plain object that owns its text and state (one per open script). Call Draw() once per frame inside an
// OkayUI window; it reads the frame's input, edits, and draws the visible lines only. The editing operations are also
// public methods, so they can be driven (and tested) without any input.
//
// Columns are byte offsets into a line and always sit on UTF-8 character boundaries. The text is kept valid UTF-8
// (invalid bytes become '?', "\r\n" and "\r" become "\n", other control characters are dropped). One character is one
// cell wide; a tab advances to the next multiple of the tab size. In the launcher look the code is drawn in the code font
// (JetBrains Mono, OkayUI::PushCodeFont: the font the script editor's ImGui widget uses), whose characters all advance the
// same whole number of pixels. With a proportional font and no code font every character is drawn on its own, centred in
// a cell as wide as the font's wide characters, so the caret and selection still line up.
#include "okay/UI/OkayUI.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OkayUI {

/// A position in the text: a line and a byte offset in it.
struct CodePos {
    int line = 0, col = 0;
    bool operator==(const CodePos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const CodePos& o) const { return !(*this == o); }
    bool operator<(const CodePos& o) const { return line < o.line || (line == o.line && col < o.col); }
    bool operator<=(const CodePos& o) const { return !(o < *this); }
};

/// What a stretch of text is, for colouring. The palette maps each to a colour.
enum CodeColor : unsigned char {
    CodeColor_Default = 0, CodeColor_Keyword, CodeColor_Type, CodeColor_Function, CodeColor_Known, CodeColor_Member,
    CodeColor_Number, CodeColor_String, CodeColor_Char, CodeColor_Comment, CodeColor_Punct, CodeColor_Preproc,
    CodeColor_COUNT
};

/// A coloured stretch [start, end) of one line (byte offsets). Stretches not covered by a token are drawn in the default colour.
struct CodeToken { int start = 0, end = 0; unsigned char color = CodeColor_Default; };

/// Colours the text of one line. `state` is what the previous line left behind (0 at the top of the text) and the return value
/// is the state the next line starts with: that is how a block comment spans lines. Append tokens to `out` in order, without
/// overlap. The callback gets the lines in order from the top, each at most once per edit (the editor caches the states).
using CodeHighlighter = std::function<int(const char* line, int len, int state, std::vector<CodeToken>& out)>;

/// The words and comment styles of a C-like language, for the built-in highlighter.
struct CodeLanguage {
    std::unordered_set<std::string> keywords, types, known;       // known: built-ins, drawn in CodeColor_Known
    std::vector<std::string> lineComments{"//"};                   // each starts a comment that runs to the end of the line
    std::string blockStart = "/*", blockEnd = "*/";                // empty blockStart: no block comments
    bool dollarAtStrings = true;                                   // $"..." and @"..." are strings
};
/// C and C++ words with "//" and block comments.
CodeLanguage CLikeLanguage();
/// OkayScript: the keywords and built-in types the script editor knew, "//" and "#" line comments and block comments.
CodeLanguage OkayScriptLanguage();
/// The built-in highlighter for a language: keywords, types, known words, function calls, members after '.', strings with
/// escapes, characters, numbers (decimal, hex, exponents, suffixes), comments (also across lines) and punctuation.
CodeHighlighter MakeCLikeHighlighter(const CodeLanguage& lang);

/// Colours for the editor. Packed 0xAABBGGRR like the rest of OkayUI (see RGBA).
struct CodePalette {
    Color32 text[CodeColor_COUNT];
    Color32 background, gutter, lineNumber, lineNumberCurrent, caret, selection, selectionInactive, currentLine, currentLineEdge,
            findMatch, bracketMatch, errorLine, errorUnderline, breakpoint, scrollTrack, scrollThumb, scrollThumbHot;
    Color32 currentLineInactive;   // the current line's fill while the editor does not have the keyboard
    Color32 warningLine;           // the wash over a line with a warning (a marker with `warning` set)
    Color32 errorText, warningText;   // the marker's message, written after the end of its line
};
/// The Darcula-flavoured palette the script editor uses.
CodePalette DefaultCodePalette();

/// An error (or warning) the caller wants shown: a wash over the whole line (gutter included), the message written after the
/// end of the line in the error (or warning) colour, clipped to the view and cut to 72 characters, a tooltip with the line and
/// the whole message, and (with `underline`) a wavy underline under the columns [colStart, colEnd) (colEnd < 0: from colStart
/// or the first non-blank to the last non-blank).
struct CodeMarker {
    int line = 0, colStart = 0, colEnd = -1;
    std::string message;
    Color32 color = 0;       // the underline; 0: the palette's errorUnderline
    bool warning = false;    // a warning: the palette's warning colours
    bool underline = true;   // false: no wavy underline (a host that draws its own under the token)
};

/// What the pointer is over this frame (for the last Draw), for a host that draws the tooltip itself (SetHostDrawsTooltip).
struct CodeHover {
    enum Kind : unsigned char { None, Error, Warning, Info };
    Kind kind = None;
    int line = -1;          // 0-based line of the marker (Error, Warning) or of the word (Info)
    std::string text;       // the marker's message (without "Error at line N: "), or the word's help
    std::string tooltip;    // what OkayUI's own tooltip would show
};

/// Where the caret goes.
enum CodeMove : int {
    CodeMove_Left, CodeMove_Right, CodeMove_Up, CodeMove_Down, CodeMove_WordLeft, CodeMove_WordRight,
    CodeMove_LineStart,   // first non-blank, then column 0 (the "smart" Home)
    CodeMove_LineEnd, CodeMove_DocStart, CodeMove_DocEnd, CodeMove_PageUp, CodeMove_PageDown
};

class CodeEdit {
public:
    CodeEdit();

    // ---- text ----
    /// Replace all text. Clears the undo history and puts the caret at the start. Markers and breakpoints are kept (lines past the end are dropped).
    void SetText(const std::string& text);
    std::string GetText() const;
    int LineCount() const { return (int)lines_.size(); }
    int LineLength(int i) const { return (int)Line(i).size(); }   // bytes
    /// Line `i` without its newline ("" when out of range).
    const std::string& Line(int i) const;
    /// Counts every change to the text (edits, undo, redo, SetText).
    unsigned Version() const { return version_; }
    bool IsEmpty() const { return lines_.size() == 1 && lines_[0].empty(); }

    // ---- caret and selection ----
    CodePos Caret() const { return caret_; }
    CodePos Anchor() const { return anchor_; }
    bool HasSelection() const { return caret_ != anchor_; }
    CodePos SelectionStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
    CodePos SelectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
    std::string SelectedText() const;
    /// Move the caret (and the anchor too unless `extend`). The position is clamped into the text and onto a character boundary.
    void SetCaret(CodePos p, bool extend = false);
    void SetSelection(CodePos anchor, CodePos caret);
    void SelectAll();
    void SelectLine(int line);              // the whole line including its newline
    void SelectWordAt(CodePos p);
    CodePos Clamp(CodePos p) const;
    void Move(CodeMove m, bool extend = false);

    // ---- editing (all of them do nothing in read-only mode) ----
    /// Replace the selection (or insert at the caret) with `text`. "\r\n" becomes "\n". Returns false if nothing changed.
    bool InsertText(const char* text);
    /// Typed text: like InsertText, except in overwrite mode (Insert key) with no selection it replaces as many characters
    /// after the caret as it inserts (never past the end of the line).
    bool TypeText(const char* text);
    bool Backspace(bool word = false);
    bool DeleteForward(bool word = false);
    /// Enter: split the line, keeping its indent (and one more level after '{', '(' or '[' ; "{|}" opens a blank line between).
    bool NewLine();
    /// Tab: indent (insert spaces to the next tab stop, or the lines of a multi-line selection); `outdent` (Shift+Tab) removes one level.
    bool Tab(bool outdent = false);
    bool Cut();          // copies to the clipboard, then deletes
    bool Copy() const;   // the selection to OkayUI::SetClipboardText
    bool Paste();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    /// Close the current undo group: the next edit starts a new one.
    void BreakUndoGroup() { breakGroup_ = true; }

    // ---- find ----
    /// Set the search text. Every match in view is highlighted. "" clears it. A needle with a newline matches nothing.
    void SetFind(const std::string& needle, bool matchCase = false);
    const std::string& FindText() const { return find_; }
    /// Select the next / previous match after / before the selection, wrapping round the ends. False if there is none.
    bool FindNext();
    bool FindPrev();
    /// How many matches there are in the whole text.
    int FindCount() const;
    /// Replace the selection if it is a match, then select the next match. ReplaceAll is one undo step and returns the count.
    bool ReplaceCurrent(const std::string& with);
    int ReplaceAll(const std::string& with);

    // ---- brackets ----
    /// When the caret is next to a bracket ( ) [ ] { } that is code (not in a string or comment) with a partner, give both
    /// positions (`a` the bracket by the caret). Looks at most 2000 lines away.
    bool MatchBracket(CodePos& a, CodePos& b);

    // ---- options ----
    void SetReadOnly(bool ro) { readOnly_ = ro; }
    /// Overwrite mode: typed characters replace the ones after the caret; the caret is drawn as a block. The Insert key toggles it.
    void SetOverwrite(bool on) { overwrite_ = on; }
    bool Overwrite() const { return overwrite_; }
    /// Help shown as a tooltip while the pointer is over a word that has an entry (built-in signatures, type notes). Empty: none.
    /// An error marker's message on the same line wins.
    void SetWordInfo(std::unordered_map<std::string, std::string> info) { wordInfo_ = std::move(info); }
    const std::string* WordInfo(const std::string& word) const { auto it = wordInfo_.find(word); return it == wordInfo_.end() ? nullptr : &it->second; }
    /// The word under a screen point (for the last Draw), or "" when the point is not over a word's characters.
    std::string WordAtScreen(float x, float y) const;
    /// false: Draw does not act on keys (the host's autocomplete popup owns them); the mouse still works.
    void SetHandleKeyboardInput(bool b) { handleKeys_ = b; }
    bool ReadOnly() const { return readOnly_; }
    void SetTabSize(int n) { tabSize_ = n < 1 ? 1 : (n > 16 ? 16 : n); }
    int TabSize() const { return tabSize_; }
    void SetInsertSpaces(bool b) { insertSpaces_ = b; }     // Tab inserts spaces (default) or a tab character
    void SetAutoIndent(bool b) { autoIndent_ = b; }
    /// Enter after '(' '[' '{' adds one level of indent, and between a bracket pair opens a blank line (default true). false: Enter
    /// only copies the line's indent, as ImGuiColorTextEdit does (the script editor adds its own `{|}` handling on top).
    void SetBracketIndent(bool b) { bracketIndent_ = b; }
    /// Pixels one mouse wheel step scrolls, down and sideways (Shift+wheel). 0 (the default): three rows / three characters.
    void SetWheelStep(float y, float x) { wheelY_ = (y > 0.0f && y < 1.0e5f) ? y : 0.0f; wheelX_ = (x > 0.0f && x < 1.0e5f) ? x : 0.0f; }
    /// The next Draw ignores the typed text it is given (the host used that character for something else: stepping over a
    /// closing bracket, a shortcut such as Alt+1). Keys are still handled.
    void DropTypedText() { dropText_ = true; }
    /// true: Draw does not show OkayUI's tooltip; the host reads Hover() and draws it (in its own tooltip layer).
    void SetHostDrawsTooltip(bool b) { hostTip_ = b; }
    const CodeHover& Hover() const { return hover_; }
    void SetShowLineNumbers(bool b) { showLineNumbers_ = b; }
    void SetShowBreakpointGutter(bool b) { showBreakpoints_ = b; }
    void SetLineSpacing(float s) { lineSpacing_ = s < 1.0f ? 1.0f : (s > 3.0f ? 3.0f : s); }
    /// The space left of the line numbers, in pixels (the breakpoint column: a click there toggles a breakpoint). < 0: as
    /// tall as a line when breakpoints are shown (the default), else none.
    void SetLeftMargin(float px) { leftMargin_ = (px == px && px < 1.0e4f) ? px : -1.0f; }
    /// Space between the edge of the editor and its lines (the gutter included), like a child window's padding.
    void SetPadding(float x, float y) { padX_ = (x >= 0.0f && x < 1.0e4f) ? x : 0.0f; padY_ = (y >= 0.0f && y < 1.0e4f) ? y : 0.0f; }
    /// Scroll bar thickness in pixels (default 10), and whether the horizontal bar is always shown (as an ImGui child window
    /// with ImGuiWindowFlags_AlwaysHorizontalScrollbar).
    void SetScrollbarSize(float px) { scrollbar_ = (px >= 4.0f && px <= 64.0f) ? px : 10.0f; }
    void SetAlwaysShowHorizontalScrollbar(bool b) { alwaysH_ = b; }
    /// Draw with the code font in the launcher look (default true; OkayUI::PushCodeFont). false: the UI font, as before.
    void SetUseCodeFont(bool b) { useCodeFont_ = b; }
    /// Write a marker's message after the end of its line (default true).
    void SetShowInlineMessages(bool b) { inlineMessages_ = b; }
    void SetPalette(const CodePalette& p) { pal_ = p; }
    const CodePalette& Palette() const { return pal_; }
    /// The syntax highlighter (empty: no colouring). Takes effect at once.
    void SetHighlighter(CodeHighlighter h);
    void SetLanguage(const CodeLanguage& lang);
    /// Extra type names (classes and enums the script declares) coloured as types, on top of the language's own. Needs SetLanguage first.
    void SetUserTypes(const std::vector<std::string>& names);
    /// Colour tokens of line `i` (what the highlighter says), for tests and for tooltips.
    void LineTokens(int i, std::vector<CodeToken>& out);

    // ---- markers and breakpoints ----
    void SetMarkers(const std::vector<CodeMarker>& m) { markers_ = m; }
    const std::vector<CodeMarker>& Markers() const { return markers_; }
    void ClearMarkers() { markers_.clear(); }
    /// Breakpoint lines (kept sorted). They move with the text when lines are inserted or removed above them.
    void SetBreakpoints(const std::vector<int>& lines);
    const std::vector<int>& Breakpoints() const { return breakpoints_; }
    bool HasBreakpoint(int line) const;
    void ToggleBreakpoint(int line);
    /// Called when the user clicks the gutter mark column: (line, whether the line now has a breakpoint). The click toggles the
    /// breakpoint first unless `autoToggle` is false (then the callback decides).
    void SetBreakpointCallback(std::function<void(int line, bool set)> cb, bool autoToggle = true) { onBreakpoint_ = std::move(cb); autoToggle_ = autoToggle; }

    // ---- view ----
    /// Draw the editor `w` x `h` pixels (0: fill what is left of the window), handle this frame's input. Returns true if the text
    /// changed this frame. Needs a window (between Begin and End).
    bool Draw(const char* id, float w = 0.0f, float h = 0.0f);
    /// Take or give up the keyboard (the widget also takes it when clicked).
    void Focus();
    bool Focused() const;
    float ScrollX() const { return scrollX_; }
    float ScrollY() const { return scrollY_; }
    void SetScroll(float x, float y) { scrollX_ = x; scrollY_ = y; reveal_ = false; clampScroll(); }
    /// Scroll so the line is in the middle of the view (or just visible) and put the caret at its start.
    void GoToLine(int line);
    void ScrollToCaret() { reveal_ = true; }
    // ---- input method (IME): see Input::composition / Input::commit ----
    /// While the input method composes (Input::composition, only when this editor has the keyboard and is not read-only) the
    /// preedit is drawn underlined at the caret (in place of a selection), the keys are left to the input method, and
    /// Input::commit is inserted like typed text. Cancelling (the host stops sending a preedit) changes nothing.
    bool Composing() const { return comp_.active; }
    /// The caret on screen (pixels) for the last Draw, kept inside the text area, or a zero rectangle when the editor is not focused:
    /// where a host puts the system candidate window. While composing it is the caret inside the preedit.
    Rect ImeCaretRect() const { return imeCaret_; }
    /// The preedit's span on screen (zero width when not composing).
    Rect ImeCompositionRect() const { return imeComp_; }
    int FirstVisibleLine() const { return firstLine_; }
    int VisibleLineCount() const { return visLines_; }
    // Geometry of the last Draw (screen pixels), so callers and tests can find things on screen.
    /// The row height: the font's line height times the line spacing (rounded to whole pixels for the 8x8 font, not for the
    /// code font, as ImGuiColorTextEdit does).
    float LineHeight() const { return lineH_; }
    float CharWidth() const { return charW_; }
    /// False when the current font gives characters different widths (each is then drawn on its own, centred in its cell).
    bool MonospaceFont() const { return mono_; }
    float GutterWidth() const { return gutterW_; }   // the padding included
    float PaddingX() const { return padX_; }
    float PaddingY() const { return padY_; }
    Rect ViewRect() const { return view_; }         // the text area: the item without the gutter and the scroll bars
    Rect ItemRect() const { return item_; }
    /// Top-left of the cell at a position, and the position under a screen point (both for the last Draw).
    Vec2 PosToScreen(CodePos p) const;
    /// Where a popup under the caret goes: the caret cell's left edge, one line down.
    Vec2 CaretScreenPosBelow() const { Vec2 v = PosToScreen(caret_); v.y += lineH_; return v; }
    CodePos ScreenToPos(float x, float y) const;
    /// Display column of a byte offset, and back (tabs expanded).
    int ByteToColumn(int line, int byte) const;
    int ColumnToByte(int line, float column) const;

private:
    struct UndoEdit { CodePos from; std::string removed, inserted; };
    struct UndoGroup { std::vector<UndoEdit> edits; CodePos caretBefore, anchorBefore, caretAfter, anchorAfter; int kind = 0; };

    void applyRaw(CodePos from, CodePos to, const std::string& ins);
    CodePos edit(int kind, CodePos from, CodePos to, const std::string& ins);
    void beginCompound();
    void endCompound();
    bool replaceSelection(const std::string& ins, int kind);
    bool deleteSelectionIfAny();
    std::string textIn(CodePos a, CodePos b) const;
    static CodePos endOf(CodePos from, const std::string& text);
    void setCaretKeep(CodePos c, CodePos a) { caret_ = c; anchor_ = a; reveal_ = true; }
    void moveCaret(CodePos p, bool extend, bool keepDesired = false);
    CodePos wordLeft(CodePos p) const;
    CodePos wordRight(CodePos p) const;
    void ensureStates(int line);
    void invalidateFrom(int line);
    void shiftLines(int firstChanged, int removedTo, int delta);
    bool findIn(int line, int from, int& start) const;
    bool findBefore(int line, int limit, int& start) const;
    void clampScroll();
    void layout(const Rect& item);
    bool wordAt(CodePos p, CodePos& a, CodePos& b) const;
    void recomputeMax();
    void handleKeys(bool& changed);
    void handleMouse(const Rect& item, int wid, bool hovered);
    void drawAll(const Rect& item, bool hovered);
    bool drawFramed(const char* id, float w, float h, unsigned v0);
    float marginW() const { return leftMargin_ >= 0.0f ? leftMargin_ : (showBreakpoints_ ? std::floor(lineH_ + 0.5f) : 0.0f); }
    bool matchBracketImpl(CodePos& a, CodePos& b);

    std::vector<std::string> lines_;
    CodePos caret_, anchor_;
    int desiredCol_ = -1;
    unsigned version_ = 1;

    std::vector<UndoGroup> undo_, redo_;
    bool groupOpen_ = false, breakGroup_ = true, compound_ = false;
    int curKind_ = 0;

    CodeHighlighter highlighter_;
    std::vector<int> stateIn_;         // stateIn_[i]: highlighter state at the start of line i, valid for i <= validTo_
    int validTo_ = 0;
    std::vector<CodeToken> scratch_;

    std::string find_; bool findCase_ = false;
    std::vector<CodeMarker> markers_;
    std::vector<int> breakpoints_;
    std::function<void(int, bool)> onBreakpoint_; bool autoToggle_ = true;

    bool overwrite_ = false;
    std::unordered_map<std::string, std::string> wordInfo_;
    bool mono_ = true; float glyphW_[128] = {}; float glyphKey_ = -1.0f;   // ASCII advances, cached while the font and scale stay the same
    bool readOnly_ = false, insertSpaces_ = true, autoIndent_ = true, showLineNumbers_ = true, showBreakpoints_ = true;
    int tabSize_ = 4;
    float lineSpacing_ = 1.2f;
    float leftMargin_ = -1.0f, padX_ = 0.0f, padY_ = 0.0f, scrollbar_ = 10.0f;
    bool alwaysH_ = false, useCodeFont_ = true, inlineMessages_ = true, codeFont_ = false;
    std::string tip_;   // the tooltip of this frame (shown after the code font is popped)
    CodeHover hover_;
    bool bracketIndent_ = true, dropText_ = false, hostTip_ = false;
    float wheelY_ = 0.0f, wheelX_ = 0.0f;
    CodePos clickA_, clickB_;   // the word or line a double or triple click selected: a drag extends by words or lines from it
    CodePalette pal_;

    // view
    float scrollX_ = 0, scrollY_ = 0;
    bool reveal_ = false;
    int firstLine_ = 0, visLines_ = 20;
    float lineH_ = 16, charW_ = 8, gutterW_ = 0;
    Rect item_, view_, vTrack_, hTrack_, vThumb_, hThumb_;
    bool needV_ = false, needH_ = false;
    float contentH_ = 0, contentW_ = 0, textLeft_ = 0;
    int maxBytes_ = 0; bool maxDirty_ = false;
    // mouse
    bool dragging_ = false, dragVBar_ = false, dragHBar_ = false;
    float dragOff_ = 0;
    double lastClickT_ = -10; float lastClickX_ = 0, lastClickY_ = 0; int clicks_ = 0;
    double lastActivity_ = 0;
    int wid_ = 0; bool wantFocus_ = false, handleKeys_ = true;
    Composition comp_; int compCells_ = 0, compCaretCells_ = 0; Rect imeCaret_, imeComp_;
    CodeLanguage language_; bool haveLanguage_ = false;
    // bracket cache
    unsigned bVersion_ = 0; CodePos bCaret_{-1, -1}; bool bOk_ = false; CodePos bA_, bB_;
};

} // namespace OkayUI
