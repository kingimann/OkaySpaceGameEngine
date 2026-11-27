// The launcher's Command line page. See CommandLineUI.hpp. Our own code on OkayUI, in Dear ImGui's names (and its key
// ownership, so Tab and the arrows reach the shell instead of moving the focus). The rules (words, completion,
// history, running okay, the shells, the layout) are CommandLine.cpp; the terminal emulator is Terminal.cpp.
#include "CommandLineUI.hpp"

#include "CommandLine.hpp"
#include "NavIcons.hpp"      // the launcher's shared controls: BeginSlateCombo, FitButtonW, PageFooterH, UiPx
#include "okay/UI/OkayUIInternals.hpp"   // SetKeyOwner
#include "okay/Core/Utf8.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include "OkayTheme.hpp"   // the theme palette's colours

namespace okaylcli {

namespace {

// ---- the tabs ---------------------------------------------------------------------------------------------------

struct Tab {
    int id = 0;
    std::string name;            ///< what the person called it ("" = the shell and the project)
    ShellKind kind = ShellKind::Okay;
    std::string root, project;   ///< the folder it runs in and its project's name
    std::unique_ptr<ShellSession> shell;   ///< shell tabs (made on the first draw, when the size is known)
    std::unique_ptr<Session> okay;         ///< okay-only tabs
    char line[1024] = "";
    unsigned seenRevision = 0;
    int follow = 0;
    bool focusInput = true;
    long long viewTop = 0;       ///< the first line shown when scrolled back
    bool atBottom = true;        ///< following the newest output
    bool selecting = false;
    bool draggingBar = false;
};

std::vector<std::unique_ptr<Tab>> g_tabs;
int g_active = -1;                 ///< index into g_tabs
int g_nextId = 1;
int g_selectId = 0;                ///< a tab to bring to the front next frame
int g_focusTerm = 0;               ///< the tab whose terminal has the keyboard (0 = none)
int g_renameId = 0;
char g_renameBuf[64] = "";
std::string g_projectRoot, g_projectName;   ///< the project new tabs start in (the card's "Command line" sets it)
bool g_projectPending = false;
float g_fontSize = 0.0f;           ///< the terminal's text size (0 = the launcher's)
ShellKind g_newKind = DefaultShell();

std::string TabLabel(const Tab& t) {
    const std::string shell = ShellLabel(t.kind);
    if (!t.name.empty()) return t.name + " (" + shell + ")";   // the shell's name always shows
    return t.project.empty() ? shell : shell + " - " + t.project;
}

void StartShell(Tab& t, const Host& host, int cols, int rows) {
    t.shell.reset(new ShellSession(cols, rows));
    t.viewTop = 0;
    t.atBottom = true;
    std::string error;
    t.shell->Start(t.kind, t.root, host.toolsDir, error);   // a failure is written into the terminal
}

Tab& NewTab(ShellKind kind, const std::string& root, const std::string& project) {
    std::unique_ptr<Tab> t(new Tab());
    t->id = g_nextId++;
    t->kind = kind;
    t->root = root;
    t->project = project;
    if (kind == ShellKind::Okay) t->okay.reset(new Session());
    g_tabs.push_back(std::move(t));
    g_active = (int)g_tabs.size() - 1;
    g_selectId = g_tabs.back()->id;
    g_focusTerm = g_tabs.back()->id;
    return *g_tabs.back();
}

/// The tab gets another shell or folder: what ran there ends and it starts again.
void Retarget(Tab& t, ShellKind kind, const std::string& root, const std::string& project) {
    if (t.shell) t.shell->Stop();
    t.shell.reset();
    if (t.okay) t.okay->Stop();
    t.kind = kind;
    t.root = root;
    t.project = project;
    if (kind == ShellKind::Okay && !t.okay) t.okay.reset(new Session());
    if (kind != ShellKind::Okay) t.okay.reset();
    t.focusInput = true;
    g_focusTerm = t.id;
}

void CloseTab(std::size_t i) {
    Tab& t = *g_tabs[i];
    if (t.shell) t.shell->Stop();
    if (t.okay) { t.okay->Stop(); t.okay->Poll(); }
    if (g_focusTerm == t.id) g_focusTerm = 0;
    g_tabs.erase(g_tabs.begin() + (long)i);
    if (g_active >= (int)g_tabs.size()) g_active = (int)g_tabs.size() - 1;
}

ImU32 Col(std::uint32_t rgb, float a = 1.0f) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(a * 255.0f));
}

void Place(const Rect& r, ImVec2 origin) { ImGui::SetCursorPos(ImVec2(origin.x + r.x, origin.y + r.y)); }

// ---- the okay-only tab (the page as it was) ---------------------------------------------------------------------

Tab* g_cbTab = nullptr;   // the tab the input callback works on
int InputCallback(ImGuiInputTextCallbackData* data) {
    if (!g_cbTab || !g_cbTab->okay) return 0;
    Session& s = *g_cbTab->okay;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        std::string line;
        const bool moved = data->EventKey == ImGuiKey_UpArrow ? s.GetHistory().Up(std::string(data->Buf, (std::size_t)data->BufTextLen), line)
                                                               : s.GetHistory().Down(line);
        if (moved) { data->DeleteChars(0, data->BufTextLen); data->InsertChars(0, line.c_str()); }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Complete what is left of the cursor; anything after it stays.
        const std::string before(data->Buf, (std::size_t)data->CursorPos);
        const Completion c = Complete(before);
        if (c.line != before) {
            data->DeleteChars(0, data->CursorPos);
            data->InsertChars(0, c.line.c_str());
            data->CursorPos = (int)c.line.size();
        }
        if (!c.choices.empty()) {
            std::string all;
            for (const std::string& w : c.choices) all += (all.empty() ? "" : "   ") + w;
            s.Note(all);
        }
    }
    return 0;
}

void DrawOkayTab(Tab& t, const Host& host, const PageLayout& L, ImVec2 origin) {
    Session& s = *t.okay;
    Place(L.output, origin);
    if (host.mono) ImGui::PushFont(host.mono, g_fontSize);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, okaytheme::kDeep);
    // New lines: show the end. For a few frames, because the box learns its new text height a frame late.
    if (s.Revision() != t.seenRevision) t.follow = 3;
    t.seenRevision = s.Revision();
    static const std::string s_empty =
        "Type an okay command below and press Enter, for example:\n"
        "  help            every command\n"
        "  scene check     check the project's scenes\n"
        "  vc status       what changed since the last save\n"
        "Tab completes a command, Up and Down go through earlier ones, clear empties this box.\n"
        "For a full shell, pick one in the list above.";
    const std::string& text = s.Lines().empty() ? s_empty : s.Text();
    ImGui::InputTextMultiline("##cliout", const_cast<char*>(text.c_str()), text.size() + 1, ImVec2(L.output.w, L.output.h),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_WordWrap);
    if (t.follow > 0) { OkayUI::ScrollTextBoxToEnd("##cliout"); --t.follow; }   // the box keeps its own scroll: to its last line
    ImGui::PopStyleColor();

    Place(L.input, origin);
    ImGui::SetNextItemWidth(L.input.w);
    if (t.focusInput) { ImGui::SetKeyboardFocusHere(); t.focusInput = false; }
    const bool running = s.Running();
    g_cbTab = &t;
    const bool entered = ImGui::InputTextWithHint("##cliline", running ? "Running... press Stop to end it" : "okay command, for example: help",
                                                  t.line, sizeof(t.line),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                                                      ImGuiInputTextFlags_CallbackCompletion,
                                                  InputCallback);
    if (host.mono) ImGui::PopFont();
    Place(L.run, origin);
    ImGui::BeginDisabled(running);
    const bool runClicked = okl::PrimaryButton("Run", ImVec2(L.run.w, L.run.h));
    ImGui::EndDisabled();
    if ((entered || runClicked) && !running) {
        s.SetProgram(host.okayExe);
        s.SetFolder(t.root);
        s.Submit(t.line);
        t.line[0] = 0;
        t.focusInput = true;
    } else if (entered) {
        t.focusInput = true;
    }
    Place(L.stop, origin);
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop", ImVec2(L.stop.w, L.stop.h))) s.Stop();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("End the running command, and anything it started.");
}

// ---- the shell tab: the terminal ---------------------------------------------------------------------------------

struct KeyMap { ImGuiKey imgui; okayterm::Key key; };
const KeyMap kKeys[] = {
    {ImGuiKey_UpArrow, okayterm::Key::Up}, {ImGuiKey_DownArrow, okayterm::Key::Down}, {ImGuiKey_LeftArrow, okayterm::Key::Left},
    {ImGuiKey_RightArrow, okayterm::Key::Right}, {ImGuiKey_Home, okayterm::Key::Home}, {ImGuiKey_End, okayterm::Key::End},
    {ImGuiKey_PageUp, okayterm::Key::PageUp}, {ImGuiKey_PageDown, okayterm::Key::PageDown}, {ImGuiKey_Insert, okayterm::Key::Insert},
    {ImGuiKey_Delete, okayterm::Key::Delete}, {ImGuiKey_Enter, okayterm::Key::Enter}, {ImGuiKey_KeypadEnter, okayterm::Key::Enter},
    {ImGuiKey_Backspace, okayterm::Key::Backspace}, {ImGuiKey_Tab, okayterm::Key::Tab}, {ImGuiKey_Escape, okayterm::Key::Escape},
    {ImGuiKey_F1, okayterm::Key::F1}, {ImGuiKey_F2, okayterm::Key::F2}, {ImGuiKey_F3, okayterm::Key::F3}, {ImGuiKey_F4, okayterm::Key::F4},
    {ImGuiKey_F5, okayterm::Key::F5}, {ImGuiKey_F6, okayterm::Key::F6}, {ImGuiKey_F7, okayterm::Key::F7}, {ImGuiKey_F8, okayterm::Key::F8},
    {ImGuiKey_F9, okayterm::Key::F9}, {ImGuiKey_F10, okayterm::Key::F10}, {ImGuiKey_F11, okayterm::Key::F11}, {ImGuiKey_F12, okayterm::Key::F12},
};

void ChangeFont(float delta) {
    if (delta == 0.0f) { g_fontSize = 0.0f; return; }
    const float base = g_fontSize > 0.0f ? g_fontSize : ImGui::GetFontSize();
    g_fontSize = std::max(8.0f, std::min(40.0f, std::round(base + delta)));
}

void CopySelection(okayterm::Terminal& term) {
    const std::string text = term.HasSelection() ? term.SelectedText() : std::string();
    if (!text.empty()) ImGui::SetClipboardText(text.c_str());
    term.ClearSelection();
}

void PasteClipboard(Tab& t) {
    const char* clip = ImGui::GetClipboardText();
    if (clip && *clip && t.shell) { t.shell->Paste(clip); t.atBottom = true; }
}

/// Keys and typed text go to the shell while its terminal has the keyboard.
void TerminalKeys(Tab& t, ImGuiID owner) {
    ShellSession& sh = *t.shell;
    okayterm::Terminal& term = sh.Term();
    ImGuiIO& io = ImGui::GetIO();
    // This terminal owns these keys, so Dear ImGui's Tab and arrow focus moves never see them.
    for (const KeyMap& k : kKeys) ImGui::SetKeyOwner(k.imgui, owner);
    okayterm::Mods m;
    m.shift = io.KeyShift;
    m.ctrl = io.KeyCtrl;
    m.alt = io.KeyAlt;
    std::string out;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    // The page's own shortcuts first.
    if (m.ctrl && !m.alt) {
        if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)) { ChangeFont(1.0f); io.InputQueueCharacters.resize(0); return; }
        if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) { ChangeFont(-1.0f); io.InputQueueCharacters.resize(0); return; }
        if (pressed(ImGuiKey_0)) { ChangeFont(0.0f); return; }
        if (pressed(ImGuiKey_V)) { PasteClipboard(t); io.InputQueueCharacters.resize(0); return; }
        if (pressed(ImGuiKey_C) && (m.shift || term.HasSelection())) { CopySelection(term); return; }   // Ctrl+C copies a selection
    }
    if (m.shift && !m.ctrl && pressed(ImGuiKey_Insert)) { PasteClipboard(t); return; }
    if (m.shift && !m.ctrl && (pressed(ImGuiKey_PageUp) || pressed(ImGuiKey_PageDown))) {   // scroll the view, as terminals do
        const long long page = std::max(1, term.Rows() - 1);
        if (t.atBottom) t.viewTop = term.ScreenTop();
        t.viewTop += ImGui::IsKeyDown(ImGuiKey_PageUp) ? -page : page;
        t.atBottom = false;
        return;
    }
    // A shell that ended starts again on Enter.
    if (!sh.Running()) {
        if (sh.Started() && (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))) t.shell.reset();
        io.InputQueueCharacters.resize(0);
        return;
    }
    for (const KeyMap& k : kKeys)
        if (pressed(k.imgui)) out += okayterm::KeySequence(k.key, m, term.AppCursorKeys());
    // Ctrl or Alt with a letter, a digit or a punctuation key: no text arrives for those, so they come from the keys.
    // Ctrl+Alt together is AltGr on many keyboards: its text is taken as typed.
    if ((m.ctrl || m.alt) && !(m.ctrl && m.alt)) {
        for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
            if (pressed((ImGuiKey)k)) out += okayterm::CharSequence((char32_t)('a' + (k - ImGuiKey_A)), m);
        if (m.alt)
            for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k)
                if (pressed((ImGuiKey)k)) out += okayterm::CharSequence((char32_t)('0' + (k - ImGuiKey_0)), m);
        if (m.ctrl) {
            const struct { ImGuiKey k; char32_t c; } punct[] = {
                {ImGuiKey_Space, U' '}, {ImGuiKey_LeftBracket, U'['}, {ImGuiKey_Backslash, U'\\'}, {ImGuiKey_RightBracket, U']'},
                {ImGuiKey_Slash, U'/'}, {ImGuiKey_2, U'@'}};
            for (const auto& p : punct)
                if (pressed(p.k)) out += okayterm::CharSequence(p.c, m);
        }
        io.InputQueueCharacters.resize(0);
    } else {
        for (ImWchar c : io.InputQueueCharacters)
            if (c >= 0x20 && c != 0x7F) out += okayterm::CharSequence((char32_t)c, okayterm::Mods());
        io.InputQueueCharacters.resize(0);
    }
    if (!out.empty()) {
        sh.Send(out);
        term.ClearSelection();
        t.atBottom = true;
    }
}

/// The terminal's text, cursor, selection and scroll bar, inside `r`.
void DrawTerminal(Tab& t, const Host& host, const Rect& r, ImVec2 origin) {
    const okayterm::Scheme& sc = okayterm::DarkScheme();
    if (host.mono) ImGui::PushFont(host.mono, g_fontSize);
    const float cellW = std::max(4.0f, ImGui::CalcTextSize("M").x);
    const float cellH = std::max(6.0f, ImGui::GetTextLineHeight());
    const float pad = okl::UiPx(6.0f), barW = okl::UiPx(8.0f);
    int cols = 2, rows = 1;
    CellsFor(r.w - 2 * pad - barW - 4.0f, r.h - 2 * pad, cellW, cellH, cols, rows);
    if (!t.shell) StartShell(t, host, cols, rows);
    t.shell->Resize(cols, rows);

    Place(r, origin);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##terminal", ImVec2(r.w, r.h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool leftClick = ImGui::IsItemClicked(ImGuiMouseButton_Left), rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + r.w, p0.y + r.h);
    const ImVec2 g0(p0.x + pad, p0.y + pad);   // the grid's top-left corner
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mp = io.MousePos;
    const float barX = p1.x - pad * 0.5f - barW;

    {
        okayterm::Terminal& term = t.shell->Term();
        const long long first = term.FirstLine(), screenTop = term.ScreenTop();
        if (t.atBottom) t.viewTop = screenTop;
        t.viewTop = std::max(first, std::min(screenTop, t.viewTop));
        if (t.viewTop >= screenTop) t.atBottom = true;
        // ---- the wheel: the scrollback, or arrow keys for a full-screen program ----
        if (hovered && io.MouseWheel != 0.0f) {
            const int lines = (int)std::round(io.MouseWheel * 3.0f);
            if (term.AltScreen() && t.shell->Running()) {
                for (int k = 0; k < std::abs(lines); ++k)
                    t.shell->Send(okayterm::KeySequence(lines > 0 ? okayterm::Key::Up : okayterm::Key::Down, okayterm::Mods(), term.AppCursorKeys()));
            } else {
                t.viewTop = std::max(first, std::min(screenTop, t.viewTop - lines));
                t.atBottom = t.viewTop >= screenTop;
            }
        }
        // ---- the mouse: focus, selection, the scroll bar ----
        const bool overBar = hovered && mp.x >= barX - 2.0f && first < screenTop;
        auto cellAt = [&](bool round) {
            const float fx = (mp.x - g0.x) / cellW, fy = (mp.y - g0.y) / cellH;
            okayterm::Pos p;
            p.col = std::max(0, std::min(cols, (int)(round ? std::floor(fx + 0.5f) : std::floor(fx))));
            const int row = std::max(0, std::min(rows - 1, (int)std::floor(fy)));
            p.line = std::max(first, std::min(term.EndLine() - 1, t.viewTop + row));
            return p;
        };
        if (leftClick) {
            g_focusTerm = t.id;
            if (overBar) t.draggingBar = true;
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { term.SelectWord(cellAt(false)); t.selecting = false; }
            else if (io.KeyShift && term.HasSelection()) { term.SelectExtend(cellAt(true)); t.selecting = true; }
            else { term.SelectStart(cellAt(true)); t.selecting = true; }
        }
        if (t.selecting) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (mp.y < g0.y && t.viewTop > first) { t.viewTop--; t.atBottom = false; }   // dragging past the edge scrolls
                if (mp.y > g0.y + rows * cellH && !t.atBottom) t.viewTop++;
                term.SelectExtend(cellAt(true));
            } else {
                t.selecting = false;
            }
        }
        if (t.draggingBar) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float f = std::max(0.0f, std::min(1.0f, (mp.y - p0.y) / std::max(1.0f, r.h)));
                t.viewTop = first + (long long)std::llround(f * (double)(screenTop - first));
                t.atBottom = t.viewTop >= screenTop;
            } else {
                t.draggingBar = false;
            }
        }
        if (hovered && !overBar) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered && g_focusTerm == t.id) g_focusTerm = 0;   // clicked elsewhere
    // ---- the right-click menu ----
    if (rightClick) { g_focusTerm = t.id; ImGui::OpenPopup("##termmenu"); }
    if (ImGui::BeginPopup("##termmenu")) {
        okayterm::Terminal& term = t.shell->Term();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, term.HasSelection())) CopySelection(term);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, t.shell->Running())) PasteClipboard(t);
        if (ImGui::MenuItem("Select all")) term.SelectAll();
        if (ImGui::MenuItem("Clear")) { term.ClearAll(); t.atBottom = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Bigger text", "Ctrl+=")) ChangeFont(1.0f);
        if (ImGui::MenuItem("Smaller text", "Ctrl+-")) ChangeFont(-1.0f);
        if (ImGui::MenuItem("Restart the shell")) { t.shell->Stop(); t.shell.reset(); }
        ImGui::EndPopup();
    }
    // ---- the keyboard ----
    const bool focused = g_focusTerm == t.id;
    if (t.shell && focused && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !io.WantTextInput) TerminalKeys(t, id);
    if (!t.shell) { if (host.mono) ImGui::PopFont(); return; }   // restarted: it starts again on the next frame

    ShellSession& sh = *t.shell;
    okayterm::Terminal& term = sh.Term();
    const long long first = term.FirstLine(), screenTop = term.ScreenTop();
    if (t.atBottom) t.viewTop = screenTop;
    t.viewTop = std::max(first, std::min(screenTop, t.viewTop));

    // ---- the cells ----
    dl->AddRectFilled(p0, p1, Col(sc.bg), okl::UiPx(4.0f));
    if (focused) dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(host.accent.x, host.accent.y, host.accent.z, 0.55f)), okl::UiPx(4.0f));
    dl->PushClipRect(p0, p1, true);
    ImFont* font = ImGui::GetFont();
    const float fsize = ImGui::GetFontSize();
    const bool blinkOn = std::fmod(ImGui::GetTime(), 1.06) < 0.53;
    for (int row = 0; row < rows; ++row) {
        const long long ln = t.viewTop + row;
        if (ln >= term.EndLine()) break;
        const okayterm::Line& line = term.LineAt(ln);
        const int n = std::min(cols, (int)line.cells.size());
        const float y = g0.y + row * cellH;
        // backgrounds, in runs of one colour
        auto bgAt = [&](int x) {
            std::uint32_t fg = 0, bg = 0;
            okayterm::CellColors(line.cells[(std::size_t)x], sc, fg, bg);
            return term.IsSelected(ln, x) ? sc.selection : bg;
        };
        for (int x = 0; x < n;) {
            const std::uint32_t bg = bgAt(x);
            int e = x + 1;
            while (e < n && bgAt(e) == bg) ++e;
            if (bg != sc.bg) dl->AddRectFilled(ImVec2(g0.x + x * cellW, y), ImVec2(g0.x + e * cellW, y + cellH), Col(bg));
            x = e;
        }
        // the characters, each at its own cell (so wide characters and any glyph width stay on the grid)
        for (int x = 0; x < n; ++x) {
            const okayterm::Cell& c = line.cells[(std::size_t)x];
            if (c.width == 0) continue;
            std::uint32_t fg = 0, bg = 0;
            okayterm::CellColors(c, sc, fg, bg);
            const float cx = g0.x + x * cellW;
            const float cw = cellW * (c.width == 2 ? 2.0f : 1.0f);
            if (c.ch != U' ' && c.ch != 0 && !(c.attr & okayterm::kHidden)) {
                std::string s;
                okay::utf8::Append(s, (std::uint32_t)c.ch);
                dl->AddText(font, fsize, ImVec2(cx, y), Col(fg), s.c_str());
                if (c.attr & okayterm::kBold) dl->AddText(font, fsize, ImVec2(cx + 1.0f, y), Col(fg), s.c_str());   // no bold face: drawn twice
            }
            if (c.attr & okayterm::kUnderline) dl->AddLine(ImVec2(cx, y + cellH - 1.5f), ImVec2(cx + cw, y + cellH - 1.5f), Col(fg));
            if (c.attr & okayterm::kStrike) dl->AddLine(ImVec2(cx, y + cellH * 0.55f), ImVec2(cx + cw, y + cellH * 0.55f), Col(fg));
        }
    }
    // the cursor: a blinking block while the terminal has the keyboard, an outline when it does not
    const long long curLine = screenTop + term.CursorY();
    if (term.CursorVisible() && sh.Running() && curLine >= t.viewTop && curLine < t.viewTop + rows) {
        const float cx = g0.x + term.CursorX() * cellW, cy = g0.y + (float)(curLine - t.viewTop) * cellH;
        const okayterm::Cell& under = term.At(term.CursorX(), term.CursorY());
        const float cw = cellW * (under.width == 2 ? 2.0f : 1.0f);
        if (focused && blinkOn) {
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + cellH), Col(sc.cursor));
            if (under.ch != U' ' && under.width != 0) {
                std::string s;
                okay::utf8::Append(s, (std::uint32_t)under.ch);
                dl->AddText(font, fsize, ImVec2(cx, cy), Col(sc.bg), s.c_str());
            }
        } else if (!focused) {
            dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + cellH), Col(sc.cursor, 0.7f));
        }
    }
    // the scroll bar, when there is scrollback
    if (first < screenTop) {
        const float trackT = p0.y + pad, trackB = p1.y - pad;
        const double total = (double)(screenTop - first + rows);
        const float thumbH = std::max(okl::UiPx(16.0f), (float)((trackB - trackT) * rows / total));
        const float f = (float)((double)(t.viewTop - first) / (double)std::max<long long>(1, screenTop - first));
        const float ty = trackT + f * (trackB - trackT - thumbH);
        const bool overBar = hovered && mp.x >= barX - 2.0f;
        dl->AddRectFilled(ImVec2(barX, trackT), ImVec2(barX + barW, trackB), Col(0xFFFFFF, 0.04f), barW * 0.5f);
        dl->AddRectFilled(ImVec2(barX, ty), ImVec2(barX + barW, ty + thumbH), Col(0xFFFFFF, overBar || t.draggingBar ? 0.35f : 0.18f), barW * 0.5f);
    }
    dl->PopClipRect();
    if (host.mono) ImGui::PopFont();
    if (!t.atBottom) {   // scrolled back: say so, and how to get back
        const char* note = "Scrolled back. Type, or scroll down, to return.";
        const ImVec2 ts = ImGui::CalcTextSize(note);
        const ImVec2 a(p1.x - pad - barW - ts.x - 14.0f, p1.y - pad - ts.y - 6.0f);
        dl->AddRectFilled(a, ImVec2(a.x + ts.x + 10.0f, a.y + ts.y + 4.0f), Col(0x2B2E36, 0.92f), 4.0f);
        dl->AddText(ImVec2(a.x + 5.0f, a.y + 2.0f), Col(sc.fg), note);
    }
}

} // namespace

const char* HintText() {
    return "Runs only the okay program, with what you type as its words; it is not a full shell.";
}

const char* ShellHintText() {
    return "Your own shell in the project's folder; okay works here. Ctrl+C stops a command, Ctrl+V pastes, right-click for more.";
}

void SetProject(const std::string& root, const std::string& name) {
    g_projectRoot = root;
    g_projectName = name;
    g_projectPending = true;
}

void OpenTerminalFor(const Host& host, const std::string& root, const std::string& name) {
    std::string error;
    const bool ok = OpenTerminal(root, host.toolsDir, error);
    if (host.toast) host.toast(ok ? "Command line opened in " + (name.empty() ? root : name) : error, ok ? 1 : 2);
}

void Tick() {
    for (auto& t : g_tabs) {
        if (t->shell) t->shell->Poll();
        if (t->okay) t->okay->Poll();
    }
}

void Shutdown() {
    for (auto& t : g_tabs) {
        if (t->shell) t->shell->Stop();
        if (t->okay) { t->okay->Stop(); t->okay->Poll(); }
    }
    g_tabs.clear();
    g_active = -1;
}

bool OpenProgram(const Host& host, const FoundProgram& p) {
    // Only programs found beside the launcher by their file name (FindPrograms); started without a shell.
    const bool ok = okaycli::StartDetached({p.path}, g_projectRoot);
    if (host.toast) host.toast(ok ? p.label + " opened." : "Could not start " + p.path + ".", ok ? 1 : 2);
    return ok;
}

void DrawProgramsRow(const Host& host) {
    if (host.programs.empty()) return;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Open in its own window:");
    for (const FoundProgram& p : host.programs) {
        ImGui::SameLine();
        ImGui::PushID(p.path.c_str());
        if (ImGui::Button(p.label.c_str())) OpenProgram(host, p);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip.c_str());
        ImGui::PopID();
    }
}

void DrawPage(const Host& host) {
    // A project from the list, until the person picks one (or the card's button set it).
    if (g_projectRoot.empty() && !host.projects.empty()) { g_projectRoot = host.projects[0].root; g_projectName = host.projects[0].name; }
    if (g_projectPending) {   // the project card's "Command line": its tab, or a new one
        g_projectPending = false;
        bool found = false;
        for (std::size_t i = 0; i < g_tabs.size() && !found; ++i)
            if (g_tabs[i]->root == g_projectRoot) { g_active = (int)i; g_selectId = g_focusTerm = g_tabs[i]->id; found = true; }
        if (!found) NewTab(g_newKind, g_projectRoot, g_projectName);
    }
    if (g_tabs.empty()) NewTab(g_newKind, g_projectRoot, g_projectName);
    if (g_active < 0 || g_active >= (int)g_tabs.size()) g_active = 0;
    Tick();

    const ImVec2 origin = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float footer = okl::PageFooterH() + okl::UiPx(6.0f);   // main.cpp's version line is pinned under every page
    const float frameH = ImGui::GetFrameHeight(), lineH = ImGui::GetTextLineHeight();
    PageButtons bw;
    bw.shell = okl::FitButtonW("Command Prompt", okl::UiPx(120.0f)) + frameH;
    bw.terminal = okl::FitButtonW("Open in external window");
    bw.copy = okl::FitButtonW("Copy all");
    bw.clear = okl::FitButtonW("Clear", okl::UiPx(56.0f));
    bw.font = std::max(frameH, okl::FitButtonW("+"));
    bw.run = okl::FitButtonW("Run", okl::UiPx(56.0f));
    bw.stop = okl::FitButtonW("Stop", okl::UiPx(56.0f));
    const float pageH = std::max(frameH * 6.0f, avail.y - footer);

    // ---- the tabs: "+" makes one with the same shell and folder; double-click or right-click to rename ----
    {
        const PageLayout top = LayoutPage(avail.x, pageH, frameH, lineH, bw, true);
        Place(top.tabs, origin);
    }
    if (ImGui::BeginTabBar("##clitabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        int closeIndex = -1;
        for (std::size_t i = 0; i < g_tabs.size(); ++i) {
            Tab& t = *g_tabs[i];
            bool open = true;
            const std::string label = TabLabel(t) + "###clitab" + std::to_string(t.id);
            const ImGuiTabItemFlags f = g_selectId == t.id ? ImGuiTabItemFlags_SetSelected : 0;
            const bool shown = ImGui::BeginTabItem(label.c_str(), g_tabs.size() > 1 ? &open : nullptr, f);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                g_renameId = t.id;
                std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", t.name.c_str());
            }
            if (ImGui::IsItemHovered() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const std::string title = t.shell ? t.shell->Term().Title() : std::string();
                ImGui::SetTooltip("%s\n%s%s%s", ShellLabel(t.kind).c_str(), t.root.empty() ? "(the home folder)" : t.root.c_str(),
                                  title.empty() ? "" : "\n", title.c_str());
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename...")) { g_renameId = t.id; std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", t.name.c_str()); }
                if (ImGui::MenuItem("Restart", nullptr, false, t.kind != ShellKind::Okay)) { if (t.shell) t.shell->Stop(); t.shell.reset(); }
                if (ImGui::MenuItem("Close", nullptr, false, g_tabs.size() > 1)) closeIndex = (int)i;
                ImGui::EndPopup();
            }
            if (shown) {
                if (g_active != (int)i) { g_active = (int)i; g_focusTerm = t.id; t.focusInput = true; }
                ImGui::EndTabItem();
            }
            if (!open) closeIndex = (int)i;
        }
        g_selectId = 0;
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            const Tab& cur = *g_tabs[(std::size_t)g_active];
            NewTab(cur.kind, cur.root, cur.project);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new tab with the same shell and folder");
        ImGui::EndTabBar();
        if (closeIndex >= 0) CloseTab((std::size_t)closeIndex);
    }
    if (g_tabs.empty()) NewTab(g_newKind, g_projectRoot, g_projectName);
    if (g_active < 0 || g_active >= (int)g_tabs.size()) g_active = 0;
    Tab& t = *g_tabs[(std::size_t)g_active];
    const bool okayMode = t.kind == ShellKind::Okay;
    if (okayMode && !t.okay) t.okay.reset(new Session());
    const PageLayout L = LayoutPage(avail.x, pageH, frameH, lineH, bw, okayMode);

    // ---- rename ----
    if (g_renameId && !ImGui::IsPopupOpen("Rename tab")) ImGui::OpenPopup("Rename tab");
    if (ImGui::BeginPopup("Rename tab")) {
        ImGui::TextUnformatted("Tab name (empty = the shell and the project):");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(okl::UiPx(260.0f));
        const bool done = ImGui::InputText("##rename", g_renameBuf, sizeof(g_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        if (done || ImGui::Button("Rename")) {
            for (auto& tab : g_tabs) if (tab->id == g_renameId) tab->name = g_renameBuf;
            g_renameId = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { g_renameId = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    } else {
        g_renameId = 0;
    }

    // ---- the shell and the project ----
    bool changed = false;   // the tab was started again: its body is drawn from the next frame
    Place(L.shell, origin);
    ImGui::SetNextItemWidth(L.shell.w);
    if (okl::BeginSlateCombo("##clishell", ShellLabel(t.kind).c_str())) {
        for (ShellKind k : AvailableShells()) {
            const std::string name = ShellLabel(k) + (k == DefaultShell() ? "  (default)" : "");
            if (ImGui::Selectable(name.c_str(), k == t.kind) && k != t.kind) { Retarget(t, k, t.root, t.project); g_newKind = k; changed = true; }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this tab runs. Changing it starts the tab again.");
    Place(L.project, origin);
    ImGui::SetNextItemWidth(L.project.w);
    const std::string shown = t.root.empty() ? std::string(okayMode ? "No project (runs in the launcher's folder)" : "No project (the home folder)")
                                             : "Project: " + (t.project.empty() ? t.root : t.project);
    if (okl::BeginSlateCombo("##cliproj", shown.c_str())) {
        if (host.projects.empty()) ImGui::TextDisabled("No projects yet. Make one on the Create page.");
        for (const ProjectRef& p : host.projects) {
            ImGui::PushID(p.root.c_str());
            if (ImGui::Selectable(p.name.c_str(), p.root == t.root) && p.root != t.root) {
                g_projectRoot = p.root;
                g_projectName = p.name;
                if (t.kind == ShellKind::Okay) { t.root = p.root; t.project = p.name; t.focusInput = true; }
                else { Retarget(t, t.kind, p.root, p.name); changed = true; }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.root.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && !t.root.empty())
        ImGui::SetTooltip(okayMode ? "Commands run in %s" : "The shell runs in %s (changing it starts the tab again)", t.root.c_str());

    // ---- the buttons ----
    Place(L.terminal, origin);
    ImGui::BeginDisabled(t.root.empty());
    if (ImGui::Button("Open in external window", ImVec2(L.terminal.w, L.terminal.h))) OpenTerminalFor(host, t.root, t.project);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A terminal window of its own in the project's folder, where okay works as a command.");
    Place(L.copy, origin);
    if (ImGui::Button(okayMode ? "Copy all" : "Copy", ImVec2(L.copy.w, L.copy.h))) {
        if (t.okay) ImGui::SetClipboardText(t.okay->Text().c_str());
        if (t.shell) {
            const okayterm::Terminal& term = t.shell->Term();
            ImGui::SetClipboardText((term.HasSelection() ? term.SelectedText() : term.AllText()).c_str());
        }
    }
    if (!okayMode && ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the selected text, or all of it when nothing is selected.");
    Place(L.clear, origin);
    if (ImGui::Button("Clear", ImVec2(L.clear.w, L.clear.h))) {
        if (t.okay) t.okay->Clear();
        if (t.shell) { t.shell->Term().ClearAll(); t.atBottom = true; }
        g_focusTerm = t.id;
    }
    if (L.smaller.w > 0) {
        Place(L.smaller, origin);
        if (ImGui::Button("-##font", ImVec2(L.smaller.w, L.smaller.h))) ChangeFont(-1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smaller text (Ctrl+-)");
        Place(L.bigger, origin);
        if (ImGui::Button("+##font", ImVec2(L.bigger.w, L.bigger.h))) ChangeFont(1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bigger text (Ctrl+=)");
    }

    // ---- the hint ----
    Place(L.hint, origin);
    const char* hint = okayMode ? HintText() : ShellHintText();
    if (!okayMode && ImGui::CalcTextSize(hint).x > L.hint.w) hint = "Ctrl+C stops a command, Ctrl+V pastes, right-click for more.";
    ImGui::TextDisabled("%s", hint);
    if (okayMode) {
        ImGui::SameLine(0, 6);
        if (ImGui::CalcTextSize(hint).x + 6.0f + ImGui::CalcTextSize("Open a full shell").x <= L.hint.w) {
            if (ImGui::TextLink("Open a full shell")) {   // a shell tab in the same project
                NewTab(DefaultShell() == ShellKind::Okay ? AvailableShells().front() : DefaultShell(), t.root, t.project);
                changed = true;
            }
        } else {
            ImGui::NewLine();
        }
    }

    // ---- the terminal, or the okay output and its input line ----
    if (!changed) {
        if (okayMode) DrawOkayTab(t, host, L, origin);
        else DrawTerminal(t, host, L.output, origin);
    }
    // Leave the cursor under the page so main.cpp's footer goes below it.
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + std::max(L.output.B(), L.stop.B()) + 4.0f));
}

}  // namespace okaylcli
