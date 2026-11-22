#pragma once
// ScriptEditorCodeEdit: the script editor's view of OkayUI::CodeEdit, in the vocabulary the script editor already speaks
// to ImGuiColorTextEdit (editor/vendor/TextEditor.h).
//
//  - Positions are Coord{line, column} where the column is a DISPLAY column (one character is one column, a tab advances
//    to the next multiple of the tab size), like TextEditor::Coordinates. CodeEdit's own positions are byte offsets.
//  - Error markers and breakpoints are keyed by 1-based line number, like TextEditor::ErrorMarkers and Breakpoints.
//  - GetText() ends with a newline, like TextEditor::GetText().
//  - Geometry (RowHeight, CharWidth, TextStartX, ContentOrigin, CaretScreenPosBelow, ScreenToCoords, FirstVisibleLine) is
//    that of the last Draw, in screen pixels.
//
// This file and its .cpp use no Dear ImGui and no SDL: they build and are tested headless (okayui/tests/test_okayui_script_adapter.cpp).
// The widget that picks between this and the ImGui editor is editor/ScriptEditorWidget.hpp.
#include "okay/UI/OkayUICodeEdit.hpp"

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace scripteditor {

struct Coord {
    int mLine = 0, mColumn = 0;
    Coord() = default;
    Coord(int line, int column) : mLine(line < 0 ? 0 : line), mColumn(column < 0 ? 0 : column) {}
    bool operator==(const Coord& o) const { return mLine == o.mLine && mColumn == o.mColumn; }
    bool operator!=(const Coord& o) const { return !(*this == o); }
    bool operator<(const Coord& o) const { return mLine != o.mLine ? mLine < o.mLine : mColumn < o.mColumn; }
};

class ScriptEditorCodeEdit {
public:
    ScriptEditorCodeEdit();

    OkayUI::CodeEdit&       Edit()       { return ed_; }
    const OkayUI::CodeEdit& Edit() const { return ed_; }

    // ---- conversion between CodeEdit's byte positions and display-column coordinates ----
    Coord            ToCoord(OkayUI::CodePos p) const;
    OkayUI::CodePos  ToPos(Coord c) const;          // a column past the end of the line lands at the end of the line

    // ---- text ----
    void SetText(const std::string& text);
    std::string GetText() const;                    // every line followed by '\n' (so the text of "a" is "a\n")
    std::vector<std::string> GetTextLines() const;
    std::string GetCurrentLineText() const;
    int GetTotalLines() const { return ed_.LineCount(); }
    int LineLength(int line) const;                 // in display columns; 0 for a line out of range
    /// Replace the selection (or insert at the caret) as one undoable edit.
    void InsertTextUndoable(const std::string& text);
    /// Delete the selection, or the character after the caret.
    void Delete();
    /// True when the text changed since the last call to this function or to ClearTextChanged.
    bool IsTextChanged();
    void ClearTextChanged() { seenVersion_ = ed_.Version(); }

    // ---- caret and selection ----
    Coord GetCursorPosition() const { return ToCoord(ed_.Caret()); }
    void  SetCursorPosition(Coord c);
    bool  HasSelection() const { return ed_.HasSelection(); }
    Coord SelStart() const { return ToCoord(ed_.SelectionStart()); }
    Coord SelEnd() const { return ToCoord(ed_.SelectionEnd()); }
    void  SetSelection(Coord start, Coord end);     // the caret ends at `end`
    void  SelectAll() { ed_.SelectAll(); }
    void  SelectWordUnderCursor() { ed_.SelectWordAt(ed_.Caret()); }
    std::string GetSelectedText() const { return ed_.SelectedText(); }

    // ---- configuration ----
    /// Colour with the OkayScript language (the script editor's keywords and types); `false` turns colouring off.
    void SetColorizerEnable(bool on);
    void SetUserTypes(const std::unordered_set<std::string>& types);
    /// Error markers by 1-based line, as TextEditor::ErrorMarkers.
    void SetErrorMarkers(const std::map<int, std::string>& markers);
    /// Breakpoint marks by 1-based line, as TextEditor::Breakpoints.
    void SetBreakpoints(const std::unordered_set<int>& lines);
    void SetHandleKeyboardInputs(bool on) { ed_.SetHandleKeyboardInput(on); }
    void SetReadOnly(bool on) { ed_.SetReadOnly(on); }
    void SetTabSize(int n) { ed_.SetTabSize(n); }
    void SetLineSpacing(float s) { ed_.SetLineSpacing(s); }

    /// Draw the code in the embedded JetBrains Mono (shared/JetBrainsMonoFontOwn.h, OFL; the font the Dear ImGui script editor
    /// uses) with a line of text `px` pixels tall. The font is decoded and made once for the whole program. False when it could
    /// not be made (the editor then keeps the UI font).
    bool UseCodeFont(float px);
    /// The JetBrains Mono font, made on first use (null if that failed). Shared by every script editor.
    static const OkayUI::Font* CodeFont();

    // ---- geometry of the last Draw ----
    /// Where OkayUI's pixels sit in the caller's coordinates. The script editor draws the widget in the editor window's own
    /// pixels (OkayUI) but places popups and reads the mouse in Dear ImGui's screen coordinates, which with the multi-window
    /// viewports are desktop positions: the offset is the window's position on the desktop. The geometry calls below are in
    /// the caller's coordinates (the offset added), ScreenToCoords takes them (the offset taken off).
    void SetScreenOffset(float x, float y) { offX_ = x; offY_ = y; }
    float RowHeight() const { return ed_.LineHeight(); }
    float CharWidth() const { return ed_.CharWidth(); }
    int   FirstVisibleLine() const { return ed_.FirstVisibleLine(); }
    /// Screen X where the text of a line starts (right of the gutter, scroll applied) and the top-left of line 0's text.
    float TextStartX() const { return ed_.PosToScreen(OkayUI::CodePos{0, 0}).x + offX_; }
    OkayUI::Vec2 ContentOrigin() const;
    OkayUI::Vec2 CaretScreenPosBelow() const { const OkayUI::Vec2 v = ed_.CaretScreenPosBelow(); return OkayUI::Vec2{v.x + offX_, v.y + offY_}; }
    Coord ScreenToCoords(float x, float y) const { return ToCoord(ed_.ScreenToPos(x - offX_, y - offY_)); }

private:
    OkayUI::CodeEdit ed_;
    unsigned seenVersion_ = 0;
    bool colorize_ = true;
    float offX_ = 0.0f, offY_ = 0.0f;
};

} // namespace scripteditor
