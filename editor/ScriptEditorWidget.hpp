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

#ifdef OKAY_HAVE_OKAYUI
#include "OkayUIHost.hpp"
#include "OkayUIImGuiEmbed.hpp"   // the code's shapes go into the ImGui window's draw list (ImGui's popups then cover them)
#else
#include "okay/UI/OkayUI.hpp"     // the editor built on OkayUI itself (OKAY_ON_OKAYUI): the code is drawn straight into the window
#endif

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
#ifdef OKAY_HAVE_OKAYUI
        // A framed Dear ImGui child window in the code's background colour, with its border and rounding,
        // its padding around the text (CodeEdit's content padding) and the scroll bars at its inside edge in ImGui's style.
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(okay_.Edit().Palette().background));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild(id, size, border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        const ImRect inner = win->InnerRect;                    // inside the border
        const ImVec2 a(std::max(inner.GetWidth(), 8.0f), std::max(inner.GetHeight(), 8.0f));
        // OkayUI draws and reads the mouse in the editor window's own pixels; Dear ImGui's screen positions are desktop positions
        // with the multi-window viewports on, so take the window's place on the desktop off (as the Console does).
        const ImVec2 vp = ImGui::GetWindowViewport()->Pos;
        const ImVec2 sp = inner.Min;
        const ImVec2 p(sp.x - vp.x, sp.y - vp.y);
        okay_.SetScreenOffset(vp.x, vp.y);                      // the geometry calls answer in ImGui's coordinates again
        ImGui::SetCursorScreenPos(sp);
        ImGui::InvisibleButton("##okaycode", a);                // the rectangle: an ImGui item, so ImGui does not scroll under it
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        oked::OkayUIRegion().Offer(p.x, p.y, a.x, a.y, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        // The code font: the embedded JetBrains Mono at the size the script editor pushed (its zoom and the UI scale).
        okay_.UseCodeFont(ImGui::GetFontSize());
        OkayUI::CodeEdit& ce = okay_.Edit();
        if (!clipHooked_) {   // copy and paste through Dear ImGui's clipboard (the system clipboard, no size limit), as before
            clipHooked_ = true;
            ce.SetClipboardFunctions([](const std::string& t) { ImGui::SetClipboardText(t.c_str()); },
                                     []() { const char* c = ImGui::GetClipboardText(); return std::string(c ? c : ""); });
        }
        ce.SetContentPadding(border ? st.WindowPadding.x : 0.0f, border ? st.WindowPadding.y : 0.0f);
        ce.SetScrollbarStyle(st.ScrollbarSize, st.ScrollbarRounding);
        ce.SetAlwaysShowHScrollbar(true);                       // the horizontal bar is always there, as it always was
        {
            OkayUI::CodePalette pal = ce.Palette();
            pal.scrollTrack = ImGui::GetColorU32(ImGuiCol_ScrollbarBg);
            pal.scrollThumb = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab);
            pal.scrollThumbHot = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabHovered);
            ce.SetPalette(pal);
        }
        // Draw the code into OkayUI, then take its shapes out of the OkayUI frame and put them into this window's draw list: they
        // are then drawn where this window is in Dear ImGui's order, under the autocomplete list, its help tooltip, the hover
        // tooltips and modal dialogs ImGui draws later (OkayUI's frame is drawn after ImGui, which used to cover all of them).
        OkayUI::BeginCapture();
        OkayUI::PushTextScale(oked::g_uiScale);
        OkayUI::BareNextWindow();
        if (OkayUI::Begin(id, p.x, p.y, a.x, a.y)) ce.Draw("##codeedit", a.x, a.y);
        OkayUI::End();
        OkayUI::PopTextScale();
        OkayUI::EndCapture(cap_);
        okayuihost::SharedImGuiEmbed().Append(ImGui::GetWindowDrawList(), cap_, vp, inner.Min, inner.Max);
        okayFocused_ = ce.Focused();
        // A mouse press: inside the code, the code's ImGui child (and so the Script Editor window) takes the focus too (its
        // shortcuts, not the scene's: Ctrl+D duplicated the selected object while typing); anywhere else, the code gives up the keyboard.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const bool inside = m.x >= sp.x && m.x < sp.x + a.x && m.y >= sp.y && m.y < sp.y + a.y && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (inside) { if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ImGui::SetWindowFocus(); }
            else if (okayFocused_) { OkayUI::SetKeyboardFocusId(0); okayFocused_ = false; }
        }
        g_scriptCodeOwnsKeys = okayFocused_;
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
#else
        // The editor on OkayUI (OKAY_ON_OKAYUI): Dear ImGui's names are OkayUI's, so the code is drawn straight into a child window.
        okay_.UseCodeFont(ImGui::GetFontSize());
        if (ImGui::BeginChild(id, size.x, size.y, border)) {
            const OkayUI::Vec2 a = OkayUI::GetContentRegionAvail();
            okay_.Edit().Draw("##codeedit", a.x > 8.0f ? a.x : 8.0f, a.y > 8.0f ? a.y : 8.0f);
        }
        ImGui::EndChild();
        okayFocused_ = okay_.Edit().Focused();
        g_scriptCodeOwnsKeys = okayFocused_;
#endif
    }

private:
    scripteditor::ScriptEditorCodeEdit okay_;
    bool okayFocused_ = false;
    bool clipHooked_ = false;
#ifdef OKAY_HAVE_OKAYUI
    OkayUI::CapturedGeometry cap_;   // this frame's code shapes, on their way into the ImGui draw list
#endif
};
