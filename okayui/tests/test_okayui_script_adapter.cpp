// The script editor's adapter over OkayUI::CodeEdit (editor/ScriptEditorCodeEdit.{hpp,cpp}): display-column coordinates,
// byte/column conversion with tabs and multi-byte characters, 1-based markers and breakpoints, GetText with a trailing newline,
// undoable insertion, selection helpers, change flag and the geometry after a real Draw. No window, no ImGui.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ScriptEditorCodeEdit.hpp"

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
    TEST_MAIN_RESULT();
}
