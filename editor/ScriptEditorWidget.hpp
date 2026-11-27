#pragma once
// ScriptEditorWidget: the script editor's code widget. It is our own OkayUI::CodeEdit behind the adapter
// (editor/ScriptEditorCodeEdit.hpp), drawn inside the Dear ImGui script editor window: its shapes go into the window's draw list,
// so Dear ImGui's popups, tooltips and modals are drawn over it. The vendored ImGuiColorTextEdit it replaced is deleted.
//
// It speaks the vocabulary the script editor already used (Coordinates with display columns, 1-based error markers and
// breakpoints, GetText ending in a newline), so editor/main.cpp calls it as before.
#include "imgui.h"                // first: OkayUI.hpp keeps Dear ImGui's texture id type when imgui.h came before it
#include "ScriptEditorCodeEdit.hpp"
#include "MainShared.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "okay/UI/OkayUI.hpp"     // the editor is built on OkayUI: the code is drawn straight into the window

/// True while an OkayUI script editor has the keyboard (set by ScriptEditorWidget::Render). editor/main.cpp turns Dear ImGui's
/// keyboard navigation off for the next frame while it is true: otherwise Space and Enter typed into the code also press
/// whichever ImGui button has the navigation focus (the "Scripts" button opened the script list while typing a space).
inline bool g_scriptCodeOwnsKeys = false;

class ScriptEditorWidget {
public:
    using Coordinates   = scripteditor::Coord;
    using Keywords      = std::unordered_set<std::string>;
    using ErrorMarkers  = std::map<int, std::string>;
    using Breakpoints   = std::unordered_set<int>;

    scripteditor::ScriptEditorCodeEdit& okay() { return okay_; }

    // ---- one-time configuration ----
    void SetLanguage(const scripteditor::LanguageWords& w) { okay_.SetLanguage(w); }
    void SetPalette(const scripteditor::ScriptPalette& p) { okay_.SetPalette(p); }
    void SetTabSize(int n) { okay_.SetTabSize(n); }
    void SetLineSpacing(float s) { okay_.SetLineSpacing(s); }
    void SetLeftMargin(int m) { okay_.SetLeftMargin(m); }

    // ---- text ----
    void SetText(const std::string& t) { okay_.SetText(t); }
    std::string GetText() { return okay_.GetText(); }
    std::vector<std::string> GetTextLines() { return okay_.GetTextLines(); }
    std::string GetCurrentLineText() { return okay_.GetCurrentLineText(); }
    int GetTotalLines() { return okay_.GetTotalLines(); }
    int LineLength(int line) { return okay_.LineLength(line); }
    void InsertTextUndoable(const std::string& t) { okay_.InsertTextUndoable(t); }
    void Delete() { okay_.Delete(); }
    bool IsTextChanged() { return okay_.IsTextChanged(); }

    // ---- caret and selection ----
    Coordinates GetCursorPosition() { return okay_.GetCursorPosition(); }
    void SetCursorPosition(const Coordinates& c) { okay_.SetCursorPosition(c); }
    bool HasSelection() { return okay_.HasSelection(); }
    Coordinates SelStart() { return okay_.SelStart(); }
    Coordinates SelEnd() { return okay_.SelEnd(); }
    void SetSelection(const Coordinates& a, const Coordinates& b) { okay_.SetSelection(a, b); }
    void SelectAll() { okay_.SelectAll(); }
    void SelectWordUnderCursor() { okay_.SelectWordUnderCursor(); }
    std::string GetSelectedText() { return okay_.GetSelectedText(); }

    // ---- per-frame settings ----
    void SetColorizerEnable(bool on) { okay_.SetColorizerEnable(on); }
    void SetUserTypes(const Keywords& t) { okay_.SetUserTypes(t); }
    void SetErrorMarkers(const ErrorMarkers& m) { okay_.SetErrorMarkers(m); }
    void SetBreakpoints(const Breakpoints& b) { okay_.SetBreakpoints(b); }
    void SetHandleKeyboardInputs(bool on) { okay_.SetHandleKeyboardInputs(on); }

    // ---- geometry of the last Render (screen pixels) ----
    float RowHeight() { return okay_.RowHeight(); }
    float CharWidth() { return okay_.CharWidth(); }
    int FirstVisibleLine() { return okay_.FirstVisibleLine(); }
    float TextStartX() { return okay_.TextStartX(); }
    ImVec2 ContentOrigin() { const auto v = okay_.ContentOrigin(); return ImVec2(v.x, v.y); }
    ImVec2 CaretScreenPosBelow() { const auto v = okay_.CaretScreenPosBelow(); return ImVec2(v.x, v.y); }
    Coordinates ScreenToCoords(const ImVec2& p) { return okay_.ScreenToCoords(p.x, p.y); }

    /// True while the code has the keyboard (call after Render).
    bool OkayHasKeyboard() const { return okayFocused_; }

    /// Draw it. `size` as ImGui: <= 0 means "what is left, plus this". `border`: a framed child with padding.
    void Render(const char* id, const ImVec2& size = ImVec2(), bool border = false) {
        // Dear ImGui's names are OkayUI's in the editor, so the code is drawn straight into a child window.
        okay_.UseCodeFont(ImGui::GetFontSize());
        if (ImGui::BeginChild(id, size.x, size.y, border)) {
            const OkayUI::Vec2 a = OkayUI::GetContentRegionAvail();
            okay_.Edit().Draw("##codeedit", a.x > 8.0f ? a.x : 8.0f, a.y > 8.0f ? a.y : 8.0f);
        }
        ImGui::EndChild();
        okayFocused_ = okay_.Edit().Focused();
        g_scriptCodeOwnsKeys = okayFocused_;
    }

private:
    scripteditor::ScriptEditorCodeEdit okay_;
    bool okayFocused_ = false;
    bool clipHooked_ = false;
};
