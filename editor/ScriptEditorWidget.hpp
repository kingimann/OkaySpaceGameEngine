#pragma once
// ScriptEditorWidget: the script editor's code widget. It owns BOTH an ImGuiColorTextEdit (the default, the one people use)
// and an OkayUI::CodeEdit behind the adapter (editor/ScriptEditorCodeEdit.hpp), and forwards every call the script editor
// makes to the one that is active. The active one follows the UI backend: OkayUI only when the developer-only switch picked it
// (oked::UseOkayUIBackend(), see docs/ui/own_ui_toolkit.md section 5); otherwise this is the ImGui editor, exactly as before.
// When the backend changes while a script is open, the text, caret and selection move to the newly active editor.
//
// It speaks TextEditor's vocabulary (TextEditor::Coordinates, 1-based error markers and breakpoints, GetText ending in a
// newline), so editor/main.cpp changes only the type name and the Render call site.
#include "TextEditor.h"
#include "ScriptEditorCodeEdit.hpp"
#include "MainShared.hpp"
#include "imgui.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef OKAY_HAVE_OKAYUI
#include "OkayUIHost.hpp"
#endif

/// True while an OkayUI script editor has the keyboard (set by ScriptEditorWidget::Render). editor/main.cpp turns Dear ImGui's
/// keyboard navigation off for the next frame while it is true: otherwise Space and Enter typed into the code also press
/// whichever ImGui button has the navigation focus (the "Scripts" button opened the script list while typing a space).
inline bool g_scriptCodeOwnsKeys = false;

class ScriptEditorWidget {
public:
    using Coordinates   = TextEditor::Coordinates;
    using Keywords      = TextEditor::Keywords;
    using ErrorMarkers  = TextEditor::ErrorMarkers;
    using Breakpoints   = TextEditor::Breakpoints;

    TextEditor& imgui() { return imgui_; }
    scripteditor::ScriptEditorCodeEdit& okay() { return okay_; }

    // ---- one-time configuration (the ImGui editor's look; the OkayUI editor has its own palette) ----
    void SetLanguageDefinition(const TextEditor::LanguageDefinition& d) { imgui_.SetLanguageDefinition(d); }
    void SetPalette(const TextEditor::Palette& p) { imgui_.SetPalette(p); }
    void SetShowWhitespaces(bool b) { imgui_.SetShowWhitespaces(b); }
    void SetTabSize(int n) { imgui_.SetTabSize(n); okay_.SetTabSize(n); }
    void SetLineSpacing(float s) { imgui_.SetLineSpacing(s); okay_.SetLineSpacing(s); }
    void SetLeftMargin(int m) { imgui_.SetLeftMargin(m); }

    // ---- text ----
    void SetText(const std::string& t) { if (Okay()) okay_.SetText(t); else imgui_.SetText(t); }
    std::string GetText() { return Okay() ? okay_.GetText() : imgui_.GetText(); }
    std::vector<std::string> GetTextLines() { return Okay() ? okay_.GetTextLines() : imgui_.GetTextLines(); }
    std::string GetCurrentLineText() { return Okay() ? okay_.GetCurrentLineText() : imgui_.GetCurrentLineText(); }
    int GetTotalLines() { return Okay() ? okay_.GetTotalLines() : imgui_.GetTotalLines(); }
    int LineLength(int line) { return Okay() ? okay_.LineLength(line) : imgui_.LineLength(line); }
    void InsertTextUndoable(const std::string& t) { if (Okay()) okay_.InsertTextUndoable(t); else imgui_.InsertTextUndoable(t); }
    void Delete() { if (Okay()) okay_.Delete(); else imgui_.Delete(); }
    bool IsTextChanged() { return Okay() ? okay_.IsTextChanged() : imgui_.IsTextChanged(); }

    // ---- caret and selection ----
    Coordinates GetCursorPosition() { return Okay() ? FromOk(okay_.GetCursorPosition()) : imgui_.GetCursorPosition(); }
    void SetCursorPosition(const Coordinates& c) { if (Okay()) okay_.SetCursorPosition(ToOk(c)); else imgui_.SetCursorPosition(c); }
    bool HasSelection() { return Okay() ? okay_.HasSelection() : imgui_.HasSelection(); }
    Coordinates SelStart() { return Okay() ? FromOk(okay_.SelStart()) : imgui_.SelStart(); }
    Coordinates SelEnd() { return Okay() ? FromOk(okay_.SelEnd()) : imgui_.SelEnd(); }
    void SetSelection(const Coordinates& a, const Coordinates& b) { if (Okay()) okay_.SetSelection(ToOk(a), ToOk(b)); else imgui_.SetSelection(a, b); }
    void SelectAll() { if (Okay()) okay_.SelectAll(); else imgui_.SelectAll(); }
    void SelectWordUnderCursor() { if (Okay()) okay_.SelectWordUnderCursor(); else imgui_.SelectWordUnderCursor(); }
    std::string GetSelectedText() { return Okay() ? okay_.GetSelectedText() : imgui_.GetSelectedText(); }

    // ---- per-frame settings ----
    void SetColorizerEnable(bool on) { imgui_.SetColorizerEnable(on); okay_.SetColorizerEnable(on); }
    void SetUserTypes(const Keywords& t) { imgui_.SetUserTypes(t); okay_.SetUserTypes(t); }
    void SetErrorMarkers(const ErrorMarkers& m) { imgui_.SetErrorMarkers(m); okay_.SetErrorMarkers(m); }
    void SetBreakpoints(const Breakpoints& b) { imgui_.SetBreakpoints(b); okay_.SetBreakpoints(b); }
    void SetHandleKeyboardInputs(bool on) { imgui_.SetHandleKeyboardInputs(on); okay_.SetHandleKeyboardInputs(on); }

    // ---- geometry of the last Render (screen pixels) ----
    float RowHeight() { return Okay() ? okay_.RowHeight() : imgui_.RowHeight(); }
    float CharWidth() { return Okay() ? okay_.CharWidth() : imgui_.CharWidth(); }
    int FirstVisibleLine() { return Okay() ? okay_.FirstVisibleLine() : imgui_.FirstVisibleLine(); }
    float TextStartX() { return Okay() ? okay_.TextStartX() : imgui_.TextStartX(); }
    ImVec2 ContentOrigin() { if (!Okay()) return imgui_.ContentOrigin(); const auto v = okay_.ContentOrigin(); return ImVec2(v.x, v.y); }
    ImVec2 CaretScreenPosBelow() { if (!Okay()) return imgui_.CaretScreenPosBelow(); const auto v = okay_.CaretScreenPosBelow(); return ImVec2(v.x, v.y); }
    Coordinates ScreenToCoords(const ImVec2& p) { return Okay() ? FromOk(okay_.ScreenToCoords(p.x, p.y)) : imgui_.ScreenToCoords(p); }

    /// True while the code has the keyboard. The ImGui editor is a child window the script editor finds through ImGui's nav
    /// window; the OkayUI one reports its own focus (call after Render).
    bool OkayHasKeyboard() const { return okayFocused_; }
    bool UsingOkayUI() { return Okay(); }

    /// Draw it. `size` as ImGui: <= 0 means "what is left, plus this".
    void Render(const char* id, const ImVec2& size = ImVec2(), bool border = false) {
        if (!Okay()) { g_scriptCodeOwnsKeys = false; imgui_.Render(id, size, border); return; }
#ifdef OKAY_HAVE_OKAYUI
        ImVec2 a = ImGui::GetContentRegionAvail();
        a.x = size.x > 0.0f ? size.x : a.x + size.x;
        a.y = size.y > 0.0f ? size.y : a.y + size.y;
        a.x = std::max(a.x, 8.0f); a.y = std::max(a.y, 8.0f);
        // OkayUI draws and reads the mouse in the editor window's own pixels; Dear ImGui's screen positions are desktop positions
        // with the multi-window viewports on, so take the window's place on the desktop off (as the Console does). Without this
        // the code was drawn that far down and to the right of its rectangle, over the Inspector and the status line.
        const ImVec2 vp = ImGui::GetWindowViewport()->Pos;
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        const ImVec2 p(sp.x - vp.x, sp.y - vp.y);
        okay_.SetScreenOffset(vp.x, vp.y);                      // the geometry calls answer in ImGui's coordinates again
        ImGui::InvisibleButton(id, a);                          // the rectangle: an ImGui item, so ImGui does not scroll under it
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        oked::OkayUIRegion().Offer(p.x, p.y, a.x, a.y, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        // The code font: the embedded JetBrains Mono at the size the script editor pushed for the ImGui editor (its zoom and the
        // UI scale), so both editors show the same text. Without it the code used the proportional UI font and drifted off its cells.
        okay_.UseCodeFont(ImGui::GetFontSize());
        OkayUI::PushTextScale(oked::g_uiScale);
        OkayUI::BareNextWindow();
        if (OkayUI::Begin(id, p.x, p.y, a.x, a.y)) okay_.Edit().Draw("##codeedit", a.x, a.y);
        OkayUI::End();
        OkayUI::PopTextScale();
        okayFocused_ = okay_.Edit().Focused();
        // A mouse press: inside the code, the script editor's ImGui window takes the focus too (its shortcuts, not the scene's:
        // Ctrl+D duplicated the selected object while typing); anywhere else, the code gives up the keyboard.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const bool inside = m.x >= sp.x && m.x < sp.x + a.x && m.y >= sp.y && m.y < sp.y + a.y;
            if (inside) { if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ImGui::SetWindowFocus(); }
            else if (okayFocused_) { OkayUI::SetKeyboardFocusId(0); okayFocused_ = false; }
        }
        g_scriptCodeOwnsKeys = okayFocused_;
#endif
    }

private:
    bool Okay() {
#ifdef OKAY_HAVE_OKAYUI
        const bool want = oked::UseOkayUIBackend();
#else
        const bool want = false;
#endif
        if (want != lastOkay_) {            // the backend changed: carry the text and the caret over
            if (want) Carry(imgui_, okay_); else Carry(okay_, imgui_);
            lastOkay_ = want;
        }
        return want;
    }
    static scripteditor::Coord ToOk(const Coordinates& c) { return scripteditor::Coord(c.mLine, c.mColumn); }
    static Coordinates FromOk(const scripteditor::Coord& c) { return Coordinates(c.mLine, c.mColumn); }
    static void Carry(TextEditor& from, scripteditor::ScriptEditorCodeEdit& to) {
        std::string t = from.GetText();
        if (!t.empty() && t.back() == '\n') t.pop_back();       // GetText ends with the newline both editors add
        const Coordinates a = from.SelStart(), b = from.SelEnd(), c = from.GetCursorPosition();
        to.SetText(t);
        if (a != b) to.SetSelection(ToOk(a), ToOk(b));
        to.SetCursorPosition(ToOk(c));
    }
    static void Carry(scripteditor::ScriptEditorCodeEdit& from, TextEditor& to) {
        std::string t = from.GetText();
        if (!t.empty() && t.back() == '\n') t.pop_back();
        const scripteditor::Coord a = from.SelStart(), b = from.SelEnd(), c = from.GetCursorPosition();
        to.SetText(t);
        if (a != b) to.SetSelection(FromOk(a), FromOk(b));
        to.SetCursorPosition(FromOk(c));
    }

    TextEditor imgui_;
    scripteditor::ScriptEditorCodeEdit okay_;
    bool lastOkay_ = false;
    bool okayFocused_ = false;
};
