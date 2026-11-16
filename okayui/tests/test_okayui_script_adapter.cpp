// The script editor's adapter over OkayUI::CodeEdit (editor/ScriptEditorCodeEdit.{hpp,cpp}): display-column coordinates,
// byte/column conversion with tabs and multi-byte characters, 1-based markers and breakpoints, GetText with a trailing newline,
// undoable insertion, selection helpers, change flag and the geometry after a real Draw. No window, no ImGui.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ScriptEditorCodeEdit.hpp"
#include "CodeEditorChoice.hpp"

#include <algorithm>
#include <cstdint>

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
        CHECK(m.size() == 2 && m[0].line == 0 && m[0].message == "first" && m[1].line == 2 && m[1].message == "third");
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
    {   // OKAY_CODE_EDITOR: own / imgui / unset (follow the UI backend); without OkayUI in the build it is always ImGui
        using namespace codeeditorchoice;
        CHECK(Parse(nullptr) == Choice::FollowUiBackend && Parse("") == Choice::FollowUiBackend);
        CHECK(Parse("own") == Choice::Own && Parse("OWN") == Choice::Own && Parse("okayui") == Choice::Own && Parse("1") == Choice::Own);
        CHECK(Parse("imgui") == Choice::ImGui && Parse("ImGui") == Choice::ImGui && Parse("0") == Choice::ImGui);
        CHECK(Parse("banana") == Choice::FollowUiBackend && Parse("ownownownownownownown") == Choice::FollowUiBackend);
        CHECK(!UseOwn(Parse(nullptr), false));                  // the default: the ImGui editor
        CHECK(UseOwn(Parse(nullptr), true));                    // the developer-only OkayUI backend: our own, as before
        CHECK(UseOwn(Parse("own"), false) && UseOwn(Parse("own"), true));
        CHECK(!UseOwn(Parse("imgui"), true) && !UseOwn(Parse("imgui"), false));
        CHECK(!UseOwn(Parse("own"), true, false));              // no OkayUI in this build
        CHECK(std::string(Describe(Parse("own"), false)).find("own") == 0);
        CHECK(std::string(Describe(Parse(nullptr), false)) == "imgui (default)");
    }
    {   // the script editor's palette (TextEditor::PaletteIndex order) reaches our editor's colours
        ScriptEditorCodeEdit s;
        std::uint32_t c[24];
        for (int i = 0; i < 24; ++i) c[i] = 0xff000000u | (std::uint32_t)(i + 1);
        c[14] = 0x80112233u;                          // a translucent selection
        c[16] = 0x40445566u;                          // a translucent breakpoint wash
        s.SetPalette(c, 24);
        const UI::CodePalette& p = s.Edit().Palette();
        CHECK(p.text[UI::CodeColor_Default] == c[0] && p.text[UI::CodeColor_Keyword] == c[1] && p.text[UI::CodeColor_Number] == c[2]);
        CHECK(p.text[UI::CodeColor_String] == c[3] && p.text[UI::CodeColor_Char] == c[4] && p.text[UI::CodeColor_Punct] == c[5]);
        CHECK(p.text[UI::CodeColor_Known] == c[8] && p.text[UI::CodeColor_Comment] == c[10]);
        CHECK(p.text[UI::CodeColor_Type] == c[21] && p.text[UI::CodeColor_Function] == c[22] && p.text[UI::CodeColor_Member] == c[23]);
        CHECK(p.background == c[12] && p.caret == c[13] && p.selection == c[14] && p.lineNumber == c[17]);
        CHECK(p.selectionInactive == 0x40112233u);    // half the selection's alpha
        CHECK(p.breakpoint == 0xff445566u);           // a dot: opaque
        CHECK(p.errorLine == c[15] && p.currentLine == c[18] && p.currentLineEdge == c[20]);
        const UI::CodePalette before = s.Edit().Palette();
        s.SetPalette(c, 10);                          // too short: ignored
        CHECK(s.Edit().Palette().background == before.background);
        s.SetPalette(nullptr, 24);
        CHECK(s.Edit().Palette().background == before.background);
    }
    {   // the language's words: keywords, types and built-ins coloured; they survive colouring off and on; hover help is kept
        ScriptEditorCodeEdit s;
        s.SetLanguageWords({"when"}, {"Thing"}, {"spawnStuff", "when"}, {{"spawnStuff", "spawnStuff(name) - makes stuff"}});
        s.SetText("when Thing spawnStuff if");
        auto colorAt = [&](int col) {
            std::vector<UI::CodeToken> t;
            s.Edit().LineTokens(0, t);
            for (const auto& k : t) if (k.start <= col && col < k.end) return (int)k.color;
            return (int)UI::CodeColor_Default;
        };
        CHECK(colorAt(0) == UI::CodeColor_Keyword);
        CHECK(colorAt(5) == UI::CodeColor_Type);
        CHECK(colorAt(11) == UI::CodeColor_Known);
        CHECK(colorAt(22) != UI::CodeColor_Keyword);  // "if" is not in this language's keyword list
        s.SetColorizerEnable(false);
        CHECK(colorAt(0) == UI::CodeColor_Default);
        s.SetColorizerEnable(true);
        CHECK(colorAt(0) == UI::CodeColor_Keyword && colorAt(11) == UI::CodeColor_Known);
        CHECK(s.Edit().WordInfo("spawnStuff") && *s.Edit().WordInfo("spawnStuff") == "spawnStuff(name) - makes stuff");
        CHECK(!s.Edit().WordInfo("Thing"));
    }
    {   // the hover help shows as a tooltip over the word, and only there
        Rig r;
        r.se.SetLanguageWords({}, {}, {"spawnStuff"}, {{"spawnStuff", "spawnStuff(name)"}});
        r.se.SetText("x = spawnStuff(1)");
        r.Frame();
        const float cw = r.se.CharWidth(), rh = r.se.RowHeight(), tx = r.se.TextStartX(), ty = r.se.ContentOrigin().y;
        CHECK(r.se.Edit().WordAtScreen(tx + 6.5f * cw, ty + 0.5f * rh) == "spawnStuff");
        CHECK(r.se.Edit().WordAtScreen(tx + 1.5f * cw, ty + 0.5f * rh) == "");          // the space after x
        CHECK(r.se.Edit().WordAtScreen(tx + 40.5f * cw, ty + 0.5f * rh) == "");         // past the end of the line
        CHECK(r.se.Edit().WordAtScreen(tx + 6.5f * cw, ty + 5.5f * rh) == "");          // below the last line
        r.sim.Move(tx + 6.5f * cw, ty + 0.5f * rh);
        r.Frame(); r.Frame();
        const okayuitest::Bitmap withTip = r.sim.Draw();
        r.sim.Move(tx + 1.5f * cw, ty + 0.5f * rh);
        r.Frame(); r.Frame();
        const okayuitest::Bitmap noTip = r.sim.Draw();
        // A tooltip changes pixels outside the editor's own drawing: below the pointer.
        const int px = (int)(tx + 6.5f * cw), py = (int)(ty + 0.5f * rh);
        int diff = 0;
        for (int y = py + 4; y < std::min(py + 60, (int)r.sim.H); ++y)
            for (int x = px; x < std::min(px + 200, (int)r.sim.W); ++x)
                if (okayuitest::PixelAt(withTip, x, y) != okayuitest::PixelAt(noTip, x, y)) ++diff;
        CHECK(diff > 50);
    }
    {   // overwrite mode (the Insert key) replaces characters, never past the end of the line, and is one undo step
        Rig r;
        r.se.SetText("abcd\nxy");
        r.se.SetCursorPosition(Coord(0, 1));
        r.se.SetOverwrite(true);
        CHECK(r.se.IsOverwrite());
        CHECK(r.se.Edit().TypeText("ZZ"));
        CHECK(r.se.Edit().Line(0) == "aZZd" && r.se.GetCursorPosition() == Coord(0, 3));
        CHECK(r.se.Edit().TypeText("123"));          // only "d" is left to replace: the rest is inserted
        CHECK(r.se.Edit().Line(0) == "aZZ123" && r.se.Edit().Line(1) == "xy");
        r.se.Edit().Undo();
        CHECK(r.se.Edit().Line(0) == "aZZd");
        r.se.SetOverwrite(false);
        r.se.SetCursorPosition(Coord(1, 0));
        r.se.Edit().TypeText("q");
        CHECK(r.se.Edit().Line(1) == "qxy");
        // through the keyboard: click to focus, Insert toggles, typed text overwrites
        r.Frame();
        r.sim.Click(r.se.TextStartX() + 0.2f * r.se.CharWidth(), r.se.ContentOrigin().y + 1.5f * r.se.RowHeight(), [&] {
            if (UI::Begin("W", 10, 10, r.sim.W - 20, r.sim.H - 20, &r.open)) r.se.Edit().Draw("ed", 500, 300);
            UI::End();
        });
        CHECK(r.se.Edit().Focused() && r.se.GetCursorPosition() == Coord(1, 0));
        r.sim.Hold(UI::KeyId_Insert, true); r.Frame(); r.sim.Hold(UI::KeyId_Insert, false); r.Frame();
        CHECK(r.se.IsOverwrite());
        r.sim.Type("W"); r.Frame();
        CHECK(r.se.Edit().Line(1) == "Wxy");
        r.sim.Hold(UI::KeyId_Insert, true); r.Frame(); r.sim.Hold(UI::KeyId_Insert, false); r.Frame();
        CHECK(!r.se.IsOverwrite());
        r.sim.Type("V"); r.Frame();
        CHECK(r.se.Edit().Line(1) == "WVxy");
        // Ctrl+Insert copies, Shift+Insert pastes, Shift+Delete cuts
        r.se.SetSelection(Coord(1, 0), Coord(1, 2));
        r.sim.ctrl = true; r.sim.Hold(UI::KeyId_Insert, true); r.Frame(); r.sim.Hold(UI::KeyId_Insert, false); r.Frame(); r.sim.ctrl = false;
        CHECK(std::string(UI::GetClipboardText()) == "WV");
        r.se.SetCursorPosition(Coord(1, 4));
        r.sim.shift = true; r.sim.Hold(UI::KeyId_Insert, true); r.Frame(); r.sim.Hold(UI::KeyId_Insert, false); r.Frame(); r.sim.shift = false;
        CHECK(r.se.Edit().Line(1) == "WVxyWV");
        r.se.SetSelection(Coord(1, 2), Coord(1, 4));
        r.sim.shift = true; r.sim.Press(okayuitest::Key_Delete); r.Frame(); r.sim.shift = false;
        CHECK(r.se.Edit().Line(1) == "WVWV" && std::string(UI::GetClipboardText()) == "xy");
    }
    TEST_MAIN_RESULT();
}
