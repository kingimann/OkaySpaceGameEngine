// The script editor's adapter over OkayUI::CodeEdit (editor/ScriptEditorCodeEdit.{hpp,cpp}): display-column coordinates,
// byte/column conversion with tabs and multi-byte characters, 1-based markers and breakpoints, GetText with a trailing newline,
// undoable insertion, selection helpers, change flag and the geometry after a real Draw. No window, no ImGui.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ScriptEditorCodeEdit.hpp"

#include <cmath>

using namespace scripteditor;
namespace UI = OkayUI;

namespace {
struct Rig {
    okayuitest::Sim sim;
    ScriptEditorCodeEdit se;
    bool open = true;
    Rig() : sim(640, 400) { okayuitest::Reset(); UI::SetClipboardText(""); }
    void Frame() {
        sim.Frame([&] {
            if (UI::Begin("W", 10, 10, sim.W - 20, sim.H - 20, &open)) se.Edit().Draw("ed", 500, 300);
            UI::End();
        });
    }
};
}

int main() {
    RUN_SUITE("script editor adapter");
    {   // text in and out: GetText always ends with a newline, lines and counts agree
        ScriptEditorCodeEdit s;
        s.SetText("a\nbb");
        CHECK(s.GetText() == "a\nbb\n");
        CHECK(s.GetTotalLines() == 2 && s.LineLength(0) == 1 && s.LineLength(1) == 2 && s.LineLength(5) == 0 && s.LineLength(-1) == 0);
        CHECK((s.GetTextLines() == std::vector<std::string>{"a", "bb"}));
        s.SetText("");
        CHECK(s.GetText() == "\n" && s.GetTotalLines() == 1);
        s.SetText("x\n");                           // a real trailing newline is a real empty last line
        CHECK(s.GetTotalLines() == 2 && s.GetText() == "x\n\n");
    }
    {   // display columns: a tab is up to the next multiple of 4, a two-byte character is one column
        ScriptEditorCodeEdit s;
        s.SetText("\tab\n\xC3\xA9x\n  \tz");
        CHECK(s.LineLength(0) == 6);                // tab (4) + a + b
        CHECK(s.LineLength(1) == 2);
        CHECK(s.LineLength(2) == 5);                // two spaces, tab to 4, z
        s.SetCursorPosition(Coord(0, 5));           // between a and b
        CHECK(s.Edit().Caret() == (UI::CodePos{0, 2}));
        CHECK(s.GetCursorPosition() == Coord(0, 5));
        s.SetCursorPosition(Coord(1, 1));           // after the e-acute: byte 2
        CHECK(s.Edit().Caret() == (UI::CodePos{1, 2}));
        CHECK(s.GetCursorPosition() == Coord(1, 1));
        s.SetCursorPosition(Coord(2, 4));           // after the tab
        CHECK(s.Edit().Caret() == (UI::CodePos{2, 3}) && s.GetCursorPosition() == Coord(2, 4));
        s.SetCursorPosition(Coord(1, 99));          // past the end: the end of the line
        CHECK(s.GetCursorPosition() == Coord(1, 2));
        s.SetCursorPosition(Coord(99, 0));          // past the last line: the last line
        CHECK(s.GetCursorPosition().mLine == 2);
        CHECK(Coord(-3, -2) == Coord(0, 0));        // negative input is clamped like an unsigned position
        CHECK(ScriptEditorCodeEdit().ToCoord(UI::CodePos{40, 40}) == Coord(0, 0));   // an out-of-range position clamps
    }
    {   // a click-style conversion rounds to the nearest cell and the round trip is stable for every column
        ScriptEditorCodeEdit s;
        s.SetText("a\tb\xE2\x82\xAC" "c");
        for (int c = 0; c <= s.LineLength(0); ++c) {
            s.SetCursorPosition(Coord(0, c));
            const Coord back = s.GetCursorPosition();
            s.SetCursorPosition(back);
            CHECK(s.GetCursorPosition() == back);
        }
        CHECK(s.LineLength(0) == 7);                // a=1, tab to 4, b=5, euro=6, c=7
    }
    {   // selection: coordinates in, coordinates out, in order; word under the cursor; selected text
        ScriptEditorCodeEdit s;
        s.SetText("foo bar\nbaz");
        s.SetSelection(Coord(0, 4), Coord(0, 7));
        CHECK(s.HasSelection() && s.SelStart() == Coord(0, 4) && s.SelEnd() == Coord(0, 7) && s.GetSelectedText() == "bar");
        s.SetSelection(Coord(1, 3), Coord(0, 4));   // given backwards: still start <= end
        CHECK(s.SelStart() == Coord(0, 4) && s.SelEnd() == Coord(1, 3) && s.GetSelectedText() == "bar\nbaz");
        s.SetCursorPosition(Coord(0, 1));
        CHECK(!s.HasSelection());
        s.SelectWordUnderCursor();
        CHECK(s.GetSelectedText() == "foo");
        s.SelectAll();
        CHECK(s.GetSelectedText() == "foo bar\nbaz");
    }
    {   // InsertTextUndoable replaces the selection, is one undo step per call, and Delete removes the selection or one character
        ScriptEditorCodeEdit s;
        s.SetText("hello world");
        s.SelectAll();
        s.InsertTextUndoable("x\ny");
        CHECK(s.GetText() == "x\ny\n" && s.GetCursorPosition() == Coord(1, 1));
        s.InsertTextUndoable("1"); s.InsertTextUndoable("2");   // two calls: two steps, even back to back
        CHECK(s.Edit().Line(1) == "y12");
        s.Edit().Undo();
        CHECK(s.Edit().Line(1) == "y1");
        s.Edit().Undo();
        CHECK(s.Edit().Line(1) == "y");
        s.Edit().Undo();
        CHECK(s.GetText() == "hello world\n");
        s.SetCursorPosition(Coord(0, 0));
        s.Delete();
        CHECK(s.Edit().Line(0) == "ello world");
        s.SetSelection(Coord(0, 0), Coord(0, 4));
        s.Delete();
        CHECK(s.Edit().Line(0) == " world");
    }
    {   // the change flag: SetText is not an edit; typing is; the flag is read once
        ScriptEditorCodeEdit s;
        s.SetText("a");
        CHECK(!s.IsTextChanged());
        s.InsertTextUndoable("b");
        CHECK(s.IsTextChanged() && !s.IsTextChanged());
        s.Edit().Undo();
        CHECK(s.IsTextChanged());
        s.InsertTextUndoable("q"); s.ClearTextChanged();
        CHECK(!s.IsTextChanged());
    }
    {   // markers and breakpoints are 1-based like the ImGui editor's; a mark below 1 is ignored
        ScriptEditorCodeEdit s;
        s.SetText("a\nb\nc");
        s.SetErrorMarkers({{1, "first"}, {3, "third"}, {0, "ignored"}, {-4, "ignored"}});
        const auto& m = s.Edit().Markers();
        CHECK(m.size() == 2 && m[0].line == 0 && m[0].message == "Error at line 1: first" && m[1].line == 2 && m[1].message == "Error at line 3: third");
        s.SetBreakpoints({3, 1, 0, -2});
        CHECK((s.Edit().Breakpoints() == std::vector<int>{0, 2}));
        CHECK(s.Edit().HasBreakpoint(0) && s.Edit().HasBreakpoint(2) && !s.Edit().HasBreakpoint(1));
        s.SetErrorMarkers({});
        CHECK(s.Edit().Markers().empty());
    }
    {   // colouring: on by default with the OkayScript words, off and on again, user types
        ScriptEditorCodeEdit s;
        s.SetText("if (x) { int y; }");
        std::vector<UI::CodeToken> t;
        s.Edit().LineTokens(0, t);
        bool kw = false; for (const auto& k : t) if (k.color == UI::CodeColor_Keyword) kw = true;
        CHECK(kw);
        s.SetColorizerEnable(false);
        s.Edit().LineTokens(0, t);
        CHECK(t.empty());
        s.SetColorizerEnable(true);
        s.SetUserTypes({"Enemy"});
        s.SetText("Enemy e;");
        s.Edit().LineTokens(0, t);
        bool ty = false; for (const auto& k : t) if (k.color == UI::CodeColor_Type && k.start == 0 && k.end == 5) ty = true;
        CHECK(ty);
    }
    {   // geometry after a real Draw: row height, char width, text start, content origin, caret popup place, hit test
        Rig r;
        r.se.SetText("one\n\ttwo\nthree");
        r.Frame(); r.Frame();
        CHECK(r.se.RowHeight() > 8.0f && r.se.CharWidth() > 4.0f);
        const UI::Vec2 o = r.se.ContentOrigin();
        const float tx = r.se.TextStartX();
        CHECK(tx > o.x && std::abs((tx - o.x) - r.se.Edit().GutterWidth()) < 0.01f);
        CHECK(r.se.FirstVisibleLine() == 0);
        r.se.SetCursorPosition(Coord(1, 4));        // after the tab: column 4
        r.Frame();
        const UI::Vec2 below = r.se.CaretScreenPosBelow();
        CHECK(std::abs(below.x - (tx + 4 * r.se.CharWidth())) < 0.01f);
        CHECK(std::abs(below.y - (o.y + 2 * r.se.RowHeight())) < 0.01f);
        // The centre of the cell of (2, 3) hits (2, 3); a point in the left half of a cell rounds to that cell's left edge.
        const float cx = tx + 3.0f * r.se.CharWidth(), cy = o.y + 2.5f * r.se.RowHeight();
        CHECK(r.se.ScreenToCoords(cx + 1.0f, cy) == Coord(2, 3));
        CHECK(r.se.ScreenToCoords(cx + r.se.CharWidth() * 0.9f, cy) == Coord(2, 4));
        CHECK(r.se.ScreenToCoords(tx + 1.0f, o.y + 1.0f) == Coord(0, 0));
        CHECK(r.se.ScreenToCoords(tx + 3.0f * r.se.CharWidth() + 1.0f, o.y + 1.5f * r.se.RowHeight()) == Coord(1, 4));   // inside the tab: the nearer edge (the tab is columns 0..3)
    }
    {   // the keyboard flag, read-only and the tab size are passed through
        ScriptEditorCodeEdit s;
        s.SetReadOnly(true);
        s.SetText("a");
        s.InsertTextUndoable("zz");
        CHECK(s.Edit().Line(0) == "a");
        s.SetReadOnly(false);
        s.SetTabSize(8);
        s.SetText("\tb");
        CHECK(s.LineLength(0) == 9);
    }
    {   // the screen offset: the script editor draws in the window's pixels and asks in desktop coordinates (multi-window
        // viewports). The geometry comes back with the offset added and a desktop point goes in with it taken off.
        Rig r;
        r.se.SetText("one\n\ttwo\nthree");
        r.Frame(); r.Frame();
        const UI::Vec2 o0 = r.se.ContentOrigin(), b0 = r.se.CaretScreenPosBelow();
        const float t0 = r.se.TextStartX();
        r.se.SetScreenOffset(160.0f, 50.0f);
        CHECK(std::abs(r.se.ContentOrigin().x - (o0.x + 160.0f)) < 0.01f && std::abs(r.se.ContentOrigin().y - (o0.y + 50.0f)) < 0.01f);
        CHECK(std::abs(r.se.TextStartX() - (t0 + 160.0f)) < 0.01f);
        CHECK(std::abs(r.se.CaretScreenPosBelow().x - (b0.x + 160.0f)) < 0.01f && std::abs(r.se.CaretScreenPosBelow().y - (b0.y + 50.0f)) < 0.01f);
        const float cx = r.se.TextStartX() + 3.0f * r.se.CharWidth() + 1.0f, cy = r.se.ContentOrigin().y + 2.5f * r.se.RowHeight();
        CHECK(r.se.ScreenToCoords(cx, cy) == Coord(2, 3));
        CHECK(r.se.ScreenToCoords(cx - 160.0f, cy - 50.0f) == Coord(0, 0));   // a point in window pixels is not where it looks
    }
    {   // the code font: JetBrains Mono, monospaced, at the size asked for; the window's own font and text scale are put back
        Rig r;
        UI::UseLauncherLook(true);                  // the editor's look: a proportional UI font (Roboto)
        r.se.SetText("iiii\nWWWW\nint count = 42;");
        r.Frame(); r.Frame();
        const float uiCell = r.se.CharWidth();
        const UI::Vec2 uiI = UI::CalcTextSize("iiii"), uiW = UI::CalcTextSize("WWWW");
        CHECK(std::abs(uiI.x - uiW.x) > 4.0f);      // the UI font is not monospaced: a cell grid drifts with it
        CHECK(ScriptEditorCodeEdit::CodeFont() != nullptr);
        CHECK(r.se.UseCodeFont(20.0f));
        CHECK(r.se.Edit().CodeFont() == ScriptEditorCodeEdit::CodeFont());
        r.Frame(); r.Frame();
        const float cell = r.se.CharWidth();
        // JetBrains Mono: advance 600 units, ascent + descent 1320 units; a 20 px line (OkayUI and Dear ImGui size a font by that
        // height) makes a cell 20 * 600 / 1320 = 9.09 px
        CHECK(std::abs(cell - 20.0f * 600.0f / 1320.0f) < 0.05f && std::abs(cell - uiCell) > 0.5f);
        CHECK(std::abs(r.se.RowHeight() - std::floor(20.0f * 1.55f + 0.5f)) < 1.01f);   // a line is 20 px times the line spacing
        // the text of a run of i and of W are the same width in the code font: measure with the font set, as Draw does
        {
            const UI::Font* prev = UI::GetFont();
            UI::SetFont(ScriptEditorCodeEdit::CodeFont());
            UI::PushTextScale(20.0f / 16.0f);
            const float wi = UI::CalcTextSize("iiii").x, ww = UI::CalcTextSize("WWWW").x, w0 = UI::CalcTextSize("0000").x;
            UI::PopTextScale();
            UI::SetFont(prev);
            CHECK(std::abs(wi - ww) < 0.01f && std::abs(w0 - 4.0f * cell) < 0.05f);
        }
        // after the Draw the window's font and size are what they were
        r.sim.Frame([&] {
            if (UI::Begin("W", 10, 10, r.sim.W - 20, r.sim.H - 20, &r.open)) {
                const UI::Font* f0 = UI::GetFont();
                const float a = UI::CalcTextSize("Hello").x, h = UI::GetTextLineHeight();
                r.se.Edit().Draw("ed", 500, 300);
                CHECK(UI::GetFont() == f0 && std::abs(UI::CalcTextSize("Hello").x - a) < 0.01f && std::abs(UI::GetTextLineHeight() - h) < 0.01f);
            }
            UI::End();
        });
        // a click lands on the cell under the pointer in the code font: the 3rd character of line 2 ("t" of "int")
        UI::Vec2 p = r.se.Edit().PosToScreen(UI::CodePos{2, 2});
        r.sim.Move(p.x + cell * 0.2f, p.y + r.se.RowHeight() * 0.5f); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(r.se.GetCursorPosition() == Coord(2, 2));
        UI::UseLauncherLook(false);
    }
    {   // the script's language from the Dear ImGui editor's words: a Lua-like one with "--" and "--[[ ]]" comments
        ScriptEditorCodeEdit s;
        LanguageWords w;
        w.keywords = {"local", "function", "end"};
        w.types = {"Vector3"};
        w.known = {"print", "Vector3"};
        w.lineComments = {"--"};
        w.blockStart = "--[["; w.blockEnd = "]]";
        s.SetLanguage(w);
        s.SetText("local x = print(1) -- note\nprint\n--[[ block\nstill ]] local y = Vector3\na.print = 1\n");
        std::vector<UI::CodeToken> t;
        auto colorAt = [&](int line, int col) { s.Edit().LineTokens(line, t); for (const UI::CodeToken& k : t) if (col >= k.start && col < k.end) return (int)k.color; return (int)UI::CodeColor_Default; };
        CHECK(colorAt(0, 0) == UI::CodeColor_Keyword);
        CHECK(colorAt(0, 10) == UI::CodeColor_Function);                 // print( : the call colour, as the ImGui editor
        CHECK(colorAt(0, 22) == UI::CodeColor_Comment);                  // -- note
        CHECK(colorAt(1, 0) == UI::CodeColor_Known);                     // a bare built-in
        CHECK(colorAt(2, 4) == UI::CodeColor_Comment && colorAt(3, 2) == UI::CodeColor_Comment);   // the block comment spans lines
        CHECK(colorAt(3, 9) == UI::CodeColor_Keyword);                   // after its end: code again
        CHECK(colorAt(3, 19) == UI::CodeColor_Type);                     // a type before a known name
        CHECK(colorAt(4, 2) == UI::CodeColor_Member);
        s.SetColorizerEnable(false);
        CHECK(colorAt(0, 0) == UI::CodeColor_Default);
        s.SetColorizerEnable(true);                                      // back on: the same language, not OkayScript
        CHECK(colorAt(0, 22) == UI::CodeColor_Comment && colorAt(0, 0) == UI::CodeColor_Keyword);
        s.SetUserTypes({"Enemy"});                                       // the script's own classes on top of its language
        s.SetText("Enemy e; -- x");
        CHECK(colorAt(0, 0) == UI::CodeColor_Type && colorAt(0, 10) == UI::CodeColor_Comment);
    }
    {   // the Dear ImGui editor's palette
        ScriptEditorCodeEdit s;
        unsigned pal[kScriptPaletteSize];
        for (int i = 0; i < kScriptPaletteSize; ++i) pal[i] = 0xff000000u | (unsigned)(i + 1);
        s.SetPaletteColors(pal, kScriptPaletteSize);
        const UI::CodePalette& p = s.Edit().Palette();
        CHECK(p.text[UI::CodeColor_Default] == pal[0] && p.text[UI::CodeColor_Keyword] == pal[1] && p.text[UI::CodeColor_Number] == pal[2]);
        CHECK(p.text[UI::CodeColor_String] == pal[3] && p.text[UI::CodeColor_Char] == pal[4] && p.text[UI::CodeColor_Punct] == pal[5]);
        CHECK(p.text[UI::CodeColor_Preproc] == pal[6] && p.text[UI::CodeColor_Known] == pal[8] && p.text[UI::CodeColor_Comment] == pal[10]);
        CHECK(p.text[UI::CodeColor_Type] == pal[21] && p.text[UI::CodeColor_Function] == pal[22] && p.text[UI::CodeColor_Member] == pal[23]);
        CHECK(p.background == pal[12] && p.gutter == pal[12] && p.caret == pal[13] && p.selection == pal[14] && p.errorLine == pal[15]);
        CHECK(p.breakpointLine == pal[16] && p.lineNumber == pal[17] && p.currentLine == pal[18] && p.currentLineInactive == pal[19]);
        CHECK(p.currentLineEdge == pal[20]);
        const UI::CodePalette before = s.Edit().Palette();
        s.SetPaletteColors(pal, 5);                                   // too short: ignored
        CHECK(s.Edit().Palette().background == before.background);
        s.SetPaletteColors(nullptr, kScriptPaletteSize);
        CHECK(s.Edit().Palette().background == before.background);
    }
    {   // markers: "warning: " messages are amber, the tooltip names the line like the ImGui editor's
        ScriptEditorCodeEdit s;
        s.SetText("a\nb\nc\n");
        s.SetErrorMarkers({{2, "unknown name 'b'"}, {3, "warning: unused variable"}});
        const auto& m = s.Edit().Markers();
        CHECK(m.size() == 2);
        if (m.size() == 2) {
            CHECK(m[0].line == 1 && m[0].lineColor == 0 && m[0].message == "Error at line 2: unknown name 'b'");
            CHECK(m[1].line == 2 && m[1].lineColor == UI::RGBA(200, 150, 40, 55) && m[1].color == UI::RGBA(230, 190, 70));
            CHECK(m[1].message == "Warning at line 3: unused variable");
        }
    }
    {   // a long message stays whole in the tooltip
        ScriptEditorCodeEdit s;
        s.SetText("a\n");
        const std::string longMsg(100, 'x');
        s.SetErrorMarkers({{1, longMsg}});
        CHECK(s.Edit().Markers().size() == 1 && s.Edit().Markers()[0].message == "Error at line 1: " + longMsg);
    }
    {   // the hover tooltip on a documented word: the language's docs (a built-in's signature and doc), nothing for other words
        Rig r;
        LanguageWords w;
        w.keywords = {"if"};
        w.known = {"print"};
        w.docs = {{"print", "print(value)\nWrites a line to the console"}, {"Vector3", "Vector3 (built-in type)"}};
        r.se.SetLanguage(w);
        r.se.SetText("if print(Vector3) other");
        r.Frame(); r.Frame();
        auto tipAt = [&](int col) {
            const UI::Vec2 p = r.se.Edit().PosToScreen(UI::CodePos{0, col});
            return r.se.Edit().HoverText(p.x + 1.0f, p.y + r.se.RowHeight() * 0.5f);
        };
        CHECK(tipAt(3) == "print(value)\nWrites a line to the console");
        CHECK(tipAt(9) == "Vector3 (built-in type)");
        CHECK(tipAt(0).empty() && tipAt(18).empty());   // a keyword and a word with no doc
        r.se.SetColorizerEnable(false);                 // the tooltips stay with colouring off, as in the vendored editor
        CHECK(tipAt(3) == "print(value)\nWrites a line to the console");
    }
    {   // the left margin between the line numbers and the text (the script editor's Density setting); at least 4 px
        ScriptEditorCodeEdit s;
        s.SetLeftMargin(26);
        CHECK(s.Edit().TextMargin() == 26.0f);
        s.SetLeftMargin(0);
        CHECK(s.Edit().TextMargin() == 4.0f);
    }
    {   // the palette type and its index names (our own; the vendored TextEditor::Palette is gone)
        ScriptPalette p{};
        p[(int)PaletteIndex::Background] = 0xff102030u;
        p[(int)PaletteIndex::Keyword] = 0xff0000ffu;
        CHECK((int)PaletteIndex::Member == kScriptPaletteSize - 1 && p.size() == (size_t)kScriptPaletteSize);
        ScriptEditorCodeEdit s;
        s.SetPalette(p);
        CHECK(s.Edit().Palette().background == 0xff102030u && s.Edit().Palette().text[UI::CodeColor_Keyword] == 0xff0000ffu);
    }
    TEST_MAIN_RESULT();
}
