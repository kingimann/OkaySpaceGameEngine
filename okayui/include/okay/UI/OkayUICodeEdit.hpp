#pragma once
// OkayUI::CodeEdit: a multi-line code editor widget (own code; replaces the vendored ImGuiColorTextEdit).
// Line-number gutter with breakpoint marks, caret and selection (mouse and keyboard), typing, auto-indent, clipboard,
// undo/redo with grouping, find (also across lines) and replace, bracket matching, auto-closed brackets and quotes, auto-dedent,
// matching-word highlight, indent guides, whitespace marks, word wrap, code folding, multiple carets and column selection, drag
// and drop of text, scrolling, a pluggable syntax highlighter (plus a built-in C-like and OkayScript one), read-only mode and
// error markers. See docs/ui/own_ui_code_editor.md.
//
// A CodeEdit is a plain object that owns its text and state (one per open script). Call Draw() once per frame inside an
// OkayUI window; it reads the frame's input, edits, and draws the visible lines only. The editing operations are also
// public methods, so they can be driven (and tested) without any input.
//
// Columns are byte offsets into a line and always sit on UTF-8 character boundaries. The text is kept valid UTF-8
// (invalid bytes become '?', "\r\n" and "\r" become "\n", other control characters are dropped). One character is one
// cell wide; a tab advances to the next multiple of the tab size. The font should be monospaced.
#include "okay/UI/OkayUI.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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

/// One caret and its selection (anchor == caret: no selection). The editor has a primary caret (Caret(), Anchor()) and any number
/// of extra ones; every edit is done at all of them and is one undo step.
struct CodeSelection {
    CodePos caret, anchor;
    CodePos Start() const { return caret < anchor ? caret : anchor; }
    CodePos End() const { return caret < anchor ? anchor : caret; }
    bool Empty() const { return caret == anchor; }
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
    /// The order a plain word is coloured in. false (the default): keyword, type, known word, then a call `name(` or a member
    /// `.name`. true: keyword, type, call, member, then known word (the Dear ImGui script editor's order: a built-in that is
    /// called shows in the call colour, `print(` like `Foo(`).
    bool callBeforeKnown = false;
    /// Words that open and close a block (Lua: function do then repeat else / end until elseif else). A word in both lists closes
    /// one block and opens the next. They drive folding, the extra indent after Enter and auto-dedent. Braces { } always count.
    std::vector<std::string> blockOpenWords, blockCloseWords;
    /// Typed as the only word of a line, these take one indent level off (Lua: end else elseif until). A typed '}' always does.
    std::vector<std::string> dedentWords;
    /// Also fold by indentation: a line followed by more deeply indented lines folds them (used where a line opens no block).
    bool foldByIndent = false;
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
    /// The caret's line: `currentLine` fills it while the editor has the keyboard, `currentLineInactive` while it does not (0: no
    /// box then), `currentLineEdge` outlines it (0: no outline). The box runs across the gutter and the text, as long as there is
    /// no selection.
    Color32 currentLineInactive = 0;
    /// A wash over the whole width of a line with a breakpoint, on top of the gutter dot (0: none).
    Color32 breakpointLine = 0;
    /// The other places of the word under the caret (0: not shown), the indent guides (0: none), the marks of SetShowWhitespace
    /// (0: the comment colour at half strength), the fold arrows and the box of a folded block (0: the line number colour).
    Color32 wordMatch = 0, indentGuide = 0, whitespace = 0, fold = 0;
};
/// The Darcula-flavoured palette the script editor uses.
CodePalette DefaultCodePalette();

/// An error (or warning) the caller wants shown: a mark in the gutter, a wash over the line and a wavy underline under the
/// columns [colStart, colEnd) (colEnd < 0: from colStart or the first non-blank to the last non-blank). `message` shows as a tooltip.
struct CodeMarker {
    int line = 0, colStart = 0, colEnd = -1;
    std::string message;
    Color32 color = 0;       // 0: the palette's errorUnderline
    Color32 lineColor = 0;   // the wash over the line; 0: the palette's errorLine (a warning uses its own, amber)
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
    /// Text typed at the keyboard (what Draw does with typed characters): like InsertText, plus, character by character, the
    /// auto-closing of brackets and quotes (SetAutoClose) and the auto-dedent of '}' and the language's dedent words (SetAutoDedent).
    bool Type(const char* text);
    bool Backspace(bool word = false);
    bool DeleteForward(bool word = false);
    /// Enter: split the line, keeping its indent (and one more level after '{', '(' or '[' ; "{|}" opens a blank line between).
    bool NewLine();
    /// Tab: indent (insert spaces to the next tab stop, or the lines of a multi-line selection); `outdent` (Shift+Tab) removes one level.
    bool Tab(bool outdent = false);
    /// Line commands (each one undo step; the lines are the selection's, a selection that ends at column 0 leaves that line out).
    /// ToggleLineComment: put `prefix` and a space before the first non-blank of every non-blank line, or take it away when every
    /// such line has it ("" prefix: the language's first line comment, else "//"). DuplicateLines copies the lines below themselves
    /// and moves the selection onto the copy. DeleteLines removes them with their newline. MoveLines swaps them with the line
    /// above (up) or below; false at the document ends. Breakpoints follow the text.
    bool ToggleLineComment(const std::string& prefix = "");
    bool DuplicateLines();
    bool DeleteLines();
    bool MoveLines(bool up);
    /// Where Copy, Cut and Paste put and take the text. By default OkayUI's clipboard (OkayUI::SetClipboardText, which holds
    /// kClipboardMax - 1 bytes: Copy of more is cut short and Cut of more does nothing). A host with a clipboard of its own (the
    /// script editor: Dear ImGui's, the system clipboard with no size limit) passes both functions; empty functions go back to OkayUI's.
    void SetClipboardFunctions(std::function<void(const std::string&)> set, std::function<std::string()> get) { clipSet_ = std::move(set); clipGet_ = std::move(get); }
    bool Cut();          // copies to the clipboard, then deletes
    bool Copy() const;   // the selection to OkayUI::SetClipboardText
    bool Paste();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    /// Close the current undo group: the next edit starts a new one.
    void BreakUndoGroup() { breakGroup_ = true; }

    // ---- many carets ----
    /// How many carets there are (1 + the extra ones).
    int CaretCount() const { return 1 + (int)extras_.size(); }
    /// Every caret with its selection, in text order (the primary one among them).
    std::vector<CodeSelection> Selections() const;
    /// Add a caret (with a selection from `anchor`); it becomes the primary one. A caret on top of another merges with it.
    void AddCaret(CodePos caret, CodePos anchor);
    void AddCaret(CodePos caret) { AddCaret(caret, caret); }
    /// Remove the caret at `p` (Ctrl+click on a caret); false when there is no caret there or it is the only one.
    bool RemoveCaretAt(CodePos p);
    /// Back to the primary caret alone.
    void ClearExtraCarets();
    /// Ctrl+D: with no selection select the word at the caret; with one, add a caret on the next place with the same text (whole
    /// words when the first selection came from a word), wrapping round the end. False when there is nothing more to add.
    bool SelectNextOccurrence();
    /// A column (block) selection from one point to another: one caret per line, each selecting the same display columns
    /// (lines too short get a caret at their end). The caret of `to`'s line is the primary one.
    void SetColumnSelection(CodePos from, CodePos to);
    /// Move (or with `copy` copy) the primary selection to `at`, as one undo step: what dropping dragged text does. False when
    /// `at` is inside the selection (nothing to do) or there is no selection.
    bool MoveSelectionTo(CodePos at, bool copy);

    // ---- folding ----
    /// Show the fold column in the gutter and allow folding (off by default; the script editor turns it on).
    void SetFolding(bool on);
    bool Folding() const { return folding_; }
    /// The line where the block that line `header` opens ends (its closing brace's or `end`'s line, or its last indented line),
    /// or -1 when the line opens no block that spans lines.
    int FoldEnd(int header);
    /// Fold the block line `line` opens (or the innermost block around it). False when there is none.
    bool Fold(int line);
    /// Unfold a folded block whose header is `line` or that hides `line`. False when nothing was folded there.
    bool Unfold(int line);
    bool ToggleFold(int line);
    int FoldAll();       // fold every block; returns how many
    void UnfoldAll();
    bool IsFolded(int header) const;
    /// True when the line is hidden inside a folded block.
    bool IsLineHidden(int line) const;
    /// The number of rows drawn: lines that are not folded away, each as many rows as word wrap makes of it.
    int VisualRowCount() const;

    // ---- find ----
    /// Set the search text. Every match in view is highlighted. "" clears it. A needle with newlines matches across lines (its
    /// first part must end its line and its last part start one).
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
    /// false: Draw does not act on keys (the host's autocomplete popup owns them); the mouse still works.
    void SetHandleKeyboardInput(bool b) { handleKeys_ = b; }
    bool ReadOnly() const { return readOnly_; }
    void SetTabSize(int n) { tabSize_ = n < 1 ? 1 : (n > 16 ? 16 : n); }
    int TabSize() const { return tabSize_; }
    void SetInsertSpaces(bool b) { insertSpaces_ = b; }     // Tab inserts spaces (default) or a tab character
    void SetAutoIndent(bool b) { autoIndent_ = b; }
    /// Typing ( [ { " ' adds the closer after the caret, typing a closer that is already next to the caret steps over it,
    /// Backspace between a fresh pair removes both, and typing an opener or quote over a selection wraps it. Not inside strings
    /// and comments (quotes: not after a letter). On by default.
    void SetAutoClose(bool b) { autoClose_ = b; }
    bool AutoClose() const { return autoClose_; }
    /// Typing '}' (or a dedent word of the language, such as Lua's `end`) as the first thing on a line takes the line back to
    /// the indent of its block. On by default.
    void SetAutoDedent(bool b) { autoDedent_ = b; }
    /// Draw a dot for every space and an arrow for every tab (off by default).
    void SetShowWhitespace(bool b) { showWs_ = b; }
    bool ShowWhitespace() const { return showWs_; }
    /// Wrap long lines at the width of the view instead of scrolling sideways (off by default). Caret movement (Up and Down go by
    /// rows), the selection, clicks and the gutter (one line number per line) follow the rows.
    void SetWordWrap(bool b) { if (wrap_ != b) { wrap_ = b; rowsDirty_ = true; scrollX_ = 0; } }
    bool WordWrap() const { return wrap_; }
    void SetShowLineNumbers(bool b) { showLineNumbers_ = b; }
    void SetShowBreakpointGutter(bool b) { showBreakpoints_ = b; }
    void SetLineSpacing(float s) { lineSpacing_ = s < 1.0f ? 1.0f : (s > 3.0f ? 3.0f : s); }
    float LineSpacing() const { return lineSpacing_; }
    /// Empty space between the gutter and the first column of text, in pixels (4 by default, 0 to 200). A click in it is column 0.
    void SetTextMargin(float px) { textMargin_ = px > 0.0f ? (px < 200.0f ? px : 200.0f) : 0.0f; }
    float TextMargin() const { return textMargin_; }
    /// What a word in the text says when the pointer rests on it (a built-in's signature and doc): called with the word under
    /// the pointer, returns the tooltip text ("" for none). Empty function: no word tooltips.
    void SetHoverInfo(std::function<std::string(const std::string& word)> f) { hoverInfo_ = std::move(f); }
    /// The tooltip for a screen point after the last Draw: the message of an error on that line (anywhere across the row), else
    /// the hover info of the word (letters, digits, '_') whose cell is under the point. "" for none.
    std::string HoverText(float x, float y) const;
    /// Key bindings for the line commands while the editor has the keyboard (on by default; the script editor's keys):
    /// Ctrl+/ ToggleLineComment, Ctrl+Shift+D DuplicateLines, Ctrl+Shift+K DeleteLines, Alt+Up and Alt+Down MoveLines.
    /// (Ctrl+D is SelectNextOccurrence; folding: Ctrl+Shift+[ and ], Ctrl+Alt+[ fold all, Ctrl+Alt+] unfold all.)
    void SetLineCommandKeys(bool on) { lineKeys_ = on; }
    bool LineCommandKeys() const { return lineKeys_; }
    /// Draw the code (text and line numbers) in this font instead of the current OkayUI font: a font made by AddFontTtf
    /// (OkayUICompat.hpp) that should be monospaced. `px` is the height of a line of text in pixels (0: the font's own size).
    /// The window's font and text scale are put back after Draw. Null: the current OkayUI font (the default).
    void SetFont(const Font* font, float px = 0.0f) { font_ = font; fontPx_ = (px > 0.0f && px < 1000.0f) ? px : 0.0f; }
    const Font* CodeFont() const { return font_; }
    void SetPalette(const CodePalette& p) { pal_ = p; }
    /// Empty space inside the editor around the gutter and the lines (left and right, top and bottom), in pixels; the scroll bars stay
    /// at the edges. Like the padding inside a framed Dear ImGui child window. 0, 0 by default.
    void SetContentPadding(float x, float y) { padX_ = (x >= 0.0f && x < 1000.0f) ? x : 0.0f; padY_ = (y >= 0.0f && y < 1000.0f) ? y : 0.0f; }
    /// Scroll bar thickness and thumb corner radius in pixels (10 and 3 by default), and whether the horizontal bar is always
    /// shown (the Dear ImGui script editor always shows it) or only when a line is wider than the view (the default).
    void SetScrollbarStyle(float size, float rounding) { sbSize_ = (size >= 2.0f && size <= 64.0f) ? size : 10.0f; sbRound_ = (rounding >= 0.0f && rounding <= 32.0f) ? rounding : 3.0f; }
    void SetAlwaysShowHScrollbar(bool b) { alwaysH_ = b; }
    float ScrollbarSize() const { return sbSize_; }
    bool HScrollbarShown() const { return needH_; }
    bool VScrollbarShown() const { return needV_; }
    Rect HScrollbarRect() const { return hTrack_; }
    Rect VScrollbarRect() const { return vTrack_; }
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
    float LineHeight() const { return lineH_; }
    float CharWidth() const { return charW_; }
    float GutterWidth() const { return gutterW_; }
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
    struct Sel { CodePos caret, anchor; int desired = -1; };
    struct UndoGroup { std::vector<UndoEdit> edits; CodePos caretBefore, anchorBefore, caretAfter, anchorAfter; int kind = 0;
                       std::vector<Sel> extrasBefore, extrasAfter; };
    struct FoldRange { int header = 0, last = 0; };   // lines header+1 .. last are hidden

    void applyRaw(CodePos from, CodePos to, const std::string& ins);
    CodePos edit(int kind, CodePos from, CodePos to, const std::string& ins);
    void beginCompound(int kind = 0);
    void endCompound();
    template <class F> bool forEachCaret(int kind, int order, F&& op);
    void normalizeSels();
    bool typeOne(const std::string& ch);
    bool backspaceOne(bool word);
    bool selectionIsWord() const;
    // rows: the lines that are drawn (folds hide some, word wrap splits some)
    void ensureRows() const;
    int rowCount() const;
    int rowLine(int r) const;
    int rowStart(int r) const;
    int rowEnd(int r) const;
    int rowCol(int r) const;
    int rowOf(CodePos p) const;
    CodePos posInRow(int r, float visualCol) const;
    bool lineOpens(int line, int& openCol, int limit = -1, bool wordsOnly = false);
    int hiddenLast(int header);
    bool inCodeText(CodePos p);
    int blockEnd(int line, int openCol);
    void unfoldAround(int line);
    void fixFolds();
    int indentOf(int line) const;
    bool matchAt(int line, int start, CodePos& end) const;
    bool nextMatch(CodePos from, CodePos& a, CodePos& b) const;
    void setFound(int line, int st);
    void lineRange(int& l0, int& l1) const;
    bool replaceSelection(const std::string& ins, int kind);
    bool deleteSelectionIfAny();
    std::string textIn(CodePos a, CodePos b) const;
    static CodePos endOf(CodePos from, const std::string& text);
    void setCaretKeep(CodePos c, CodePos a) { caret_ = c; anchor_ = a; reveal_ = true; }
    void moveCaret(CodePos p, bool extend, bool keepDesired = false);
    void moveOne(CodeMove m, bool extend);
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

    std::string find_; bool findCase_ = false; std::vector<std::string> findParts_;
    std::vector<CodeMarker> markers_;
    std::vector<int> breakpoints_;
    std::function<void(int, bool)> onBreakpoint_; bool autoToggle_ = true;

    bool readOnly_ = false, insertSpaces_ = true, autoIndent_ = true, showLineNumbers_ = true, showBreakpoints_ = true, lineKeys_ = true;
    bool autoClose_ = true, autoDedent_ = true, showWs_ = false, wrap_ = false, folding_ = false;
    // many carets: the extra ones (the primary is caret_ / anchor_), and the positions an edit at many carets keeps moving
    std::vector<Sel> extras_, track_;
    bool tracking_ = false; int opRank_ = 0;
    int compoundDepth_ = 0; bool pushedGroup_ = false;
    // the last auto-dedent (line, the indent taken off), put back when the word typed turns out longer ("end" -> "ending")
    int dedentLine_ = -1; std::string dedentText_; unsigned dedentVersion_ = 0;
    // folds and the row map (rebuilt when the text, the folds or the wrap width change)
    std::vector<FoldRange> folds_; unsigned foldsVersion_ = 1, foldsCheckedAt_ = 0;
    mutable bool rowsDirty_ = true, identity_ = true;
    mutable unsigned rowsVersion_ = 0, rowsFolds_ = 0; mutable int rowsWrap_ = -1, rowsTab_ = 0;
    mutable std::vector<int> rowLine_, rowByte_, rowCol_, lineRow_;
    mutable std::vector<unsigned char> hidden_;
    int wrapCols_ = 0, firstRow_ = 0; float foldW_ = 0.0f;
    const Font* font_ = nullptr; float fontPx_ = 0.0f;
    std::function<void(const std::string&)> clipSet_; std::function<std::string()> clipGet_;
    bool drawImpl(const char* id, float w, float h);
    int tabSize_ = 4;
    float lineSpacing_ = 1.2f;
    float padX_ = 0.0f, padY_ = 0.0f, sbSize_ = 10.0f, sbRound_ = 3.0f, textMargin_ = 4.0f;
    std::function<std::string(const std::string&)> hoverInfo_;
    bool alwaysH_ = false;
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
    bool boxSel_ = false, dropPending_ = false, dropping_ = false; CodePos dropAt_, pressPos_; int boxLine_ = 0; float boxCol_ = 0, pressX_ = 0, pressY_ = 0;
    void setBox(int l0, float c0, int l1, float c1);
    float dragOff_ = 0;
    double lastClickT_ = -10; float lastClickX_ = 0, lastClickY_ = 0; int clicks_ = 0;
    double lastActivity_ = 0;
    int wid_ = 0; bool wantFocus_ = false, handleKeys_ = true;
    Composition comp_; int compCells_ = 0, compCaretCells_ = 0; Rect imeCaret_, imeComp_;
    CodeLanguage language_; bool haveLanguage_ = false;
    // bracket cache
    unsigned bVersion_ = 0; CodePos bCaret_{-1, -1}; bool bOk_ = false; CodePos bA_, bB_;
};

/// The palette of a plain text box (InputTextMultiline, the Crash Log's reports): the current theme's text on its field colour, no
/// gutter colours, no current-line wash. Read the theme at the time of the call.
CodePalette TextBoxPalette();
/// Make `ed` a plain text box, as InputTextMultiline's: no line numbers, no breakpoint gutter, no highlighting or auto-indent, tabs kept.
void MakeTextBox(CodeEdit& ed);

} // namespace OkayUI
