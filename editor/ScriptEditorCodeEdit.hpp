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

    // ---- geometry of the last Draw (screen pixels) ----
    float RowHeight() const { return ed_.LineHeight(); }
    float CharWidth() const { return ed_.CharWidth(); }
    int   FirstVisibleLine() const { return ed_.FirstVisibleLine(); }
    /// Screen X where the text of a line starts (right of the gutter, scroll applied) and the top-left of line 0's text.
    float TextStartX() const { return ed_.PosToScreen(OkayUI::CodePos{0, 0}).x; }
    OkayUI::Vec2 ContentOrigin() const;
    OkayUI::Vec2 CaretScreenPosBelow() const { return ed_.CaretScreenPosBelow(); }
    Coord ScreenToCoords(float x, float y) const { return ToCoord(ed_.ScreenToPos(x, y)); }

private:
    OkayUI::CodeEdit ed_;
    unsigned seenVersion_ = 0;
    bool colorize_ = true;
};

} // namespace scripteditor
