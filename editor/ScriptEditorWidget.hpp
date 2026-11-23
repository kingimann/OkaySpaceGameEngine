#pragma once
// ScriptEditorWidget: the script editor's code widget. It owns BOTH an ImGuiColorTextEdit (the default, the one people use)
// and an OkayUI::CodeEdit behind the adapter (editor/ScriptEditorCodeEdit.hpp), and forwards every call the script editor
// makes to the one that is active. The active one is OkayUI only when oked::UseOkayUICodeEdit() says so (Settings > Experimental
// "Our own code editor", OKAY_UI_CODEEDIT=1, or the developer-only OkayUI backend, docs/ui/own_ui_toolkit.md section 5); otherwise
// this is the ImGui editor, exactly as before. When that changes while a script is open, the text, caret and selection move over.
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
#include "OkayUIImGuiEmbed.hpp"   // the code's shapes go into the ImGui window's draw list (ImGui's popups then cover them)
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

    // ---- one-time configuration: both editors get the same language and colours ----
    void SetLanguageDefinition(const TextEditor::LanguageDefinition& d) { imgui_.SetLanguageDefinition(d); okay_.SetLanguage(WordsOf(d)); }
    void SetPalette(const TextEditor::Palette& p) { imgui_.SetPalette(p); okay_.SetPaletteFromImGui(p.data(), (int)p.size()); }

    /// The Dear ImGui language definition as plain words for the OkayUI editor: keywords, type names, the known names (its
    /// identifiers: the built-ins with a tooltip), the line comment, the block comment pair, and a regex token "<literal>.*" that
    /// colours as a comment (OkayScript's "#" comments) as one more line comment.
    static scripteditor::LanguageWords WordsOf(const TextEditor::LanguageDefinition& d) {
        scripteditor::LanguageWords w;
        w.keywords.assign(d.mKeywords.begin(), d.mKeywords.end());
        w.types.assign(d.mTypeNames.begin(), d.mTypeNames.end());
        for (const auto& kv : d.mIdentifiers) w.known.push_back(kv.first);
        if (!d.mSingleLineComment.empty()) w.lineComments.push_back(d.mSingleLineComment);
        for (const auto& r : d.mTokenRegexStrings) {
            if (r.second != TextEditor::PaletteIndex::Comment || r.first.size() < 3 || r.first.compare(r.first.size() - 2, 2, ".*") != 0) continue;
            const std::string lit = r.first.substr(0, r.first.size() - 2);
            if (lit.find_first_of("\\[](){}^$.*+?|") == std::string::npos) w.lineComments.push_back(lit);
        }
        w.blockStart = d.mCommentStart; w.blockEnd = d.mCommentEnd;
        return w;
    }
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

    /// Draw it. `size` as ImGui: <= 0 means "what is left, plus this". `border`: the framed child the Dear ImGui editor draws.
    void Render(const char* id, const ImVec2& size = ImVec2(), bool border = false) {
        if (!Okay()) { g_scriptCodeOwnsKeys = false; imgui_.Render(id, size, border); return; }
#ifdef OKAY_HAVE_OKAYUI
        // The same frame as the Dear ImGui editor: a child window in the code's background colour, with its border and rounding,
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
        // The code font: the embedded JetBrains Mono at the size the script editor pushed for the ImGui editor (its zoom and the
        // UI scale), so both editors show the same text.
        okay_.UseCodeFont(ImGui::GetFontSize());
        OkayUI::CodeEdit& ce = okay_.Edit();
        if (!clipHooked_) {   // copy and paste through Dear ImGui's clipboard (the system clipboard, no size limit), as the ImGui editor
            clipHooked_ = true;
            ce.SetClipboardFunctions([](const std::string& t) { ImGui::SetClipboardText(t.c_str()); },
                                     []() { const char* c = ImGui::GetClipboardText(); return std::string(c ? c : ""); });
        }
        ce.SetContentPadding(border ? st.WindowPadding.x : 0.0f, border ? st.WindowPadding.y : 0.0f);
        ce.SetScrollbarStyle(st.ScrollbarSize, st.ScrollbarRounding);
        ce.SetAlwaysShowHScrollbar(true);                       // as the ImGui editor's child (AlwaysHorizontalScrollbar)
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
#endif
    }

private:
    bool Okay() {
#ifdef OKAY_HAVE_OKAYUI
        const bool want = oked::UseOkayUICodeEdit();
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
    bool clipHooked_ = false;
#ifdef OKAY_HAVE_OKAYUI
    OkayUI::CapturedGeometry cap_;   // this frame's code shapes, on their way into the ImGui draw list
#endif
};
