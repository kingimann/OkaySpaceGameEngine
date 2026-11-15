// The script editor's adapter over OkayUI::CodeEdit (editor/ScriptEditorCodeEdit.{hpp,cpp}): display-column coordinates,
// byte/column conversion with tabs and multi-byte characters, 1-based markers and breakpoints, GetText with a trailing newline,
// undoable insertion, selection helpers, change flag and the geometry after a real Draw. No window, no ImGui.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ScriptEditorCodeEdit.hpp"
#include "ScriptEditorKeys.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifndef OKAYUI_EXAMPLES_DIR
#define OKAYUI_EXAMPLES_DIR "examples"      // the tests run from the repository root when the build did not say where the samples are
#endif

using namespace scripteditor;
namespace UI = OkayUI;

namespace {
// ---- files: what the script editor does with a script on disk ----
std::string TempPath(const std::string& name) { return (std::filesystem::temp_directory_path() / ("okay_script_adapter_" + name)).string(); }
void WriteBytes(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), (std::streamsize)bytes.size());
}
std::string ReadBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
// Write `original` to a file, read it back, give it to the editor, let `use` press keys, then do what editor/main.cpp does with the
// widget's text (GetText, drop the one newline the adapter adds, store it) and write that to a second file. Returns the bytes on disk.
template <class Use> std::string RoundTrip(ScriptEditorCodeEdit& s, const std::string& original, const std::string& name, Use use) {
    const std::string in = TempPath(name + ".in"), out = TempPath(name + ".out");
    WriteBytes(in, original);
    s.SetText(ReadBytes(in));
    use(s);
    std::string t = s.GetText();
    if (!t.empty() && t.back() == '\n') t.pop_back();
    WriteBytes(out, t);
    const std::string back = ReadBytes(out);
    std::remove(in.c_str()); std::remove(out.c_str());
    return back;
}
// Every way the script editor (or the user) could try to change the text of the open script.
void TryEveryEdit(ScriptEditorCodeEdit& s) {
    s.InsertTextUndoable("zzz");
    s.SetCursorPosition(Coord(0, 1)); s.Delete();
    s.SelectAll(); s.InsertTextUndoable("Q");
    s.Edit().InsertText("typed"); s.Edit().NewLine(); s.Edit().Backspace(); s.Edit().Tab(false); s.Edit().Cut();
    s.Edit().SetFind("a"); s.Edit().ReplaceAll("b");
    UI::SetClipboardText("pasted"); s.Edit().Paste();
}

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
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Keys(const std::string& text) { for (char c : text) { sim.Type(std::string(1, c)); Frame(); } }   // one typed character per frame, as a keyboard sends
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
    {   // identifier docs (the script editor's built-in names with their signatures): known-word colour, hover tooltip, and they survive
        // the colouring being switched off and on and user types being set every frame, as the script editor does
        Rig r;
        r.se.SetText("Debug.Log(x)\nvar Enemy e");
        r.se.SetIdentifierDocs({{"Log", "Log(message)"}, {"Enemy", "a class"}});
        r.se.SetUserTypes({"Enemy"});
        r.Frame(); r.Frame();
        std::vector<UI::CodeToken> t;
        r.se.Edit().LineTokens(0, t);
        bool known = false; for (const auto& k : t) if (k.color == UI::CodeColor_Known && k.start == 6 && k.end == 9) known = true;
        CHECK(known);
        const int rev = r.se.Edit().LanguageRevision();
        for (int i = 0; i < 5; ++i) { r.se.SetUserTypes({"Enemy"}); r.se.SetErrorMarkers({}); r.se.SetBreakpoints({}); r.Frame(); }
        CHECK(r.se.Edit().LanguageRevision() == rev);              // per-frame settings that did not change rebuild nothing
        r.sim.Move(r.se.TextStartX() + 7.5f * r.se.CharWidth(), r.se.ContentOrigin().y + 0.5f * r.se.RowHeight());   // over "Log"
        r.Frame();
        CHECK(r.se.Edit().HoverTip() == "Log(message)");
        r.se.SetColorizerEnable(false); r.se.SetColorizerEnable(true);
        r.Frame();
        r.se.Edit().LineTokens(0, t);
        known = false; for (const auto& k : t) if (k.color == UI::CodeColor_Known) known = true;
        CHECK(known);                                              // docs and user types are kept when colouring is turned back on
        r.se.Edit().LineTokens(1, t);
        bool ty = false; for (const auto& k : t) if (k.color == UI::CodeColor_Type && k.start == 4) ty = true;
        CHECK(ty);
    }
    {   // a click on the breakpoint column is reported (1-based) and changes nothing by itself: the script editor owns its bookmarks and
        // hands them back every frame
        Rig r;
        r.se.SetText("a\nb\nc");
        r.se.SetBreakpoints({3});
        r.Frame(); r.Frame();
        const UI::Rect item = r.se.Edit().ItemRect();
        const float x = item.x + r.se.RowHeight() * 0.5f, y = r.se.ContentOrigin().y + 1.5f * r.se.RowHeight();
        r.sim.Click(x, y, [&] { if (UI::Begin("W", 10, 10, r.sim.W - 20, r.sim.H - 20, &r.open)) r.se.Edit().Draw("ed", 500, 300); UI::End(); });
        CHECK((r.se.TakeGutterClicks() == std::vector<int>{2}));
        CHECK(r.se.TakeGutterClicks().empty());                    // taken once
        CHECK((r.se.Edit().Breakpoints() == std::vector<int>{2}));  // still the one the caller set (line 3, 1-based)
    }
    {   // errors and warnings: the first message of a line is shown next to the code, a warning in amber, an error in the palette's red
        ScriptEditorCodeEdit s;
        s.SetText("a\nb\nc");
        s.SetErrorMarkers({{1, "unknown name 'a'"}, {2, "warning: unused variable"}});
        const auto& m = s.Edit().Markers();
        CHECK(m.size() == 2 && m[0].color == 0 && m[1].color == UI::RGBA(230, 195, 90));
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
    {   // a script that is not valid UTF-8, or has control characters or lone carriage returns, comes back byte for byte: the editor
        // cannot show those bytes, so it must not write its repaired copy over the file (a Latin-1 e-acute used to become '?')
        const std::string cases[] = {
            "// caf\xE9 au lait\nvar x = 1;\n",                  // Latin-1 (or ANSI): invalid UTF-8
            "var s = \"\xFF\xFE\";\n",                          // bytes that are never UTF-8
            "a\x1b[0m b\x0c c\x7f d\n",                         // ESC, form feed and DEL
            "one\rtwo\rthree",                                   // lone carriage returns (an old Mac file)
            std::string("a\0b\n", 4),                           // a NUL
            "ok \xC3\xA9 then a cut character \xC3",           // valid text, then a truncated sequence at the very end
        };
        int n = 0;
        for (const std::string& c : cases) {
            const std::string name = "damaged" + std::to_string(n++);
            ScriptEditorCodeEdit idle, busy;
            CHECK(RoundTrip(idle, c, name, [](ScriptEditorCodeEdit&) {}) == c);                // no edit at all
            CHECK(RoundTrip(busy, c, name, [](ScriptEditorCodeEdit& s) { TryEveryEdit(s); }) == c);   // every edit attempted
            CHECK(idle.ReadOnlyBecauseDamaged() && busy.ReadOnlyBecauseDamaged() && busy.Edit().ReadOnly());
            CHECK(!busy.IsTextChanged());                                                       // so the editor never asks to be saved
            CHECK(busy.Notice().find("read-only so nothing is lost") != std::string::npos);
            busy.SetReadOnly(false);                                                            // the host cannot lift it either
            busy.InsertTextUndoable("zzz");
            CHECK(busy.Edit().ReadOnly() && busy.GetText() == c + "\n");
        }
        ScriptEditorCodeEdit l1, ctl;
        l1.SetText("caf\xE9");
        CHECK(l1.Notice() == "This file is not valid UTF-8; it is open read-only so nothing is lost.");
        ctl.SetText("a\x1b" "b");                                                                // valid UTF-8, but a control character
        CHECK(ctl.ReadOnlyBecauseDamaged() && ctl.Notice().find("control characters") != std::string::npos);
        l1.SetText("caf\xC3\xA9");                                                              // the next, clean script is editable again
        CHECK(!l1.ReadOnlyBecauseDamaged() && l1.Notice().empty() && !l1.Edit().ReadOnly());
        l1.InsertTextUndoable("!");
        CHECK(l1.Edit().Line(0) == "!caf\xC3\xA9" && l1.GetText() == "!caf\xC3\xA9\n");
    }
    {   // a valid UTF-8 script is untouched by a round trip, and CRLF becomes LF as it always did (the old editor did the same)
        ScriptEditorCodeEdit s;
        const std::string ok = "// caf\xC3\xA9 \xE2\x82\xAC\nvar x = 1;\n";
        CHECK(RoundTrip(s, ok, "valid", [](ScriptEditorCodeEdit&) {}) == ok);
        ScriptEditorCodeEdit t;
        CHECK(RoundTrip(t, "a\r\nb\r\n", "crlf", [](ScriptEditorCodeEdit&) {}) == "a\nb\n");
        ScriptEditorCodeEdit e;
        CHECK(RoundTrip(e, ok, "validedit", [](ScriptEditorCodeEdit& x) { x.SetCursorPosition(Coord(1, 0)); x.InsertTextUndoable("// hi\n"); }) ==
              "// caf\xC3\xA9 \xE2\x82\xAC\n// hi\nvar x = 1;\n");                        // a clean script is still editable
    }
    {   // a UTF-8 byte order mark: written back unchanged, never edited into the text, and it does not spoil the first token's colour
        const std::string bom = "\xEF\xBB\xBF", body = "var x = 1;\n// note\n";
        ScriptEditorCodeEdit a;
        CHECK(RoundTrip(a, bom + body, "bom", [](ScriptEditorCodeEdit&) {}) == bom + body);
        std::vector<UI::CodeToken> t;
        a.Edit().LineTokens(0, t);
        CHECK(!t.empty() && t[0].start == 0 && t[0].end == 3 && t[0].color == UI::CodeColor_Keyword);   // "var" at column 0 is a keyword
        CHECK(a.Edit().Line(0) == "var x = 1;" && a.GetCursorPosition() == Coord(0, 0));
        ScriptEditorCodeEdit b;
        CHECK(RoundTrip(b, bom + body, "bomedit", [](ScriptEditorCodeEdit& x) { x.SetCursorPosition(Coord(0, 0)); x.InsertTextUndoable("// first\n"); }) ==
              bom + "// first\n" + body);                                                      // edited, and the mark is still the first three bytes
        ScriptEditorCodeEdit c;
        c.SetText(bom + body);
        c.SetText(body);                                                                        // a new script without a mark loses the old script's mark
        CHECK(c.GetText() == body + "\n");
    }
    {   // B1: which of editor/main.cpp's own key helpers (autocomplete popup, bracket pairing, Tab spaces, written around the ImGui
        // editor) may run. With the OkayUI editor none of them: it does each of those itself, so every key does one thing
        for (bool pair : {false, true}) {
            const LegacyKeyHandling okay = PlanLegacyKeyHandling(true, pair);
            CHECK(!okay.autocompleteOwnsKeys && !okay.autoPair && !okay.tabInsertsSpaces);
            const LegacyKeyHandling imgui = PlanLegacyKeyHandling(false, pair);
            CHECK(imgui.autocompleteOwnsKeys && imgui.tabInsertsSpaces && imgui.autoPair == pair);   // the ImGui editor: as before
        }
    }
    {   // each key does one thing in the OkayUI editor, through the real key path (typed text and the Enter key of a frame)
        Rig r;
        r.se.SetAutoPair(true);
        r.se.Edit().Focus(); r.Frames(2);
        r.Keys("foo");
        r.sim.Press(okayuitest::Key_Enter); r.Frame();                       // Enter after an identifier: a new line, nothing else
        CHECK(r.se.Edit().GetText() == "foo\n" && r.se.GetCursorPosition() == Coord(1, 0));
        r.Keys("if (a");                                                     // ( pairs; typing goes on inside it
        CHECK(r.se.Edit().GetText() == "foo\nif (a)" && r.se.GetCursorPosition() == Coord(1, 5));
        r.Keys(")");                                                         // a ) typed over the paired ) steps over it: not doubled
        CHECK(r.se.Edit().GetText() == "foo\nif (a)" && r.se.GetCursorPosition() == Coord(1, 6));
        r.Keys(" {");                                                        // { pairs (what follows is the end of the line)
        CHECK(r.se.Edit().GetText() == "foo\nif (a) {}" && r.se.GetCursorPosition() == Coord(1, 8));
        r.sim.Press(okayuitest::Key_Enter); r.Frame();                       // { then Enter opens the block once
        CHECK(r.se.Edit().GetText() == "foo\nif (a) {\n    \n}" && r.se.GetCursorPosition() == Coord(2, 4) && r.se.GetTotalLines() == 4);
        r.Keys("x");
        CHECK(r.se.Edit().GetText() == "foo\nif (a) {\n    x\n}");
    }
    {   // what the script editor does around the widget that could change a script behind the user's back
        // "Format", "Rename" and "Replace All" hand the whole text back as one undoable insert over a select-all: a script with a byte order mark
        // gets its text with the mark in front, which must not become a second mark (or a character in the first line)
        const std::string bom = "\xEF\xBB\xBF";
        ScriptEditorCodeEdit s;
        s.SetText(bom + "int  x;\n");
        s.SelectAll();
        s.InsertTextUndoable(bom + "int x;\n");
        CHECK(s.GetText() == bom + "int x;\n\n" && s.Edit().Line(0) == "int x;");
        std::vector<UI::CodeToken> t;
        s.Edit().LineTokens(0, t);
        CHECK(!t.empty() && t[0].start == 0 && t[0].color == UI::CodeColor_Keyword);              // the first token is still coloured (int)
        // "Reload from disk" of a file that is not valid UTF-8 into an open, editable script: refused, never repaired into the text
        ScriptEditorCodeEdit r;
        r.SetText("abc");
        r.SelectAll();
        r.InsertTextUndoable("caf\xE9");
        CHECK(r.GetText() == "abc\n" && !r.IsTextChanged());
        CHECK(r.Notice().find("the script was not changed") != std::string::npos);
        r.SetCursorPosition(Coord(0, 3));
        r.InsertTextUndoable("d");                                                                  // the next real edit clears the message
        CHECK(r.GetText() == "abcd\n" && r.Notice().empty());
        r.InsertTextUndoable("a\x1b" "b");                                                         // control characters too
        CHECK(r.GetText() == "abcd\n" && !r.Notice().empty());
        r.InsertTextUndoable("\r\nline");                                                          // CRLF is fine: it becomes LF
        CHECK(r.GetText() == "abcd\nline\n" && r.Notice().empty());
    }
    {   // the samples that ship with the release (examples/script_editor, copied by scripts/build-windows-dist.sh)
        ScriptEditorCodeEdit ok, latin;
        const std::string good = ReadBytes(std::string(OKAYUI_EXAMPLES_DIR) + "/script_editor/highlight_sample.okay");
        const std::string bad = ReadBytes(std::string(OKAYUI_EXAMPLES_DIR) + "/script_editor/latin1_sample.okay");
        CHECK(!good.empty() && !bad.empty());
        ok.SetText(good);
        CHECK(!ok.ReadOnlyBecauseDamaged() && ok.Notice().empty() && !ok.Edit().ReadOnly());
        CHECK(!okay::text::Buffer::IsValidUtf8(bad));                                               // the sample really is not UTF-8 (a byte E9)
        latin.SetText(bad);
        CHECK(latin.ReadOnlyBecauseDamaged() && latin.Edit().ReadOnly());
        CHECK(latin.Notice() == "This file is not valid UTF-8; it is open read-only so nothing is lost.");
        TryEveryEdit(latin);
        CHECK(!latin.IsTextChanged() && latin.GetText() == bad + "\n");                            // what try_it.md says: byte for byte
        std::vector<UI::CodeToken> t;
        latin.Edit().LineTokens(6, t);                                                              // line 7, "void Start()": still coloured
        CHECK(!t.empty() && t[0].color == UI::CodeColor_Keyword);
    }
    {   // the same keys with the setting off: no pairing, no stepping over (the user turned "Auto-close Brackets" off)
        Rig r;
        r.se.SetAutoPair(false);
        r.se.Edit().Focus(); r.Frames(2);
        r.Keys("f(x");
        CHECK(r.se.Edit().GetText() == "f(x");
        r.Keys("){");
        CHECK(r.se.Edit().GetText() == "f(x){");
    }
    TEST_MAIN_RESULT();
}
