#pragma once
// ScriptEditorWidget: the script editor's code widget. It owns BOTH an ImGuiColorTextEdit (the default, the one people use)
// and an OkayUI::CodeEdit behind the adapter (editor/ScriptEditorCodeEdit.hpp), and forwards every call the script editor
// makes to the one that is active. The active one is OkayUI when oked::UseOwnCodeEditor() says so: OKAY_CODE_EDITOR=own, or
// (unset) the developer-only OkayUI backend (docs/own_ui_toolkit.md section 5); otherwise this is the ImGui editor, exactly as
// before. See editor/CodeEditorChoice.hpp.
// When the backend changes while a script is open, the text, caret and selection move to the newly active editor.
//
// It speaks TextEditor's vocabulary (TextEditor::Coordinates, 1-based error markers and breakpoints, GetText ending in a
// newline), so editor/main.cpp changes only the type name and the Render call site.
#include "TextEditor.h"
#include "ScriptEditorCodeEdit.hpp"
#include "MainShared.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef OKAY_HAVE_OKAYUI
#include "OkayUIHost.hpp"
#include "OkayUIInImGui.hpp"
namespace oked { extern SDL_Renderer* g_sdlRenderer; }   // main.cpp: the renderer ImGui and OkayUI draw with
#endif

class ScriptEditorWidget {
public:
    using Coordinates   = TextEditor::Coordinates;
    using Keywords      = TextEditor::Keywords;
    using ErrorMarkers  = TextEditor::ErrorMarkers;
    using Breakpoints   = TextEditor::Breakpoints;

    TextEditor& imgui() { return imgui_; }
    scripteditor::ScriptEditorCodeEdit& okay() { return okay_; }

    // ---- one-time configuration (the ImGui editor's look; the OkayUI editor has its own palette) ----
    void SetLanguageDefinition(const TextEditor::LanguageDefinition& d) {
        imgui_.SetLanguageDefinition(d);
        // the same words for our own editor: keywords, type names, built-ins (with their hover help)
        std::unordered_set<std::string> kw(d.mKeywords.begin(), d.mKeywords.end()), types(d.mTypeNames.begin(), d.mTypeNames.end()), known;
        std::unordered_map<std::string, std::string> info;
        for (const auto& id : d.mIdentifiers) {
            if (!types.count(id.first)) known.insert(id.first);
            if (!id.second.mDeclaration.empty()) info[id.first] = id.second.mDeclaration;
        }
        okay_.SetLanguageWords(kw, types, known, std::move(info));
    }
    void SetPalette(const TextEditor::Palette& p) {
        imgui_.SetPalette(p);
        std::uint32_t c[(int)TextEditor::PaletteIndex::Max];
        for (int i = 0; i < (int)TextEditor::PaletteIndex::Max; ++i) c[i] = (std::uint32_t)p[(size_t)i];
        okay_.SetPalette(c, (int)TextEditor::PaletteIndex::Max);
    }
    void SetReadOnly(bool on) { imgui_.SetReadOnly(on); okay_.SetReadOnly(on); }
    /// The script editor's zoom (Ctrl+wheel); the ImGui editor gets it through the pushed code font, ours through the text scale.
    void SetZoom(float z) { zoom_ = (z > 0.25f && z < 8.0f) ? z : 1.0f; }
    void SetShowWhitespaces(bool b) { imgui_.SetShowWhitespaces(b); }
    void SetTabSize(int n) { imgui_.SetTabSize(n); okay_.SetTabSize(n); }
    void SetLineSpacing(float s) { imgui_.SetLineSpacing(s); okay_.SetLineSpacing(s); }
    void SetLeftMargin(int m) { imgui_.SetLeftMargin(m); okay_.SetLeftMargin(m); }

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
    /// The script editor used this frame's typed character for something else (stepping over a closing bracket, Alt+1): the
    /// ImGui editor never sees it because the character was taken out of ImGui's queue; ours gets its text from SDL, so it is
    /// told to drop it. Call before Render.
    void DropTypedText() { if (Okay()) okay_.Edit().DropTypedText(); }

    /// Draw it. `size` as ImGui: <= 0 means "what is left, plus this".
    /// Ours is drawn INTO this ImGui window's draw list (okayimgui::DrawOkayUIHere), at ImGui's layer, so the autocomplete
    /// list, the hover help and any ImGui popup or window opened over the code later in the frame are drawn on top of it.
    void Render(const char* id, const ImVec2& size = ImVec2(), bool border = false) {
        if (!Okay()) { imgui_.Render(id, size, border); return; }
#ifdef OKAY_HAVE_OKAYUI
        ImVec2 a = ImGui::GetContentRegionAvail();
        a.x = size.x > 0.0f ? size.x : a.x + size.x;
        a.y = size.y > 0.0f ? size.y : a.y + size.y;
        a.x = std::max(a.x, 8.0f); a.y = std::max(a.y, 8.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, a);                          // the rectangle: an ImGui item, so ImGui does not scroll under it
        const ImGuiID item = ImGui::GetItemID();
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        // The keyboard is the code's only while this ImGui window is focused, as the ImGui editor's is only while its child window
        // is: a popup, another window (Ctrl+Tab, a click) or the find box takes it, and it comes back with the window's focus.
        const bool hostFocused = ImGui::IsWindowFocused();
        if (!hostFocused) okay_.SetHandleKeyboardInputs(false);
        okay_.Edit().SetHostDrawsTooltip(true);                 // the hover help goes into ImGui's tooltip layer (below)
        {   // the wheel scrolls as an ImGui child window does: 5 font heights down, 2 sideways, at most 2/3 of the view
            const float fs = ImGui::GetFontSize();
            okay_.Edit().SetWheelStep(std::floor(std::min(5.0f * fs, a.y * 0.67f)), std::floor(std::min(2.0f * fs, a.x * 0.67f)));
        }
        oked::OkayUIRegion().Offer(p.x, p.y, a.x, a.y, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        okayimgui::StyleLikeImGuiChild(okay_.Edit(), ImGui::GetStyle());   // sits in the rectangle as the ImGui editor's child does
        OkayUI::PushTextScale(oked::g_uiScale * zoom_);         // 16 px code at 100 %, the ImGui editor's kCodeFontDispPx x zoom
        OkayUI::MouseCursor cursor = OkayUI::MouseCursor_Arrow;   // the I-beam over the text, as the ImGui editor shows it
        okayimgui::DrawOkayUIHere(id, p, a, [&] { cursor = okayimgui::CursorOf([&] { okay_.Edit().Draw("##codeedit", a.x, a.y); }); },
                                  okayimgui::SdlRendererTextures(oked::g_sdlRenderer), geo_);
        okayimgui::SetImGuiCursor(cursor);
        OkayUI::PopTextScale();
        if (border) {                                           // the child window's border the ImGui editor has
            const ImGuiStyle& st = ImGui::GetStyle();
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + a.x, p.y + a.y), ImGui::GetColorU32(ImGuiCol_Border), st.ChildRounding, 0,
                                                st.ChildBorderSize > 0.0f ? st.ChildBorderSize : 1.0f);
        }
        okayFocused_ = okay_.Edit().Focused() && hostFocused;
        if (!hostFocused) okay_.SetHandleKeyboardInputs(true);  // (the script editor sets it again before the next Render)
        if (okayFocused_) okayimgui::ClaimKeyboardForCode(item);
        okayimgui::ShowCodeHover(okay_.Edit().Hover());
#endif
    }

private:
    bool Okay() {
#ifdef OKAY_HAVE_OKAYUI
        const bool want = oked::UseOwnCodeEditor();
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
    float zoom_ = 1.0f;
#ifdef OKAY_HAVE_OKAYUI
    OkayUI::CapturedGeometry geo_;   // the code area's geometry of this frame, on its way into ImGui's draw list
#endif
};
