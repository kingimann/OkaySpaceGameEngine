// OkayUI::CodeEdit (the multi-line code editor): the editing operations driven directly, then the widget driven headless
// with synthetic mouse and keys (Sim), the highlighter, golden image, and a 100k-line performance check.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <chrono>
#include <cmath>
#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::CodePos;
using UI::CodeEdit;

namespace {

CodePos P(int l, int c) { return CodePos{l, c}; }
bool At(const CodeEdit& e, int l, int c) { return e.Caret() == P(l, c) && !e.HasSelection(); }

// A window with one editor in it.
struct Rig {
    Sim sim;
    CodeEdit ce;
    bool open = true, changed = false;
    float w = 500, h = 300;
    Rig(float W = 640, float H = 400) : sim(W, H) { Reset(); UI::SetClipboardText(""); }
    void Frame() {
        sim.Frame([&] {
            if (UI::Begin("W", 10, 10, sim.W - 20, sim.H - 20, &open)) { if (ce.Draw("ed", w, h)) changed = true; }
            UI::End();
        });
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Key(okayuitest::Key k) { sim.Press(k); Frame(); }
    void Named(UI::KeyId k) { sim.Hold(k, true); Frame(); sim.Hold(k, false); Frame(); }
    void Type(const std::string& s) { sim.Type(s); Frame(); }
    float X(CodePos p) { return ce.PosToScreen(p).x; }
    float Y(CodePos p) { return ce.PosToScreen(p).y + ce.LineHeight() * 0.5f; }
};
void ClickAt(Rig& r, float x, float y) {
    r.sim.Move(x, y); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
}
void ClickPos(Rig& r, CodePos p, float dx = 1.0f) { ClickAt(r, r.X(p) + dx, r.Y(p)); }

// ---------------------------------------------------------------------------------------------
void TextTests() {
    CodeEdit e;
    CHECK(e.LineCount() == 1 && e.GetText().empty() && e.IsEmpty());
    e.SetText("one\ntwo\r\nthree\rfour\n");
    CHECK(e.LineCount() == 5);
    CHECK(e.Line(0) == "one" && e.Line(1) == "two" && e.Line(2) == "three" && e.Line(3) == "four" && e.Line(4).empty());
    CHECK(e.GetText() == "one\ntwo\nthree\nfour\n");
    CHECK(e.Line(-1).empty() && e.Line(99).empty());
    e.SetText(std::string("a\x01" "b\x7f" "c\0d", 7));   // control characters and NUL are dropped
    CHECK(e.GetText() == "abcd");
    e.SetText("ok \xff\xfe bad \xc3\x28 \xe2\x82 \xed\xa0\x80 \xc0\x80 end");   // invalid UTF-8 becomes '?'
    const std::string t = e.GetText();
    CHECK(t.find('\xff') == std::string::npos && t.find("ok ") == 0 && t.find("end") != std::string::npos);
    for (unsigned char c : t) CHECK(c < 0x80 || c >= 0xC2);   // nothing but whole characters left
    e.SetText("h\xc3\xa9llo \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80");   // valid text survives
    CHECK(e.GetText() == "h\xc3\xa9llo \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80");
    e.SetText("x");
    CHECK(At(e, 0, 0) && !e.CanUndo());
    CHECK(e.Clamp(P(5, 5)) == P(0, 1) && e.Clamp(P(-1, 3)) == P(0, 0));
}

void TypingTests() {
    CodeEdit e;
    CHECK(e.InsertText("abc") && e.GetText() == "abc" && At(e, 0, 3));
    CHECK(e.InsertText("\n") && e.LineCount() == 2 && At(e, 1, 0));
    CHECK(e.InsertText("x\ny\r\nz") && e.GetText() == "abc\nx\ny\nz" && At(e, 3, 1));
    CHECK(!e.InsertText("") && !e.InsertText(nullptr));
    e.SetText("hello world");
    e.SetSelection(P(0, 0), P(0, 5));
    CHECK(e.SelectedText() == "hello");
    e.InsertText("bye");
    CHECK(e.GetText() == "bye world" && At(e, 0, 3));
    e.SetSelection(P(0, 9), P(0, 3));                  // backwards selection
    e.InsertText("!");
    CHECK(e.GetText() == "bye!" && At(e, 0, 4));
    e.InsertText("\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80");
    CHECK(e.GetText() == "bye!\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80" && e.Caret().col == 4 + 2 + 3 + 4);
    // a selection over several lines is replaced by one insert
    e.SetText("aaa\nbbb\nccc");
    e.SetSelection(P(0, 1), P(2, 2));
    e.InsertText("X");
    CHECK(e.GetText() == "aXc" && At(e, 0, 2));
    // whole-document replacement
    e.SelectAll(); e.InsertText("new");
    CHECK(e.GetText() == "new" && e.LineCount() == 1);
    // tabs and bad bytes in inserted text
    e.SetText("");
    e.InsertText("a\tb\xff");
    CHECK(e.GetText() == "a\tb?");
}

void DeleteTests() {
    CodeEdit e;
    e.SetText("ab\ncd");
    e.SetCaret(P(1, 0));
    CHECK(e.Backspace() && e.GetText() == "abcd" && At(e, 0, 2));       // joins lines
    CHECK(e.DeleteForward() && e.GetText() == "abd" && At(e, 0, 2));
    e.SetCaret(P(0, 0));
    CHECK(!e.Backspace() && e.GetText() == "abd");                       // at the start: nothing
    e.SetCaret(P(0, 3));
    CHECK(!e.DeleteForward());                                            // at the end: nothing
    e.SetText("a\nb");
    e.SetCaret(P(0, 1));
    CHECK(e.DeleteForward() && e.GetText() == "ab");                      // joins with the next line
    // whole characters
    e.SetText("\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80z");
    e.SetCaret(P(0, 10));
    CHECK(e.Backspace() && e.GetText() == "\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80" && At(e, 0, 9));
    CHECK(e.Backspace() && e.GetText() == "\xc3\xa9\xe6\x97\xa5" && At(e, 0, 5));
    CHECK(e.Backspace() && e.GetText() == "\xc3\xa9" && At(e, 0, 2));
    e.SetCaret(P(0, 0));
    CHECK(e.DeleteForward() && e.GetText().empty());
    // a selection goes first
    e.SetText("one two three");
    e.SetSelection(P(0, 4), P(0, 8));
    CHECK(e.Backspace() && e.GetText() == "one three" && At(e, 0, 4));
    e.SetSelection(P(0, 4), P(0, 9));
    CHECK(e.DeleteForward() && e.GetText() == "one " && At(e, 0, 4));
    // by word
    e.SetText("foo bar_baz  qux");
    e.SetCaret(P(0, 16));
    CHECK(e.Backspace(true) && e.GetText() == "foo bar_baz  ");
    CHECK(e.Backspace(true) && e.GetText() == "foo ");
    CHECK(e.Backspace(true) && e.GetText().empty());
    e.SetText("foo(bar)");
    e.SetCaret(P(0, 0));
    CHECK(e.DeleteForward(true) && e.GetText() == "(bar)");
    CHECK(e.DeleteForward(true) && e.GetText() == "bar)");
    // read-only
    e.SetReadOnly(true);
    CHECK(!e.DeleteForward() && !e.Backspace() && !e.InsertText("x") && !e.NewLine() && !e.Tab() && e.GetText() == "bar)");
    e.SetReadOnly(false);
}

void NewLineTests() {
    CodeEdit e;
    e.SetText("    foo");
    e.SetCaret(P(0, 7));
    CHECK(e.NewLine() && e.GetText() == "    foo\n    " && At(e, 1, 4));       // keeps the indent
    e.SetText("\tfoo");
    e.SetCaret(P(0, 4));
    e.NewLine();
    CHECK(e.GetText() == "\tfoo\n\t" && At(e, 1, 1));
    e.SetText("if (x) {");
    e.SetCaret(P(0, 8));
    e.NewLine();
    CHECK(e.GetText() == "if (x) {\n    " && At(e, 1, 4));                      // one level deeper after '{'
    e.SetText("  f(");
    e.SetCaret(P(0, 4));
    e.NewLine();
    CHECK(e.GetText() == "  f(\n      " && At(e, 1, 6));
    e.SetText("  {}");
    e.SetCaret(P(0, 3));
    e.NewLine();
    CHECK(e.GetText() == "  {\n      \n  }" && At(e, 1, 6));                    // {|} opens a blank line between
    e.SetText("a {  }");
    e.SetCaret(P(0, 4));
    e.NewLine();
    CHECK(e.LineCount() == 3 && e.Line(2).find('}') != std::string::npos);
    e.SetText("  ab");                                                            // inside the indent: only the part before the caret
    e.SetCaret(P(0, 1));
    e.NewLine();
    CHECK(e.GetText() == " \n  ab" && At(e, 1, 1));
    e.SetText("x");
    e.SetAutoIndent(false);
    e.SetText("    x");
    e.SetCaret(P(0, 5));
    e.NewLine();
    CHECK(e.GetText() == "    x\n" && At(e, 1, 0));
    e.SetAutoIndent(true);
    e.SetText("abcdef");
    e.SetSelection(P(0, 2), P(0, 4));
    e.NewLine();
    CHECK(e.GetText() == "ab\nef" && At(e, 1, 0));
    e.SetInsertSpaces(false);
    e.SetText("{");
    e.SetCaret(P(0, 1));
    e.NewLine();
    CHECK(e.GetText() == "{\n\t" && At(e, 1, 1));
    e.SetInsertSpaces(true);
}

void TabTests() {
    CodeEdit e;
    e.SetText("ab");
    e.SetCaret(P(0, 1));
    CHECK(e.Tab() && e.GetText() == "a   b" && At(e, 0, 4));                     // to the next multiple of 4
    e.SetText("");
    e.Tab();
    CHECK(e.GetText() == "    ");
    e.SetInsertSpaces(false);
    e.SetText("");
    e.Tab();
    CHECK(e.GetText() == "\t");
    e.SetInsertSpaces(true);
    // multi-line selection indents every line, empty lines stay empty
    e.SetText("a\n\nb\nc");
    e.SetSelection(P(0, 0), P(2, 1));
    CHECK(e.Tab());
    CHECK(e.GetText() == "    a\n\n    b\nc");
    CHECK(e.Anchor() == P(0, 0) && e.Caret() == P(2, 5));
    CHECK(e.Tab(true));
    CHECK(e.GetText() == "a\n\nb\nc");
    CHECK(e.Anchor() == P(0, 0) && e.Caret() == P(2, 1));
    // a selection that ends at column 0 of a line does not indent that line
    e.SetText("a\nb\nc");
    e.SetSelection(P(0, 0), P(2, 0));
    e.Tab();
    CHECK(e.GetText() == "    a\n    b\nc");
    // Shift+Tab with no selection takes one level off the caret's line; partial indents, tabs
    e.SetText("      x");
    e.SetCaret(P(0, 7));
    CHECK(e.Tab(true) && e.GetText() == "  x" && At(e, 0, 3));
    e.SetText("\tx");
    e.SetCaret(P(0, 2));
    e.Tab(true);
    CHECK(e.GetText() == "x" && At(e, 0, 1));
    e.SetText("x");
    CHECK(!e.Tab(true));                                                          // nothing to remove
    // a one-line selection is replaced by a tab stop, not indented
    e.SetText("abcdef");
    e.SetSelection(P(0, 1), P(0, 3));
    e.Tab();
    CHECK(e.GetText() == "a   def");
    // tabs count to the tab stop
    e.SetText("\tb");
    e.SetCaret(P(0, 1));
    e.Tab();
    CHECK(e.GetText() == "\t    b");
    // undo of an indent is one step
    e.SetText("a\nb");
    e.SelectAll();
    e.Tab();
    e.Undo();
    CHECK(e.GetText() == "a\nb" && e.Anchor() == P(0, 0) && e.Caret() == P(1, 1));
}

void MoveTests() {
    CodeEdit e;
    e.SetText("abc\n\xc3\xa9\xe6\x97\xa5z\n\nlast");
    e.SetCaret(P(0, 3));
    e.Move(UI::CodeMove_Right);
    CHECK(At(e, 1, 0));                                                  // wraps to the next line
    e.Move(UI::CodeMove_Right); CHECK(At(e, 1, 2));                      // over the two-byte e-acute
    e.Move(UI::CodeMove_Right); CHECK(At(e, 1, 5));                      // over the three-byte character
    e.Move(UI::CodeMove_Left); CHECK(At(e, 1, 2));
    e.Move(UI::CodeMove_Left); e.Move(UI::CodeMove_Left); CHECK(At(e, 0, 3));
    e.SetCaret(P(0, 0)); e.Move(UI::CodeMove_Left); CHECK(At(e, 0, 0));  // stays
    e.SetCaret(P(3, 4)); e.Move(UI::CodeMove_Right); CHECK(At(e, 3, 4));
    // up and down keep the column; a short line clips it and the column comes back
    e.SetText("abcdef\nab\nabcdefgh");
    e.SetCaret(P(0, 5));
    e.Move(UI::CodeMove_Down); CHECK(At(e, 1, 2));
    e.Move(UI::CodeMove_Down); CHECK(At(e, 2, 5));
    e.Move(UI::CodeMove_Up); e.Move(UI::CodeMove_Up); CHECK(At(e, 0, 5));
    e.Move(UI::CodeMove_Up); CHECK(At(e, 0, 0));                         // first line: to the start
    e.Move(UI::CodeMove_Down); e.Move(UI::CodeMove_Down); e.Move(UI::CodeMove_Down); CHECK(At(e, 2, 8));   // last line: to the end
    // columns through a tab and a wide character
    e.SetText("\tabc\n12345678\nx\xe6\x97\xa5yz");
    e.SetCaret(P(0, 2));                                                 // after "\ta": column 5
    e.Move(UI::CodeMove_Down); CHECK(At(e, 1, 5));
    e.Move(UI::CodeMove_Down); CHECK(At(e, 2, 6));                       // x, wide char, y: bytes 0 1 4 5 6
    // Home toggles between the first non-blank and column 0; End
    e.SetText("    foo");
    e.SetCaret(P(0, 6));
    e.Move(UI::CodeMove_LineStart); CHECK(At(e, 0, 4));
    e.Move(UI::CodeMove_LineStart); CHECK(At(e, 0, 0));
    e.Move(UI::CodeMove_LineStart); CHECK(At(e, 0, 4));
    e.SetCaret(P(0, 1));
    e.Move(UI::CodeMove_LineStart); CHECK(At(e, 0, 4));
    e.Move(UI::CodeMove_LineEnd); CHECK(At(e, 0, 7));
    e.SetText("x\ny\nz");
    e.Move(UI::CodeMove_DocEnd); CHECK(At(e, 2, 1));
    e.Move(UI::CodeMove_DocStart); CHECK(At(e, 0, 0));
    // words
    e.SetText("foo.bar  baz_1(qux)\nnext");
    e.SetCaret(P(0, 0));
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 3));                  // end of "foo"
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 4));                  // over "."
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 9));                  // "bar" and the blanks
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 14));                 // "baz_1"
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 15));                 // "("
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 18));                 // "qux"
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 0, 19));                 // ")"
    e.Move(UI::CodeMove_WordRight); CHECK(At(e, 1, 0));                  // wraps
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 19));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 18));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 15));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 14));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 9));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 4));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 3));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 0));
    e.Move(UI::CodeMove_WordLeft); CHECK(At(e, 0, 0));
    // shift extends; moving without shift collapses to the edge of the selection
    e.SetText("hello world");
    e.SetCaret(P(0, 2));
    e.Move(UI::CodeMove_Right, true); e.Move(UI::CodeMove_Right, true);
    CHECK(e.Anchor() == P(0, 2) && e.Caret() == P(0, 4) && e.SelectedText() == "ll");
    e.Move(UI::CodeMove_Left, true); e.Move(UI::CodeMove_Left, true); e.Move(UI::CodeMove_Left, true);
    CHECK(e.SelectedText() == "e" && e.Caret() == P(0, 1) && e.SelectionStart() == P(0, 1) && e.SelectionEnd() == P(0, 2));
    e.SetSelection(P(0, 2), P(0, 6));
    e.Move(UI::CodeMove_Left); CHECK(At(e, 0, 2));
    e.SetSelection(P(0, 6), P(0, 2));
    e.Move(UI::CodeMove_Right); CHECK(At(e, 0, 6));
    e.Move(UI::CodeMove_LineEnd, true); e.Move(UI::CodeMove_DocStart, true);
    CHECK(e.Anchor() == P(0, 6) && e.Caret() == P(0, 0));
    // page up and down use the visible line count
    std::string big; for (int i = 0; i < 100; ++i) big += "line " + std::to_string(i) + "\n";
    e.SetText(big);
    e.Move(UI::CodeMove_PageDown); CHECK(e.Caret().line == 19);   // 20 visible lines by default: a page is 19
    e.Move(UI::CodeMove_PageDown); CHECK(e.Caret().line == 38);
    e.Move(UI::CodeMove_PageUp); CHECK(e.Caret().line == 19);
    e.Move(UI::CodeMove_PageUp, true); e.Move(UI::CodeMove_PageUp, true);
    CHECK(e.Caret() == P(0, 0) && e.Anchor().line == 19);
}

void SelectionTests() {
    CodeEdit e;
    e.SetText("foo bar_baz  \n\nqux");
    e.SelectAll();
    CHECK(e.Anchor() == P(0, 0) && e.Caret() == P(2, 3) && e.SelectedText() == e.GetText());
    e.SelectWordAt(P(0, 5)); CHECK(e.SelectedText() == "bar_baz");
    e.SelectWordAt(P(0, 0)); CHECK(e.SelectedText() == "foo");
    e.SelectWordAt(P(0, 3)); CHECK(e.SelectedText() == " ");             // on the blank after the word: that blank
    e.SelectWordAt(P(0, 12)); CHECK(e.SelectedText() == "  ");           // a run of blanks
    e.SelectWordAt(P(1, 0)); CHECK(!e.HasSelection());                   // empty line
    e.SelectLine(0); CHECK(e.SelectedText() == "foo bar_baz  \n");
    e.SelectLine(2); CHECK(e.SelectedText() == "qux" && e.Caret() == P(2, 3));
    e.SelectLine(9); CHECK(e.SelectedText() == "qux");                    // out of range: unchanged
    // positions inside a multi-byte character snap to a boundary
    e.SetText("a\xe6\x97\xa5" "b");
    e.SetCaret(P(0, 2)); CHECK(e.Caret() == P(0, 1));
    e.SetCaret(P(0, 3)); CHECK(e.Caret() == P(0, 1));
    e.SetSelection(P(0, 3), P(0, 99)); CHECK(e.Anchor() == P(0, 1) && e.Caret() == P(0, 5));
    e.SetCaret(P(-4, 2)); CHECK(e.Caret() == P(0, 0));
    e.SetCaret(P(40, 2)); CHECK(e.Caret() == P(0, 5));
    // a multi-line selection and text in between
    e.SetText("ab\ncd\nef");
    e.SetSelection(P(0, 1), P(2, 1));
    CHECK(e.SelectedText() == "b\ncd\ne");
    e.SetSelection(P(2, 1), P(0, 1));
    CHECK(e.SelectedText() == "b\ncd\ne" && e.SelectionStart() == P(0, 1));
}

void ClipboardTests() {
    CodeEdit e;
    UI::SetClipboardText("");
    e.SetText("hello world");
    CHECK(!e.Copy());                                                     // nothing selected
    e.SetSelection(P(0, 0), P(0, 5));
    CHECK(e.Copy() && std::string(UI::GetClipboardText()) == "hello" && e.GetText() == "hello world");
    e.SetCaret(P(0, 11));
    CHECK(e.Paste() && e.GetText() == "hello worldhello" && At(e, 0, 16));
    e.SetSelection(P(0, 0), P(0, 6));
    CHECK(e.Cut() && e.GetText() == "worldhello" && std::string(UI::GetClipboardText()) == "hello ");
    CHECK(!e.Cut());                                                      // no selection
    e.SetText("a\nb");
    e.SelectAll(); e.Copy();
    CHECK(std::string(UI::GetClipboardText()) == "a\nb");
    e.SetText("x");
    e.SetCaret(P(0, 1));
    e.Paste();
    CHECK(e.GetText() == "xa\nb" && At(e, 1, 1));                         // multi-line paste
    UI::SetClipboardText("p\r\nq\rr\x01");
    e.SetText(""); e.Paste();
    CHECK(e.GetText() == "p\nq\nr");                                       // new lines normalised, control characters gone
    UI::SetClipboardText("");
    CHECK(!e.Paste());
    // paste replaces the selection, as one undo step
    UI::SetClipboardText("ZZ");
    e.SetText("abcdef");
    e.SetSelection(P(0, 1), P(0, 5));
    e.Paste();
    CHECK(e.GetText() == "aZZf");
    e.Undo();
    CHECK(e.GetText() == "abcdef" && e.SelectedText() == "bcde");
    // read-only: copy works, cut and paste do not
    e.SetReadOnly(true);
    UI::SetClipboardText("");
    e.SelectAll();
    CHECK(e.Copy() && std::string(UI::GetClipboardText()) == "abcdef");
    CHECK(!e.Cut() && !e.Paste() && e.GetText() == "abcdef");
    e.SetReadOnly(false);
    // a selection too big for the clipboard is not cut (it could not be pasted back)
    e.SetText(std::string(5000, 'x'));
    e.SelectAll();
    UI::SetClipboardText("");
    CHECK(!e.Cut() && e.GetText().size() == 5000);
    CHECK(e.Copy() && std::string(UI::GetClipboardText()).size() < 5000);
}

void UndoTests() {
    CodeEdit e;
    CHECK(!e.Undo() && !e.Redo() && !e.CanUndo());
    // typing a word is one step; a space and the next word are others
    for (char c : std::string("hello")) { char s[2] = {c, 0}; e.InsertText(s); }
    CHECK(e.GetText() == "hello");
    e.InsertText(" ");
    for (char c : std::string("world")) { char s[2] = {c, 0}; e.InsertText(s); }
    CHECK(e.GetText() == "hello world");
    CHECK(e.Undo() && e.GetText() == "hello ");
    CHECK(e.Undo() && e.GetText() == "hello");
    CHECK(e.Undo() && e.GetText().empty() && At(e, 0, 0));
    CHECK(!e.Undo());
    CHECK(e.Redo() && e.GetText() == "hello" && At(e, 0, 5));
    CHECK(e.Redo() && e.GetText() == "hello ");
    CHECK(e.Redo() && e.GetText() == "hello world" && At(e, 0, 11));
    CHECK(!e.Redo());
    // a new edit drops the redo history
    e.Undo(); e.Undo();
    CHECK(e.CanRedo());
    e.InsertText("!");
    CHECK(!e.CanRedo() && e.GetText() == "hello!");
    // moving the caret ends a group
    e.SetText("");
    e.InsertText("a"); e.InsertText("b");
    e.SetCaret(P(0, 1));
    e.InsertText("X");
    CHECK(e.GetText() == "aXb");
    e.Undo(); CHECK(e.GetText() == "ab");
    e.Undo(); CHECK(e.GetText().empty());
    // backspaces group, deletes group, they do not mix
    e.SetText("abcdefgh");
    e.SetCaret(P(0, 8));
    e.Backspace(); e.Backspace(); e.Backspace();
    CHECK(e.GetText() == "abcde");
    e.SetCaret(P(0, 0));
    e.DeleteForward(); e.DeleteForward();
    CHECK(e.GetText() == "cde");
    e.Undo(); CHECK(e.GetText() == "abcde" && At(e, 0, 0));
    e.Undo(); CHECK(e.GetText() == "abcdefgh" && At(e, 0, 8));
    e.Redo(); e.Redo(); CHECK(e.GetText() == "cde");
    // Enter is its own step and restores the caret
    e.SetText("ab");
    e.SetCaret(P(0, 2));
    e.InsertText("c");
    e.NewLine();
    e.InsertText("d");
    CHECK(e.GetText() == "abc\nd");
    e.Undo(); CHECK(e.GetText() == "abc\n" && At(e, 1, 0));
    e.Undo(); CHECK(e.GetText() == "abc" && At(e, 0, 3));
    e.Undo(); CHECK(e.GetText() == "ab" && At(e, 0, 2));
    // typing over a selection restores the selection
    e.SetText("hello");
    e.SetSelection(P(0, 1), P(0, 4));
    e.InsertText("a");
    CHECK(e.GetText() == "hao");
    e.Undo();
    CHECK(e.GetText() == "hello" && e.Anchor() == P(0, 1) && e.Caret() == P(0, 4));
    e.Redo();
    CHECK(e.GetText() == "hao" && At(e, 0, 2));
    // BreakUndoGroup, and the history is dropped by SetText
    e.SetText("");
    e.InsertText("a"); e.BreakUndoGroup(); e.InsertText("b");
    e.Undo(); CHECK(e.GetText() == "a");
    e.SetText("zz");
    CHECK(!e.CanUndo() && !e.CanRedo());
    // undo across lines (join, split) and UTF-8
    e.SetText("one\ntwo\nthree");
    e.SetSelection(P(0, 2), P(2, 2));
    e.InsertText("\xe6\x97\xa5");
    CHECK(e.GetText() == "on\xe6\x97\xa5" "ree");
    e.Undo();
    CHECK(e.GetText() == "one\ntwo\nthree");
    e.Redo();
    CHECK(e.GetText() == "on\xe6\x97\xa5" "ree" && e.LineCount() == 1);
    // the limit on the history does not break anything
    e.SetText("");
    for (int i = 0; i < 3000; ++i) { e.InsertText("x\n"); }
    CHECK(e.LineCount() == 3001);
    int undone = 0; while (e.Undo()) ++undone;
    CHECK(undone == 2000 && e.LineCount() == 1001);
    // read-only blocks everything and records nothing
    e.SetText("a");
    e.SetReadOnly(true);
    e.InsertText("b");
    CHECK(!e.CanUndo());
    e.SetReadOnly(false);
}

void FindTests() {
    CodeEdit e;
    e.SetText("Foo foo\nbar FOO\nfoofoo");
    CHECK(!e.FindNext() && !e.FindPrev() && e.FindCount() == 0);          // no needle
    e.SetFind("foo");
    CHECK(e.FindCount() == 5 && e.FindText() == "foo");
    CHECK(e.FindNext() && e.Anchor() == P(0, 0) && e.Caret() == P(0, 3));
    CHECK(e.FindNext() && e.Anchor() == P(0, 4));
    CHECK(e.FindNext() && e.Anchor() == P(1, 4));
    CHECK(e.FindNext() && e.Anchor() == P(2, 0));
    CHECK(e.FindNext() && e.Anchor() == P(2, 3));                          // overlapping starts are not skipped past the previous match's end
    CHECK(e.FindNext() && e.Anchor() == P(0, 0));                          // wraps
    CHECK(e.FindPrev() && e.Anchor() == P(2, 3));                          // wraps backwards
    CHECK(e.FindPrev() && e.Anchor() == P(2, 0));
    CHECK(e.FindPrev() && e.Anchor() == P(1, 4));
    e.SetFind("foo", true);
    CHECK(e.FindCount() == 3);
    e.SetFind("FOO", true);
    CHECK(e.FindCount() == 1 && e.FindNext() && e.Anchor() == P(1, 4));
    e.SetFind("nothing here");
    CHECK(!e.FindNext() && !e.FindPrev() && e.FindCount() == 0);
    e.SetFind("a\nb");                                    // across lines (more in MultiLineFindTests)
    CHECK(e.FindText() == "a\nb");
    // find starts from the caret when nothing is selected
    e.SetText("ab ab ab");
    e.SetFind("ab");
    e.SetCaret(P(0, 4));
    CHECK(e.FindNext() && e.Anchor() == P(0, 6));
    e.SetCaret(P(0, 4));
    CHECK(e.FindPrev() && e.Anchor() == P(0, 3));
    // UTF-8 needles
    e.SetText("x\xe6\x97\xa5y\xe6\x97\xa5");
    e.SetFind("\xe6\x97\xa5");
    CHECK(e.FindCount() == 2 && e.FindNext() && e.Anchor() == P(0, 1) && e.Caret() == P(0, 4));
    // replace
    e.SetText("cat dog cat\ncat");
    e.SetFind("cat");
    CHECK(!e.ReplaceCurrent("bird"));                                       // not on a match yet
    e.FindNext();
    CHECK(e.ReplaceCurrent("bird") && e.GetText() == "bird dog cat\ncat" && e.SelectedText() == "cat");
    CHECK(e.ReplaceAll("lion") == 2 && e.GetText() == "bird dog lion\nlion");
    e.Undo();
    CHECK(e.GetText() == "bird dog cat\ncat");                              // ReplaceAll is one step
    e.SetText("aaa");
    e.SetFind("a");
    CHECK(e.ReplaceAll("bb") == 3 && e.GetText() == "bbbbbb");
    CHECK(e.ReplaceAll("") == 0);
    e.SetText("a a");
    e.SetFind("a");
    CHECK(e.ReplaceAll("x\ny") == 2 && e.GetText() == "x\ny x\ny" && e.LineCount() == 3);
    e.Undo(); CHECK(e.GetText() == "a a");
    // overlapping matches: "aa" in "aaaa" is replaced left to right without reusing characters; the whole thing is one undo step
    e.SetText("aaaa");
    e.SetFind("aa");
    CHECK(e.ReplaceAll("b") == 2 && e.GetText() == "bb");
    e.Undo(); CHECK(e.GetText() == "aaaa" && !e.CanUndo());
    e.SetText("aaa");
    CHECK(e.ReplaceAll("b") == 1 && e.GetText() == "ba");
    // replacing with text that contains the needle must not loop
    e.SetText("ab");
    e.SetFind("a");
    CHECK(e.ReplaceAll("aa") == 1 && e.GetText() == "aab");
    // case-insensitive replace-all, then the caret/selection is on a valid position
    e.SetText("Foo FOO foo");
    e.SetFind("foo");
    CHECK(e.ReplaceAll("x") == 3 && e.GetText() == "x x x" && e.Clamp(e.Caret()) == e.Caret());
    e.SetReadOnly(true);
    CHECK(e.ReplaceAll("z") == 0 && !e.ReplaceCurrent("z"));
}

void HighlightTests() {
    CodeEdit e;
    e.SetLanguage(UI::OkayScriptLanguage());
    auto tokens = [&](int line) { std::vector<UI::CodeToken> t; e.LineTokens(line, t); return t; };
    auto colorAt = [&](int line, int col) {
        for (const UI::CodeToken& t : tokens(line)) if (col >= t.start && col < t.end) return (int)t.color;
        return (int)UI::CodeColor_Default;
    };
    e.SetText("if (x > 10) { return \"hi\\\"there\"; } // done\nvar f = 3.5e-2f + 0xFF;\n'a' '\\n' Vector3 foo.bar(1) # hash\nstr = \"#fff\"");
    CHECK(colorAt(0, 0) == UI::CodeColor_Keyword && colorAt(0, 1) == UI::CodeColor_Keyword);
    CHECK(colorAt(0, 2) == UI::CodeColor_Default);                          // space
    CHECK(colorAt(0, 3) == UI::CodeColor_Punct);                            // (
    CHECK(colorAt(0, 4) == UI::CodeColor_Default);                          // x
    CHECK(colorAt(0, 8) == UI::CodeColor_Number && colorAt(0, 9) == UI::CodeColor_Number);
    CHECK(colorAt(0, 14) == UI::CodeColor_Keyword);                         // return
    CHECK(colorAt(0, 21) == UI::CodeColor_String && colorAt(0, 25) == UI::CodeColor_String && colorAt(0, 31) == UI::CodeColor_String);   // the escaped quote stays inside the string
    CHECK(colorAt(0, 32) == UI::CodeColor_Punct);                           // ;
    CHECK(colorAt(0, 36) == UI::CodeColor_Comment && colorAt(0, 42) == UI::CodeColor_Comment);
    CHECK(colorAt(1, 0) == UI::CodeColor_Keyword);
    CHECK(colorAt(1, 8) == UI::CodeColor_Number && colorAt(1, 14) == UI::CodeColor_Number && colorAt(1, 15) == UI::CodeColor_Default);   // 3.5e-2f
    CHECK(colorAt(1, 18) == UI::CodeColor_Number && colorAt(1, 21) == UI::CodeColor_Number && colorAt(1, 22) == UI::CodeColor_Punct);     // 0xFF;
    CHECK(colorAt(2, 0) == UI::CodeColor_Char && colorAt(2, 2) == UI::CodeColor_Char);
    CHECK(colorAt(2, 4) == UI::CodeColor_Char && colorAt(2, 7) == UI::CodeColor_Char);
    CHECK(colorAt(2, 9) == UI::CodeColor_Type);                             // Vector3
    CHECK(colorAt(2, 17) == UI::CodeColor_Default && colorAt(2, 20) == UI::CodeColor_Punct && colorAt(2, 21) == UI::CodeColor_Function);   // foo.bar(
    CHECK(colorAt(2, 25) == UI::CodeColor_Number);
    CHECK(colorAt(2, 28) == UI::CodeColor_Comment && colorAt(2, 33) == UI::CodeColor_Comment);   // '#' comment, OkayScript
    CHECK(colorAt(3, 6) == UI::CodeColor_String && colorAt(3, 10) == UI::CodeColor_String);   // "#fff" stays a string
    // block comments carry over lines
    e.SetText("a /* start\nstill comment\nend */ b\nc");
    CHECK(colorAt(0, 0) == UI::CodeColor_Default && colorAt(0, 2) == UI::CodeColor_Comment);
    CHECK(colorAt(1, 0) == UI::CodeColor_Comment && colorAt(1, 12) == UI::CodeColor_Comment);
    CHECK(colorAt(2, 0) == UI::CodeColor_Comment && colorAt(2, 5) == UI::CodeColor_Comment && colorAt(2, 6) == UI::CodeColor_Default && colorAt(2, 7) == UI::CodeColor_Default);
    CHECK(colorAt(3, 0) == UI::CodeColor_Default);
    // editing the first line re-highlights the ones below
    e.SetCaret(P(0, 0));
    e.InsertText("//");                                                      // the block comment no longer starts (line comment first)
    CHECK(colorAt(1, 0) == UI::CodeColor_Default);
    e.Undo();
    CHECK(colorAt(1, 0) == UI::CodeColor_Comment);
    // an unterminated string ends at the end of the line
    e.SetText("s = \"open\nnext");
    CHECK(colorAt(0, 4) == UI::CodeColor_String && colorAt(1, 0) == UI::CodeColor_Default);
    // known words and C-like language
    UI::CodeLanguage lang = UI::CLikeLanguage();
    lang.known.insert("printf");
    e.SetLanguage(lang);
    e.SetText("printf(\"%d\", x); // c\n#include <a>\nint main() { return 0; }");
    CHECK(colorAt(0, 0) == UI::CodeColor_Known && colorAt(0, 7) == UI::CodeColor_String && colorAt(0, 20) == UI::CodeColor_Comment);
    CHECK(colorAt(2, 0) == UI::CodeColor_Type && colorAt(2, 4) == UI::CodeColor_Function && colorAt(2, 13) == UI::CodeColor_Keyword);
    CHECK(colorAt(1, 0) == UI::CodeColor_Default && colorAt(1, 9) == UI::CodeColor_Punct);   // '#' is not a comment in the C-like language
    // no highlighter: no tokens
    e.SetHighlighter(nullptr);
    CHECK(tokens(0).empty());
    // a custom highlighter: called once per line from the top, states cached, invalidated by edits
    int calls = 0;
    e.SetHighlighter([&](const char* s, int n, int state, std::vector<UI::CodeToken>& out) {
        ++calls;
        if (n > 0 && s[0] == '!') out.push_back(UI::CodeToken{0, n, UI::CodeColor_Keyword});
        return state + 1;                                                    // the state counts lines
    });
    std::string many; for (int i = 0; i < 50; ++i) many += "!line\n";
    e.SetText(many);
    calls = 0;
    tokens(30);
    CHECK(calls == 31);                                                      // 30 for the states plus the line itself
    calls = 0;
    tokens(20); tokens(30);
    CHECK(calls == 2);                                                       // cached
    e.SetCaret(P(25, 0)); e.InsertText("z");
    calls = 0;
    tokens(30);
    CHECK(calls == 5 + 1);                                                   // from the edited line down to 30: the states of 25..29, then the line
    CHECK(tokens(3).size() == 1 && tokens(3)[0].color == UI::CodeColor_Keyword && tokens(25).empty());
}

void BracketTests() {
    CodeEdit e;
    e.SetLanguage(UI::OkayScriptLanguage());
    CodePos a, b;
    e.SetText("f(a, [b], {c})");
    e.SetCaret(P(0, 1));                                                     // before '('
    CHECK(e.MatchBracket(a, b) && a == P(0, 1) && b == P(0, 13));
    e.SetCaret(P(0, 2));                                                     // after '('
    CHECK(e.MatchBracket(a, b) && a == P(0, 1) && b == P(0, 13));
    e.SetCaret(P(0, 14));                                                    // after ')'
    CHECK(e.MatchBracket(a, b) && a == P(0, 13) && b == P(0, 1));
    e.SetCaret(P(0, 5));                                                     // before '['
    CHECK(e.MatchBracket(a, b) && a == P(0, 5) && b == P(0, 7));
    e.SetCaret(P(0, 0));
    CHECK(!e.MatchBracket(a, b));                                            // 'f' is no bracket
    e.SetText("(((x)))");
    e.SetCaret(P(0, 1));
    CHECK(e.MatchBracket(a, b) && b == P(0, 5));
    e.SetCaret(P(0, 0));
    CHECK(e.MatchBracket(a, b) && b == P(0, 6));
    // over lines, other kinds in between are ignored
    e.SetText("if (a) {\n  b[0] = (1);\n}\nend");
    e.SetCaret(P(0, 7));
    CHECK(e.MatchBracket(a, b) && a == P(0, 7) && b == P(2, 0));
    e.SetCaret(P(2, 1));
    CHECK(e.MatchBracket(a, b) && a == P(2, 0) && b == P(0, 7));
    // unbalanced
    e.SetText("(a");
    e.SetCaret(P(0, 0));
    CHECK(!e.MatchBracket(a, b));
    e.SetText("a)");
    e.SetCaret(P(0, 2));
    CHECK(!e.MatchBracket(a, b));
    // brackets in strings and comments do not count, and a bracket inside one has no partner
    e.SetText("f(\")\", ')', /* ) */ x) // )\n)");
    e.SetCaret(P(0, 1));
    CHECK(e.MatchBracket(a, b) && b == P(0, 21));
    e.SetCaret(P(0, 3));                                                     // the ')' inside the string
    CHECK(!e.MatchBracket(a, b));
    // the answer follows edits
    e.SetText("(x)");
    e.SetCaret(P(0, 0));
    CHECK(e.MatchBracket(a, b) && b == P(0, 2));
    e.SetCaret(P(0, 2)); e.InsertText("y");
    e.SetCaret(P(0, 0));
    CHECK(e.MatchBracket(a, b) && b == P(0, 3));
    // without a highlighter all brackets count
    e.SetHighlighter(nullptr);
    e.SetText("(\")\")");
    e.SetCaret(P(0, 0));
    CHECK(e.MatchBracket(a, b) && b == P(0, 2));
}

void MarkerTests() {
    CodeEdit e;
    e.SetText("a\nb\nc\nd\ne");
    e.SetBreakpoints({3, 1, 1, -4, 3});
    CHECK(e.Breakpoints() == std::vector<int>({1, 3}));
    CHECK(e.HasBreakpoint(1) && !e.HasBreakpoint(2));
    e.ToggleBreakpoint(2); e.ToggleBreakpoint(1);
    CHECK(e.Breakpoints() == std::vector<int>({2, 3}));
    e.ToggleBreakpoint(-1);
    CHECK(e.Breakpoints() == std::vector<int>({2, 3}));
    // they move with the text
    e.SetCaret(P(0, 1));
    e.InsertText("\n");                                                       // a line inserted above
    CHECK(e.Breakpoints() == std::vector<int>({3, 4}));
    e.SetSelection(P(0, 1), P(1, 0));                                         // and removed again
    e.InsertText("");
    e.Backspace();
    CHECK(e.Breakpoints() == std::vector<int>({2, 3}));
    e.SetText("a\nb\nc\nd\ne");
    e.SetBreakpoints({1, 2, 4});
    e.SetMarkers({UI::CodeMarker{2, 0, -1, "err", 0}, UI::CodeMarker{4, 0, 1, "warn", 0}});
    e.SetSelection(P(1, 0), P(3, 0));                                         // delete lines 1 and 2
    e.Backspace();
    CHECK(e.LineCount() == 3 && e.GetText() == "a\nd\ne");
    CHECK(e.Breakpoints() == std::vector<int>({1, 2}));                       // line 2 was removed, 4 moved to 2, 1 is where the cut began
    CHECK(e.Markers().size() == 1 && e.Markers()[0].line == 2);
    e.Undo();
    CHECK(e.LineCount() == 5);
    e.ClearMarkers();
    CHECK(e.Markers().empty());
    // SetText drops breakpoints past the end
    e.SetBreakpoints({0, 10});
    e.SetText("one\ntwo");
    CHECK(e.Breakpoints() == std::vector<int>({0}));
}

// ---------------------------------------------------------------------------------------------
// The widget, driven by synthetic input
// ---------------------------------------------------------------------------------------------
void WidgetTests() {
    {   // geometry and focus
        Rig r;
        r.ce.SetText("hello\nworld");
        r.Frames(2);
        CHECK(r.ce.LineHeight() >= 16 && r.ce.CharWidth() >= 8);
        CHECK(r.ce.ItemRect().w == 500 && r.ce.ItemRect().h == 300);
        CHECK(r.ce.GutterWidth() > 0 && r.ce.ViewRect().x > r.ce.ItemRect().x);
        CHECK(!r.ce.Focused());
        r.sim.Type("zzz"); r.Frame();
        CHECK(r.ce.GetText() == "hello\nworld");                              // not focused: typing does nothing
        CHECK(!UI::WantsTextInput());
        ClickPos(r, P(0, 3));
        CHECK(r.ce.Focused() && UI::WantsTextInput());
        CHECK(At(r.ce, 0, 3));
        r.Type("X");
        CHECK(r.ce.GetText() == "helXlo\nworld" && At(r.ce, 0, 4) && r.changed);
        ClickAt(r, 600, 380);                                                  // click outside the editor: focus goes
        CHECK(!r.ce.Focused());
        r.Type("Q");
        CHECK(r.ce.GetText() == "helXlo\nworld");
    }
    {   // mouse: click, shift+click, drag, double and triple click, clicks past the ends
        Rig r;
        r.ce.SetText("alpha beta\n\tgamma\n\xc3\xa9\xe6\x97\xa5z");
        r.Frames(2);
        ClickPos(r, P(0, 6));
        CHECK(At(r.ce, 0, 6));
        ClickPos(r, P(1, 3));                                                  // 'g' after the tab: click on the cell edge
        CHECK(At(r.ce, 1, 3));
        r.sim.shift = true;
        ClickPos(r, P(0, 2));
        r.sim.shift = false;
        CHECK(r.ce.Anchor() == P(1, 3) && r.ce.Caret() == P(0, 2));          // extended from the old caret
        ClickAt(r, r.X(P(0, 0)) + 400, r.Y(P(0, 0)));                         // far to the right: end of line
        CHECK(At(r.ce, 0, 10));
        ClickAt(r, r.X(P(0, 0)) + 5, r.ce.ViewRect().y + 250);                // below the text: last line
        CHECK(r.ce.Caret().line == 2);
        ClickPos(r, P(2, 2));                                                  // between the two characters
        CHECK(At(r.ce, 2, 2));
        ClickPos(r, P(2, 5), -3);                                              // a little left of the cell edge: still that boundary
        CHECK(At(r.ce, 2, 5));
        // drag
        const UI::Vec2 a{r.X(P(0, 1)) + 1, r.Y(P(0, 1))}, b{r.X(P(1, 4)) + 1, r.Y(P(1, 4))};
        r.sim.Move(a.x, a.y); r.Frame(); r.sim.Down(); r.Frame();
        r.sim.Move(b.x, b.y); r.Frame();
        CHECK(r.ce.Anchor() == P(0, 1) && r.ce.Caret() == P(1, 4));
        r.sim.Move(r.X(P(0, 5)) + 1, r.Y(P(0, 5))); r.Frame();
        CHECK(r.ce.Caret() == P(0, 5) && r.ce.SelectedText() == "lpha");
        r.sim.Up(); r.Frame();
        r.sim.Move(b.x, b.y); r.Frame();
        CHECK(r.ce.Caret() == P(0, 5));                                        // released: moving does not select
        // double click: word; triple: line
        r.ce.SetCaret(P(0, 0)); r.Frames(1);
        r.sim.Move(r.X(P(0, 7)) + 1, r.Y(P(0, 7))); r.Frame();
        r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame();
        CHECK(r.ce.SelectedText() == "beta");
        r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame();
        CHECK(r.ce.SelectedText() == "alpha beta\n");
        r.sim.Up(); r.Frame();
        for (int i = 0; i < 40; ++i) r.Frame();                                // time passes: a new click is a single click again
        ClickPos(r, P(0, 7));
        CHECK(At(r.ce, 0, 7));
    }
    {   // keyboard: arrows, Home/End, Enter, Backspace, Delete, Ctrl+A, Tab, undo
        Rig r;
        r.ce.SetText("one two\nthree");
        r.Frames(2);
        ClickPos(r, P(0, 0));
        r.Key(Key_Right); r.Key(Key_Right);
        CHECK(At(r.ce, 0, 2));
        r.sim.ctrl = true; r.Key(Key_Right); CHECK(At(r.ce, 0, 4)); r.Key(Key_Right); CHECK(At(r.ce, 0, 7)); r.Key(Key_Left); CHECK(At(r.ce, 0, 4)); r.sim.ctrl = false;
        r.Key(Key_Down); CHECK(At(r.ce, 1, 4));
        r.Key(Key_Up); CHECK(At(r.ce, 0, 4));
        r.sim.shift = true; r.Key(Key_End); r.sim.shift = false;
        CHECK(r.ce.SelectedText() == "two");
        r.Key(Key_Home); CHECK(At(r.ce, 0, 0));
        r.sim.ctrl = true; r.Key(Key_End); CHECK(At(r.ce, 1, 5)); r.Key(Key_Home); CHECK(At(r.ce, 0, 0)); r.sim.ctrl = false;
        r.Key(Key_SelectAll);
        CHECK(r.ce.SelectedText() == "one two\nthree");
        r.Key(Key_Backspace);
        CHECK(r.ce.GetText().empty());
        r.Type("ab"); r.Key(Key_Enter); r.Type("cd");
        CHECK(r.ce.GetText() == "ab\ncd" && At(r.ce, 1, 2));
        r.Key(Key_Left); r.Key(Key_Delete);
        CHECK(r.ce.GetText() == "ab\nc");
        r.Key(Key_Home);
        r.Named(UI::KeyId_Tab);
        CHECK(r.ce.GetText() == "ab\n    c");
        r.sim.shift = true; r.Named(UI::KeyId_Tab); r.sim.shift = false;
        CHECK(r.ce.GetText() == "ab\nc");
        r.sim.ctrl = true; r.Named(UI::KeyId_Z); r.sim.ctrl = false;           // undo the outdent
        CHECK(r.ce.GetText() == "ab\n    c");
        r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_Z); r.sim.shift = false; r.sim.ctrl = false;   // redo
        CHECK(r.ce.GetText() == "ab\nc");
        r.sim.ctrl = true; r.Named(UI::KeyId_Z); r.Named(UI::KeyId_Z); r.Named(UI::KeyId_Y); r.sim.ctrl = false;
        CHECK(r.ce.GetText() == "ab\n    c");
        r.Key(Key_Escape);                                                     // no selection: nothing happens
        r.Key(Key_End);
        // typed text with a control key held is not text; the host's characters with newlines are filtered
        r.sim.ctrl = true; r.Type("q"); r.sim.ctrl = false;
        CHECK(r.ce.GetText() == "ab\n    c");
        r.Type("x\ny\tz");
        CHECK(r.ce.GetText() == "ab\n    cxyz");
        // the clipboard keys
        r.Key(Key_SelectAll); r.Key(Key_Cut);
        CHECK(r.ce.GetText().empty() && std::string(UI::GetClipboardText()) == "ab\n    cxyz");
        r.Key(Key_Paste);
        CHECK(r.ce.GetText() == "ab\n    cxyz");
        r.Key(Key_SelectAll); r.Key(Key_Copy); r.Key(Key_Right);
        CHECK(At(r.ce, 1, 8) && r.ce.GetText() == "ab\n    cxyz");
        // read-only: typing and deleting do nothing, moving and copying work
        r.ce.SetReadOnly(true);
        r.Type("zz"); r.Key(Key_Backspace); r.Key(Key_Enter);
        CHECK(r.ce.GetText() == "ab\n    cxyz");
        r.Key(Key_Home);
        CHECK(r.ce.Caret().line == 1 && r.ce.Caret().col == 4);   // Home goes to the first non-blank
    }
    {   // PageUp/PageDown, scrolling follows the caret, wheel, horizontal scroll
        Rig r;
        std::string big; for (int i = 0; i < 200; ++i) big += "line " + std::to_string(i) + "\n";
        r.ce.SetText(big);
        r.Frames(2);
        ClickPos(r, P(0, 0));
        CHECK(r.ce.ScrollY() == 0 && r.ce.FirstVisibleLine() == 0);
        const int vis = r.ce.VisibleLineCount();
        CHECK(vis > 10 && vis < 25);
        r.Named(UI::KeyId_PageDown);
        CHECK(r.ce.Caret().line == vis - 1);
        r.Named(UI::KeyId_PageDown);
        CHECK(r.ce.Caret().line == 2 * (vis - 1));
        const float py = r.ce.PosToScreen(r.ce.Caret()).y;
        CHECK(py >= r.ce.ViewRect().y - 1 && py + r.ce.LineHeight() <= r.ce.ViewRect().y + r.ce.ViewRect().h + 1);   // caret in view
        r.sim.ctrl = true; r.Key(Key_End); r.sim.ctrl = false;
        CHECK(r.ce.Caret().line == 200);
        const float py2 = r.ce.PosToScreen(r.ce.Caret()).y;
        CHECK(py2 >= r.ce.ViewRect().y - 1 && py2 + r.ce.LineHeight() <= r.ce.ViewRect().y + r.ce.ViewRect().h + 1);
        CHECK(r.ce.FirstVisibleLine() > 150);
        r.sim.ctrl = true; r.Key(Key_Home); r.sim.ctrl = false;
        CHECK(r.ce.ScrollY() == 0 && r.ce.FirstVisibleLine() == 0);
        // wheel over the editor scrolls 3 lines per notch; the caret stays where it is
        r.sim.Move(r.ce.ViewRect().x + 50, r.ce.ViewRect().y + 50); r.Frame();
        r.sim.wheel = -2; r.Frame();
        CHECK_NEAR(r.ce.ScrollY(), 6 * r.ce.LineHeight(), 0.01);
        CHECK(At(r.ce, 0, 0));
        r.sim.wheel = 1; r.Frame();
        CHECK_NEAR(r.ce.ScrollY(), 3 * r.ce.LineHeight(), 0.01);
        r.sim.wheel = 100; r.Frame();
        CHECK(r.ce.ScrollY() == 0);                                            // clamped at the top
        r.sim.wheel = -1000; r.Frame();
        CHECK(r.ce.ScrollY() > 0 && r.ce.FirstVisibleLine() >= 200 - vis - 1);   // clamped at the bottom
        // wheel when the pointer is elsewhere does nothing
        const float sy = r.ce.ScrollY();
        r.sim.Move(620, 390); r.Frame();
        r.sim.wheel = 5; r.Frame();
        CHECK(r.ce.ScrollY() == sy);
    }
    {   // horizontal scrolling follows the caret along a long line
        Rig r;
        r.ce.SetText(std::string(300, 'x') + "\nshort");
        r.Frames(2);
        ClickPos(r, P(0, 0));
        CHECK(r.ce.ScrollX() == 0);
        r.Key(Key_End);
        CHECK(r.ce.ScrollX() > 0);
        const float cx = r.ce.PosToScreen(r.ce.Caret()).x;
        CHECK(cx >= r.ce.ViewRect().x && cx < r.ce.ViewRect().x + r.ce.ViewRect().w);
        r.Key(Key_Home);
        CHECK(r.ce.ScrollX() == 0);
        r.sim.shift = true;
        r.sim.Move(r.ce.ViewRect().x + 30, r.ce.ViewRect().y + 20); r.Frame();
        r.sim.wheel = -2; r.Frame();                                           // shift+wheel scrolls sideways
        r.sim.shift = false;
        CHECK_NEAR(r.ce.ScrollX(), 6 * r.ce.CharWidth(), 0.01);
        r.ce.SetScroll(1e9f, 1e9f); r.Frame();
        CHECK(r.ce.ScrollX() < 300 * r.ce.CharWidth() + 100 && r.ce.ScrollY() == 0);   // clamped (two lines fit, so no vertical scroll)
        r.ce.SetScroll(-5, -5); r.Frame();
        CHECK(r.ce.ScrollX() == 0 && r.ce.ScrollY() == 0);
        // dragging the horizontal scroll bar thumb
        const UI::Rect tr = r.ce.ViewRect();
        const float hy = tr.y + tr.h + 5, hx = tr.x + 10;
        r.sim.Move(hx, hy); r.Frame(); r.sim.Down(); r.Frame();
        r.sim.Move(hx + 60, hy); r.Frame();
        CHECK(r.ce.ScrollX() > 0);
        r.sim.Up(); r.Frame();
    }
    {   // vertical scroll bar: thumb drag, track click
        Rig r;
        std::string big; for (int i = 0; i < 300; ++i) big += "l\n";
        r.ce.SetText(big);
        r.Frames(2);
        const UI::Rect v = r.ce.ViewRect();
        const float bx = v.x + v.w + 5;
        r.sim.Move(bx, v.y + 3); r.Frame(); r.sim.Down(); r.Frame();
        r.sim.Move(bx, v.y + 103); r.Frame();
        const float s1 = r.ce.ScrollY();
        CHECK(s1 > 0);
        r.sim.Move(bx, v.y + v.h + 500); r.Frame();
        CHECK(r.ce.ScrollY() >= s1);
        r.sim.Up(); r.Frame();
        r.ce.SetScroll(0, 0); r.Frame();
        r.sim.Move(bx, v.y + v.h - 3); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();   // click in the track below the thumb: a page down
        CHECK_NEAR(r.ce.ScrollY(), v.h, 0.01);
    }
    {   // breakpoint gutter
        Rig r;
        r.ce.SetText("a\nb\nc");
        std::vector<std::pair<int, bool>> calls;
        r.ce.SetBreakpointCallback([&](int l, bool s) { calls.push_back({l, s}); });
        r.Frames(2);
        const float gx = r.ce.ItemRect().x + 4;
        ClickAt(r, gx, r.Y(P(1, 0)));
        CHECK(calls.size() == 1 && calls[0].first == 1 && calls[0].second && r.ce.HasBreakpoint(1));
        ClickAt(r, gx, r.Y(P(1, 0)));
        CHECK(calls.size() == 2 && !calls[1].second && !r.ce.HasBreakpoint(1));
        ClickAt(r, gx, r.Y(P(0, 0)) + 5 * r.ce.LineHeight());                  // below the last line: nothing
        CHECK(calls.size() == 2);
        // the line-number area selects the line
        ClickAt(r, r.ce.ViewRect().x - 4, r.Y(P(2, 0)));
        CHECK(r.ce.SelectedText() == "c");
    }
}

void ExtraTests() {
    CodeEdit e;
    e.SetUserTypes({"Orphan"});                                                // no language yet: ignored
    e.SetLanguage(UI::OkayScriptLanguage());
    e.SetText("Enemy x; Vector3 y;");
    std::vector<UI::CodeToken> t;
    e.LineTokens(0, t);
    CHECK(t.empty() || t[0].color != UI::CodeColor_Type);
    e.SetUserTypes({"Enemy"});
    e.LineTokens(0, t);
    CHECK(!t.empty() && t[0].start == 0 && t[0].end == 5 && t[0].color == UI::CodeColor_Type);
    e.SetText("ab\nxyz");
    CHECK(e.LineLength(0) == 2 && e.LineLength(1) == 3 && e.LineLength(7) == 0);
    Rig r;
    r.ce.SetText("hello");
    r.Frames(2);
    ClickPos(r, P(0, 2));
    r.ce.SetHandleKeyboardInput(false);
    r.Type("zz"); r.Key(Key_Backspace);
    CHECK(r.ce.GetText() == "hello" && r.ce.Focused());                        // keys left to the host
    ClickPos(r, P(0, 4));
    CHECK(At(r.ce, 0, 4));                                                      // the mouse still works
    r.ce.SetHandleKeyboardInput(true);
    r.Type("!");
    CHECK(r.ce.GetText() == "hell!o");
    const UI::Vec2 below = r.ce.CaretScreenPosBelow(), at = r.ce.PosToScreen(r.ce.Caret());
    CHECK(below.x == at.x && below.y == at.y + r.ce.LineHeight());
}

// ---------------------------------------------------------------------------------------------
// What is drawn
// ---------------------------------------------------------------------------------------------
unsigned C32(UI::Color32 c) { return Rgba((int)(c & 255), (int)((c >> 8) & 255), (int)((c >> 16) & 255), (int)((c >> 24) & 255)); }

const char* kSample =
    "// OkayScript sample\n"
    "class Player {\n"
    "    var speed = 4.5;\n"
    "    function Update(dt) {\n"
    "        if (Input.GetKey(\"left\") && speed > 0) {\n"
    "            transform.position.x -= speed * dt; /* move */\n"
    "        }\n"
    "        Debug.Log('x', \"done\");   # note\n"
    "    }\n"
    "}\n";

void DrawTests() {
    Rig r(560, 340);
    r.w = 520; r.h = 300;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetText(kSample);
    r.ce.SetBreakpoints({2});
    r.ce.SetMarkers({UI::CodeMarker{4, 0, -1, "unknown name", 0}});
    r.ce.SetFind("speed");
    r.Frames(2);
    ClickPos(r, P(5, 12));                                                      // in the "{" of the if: caret next to a bracket
    r.Key(Key_Home);
    r.Key(Key_Home);
    r.ce.SetSelection(P(2, 4), P(3, 12));
    r.Frames(2);
    const Bitmap& bmp = r.sim.Draw(12, 14, 20);
    const UI::CodePalette& pal = r.ce.Palette();
    const UI::Rect it = r.ce.ItemRect(), vr = r.ce.ViewRect();
    const float lh = r.ce.LineHeight();
    auto rowY = [&](int l) { return r.ce.PosToScreen(P(l, 0)).y; };
    // breakpoint dot: centre pixel is exactly the palette colour
    CHECK(PixelAt(bmp, (int)(it.x + lh * 0.5f), (int)(rowY(2) + lh * 0.5f)) == C32(pal.breakpoint));
    CHECK(PixelAt(bmp, (int)(it.x + lh * 0.5f), (int)(rowY(1) + lh * 0.5f)) == C32(pal.gutter));
    // the error mark in the gutter, and the wash over the line
    CHECK(PixelAt(bmp, (int)(vr.x - 2), (int)(rowY(4) + 4)) == C32(pal.errorUnderline));
    CHECK(PixelAt(bmp, (int)(vr.x - 2), (int)(rowY(5) + 4)) == C32(pal.gutter));
    CHECK(PixelAt(bmp, (int)(vr.x + vr.w - 3), (int)(rowY(4) + 2)) != PixelAt(bmp, (int)(vr.x + vr.w - 3), (int)(rowY(6) + 2)));
    // line numbers are drawn
    CHECK(CountNot(bmp, (int)(it.x + lh), (int)rowY(0), (int)vr.x - 4, (int)(rowY(0) + lh), C32(pal.gutter)) > 5);
    // the selection tints empty space after the end of line 2 (the newline is selected) but not after line 1... (line 1 is not selected)
    const int ex = (int)(r.ce.PosToScreen(P(2, (int)r.ce.Line(2).size())).x + 4), ey = (int)(rowY(2) + 3);
    CHECK(PixelAt(bmp, ex, ey) != PixelAt(bmp, ex, (int)(rowY(1) + 3)));
    // highlighting: a comment glyph pixel has the comment colour (the first line is all comment)
    bool sawComment = false, sawKeyword = false;
    for (int y = (int)rowY(0); y < (int)(rowY(0) + lh); ++y) for (int x = (int)vr.x; x < (int)(vr.x + 200); ++x) if (PixelAt(bmp, x, y) == C32(pal.text[UI::CodeColor_Comment])) sawComment = true;
    for (int y = (int)rowY(1); y < (int)(rowY(1) + lh); ++y) for (int x = (int)vr.x; x < (int)(vr.x + 80); ++x) if (PixelAt(bmp, x, y) == C32(pal.text[UI::CodeColor_Keyword])) sawKeyword = true;
    CHECK(sawComment && sawKeyword);
    // the caret is drawn where the caret is (selection end), in the caret colour
    const UI::Vec2 cp = r.ce.PosToScreen(r.ce.Caret());
    CHECK(PixelAt(bmp, (int)cp.x, (int)(cp.y + lh * 0.5f)) == C32(pal.caret));
    // golden image
    std::string why;
    const bool ok = MatchesGolden("codeedit_highlight", bmp, &why);
    if (!ok) std::cerr << "  golden codeedit_highlight: " << why << "\n";
    CHECK(ok);
    // an unfocused editor draws no caret and a dimmer selection
    ClickAt(r, 5, 330);
    const Bitmap& b2 = r.sim.Draw(12, 14, 20);
    CHECK(PixelAt(b2, (int)cp.x, (int)(cp.y + lh * 0.5f)) != C32(pal.caret));
    // an empty editor, a tiny one
    Rig e;
    e.w = 40; e.h = 30;
    e.Frames(2);
    e.w = 0; e.h = 0; e.Frames(2);
    CHECK(e.ce.ItemRect().w > 100);                                              // 0 means: fill the window
    // outside any window: nothing happens
    CodeEdit loose;
    Reset();
    Sim s2;
    bool drew = true;
    s2.Frame([&] { drew = loose.Draw("x", 100, 100); });
    CHECK(!drew);
}


// ---------------------------------------------------------------------------------------------
// The Dear ImGui script editor's look, as options (the script editor turns them on through editor/ScriptEditorCodeEdit)
// ---------------------------------------------------------------------------------------------
void ScriptLookTests() {
    // colouring order: a called built-in takes the call colour, a member the member colour, only a bare built-in the known colour
    {
        UI::CodeLanguage lang = UI::OkayScriptLanguage();
        lang.known.insert("print");
        CodeEdit e;
        e.SetLanguage(lang);
        e.SetText("print(1); x = print; a.print = 2;");
        std::vector<UI::CodeToken> t;
        auto colorAt = [&](int col) { for (const UI::CodeToken& k : t) if (col >= k.start && col < k.end) return (int)k.color; return (int)UI::CodeColor_Default; };
        e.LineTokens(0, t);
        CHECK(colorAt(0) == UI::CodeColor_Known);                       // the default order: known first
        lang.callBeforeKnown = true;
        e.SetLanguage(lang);
        e.LineTokens(0, t);
        CHECK(colorAt(0) == UI::CodeColor_Function);                    // print( is a call
        CHECK(colorAt(14) == UI::CodeColor_Known);                      // = print; is the built-in
        CHECK(colorAt(23) == UI::CodeColor_Member);                     // a.print is a member
        CHECK(colorAt(10) == UI::CodeColor_Default);                    // x
    }
    const UI::Color32 kFill = UI::RGBA(0, 200, 0), kEdge = UI::RGBA(200, 0, 0), kIdle = UI::RGBA(0, 0, 200), kBp = UI::RGBA(0, 120, 250);
    const UI::Color32 kWash = UI::RGBA(250, 250, 0);
    auto look = [&](Rig& r) {
        UI::CodePalette p = UI::DefaultCodePalette();
        p.currentLine = kFill; p.currentLineEdge = kEdge; p.currentLineInactive = kIdle; p.breakpointLine = kBp;
        r.ce.SetPalette(p);
        r.ce.SetText("one\ntwo\nthree\nfour\nfive\n");
    };
    // the caret's line: filled across the gutter and the text, outlined; another fill while the editor does not have the keyboard
    {
        Rig r(560, 340);
        r.w = 520; r.h = 300;
        look(r);
        r.Frames(2);
        ClickPos(r, P(2, 2));
        r.Frames(1);
        CHECK(r.ce.Focused());
        const Bitmap& b = r.sim.Draw(12, 14, 20);
        const UI::Rect it = r.ce.ItemRect(), vr = r.ce.ViewRect();
        const float y = r.ce.PosToScreen(P(2, 0)).y, lh = r.ce.LineHeight();
        CHECK(PixelAt(b, (int)(vr.x + vr.w - 20), (int)(y + lh * 0.5f)) == C32(kFill));     // the text part
        CHECK(PixelAt(b, (int)(it.x + 2), (int)(y + lh * 0.5f)) == C32(kFill));             // the gutter part
        CHECK(PixelAt(b, (int)(vr.x + vr.w - 20), (int)y) == C32(kEdge));                   // the outline, top
        CHECK(PixelAt(b, (int)(vr.x + vr.w - 20), (int)(y + lh - 1)) == C32(kEdge));        // and bottom
        CHECK(PixelAt(b, (int)(vr.x + vr.w - 20), (int)(y - lh * 0.5f)) != C32(kFill));    // the line above: no box
        ClickAt(r, 5, 330);                                                                 // outside: the editor loses the keyboard
        CHECK(!r.ce.Focused());
        const Bitmap& b2 = r.sim.Draw(12, 14, 20);
        CHECK(PixelAt(b2, (int)(vr.x + vr.w - 20), (int)(y + lh * 0.5f)) == C32(kIdle));
        CHECK(PixelAt(b2, (int)(vr.x + vr.w - 20), (int)y) == C32(kEdge));
        // with a selection there is no box
        r.ce.SetSelection(P(2, 0), P(2, 3));
        r.Frames(1);
        const Bitmap& b3 = r.sim.Draw(12, 14, 20);
        CHECK(PixelAt(b3, (int)(vr.x + vr.w - 20), (int)(y + lh * 0.5f)) != C32(kIdle));
        // no inactive colour: no box while unfocused (the default palette)
        r.ce.SetPalette(UI::DefaultCodePalette());
        r.ce.SetCaret(P(2, 0));
        r.Frames(1);
        const Bitmap& b4 = r.sim.Draw(12, 14, 20);
        CHECK(PixelAt(b4, (int)(vr.x + vr.w - 20), (int)(y + lh * 0.5f)) == C32(UI::DefaultCodePalette().background));
    }
    // a breakpoint washes its whole line; a marker can bring its own wash; the widget draws no message text after the line
    {
        Rig r(560, 340);
        r.w = 520; r.h = 300;
        look(r);
        r.ce.SetBreakpoints({1});
        UI::CodeMarker m; m.line = 3; m.message = "a long message that must not be drawn after the code by the widget itself"; m.lineColor = kWash;
        r.ce.SetMarkers({m});
        r.Frames(2);
        const Bitmap& b = r.sim.Draw(12, 14, 20);
        const UI::Rect vr = r.ce.ViewRect();
        const float lh = r.ce.LineHeight();
        const float y1 = r.ce.PosToScreen(P(1, 0)).y, y3 = r.ce.PosToScreen(P(3, 0)).y;
        CHECK(PixelAt(b, (int)(vr.x + vr.w - 20), (int)(y1 + lh * 0.5f)) == C32(kBp));
        CHECK(PixelAt(b, (int)(vr.x + vr.w - 20), (int)(y3 + lh * 0.5f)) == C32(kWash));
        const int endX = (int)r.ce.PosToScreen(P(3, r.ce.LineLength(3))).x + 8;
        CHECK(CountNot(b, endX, (int)y3 + 1, (int)(vr.x + vr.w) - 1, (int)(y3 + lh * 0.6f), C32(kWash)) == 0);   // nothing written after "four"
    }
    // padding inside the editor: the first line starts below it, the gutter right of it, the last line can scroll fully into view
    {
        Rig r(560, 340);
        r.w = 520; r.h = 200;
        std::string many; for (int i = 0; i < 60; ++i) many += "line " + std::to_string(i) + "\n";
        r.ce.SetText(many);
        r.ce.SetContentPadding(14.0f, 10.0f);
        r.Frames(2);
        const UI::Rect it = r.ce.ItemRect();
        const float lh = r.ce.LineHeight();
        CHECK_NEAR(r.ce.PosToScreen(P(0, 0)).y, it.y + 10.0f, 0.01f);
        CHECK(r.ce.ScreenToPos(r.ce.PosToScreen(P(3, 2)).x + 1.0f, r.ce.PosToScreen(P(3, 2)).y + lh * 0.5f) == P(3, 2));
        r.ce.SetBreakpoints({});
        ClickAt(r, it.x + 14.0f + lh * 0.5f, r.ce.PosToScreen(P(1, 0)).y + lh * 0.5f);   // the breakpoint column, right of the padding
        CHECK(r.ce.HasBreakpoint(1));
        ClickAt(r, it.x + 3.0f, r.ce.PosToScreen(P(2, 0)).y + lh * 0.5f);                 // inside the padding: nothing
        CHECK(!r.ce.HasBreakpoint(2));
        r.ce.SetCaret(P(60, 0)); r.ce.ScrollToCaret(); r.Frames(2);
        const float lastBottom = r.ce.PosToScreen(P(60, 0)).y + lh;
        const UI::Rect vr = r.ce.ViewRect();
        CHECK(lastBottom <= vr.y + vr.h + 0.01f);
        r.ce.SetScroll(0, 1e9f); r.Frames(1);
        CHECK_NEAR(r.ce.PosToScreen(P(60, 0)).y + lh + 10.0f, vr.y + vr.h, 0.6f);         // scrolled to the end: the padding below the last line
        CHECK(r.ce.FirstVisibleLine() > 40);
    }
    // the scroll bars: thickness, and a horizontal bar that is always there when asked for
    {
        Rig r(560, 340);
        r.w = 520; r.h = 200;
        r.ce.SetText("short\n");
        r.Frames(2);
        CHECK(!r.ce.HScrollbarShown());
        r.ce.SetScrollbarStyle(14.0f, 7.0f);
        r.ce.SetAlwaysShowHScrollbar(true);
        r.Frames(2);
        const UI::Rect it = r.ce.ItemRect(), hb = r.ce.HScrollbarRect(), vr = r.ce.ViewRect();
        CHECK(r.ce.HScrollbarShown());
        CHECK_NEAR(hb.h, 14.0f, 0.01f);
        CHECK_NEAR(hb.y + hb.h, it.y + it.h, 0.01f);
        CHECK_NEAR(vr.h, it.h - 14.0f, 0.01f);
        CHECK(!r.ce.VScrollbarShown());
        // dragging its thumb with nothing to scroll leaves the view where it is
        r.sim.Move(hb.x + 20, hb.y + 7); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Move(hb.x + 200, hb.y + 7); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(r.ce.ScrollX() == 0.0f);
    }
}


// A host clipboard (the script editor passes Dear ImGui's, the system clipboard): no 4 KB limit, and OkayUI's clipboard is not used.
void HostClipboardTests() {
    std::string host;
    int sets = 0, gets = 0;
    CodeEdit e;
    e.SetClipboardFunctions([&](const std::string& t) { host = t; ++sets; }, [&]() { ++gets; return host; });
    UI::SetClipboardText("okayui's");
    std::string big;
    for (int i = 0; i < 1000; ++i) big += "line number " + std::to_string(i) + "\n";   // about 16 KB
    e.SetText(big);
    e.SelectAll();
    CHECK(e.Copy());
    CHECK(host == e.GetText() && sets == 1);
    CHECK(std::string(UI::GetClipboardText()) == "okayui's");              // OkayUI's clipboard untouched
    CHECK(e.Cut());                                                          // more than kClipboardMax: still cut, nothing lost
    CHECK(e.IsEmpty() && host.size() > (size_t)UI::kClipboardMax);
    CHECK(e.Paste());
    CHECK(gets == 1 && e.GetText() == host);
    host = "a\r\nb";                                                         // line endings normalised on paste, as before
    e.SetText("");
    CHECK(e.Paste() && e.GetText() == "a\nb");
    // back to OkayUI's clipboard
    e.SetClipboardFunctions(nullptr, nullptr);
    e.SelectAll();
    CHECK(e.Copy() && std::string(UI::GetClipboardText()) == "a\nb" && host == "a\r\nb");
}

// ---------------------------------------------------------------------------------------------
// 100k lines
// ---------------------------------------------------------------------------------------------
double Ms(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

void PerfTests() {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
    const double slack = 20.0;
#else
    const double slack = 1.0;
#endif
    std::string big;
    big.reserve(5u << 20);
    for (int i = 0; i < 100000; ++i) {
        big += "    var value" + std::to_string(i) + " = Compute(" + std::to_string(i) + ", \"text\") + 3.5; // note ";
        big += std::to_string(i) + "\n";
        if (i % 1000 == 0) big += "/* block\n   comment */\n";
    }
    Rig r(800, 600);
    r.w = 760; r.h = 540;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    auto t0 = std::chrono::steady_clock::now();
    r.ce.SetText(big);
    const double tSet = Ms(t0);
    CHECK(r.ce.LineCount() > 100000);
    t0 = std::chrono::steady_clock::now();
    r.Frame();
    const double tFirst = Ms(t0);
    const int vTop = r.sim.Draw().w; (void)vTop;
    const UI::DrawData dTop = UI::GetDrawData();
    t0 = std::chrono::steady_clock::now();
    const int kFrames = 60;
    for (int i = 0; i < kFrames; ++i) r.Frame();
    const double tTop = Ms(t0) / kFrames;
    // jump to the end: the highlighter must run over the whole text once
    t0 = std::chrono::steady_clock::now();
    r.ce.SetCaret(P(r.ce.LineCount() - 1, 0));
    r.Frame();
    const double tEnd = Ms(t0);
    const UI::DrawData dEnd = UI::GetDrawData();
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) r.Frame();
    const double tEndSteady = Ms(t0) / kFrames;
    // jump to the middle and edit there
    r.ce.GoToLine(50000);
    r.Frame();
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) { r.ce.InsertText("x"); r.ce.InsertText("\n"); r.Frame(); }
    const double tEdit = Ms(t0) / 20;
    t0 = std::chrono::steady_clock::now();
    r.ce.SelectAll();
    r.ce.SetFind("Compute");
    const int cnt = r.ce.FindCount();
    const double tFind = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    r.ce.SetCaret(P(60000, 0)); r.ce.Move(UI::CodeMove_PageDown); r.Frame();
    const double tPage = Ms(t0);
    std::cout << "  100k lines (" << big.size() / 1024 << " KiB): SetText " << tSet << " ms, first frame " << tFirst << " ms, steady frame at top " << tTop
              << " ms, jump to end " << tEnd << " ms, steady at end " << tEndSteady << " ms, edit+frame in the middle " << tEdit
              << " ms, find-count " << tFind << " ms (" << cnt << " matches), page " << tPage << " ms; vertices at top " << dTop.vertexCount << ", at end " << dEnd.vertexCount << "\n";
    CHECK(tSet < 2000 * slack);
    CHECK(tFirst < 500 * slack);
    CHECK(tTop < 20 * slack);                       // only the visible lines are drawn
    CHECK(tEnd < 2000 * slack);
    CHECK(tEndSteady < 20 * slack);
    CHECK(tEdit < 100 * slack);
    CHECK(cnt == 100000);
    CHECK(dTop.vertexCount > 0 && dTop.vertexCount < 262144 && dEnd.vertexCount < 262144);
    CHECK(dTop.dropped == 0 && dEnd.dropped == 0);
    // the cost of a frame does not depend on the length of the text: the same frame count on a tiny text is in the same range
    Rig small(800, 600);
    small.w = 760; small.h = 540;
    small.ce.SetLanguage(UI::OkayScriptLanguage());
    small.ce.SetText(big.substr(0, 4000));
    small.Frames(3);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) small.Frame();
    const double tSmall = Ms(t0) / kFrames;
    std::cout << "  same frame on a 4 KB text: " << tSmall << " ms\n";
    CHECK(tTop < tSmall * 4 + 2 * slack);
    // undo of the edits in the middle, and of a huge delete
    t0 = std::chrono::steady_clock::now();
    r.ce.SelectAll(); r.ce.InsertText("");
    CHECK(r.ce.LineCount() == 1);
    r.ce.Undo();
    CHECK(r.ce.LineCount() > 100000);
    const double tUndo = Ms(t0);
    std::cout << "  delete all and undo: " << tUndo << " ms\n";
    CHECK(tUndo < 3000 * slack);
}

// ---------------------------------------------------------------------------------------------
// Line commands the script editor does itself today (Ctrl+/, Ctrl+D, Ctrl+Shift+K, Alt+Up and Alt+Down).
void LineCommandTests() {
    {   // toggle comment: comment, then uncomment; blank lines left alone; indent kept
        CodeEdit e; e.SetText("a\n  b\n\nc");
        e.SetSelection(P(0, 0), P(3, 1));
        CHECK(e.ToggleLineComment() && e.GetText() == "// a\n  // b\n\n// c");
        CHECK(e.ToggleLineComment() && e.GetText() == "a\n  b\n\nc");
        CHECK(e.Undo() && e.GetText() == "// a\n  // b\n\n// c");
        CHECK(e.Undo() && e.GetText() == "a\n  b\n\nc");
        // mixed: any uncommented line makes it comment all
        e.SetText("// a\nb"); e.SetSelection(P(0, 0), P(1, 1));
        CHECK(e.ToggleLineComment() && e.GetText() == "// // a\n// b");
        // a selection ending at column 0 leaves that line out
        e.SetText("a\nb\nc"); e.SetSelection(P(0, 0), P(2, 0));
        CHECK(e.ToggleLineComment() && e.GetText() == "// a\n// b\nc");
        // caret moves with the text; another prefix; "//" with no space removes two
        e.SetText("x"); e.SetCaret(P(0, 1));
        CHECK(e.ToggleLineComment("#") && e.GetText() == "# x" && At(e, 0, 3));
        e.SetText("//x"); CHECK(e.ToggleLineComment() && e.GetText() == "x");
        // all blank: nothing; read only: nothing
        e.SetText("\n"); CHECK(!e.ToggleLineComment());
        e.SetText("a"); e.SetReadOnly(true); CHECK(!e.ToggleLineComment() && e.GetText() == "a");
        // the language's own line comment is the default
        CodeEdit f; f.SetLanguage(UI::OkayScriptLanguage()); f.SetText("a");
        CHECK(f.ToggleLineComment() && f.GetText().find("a") != std::string::npos && f.Line(0) != "a");
    }
    {   // duplicate
        CodeEdit e; e.SetText("a\nb\nc"); e.SetCaret(P(1, 1));
        CHECK(e.DuplicateLines() && e.GetText() == "a\nb\nb\nc" && At(e, 2, 1));
        e.SetText("a\nb\nc"); e.SetSelection(P(0, 0), P(1, 1));
        CHECK(e.DuplicateLines() && e.GetText() == "a\nb\na\nb\nc" && e.SelectionStart() == P(2, 0) && e.SelectionEnd() == P(3, 1));
        e.SetText("a"); CHECK(e.DuplicateLines() && e.GetText() == "a\na");
        CHECK(e.Undo() && e.GetText() == "a");
    }
    {   // delete lines
        CodeEdit e; e.SetText("a\nb\nc"); e.SetCaret(P(1, 1));
        CHECK(e.DeleteLines() && e.GetText() == "a\nc" && At(e, 1, 0));
        CHECK(e.DeleteLines() && e.GetText() == "a" && At(e, 0, 0));   // last line: the newline before it goes
        CHECK(e.DeleteLines() && e.GetText().empty() && e.LineCount() == 1);
        CHECK(!e.DeleteLines() || e.GetText().empty());
        e.SetText("a\nb\nc\nd"); e.SetSelection(P(1, 0), P(2, 1));
        CHECK(e.DeleteLines() && e.GetText() == "a\nd");
        CHECK(e.Undo() && e.GetText() == "a\nb\nc\nd");
        e.SetReadOnly(true); CHECK(!e.DeleteLines());
    }
    {   // move lines
        CodeEdit e; e.SetText("a\nb\nc"); e.SetCaret(P(1, 1));
        CHECK(e.MoveLines(true) && e.GetText() == "b\na\nc" && At(e, 0, 1));
        CHECK(!e.MoveLines(true) && e.GetText() == "b\na\nc");
        CHECK(e.MoveLines(false) && e.MoveLines(false) && e.GetText() == "a\nc\nb" && At(e, 2, 1));
        CHECK(!e.MoveLines(false));
        CHECK(e.Undo() && e.Undo() && e.Undo() && e.GetText() == "a\nb\nc");
        e.SetSelection(P(0, 1), P(1, 1));
        CHECK(e.MoveLines(false) && e.GetText() == "c\na\nb" && e.Anchor() == P(1, 1) && e.Caret() == P(2, 1));
        // breakpoints follow the text
        e.SetText("a\nb\nc"); e.SetBreakpoints({2}); e.SetCaret(P(2, 0));
        CHECK(e.MoveLines(true) && e.GetText() == "a\nc\nb");
        e.SetReadOnly(true); CHECK(!e.MoveLines(true));
    }
}

// The line commands on the script editor's keys, through the widget: Ctrl+/ comment, Ctrl+D duplicate, Ctrl+Shift+K delete,
// Alt+Up and Alt+Down move. Alt+Up must not also move the caret up; plain Up still moves.
void LineCommandKeyTests() {
    Rig r;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetText("a\nb\nc");
    r.Frames(2);
    ClickPos(r, P(1, 1));
    CHECK(r.ce.Focused() && At(r.ce, 1, 1));
    r.sim.ctrl = true; r.Named(UI::KeyId_Slash); r.sim.ctrl = false;
    CHECK(r.ce.GetText() == "a\n// b\nc");
    r.sim.ctrl = true; r.Named(UI::KeyId_Slash); r.sim.ctrl = false;
    CHECK(r.ce.GetText() == "a\nb\nc");
    r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_D); r.sim.ctrl = false; r.sim.shift = false;   // Ctrl+Shift+D (Ctrl+D: next match)
    CHECK(r.ce.GetText() == "a\nb\nb\nc" && r.ce.Caret().line == 2);
    r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_K); r.sim.ctrl = false; r.sim.shift = false;
    CHECK(r.ce.GetText() == "a\nb\nc" && r.ce.Caret().line == 2);
    r.sim.Hold(UI::KeyId_Alt, true);
    r.Key(Key_Up);                                         // c moves above b; the caret goes with it, one line, not two
    CHECK(r.ce.GetText() == "a\nc\nb" && r.ce.Caret().line == 1);
    r.Key(Key_Down);
    CHECK(r.ce.GetText() == "a\nb\nc" && r.ce.Caret().line == 2);
    r.sim.Hold(UI::KeyId_Alt, false); r.Frame();
    r.Key(Key_Up);                                         // without Alt: a plain caret move
    CHECK(r.ce.GetText() == "a\nb\nc" && r.ce.Caret().line == 1);
    // each command is one undo step (Ctrl+Z)
    r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_D); r.sim.shift = false; r.Named(UI::KeyId_Z); r.sim.ctrl = false;
    CHECK(r.ce.GetText() == "a\nb\nc");
    // typing a '/' with Ctrl held is a command, not text; plain '/' is text
    r.sim.ctrl = true; r.sim.Type("/"); r.Named(UI::KeyId_Slash); r.sim.ctrl = false;
    CHECK(r.ce.GetText() == "a\n// b\nc");
    // off: the keys do nothing
    r.ce.SetLineCommandKeys(false);
    r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_D); r.sim.ctrl = false; r.sim.shift = false;
    CHECK(r.ce.GetText() == "a\n// b\nc");
    r.ce.SetLineCommandKeys(true);
    // read only: nothing
    r.ce.SetReadOnly(true);
    r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_D); r.sim.ctrl = false; r.sim.shift = false;
    CHECK(r.ce.GetText() == "a\n// b\nc");
}

// What the script editor still took from ImGuiColorTextEdit before it was deleted: the gap between the line numbers and the
// text (its SetLeftMargin) and a hover tooltip on known words (its identifier declarations: a built-in's signature and doc).
// (The error's message after the line is the script editor's own overlay in main.cpp; the widget draws none, see DrawTests.)
int DiffPixels(const Bitmap& a, const Bitmap& b, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (PixelAt(a, x, y) != PixelAt(b, x, y)) ++n;
    return n;
}
void ScriptEditorGapTests() {
    {   // the text margin: where the text starts right of the gutter; a click in the margin is column 0
        Rig r;
        r.ce.SetText("abc\ndef");
        r.Frame();
        CHECK(r.ce.TextMargin() == 4.0f);
        CHECK(std::abs(r.X(P(0, 0)) - r.ce.ViewRect().x - 4.0f) < 0.01f);
        r.ce.SetTextMargin(24.0f);
        r.Frame();
        CHECK(std::abs(r.X(P(0, 0)) - r.ce.ViewRect().x - 24.0f) < 0.01f);
        CHECK(std::abs(r.X(P(0, 2)) - r.X(P(0, 0)) - 2.0f * r.ce.CharWidth()) < 0.01f);
        CHECK(r.ce.ScreenToPos(r.ce.ViewRect().x + 2.0f, r.Y(P(1, 0))) == P(1, 0));
        r.ce.SetCaret(P(0, 3));
        ClickAt(r, r.ce.ViewRect().x + 3.0f, r.Y(P(1, 0)));
        CHECK(At(r.ce, 1, 0));
        r.ce.SetTextMargin(-5.0f); CHECK(r.ce.TextMargin() == 0.0f);      // clamped
        r.ce.SetTextMargin(5000.0f); CHECK(r.ce.TextMargin() == 200.0f);
    }
    {   // hover text: a documented word under the pointer, nothing over punctuation, blanks, past the line end or in the gutter
        Rig r;
        r.ce.SetText("print(x) foo\n  bar.baz\n\n");
        r.ce.SetHoverInfo([](const std::string& w) -> std::string { return w == "print" ? "print(value)" : w == "bar" ? "bar doc" : w == "baz" ? "member" : ""; });
        r.Frame();
        const float cw = r.ce.CharWidth();
        CHECK(r.ce.HoverText(r.X(P(0, 0)) + 1.0f, r.Y(P(0, 0))) == "print(value)");
        CHECK(r.ce.HoverText(r.X(P(0, 4)) + cw * 0.8f, r.Y(P(0, 0))) == "print(value)");   // the right half of the last letter
        CHECK(r.ce.HoverText(r.X(P(0, 5)) + 1.0f, r.Y(P(0, 0))).empty());                  // '('
        CHECK(r.ce.HoverText(r.X(P(0, 6)) + 1.0f, r.Y(P(0, 0))).empty());                  // x: no info
        CHECK(r.ce.HoverText(r.X(P(0, 8)) + 1.0f, r.Y(P(0, 0))).empty());                  // the blank
        CHECK(r.ce.HoverText(r.X(P(0, 12)) + 3.0f * cw, r.Y(P(0, 0))).empty());            // past the end of the line ("foo" is last)
        CHECK(r.ce.HoverText(r.X(P(1, 0)) + 1.0f, r.Y(P(1, 0))).empty());                  // the indent
        CHECK(r.ce.HoverText(r.X(P(1, 2)) + 1.0f, r.Y(P(1, 0))) == "bar doc");
        CHECK(r.ce.HoverText(r.X(P(1, 6)) + 1.0f, r.Y(P(1, 0))) == "member");
        CHECK(r.ce.HoverText(r.X(P(2, 0)) + 1.0f, r.Y(P(2, 0))).empty());                  // an empty line
        CHECK(r.ce.HoverText(r.ce.ItemRect().x + 2.0f, r.Y(P(0, 0))).empty());             // the gutter
        CHECK(r.ce.HoverText(r.X(P(0, 0)) + 1.0f, r.ce.ItemRect().y + r.ce.ItemRect().h + 40.0f).empty());   // below the widget
        // an error's message wins on its line (the gutter too); other lines keep their word tips
        UI::CodeMarker m; m.line = 0; m.message = "Error at line 1: bad";
        r.ce.SetMarkers({m});
        CHECK(r.ce.HoverText(r.X(P(0, 0)) + 1.0f, r.Y(P(0, 0))) == "Error at line 1: bad");
        CHECK(r.ce.HoverText(r.ce.ItemRect().x + 2.0f, r.Y(P(0, 0))) == "Error at line 1: bad");
        CHECK(r.ce.HoverText(r.X(P(1, 2)) + 1.0f, r.Y(P(1, 0))) == "bar doc");
        r.ce.SetHoverInfo(nullptr);
        r.ce.ClearMarkers();
        CHECK(r.ce.HoverText(r.X(P(0, 0)) + 1.0f, r.Y(P(0, 0))).empty());
        // Draw shows it: a tooltip appears near the pointer over "print" and not over "x"
        r.ce.SetHoverInfo([](const std::string& w) -> std::string { return w == "print" ? "print(value) writes a line to the console" : ""; });
        r.sim.Move(r.X(P(0, 6)) + 1.0f, r.Y(P(0, 0))); r.Frames(3);
        const Bitmap none = r.sim.Draw();
        r.sim.Move(r.X(P(0, 0)) + 1.0f, r.Y(P(0, 0))); r.Frames(3);
        const Bitmap tip = r.sim.Draw();
        const int my = (int)r.Y(P(0, 0));
        CHECK(DiffPixels(none, tip, 0, my + 4, (int)r.sim.W, std::min((int)r.sim.H, my + 80)) > 200);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The script editor upgrade, batch 1: auto-close, auto-dedent, word wrap, find across lines, the new drawing
// ---------------------------------------------------------------------------------------------
UI::CodeLanguage LuaLike() {
    UI::CodeLanguage l;
    for (const char* k : {"function", "local", "if", "then", "else", "elseif", "end", "for", "while", "do", "repeat", "until", "return"}) l.keywords.insert(k);
    l.lineComments = {"--"};
    l.blockStart = "--[["; l.blockEnd = "]]";
    l.blockOpenWords = {"function", "do", "then", "repeat", "else"};
    l.blockCloseWords = {"end", "until", "elseif", "else"};
    l.dedentWords = {"end", "else", "elseif", "until"};
    l.foldByIndent = true;
    return l;
}

void AutoCloseTests() {
    CodeEdit e;
    e.SetLanguage(UI::OkayScriptLanguage());
    CHECK(e.Type("(") && e.GetText() == "()" && At(e, 0, 1));
    CHECK(e.Type(")") && e.GetText() == "()" && At(e, 0, 2));              // typed over the closer
    e.SetText("");
    e.Type("f(");   CHECK(e.GetText() == "f()" && At(e, 0, 2));
    e.Type("\"");   CHECK(e.GetText() == "f(\"\")" && At(e, 0, 3));
    e.Type("hi");   CHECK(e.GetText() == "f(\"hi\")");
    e.Type("\"");   CHECK(e.GetText() == "f(\"hi\")" && At(e, 0, 6));      // over the closing quote
    e.Type(")");    CHECK(e.GetText() == "f(\"hi\")" && At(e, 0, 7));
    while (e.Undo()) {}
    CHECK(e.GetText().empty());
    // not before a word, not inside strings or comments, a quote not after a letter
    e.SetText("x"); e.SetCaret(P(0, 0)); e.Type("(");           CHECK(e.GetText() == "(x");
    e.SetText("it"); e.SetCaret(P(0, 2)); e.Type("'");          CHECK(e.GetText() == "it'");
    e.SetText("// c"); e.SetCaret(P(0, 4)); e.Type("(");        CHECK(e.GetText() == "// c(");
    e.SetText("\"ab\""); e.SetCaret(P(0, 2)); e.Type("[");      CHECK(e.GetText() == "\"a[b\"");
    e.SetText("f(x"); e.SetCaret(P(0, 3)); e.Type(")");         CHECK(e.GetText() == "f(x)");   // no closer there: typed
    // a selection is wrapped and stays selected
    e.SetText("abc"); e.SetSelection(P(0, 0), P(0, 3)); e.Type("(");
    CHECK(e.GetText() == "(abc)" && e.SelectedText() == "abc");
    e.Type("\"");
    CHECK(e.GetText() == "(\"abc\")" && e.SelectedText() == "abc");
    CHECK(e.Undo() && e.GetText() == "(abc)");
    // Backspace between a pair takes both
    e.SetText(""); e.Type("[");
    CHECK(e.GetText() == "[]" && e.Backspace() && e.GetText().empty());
    // Enter between braces opens one block
    e.SetText(""); e.Type("{");
    CHECK(e.GetText() == "{}");
    e.NewLine();
    CHECK(e.GetText() == "{\n    \n}" && At(e, 1, 4));
    // off: plain typing; InsertText (paste, snippets) never pairs
    e.SetAutoClose(false); e.SetText(""); e.Type("(\"");
    CHECK(e.GetText() == "(\"");
    e.SetAutoClose(true); e.SetText(""); e.InsertText("(");
    CHECK(e.GetText() == "(");
    // read only: nothing
    e.SetReadOnly(true); CHECK(!e.Type("(") && e.GetText() == "("); e.SetReadOnly(false);
    // the widget's typing goes through Type
    Rig r;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.Frames(2);
    ClickAt(r, r.X(P(0, 0)) + 2, r.Y(P(0, 0)));
    r.Type("g(");
    CHECK(r.ce.GetText() == "g()" && At(r.ce, 0, 2));
    r.Type(")");
    CHECK(r.ce.GetText() == "g()" && At(r.ce, 0, 3));
}

void DedentTests() {
    CodeEdit e;
    e.SetLanguage(UI::OkayScriptLanguage());
    e.SetText("if (a) {\n    b();\n    ");
    e.SetCaret(P(2, 4));
    e.Type("}");
    CHECK(e.GetText() == "if (a) {\n    b();\n}" && At(e, 2, 1));
    e.SetText("a {\n    b {\n        c\n        ");
    e.SetCaret(P(3, 8)); e.Type("}");
    CHECK(e.Line(3) == "    }");                                         // its own block's indent
    e.SetText("    x"); e.SetCaret(P(0, 5)); e.Type("}");
    CHECK(e.GetText() == "    x}");                                       // something before it: no dedent
    e.SetText("{\n    "); e.SetCaret(P(1, 4)); e.Type("}");
    CHECK(e.GetText() == "{\n}");
    CHECK(e.Undo() && e.GetText() == "{\n    ");                          // one step: the '}' and the dedent
    e.SetAutoDedent(false); e.SetText("{\n    "); e.SetCaret(P(1, 4)); e.Type("}");
    CHECK(e.GetText() == "{\n    }");
    // Lua: a block word opens a level on Enter, `end` and `else` take it off, a longer word gets it back
    CodeEdit l;
    l.SetLanguage(LuaLike());
    l.SetText("if x then");
    l.SetCaret(P(0, 9)); l.NewLine();
    CHECK(l.GetText() == "if x then\n    " && At(l, 1, 4));
    l.Type("y()");
    CHECK(l.Line(1) == "    y()");
    l.NewLine();
    CHECK(l.Line(2) == "    ");
    l.Type("end");
    CHECK(l.Line(2) == "end" && At(l, 2, 3));
    l.SetText("if x then\n    y()\n    "); l.SetCaret(P(2, 4)); l.Type("endless");
    CHECK(l.Line(2) == "    endless");
    l.SetText("if x then\n    y()\n    "); l.SetCaret(P(2, 4)); l.Type("else");
    CHECK(l.Line(2) == "else");
    l.SetText("if x then\n    y()\n"); l.SetCaret(P(2, 0)); l.Type("end");
    CHECK(l.Line(2) == "end");                                            // already left: nothing to take off
    l.SetText("function f()"); l.SetCaret(P(0, 12)); l.NewLine();
    CHECK(l.Line(1) == "    ");
    l.SetText("x = 1 -- then"); l.SetCaret(P(0, 13)); l.NewLine();
    CHECK(l.Line(1).empty());                                             // the word in a comment does not count
}

void WordWrapTests() {
    Rig r(400, 320);
    r.w = 300; r.h = 240;
    r.ce.SetText("aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm\nshort");
    r.ce.SetWordWrap(true);
    r.Frames(2);
    CHECK(r.ce.WordWrap() && r.ce.VisualRowCount() > 2 && r.ce.LineCount() == 2);
    CHECK(!r.ce.HScrollbarShown());
    int brk = -1;
    for (int c = 1; c < (int)r.ce.Line(0).size(); ++c) if (r.ce.PosToScreen(P(0, c)).y > r.ce.PosToScreen(P(0, 0)).y) { brk = c; break; }
    CHECK(brk > 0 && r.ce.Line(0)[(size_t)brk - 1] == ' ');                 // broken after a space
    CHECK(std::fabs(r.ce.PosToScreen(P(0, brk)).x - r.ce.PosToScreen(P(0, 0)).x) < 0.5f);
    CHECK(std::fabs(r.ce.PosToScreen(P(0, brk)).y - r.ce.PosToScreen(P(0, 0)).y - r.ce.LineHeight()) < 0.5f);
    // every row fits in the view
    const UI::Rect vr = r.ce.ViewRect();
    for (int c = 0; c <= (int)r.ce.Line(0).size(); ++c) CHECK(r.ce.PosToScreen(P(0, c)).x < vr.x + vr.w);
    // Up and Down go by rows
    r.ce.SetCaret(P(0, 1)); r.ce.Move(UI::CodeMove_Down);
    CHECK(r.ce.Caret() == P(0, brk + 1));
    r.ce.Move(UI::CodeMove_Up);
    CHECK(r.ce.Caret() == P(0, 1));
    r.ce.SetCaret(P(0, (int)r.ce.Line(0).size())); r.ce.Move(UI::CodeMove_Down);
    CHECK(r.ce.Caret().line == 1);
    // a click on the second row lands in it, a drag across rows selects
    ClickPos(r, P(0, brk + 2));
    CHECK(At(r.ce, 0, brk + 2));
    r.sim.Move(r.X(P(0, 1)) + 1, r.Y(P(0, 1))); r.Frame(); r.sim.Down(); r.Frame();
    r.sim.Move(r.X(P(0, brk + 3)) + 1, r.Y(P(0, brk + 3))); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(r.ce.SelectionStart() == P(0, 1) && r.ce.SelectionEnd() == P(0, brk + 3));
    // the gutter: a number on the first row of a line only
    const Bitmap& bmp = r.sim.Draw(12, 14, 20);
    const UI::CodePalette& pal = r.ce.Palette();
    const UI::Rect it = r.ce.ItemRect();
    const float lh = r.ce.LineHeight(), y0 = r.ce.PosToScreen(P(0, 0)).y, y1 = r.ce.PosToScreen(P(0, brk)).y;
    CHECK(CountNot(bmp, (int)(it.x + lh), (int)y0, (int)vr.x - 4, (int)(y0 + lh), C32(pal.gutter)) > 5);
    CHECK(CountNot(bmp, (int)(it.x + lh), (int)y1, (int)vr.x - 4, (int)(y1 + lh), C32(pal.gutter)) == 0);
    // the selection is drawn on both rows
    CHECK(PixelAt(bmp, (int)(r.X(P(0, 2)) + 2), (int)y0 + 2) != PixelAt(bmp, (int)(r.X(P(1, 2)) + 2), (int)r.ce.PosToScreen(P(1, 0)).y + 2));
    std::string why;
    const bool ok = MatchesGolden("codeedit_wrap", bmp, &why);
    if (!ok) std::cerr << "  golden codeedit_wrap: " << why << "\n";
    CHECK(ok);
    // typing keeps the rows up to date
    r.ce.SetCaret(P(1, 5)); r.ce.InsertText(" and a much longer line now that needs to wrap as well, surely");
    r.Frames(1);
    CHECK(r.ce.PosToScreen(P(1, 60)).y > r.ce.PosToScreen(P(1, 0)).y);
    // off again: one row per line, sideways scrolling back
    r.ce.SetWordWrap(false);
    r.Frames(2);
    CHECK(r.ce.VisualRowCount() == 2 && r.ce.HScrollbarShown());
}

void MultiLineFindTests() {
    CodeEdit e;
    e.SetText("one\ntwo\nthree\none\ntwo\nX");
    e.SetFind("one\ntw");
    CHECK(e.FindCount() == 2);
    CHECK(e.FindNext() && e.Anchor() == P(0, 0) && e.Caret() == P(1, 2) && e.SelectedText() == "one\ntw");
    CHECK(e.FindNext() && e.Anchor() == P(3, 0));
    CHECK(e.FindNext() && e.Anchor() == P(0, 0));                          // wraps round
    CHECK(e.FindPrev() && e.Anchor() == P(3, 0));
    e.SetFind("e\nt");                 CHECK(e.FindCount() == 2);
    e.SetFind("TWO\nTHREE");           CHECK(e.FindCount() == 1);          // case does not matter by default
    e.SetFind("TWO\nTHREE", true);     CHECK(e.FindCount() == 0);
    e.SetFind("two\nthree\none");      CHECK(e.FindCount() == 1);          // a whole line in the middle
    e.SetFind("tw\nthree");            CHECK(e.FindCount() == 0);          // the first part must end its line
    e.SetFind("three\r\none");         CHECK(e.FindCount() == 1);          // "\r\n" is "\n"
    e.SetFind("one\ntwo");
    CHECK(e.ReplaceAll("1-2") == 2 && e.GetText() == "1-2\nthree\n1-2\nX");
    CHECK(e.Undo() && e.GetText() == "one\ntwo\nthree\none\ntwo\nX");        // one step
    CHECK(e.FindNext() && e.ReplaceCurrent("A\nB") && e.GetText() == "A\nB\nthree\none\ntwo\nX");
    e.SetText("a\na\na"); e.SetFind("a\na");
    CHECK(e.FindCount() == 1 && e.ReplaceAll("b") == 1 && e.GetText() == "b\na");   // one after the other, from the top
    // both lines of a match are washed
    Rig r;
    r.ce.SetText("xx one\ntwo yy\nzz");
    r.ce.SetFind("one\ntwo");
    r.Frames(2);
    const Bitmap& bmp = r.sim.Draw(12, 14, 20);
    const UI::Rect vr = r.ce.ViewRect();
    auto bg = [&](CodePos p) { return PixelAt(bmp, (int)r.X(p) + 1, (int)r.ce.PosToScreen(p).y + 1); };
    CHECK(bg(P(0, 4)) != C32(r.ce.Palette().background) && bg(P(1, 1)) != C32(r.ce.Palette().background));
    CHECK(bg(P(0, 1)) == C32(r.ce.Palette().background) && bg(P(1, 5)) == C32(r.ce.Palette().background));
    (void)vr;
}

// Matching word, indent guides, whitespace marks (and, from batch 2, the fold column and many carets) in one picture.
void FeatureDrawTests() {
    Rig r(560, 340);
    r.w = 520; r.h = 300;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetText("void Update() {\n    float speed = 2;\n    if (speed > 1) {\n\tspeed = speed * 2;\n    }\n\n}\nvoid Other() {\n    x();\n}");
    UI::CodePalette pal = r.ce.Palette();
    const UI::CodePalette plain = pal;
    pal.wordMatch = UI::RGBA(255, 230, 120, 70); pal.indentGuide = UI::RGBA(200, 200, 200, 70); pal.whitespace = UI::RGBA(150, 150, 150, 160);
    r.Frames(2);
    ClickPos(r, P(1, 11));                                                       // in "speed"
    r.Frames(1);
    const Bitmap before = r.sim.Draw(12, 14, 20);
    r.ce.SetPalette(pal);
    r.ce.SetShowWhitespace(true);
    r.Frames(1);
    const Bitmap& bmp = r.sim.Draw(12, 14, 20);
    auto cell = [&](const Bitmap& b, CodePos p, float fx, float fy) { return PixelAt(b, (int)(r.X(p) + r.ce.CharWidth() * fx), (int)(r.ce.PosToScreen(p).y + r.ce.LineHeight() * fy)); };
    // the other "speed"s are tinted, the one at the caret is not tinted that way
    CHECK(cell(bmp, P(2, 8), 0.1f, 0.15f) != cell(before, P(2, 8), 0.1f, 0.15f));
    CHECK(cell(bmp, P(3, 1), 0.1f, 0.15f) != cell(before, P(3, 1), 0.1f, 0.15f));
    // an indent guide at column 0 of an indented line, none on an unindented one
    const float gx = r.ce.PosToScreen(P(1, 0)).x;
    CHECK(PixelAt(bmp, (int)std::floor(gx), (int)(r.ce.PosToScreen(P(1, 0)).y + 3)) != PixelAt(before, (int)std::floor(gx), (int)(r.ce.PosToScreen(P(1, 0)).y + 3)));
    CHECK(PixelAt(bmp, (int)std::floor(gx), (int)(r.ce.PosToScreen(P(0, 0)).y + 3)) == PixelAt(before, (int)std::floor(gx), (int)(r.ce.PosToScreen(P(0, 0)).y + 3)));
    // a blank line takes the smaller indent of the lines around it (here the closing brace's: none)
    CHECK(PixelAt(bmp, (int)std::floor(gx), (int)(r.ce.PosToScreen(P(5, 0)).y + 3)) == PixelAt(before, (int)std::floor(gx), (int)(r.ce.PosToScreen(P(5, 0)).y + 3)));
    // a space shows a dot in its middle, a tab an arrow
    CHECK(cell(bmp, P(1, 9), 0.5f, 0.5f) != cell(before, P(1, 9), 0.5f, 0.5f));
    int tabDiff = 0;
    for (int x = (int)r.X(P(3, 0)) + 3; x < (int)r.X(P(3, 1)) - 3; ++x) {
        const int y = (int)(r.ce.PosToScreen(P(3, 0)).y + r.ce.LineHeight() * 0.5f);
        if (PixelAt(bmp, x, y) != PixelAt(before, x, y)) ++tabDiff;
    }
    CHECK(tabDiff > 4);
    // off again: the same pixels as before
    r.ce.SetPalette(plain); r.ce.SetShowWhitespace(false);
    r.Frames(1);
    const Bitmap& again = r.sim.Draw(12, 14, 20);
    CHECK(DiffPixels(again, before, 0, 0, 560, 340) == 0);
    // the golden picture: everything on, a folded block, two carets
    r.ce.SetPalette(pal); r.ce.SetShowWhitespace(true); r.ce.SetFolding(true);
    r.Frames(1);
    CHECK(r.ce.Fold(7));
    r.ce.SetCaret(P(1, 11)); r.ce.AddCaret(P(4, 5));
    r.Frames(2);
    CHECK(r.ce.CaretCount() == 2);
    const Bitmap& g = r.sim.Draw(12, 14, 20);
    // the second caret is drawn
    CHECK(PixelAt(g, (int)r.X(P(4, 5)), (int)(r.ce.PosToScreen(P(4, 5)).y + r.ce.LineHeight() * 0.5f)) == C32(pal.caret));
    std::string why;
    const bool ok = MatchesGolden("codeedit_features", g, &why);
    if (!ok) std::cerr << "  golden codeedit_features: " << why << "\n";
    CHECK(ok);
}

// ---------------------------------------------------------------------------------------------
// Batch 2: folding, many carets, column selection, drag and drop
// ---------------------------------------------------------------------------------------------
void FoldTests() {
    CodeEdit e;
    e.SetLanguage(UI::OkayScriptLanguage());
    e.SetFolding(true);
    e.SetText("void A() {\n    if (x) {\n        y();\n    }\n}\nvoid B() {\n}\n// \"{\"\nz");
    CHECK(e.FoldEnd(0) == 4 && e.FoldEnd(1) == 3 && e.FoldEnd(2) == -1 && e.FoldEnd(5) == 6 && e.FoldEnd(7) == -1);
    CHECK(e.Fold(1) && e.IsFolded(1) && e.IsLineHidden(2) && !e.IsLineHidden(3) && e.VisualRowCount() == 8);
    CHECK(!e.Fold(1));
    CHECK(e.Fold(0) && e.IsLineHidden(1) && e.IsLineHidden(3) && !e.IsLineHidden(4) && e.VisualRowCount() == 6);
    CHECK(!e.Fold(5));                                                         // "{" then "}": nothing to hide
    // Up and Down step over the hidden lines; Right and Left too
    e.SetCaret(P(0, 2));
    CHECK(e.IsFolded(0));
    e.Move(UI::CodeMove_Down);  CHECK(e.Caret().line == 4);
    e.Move(UI::CodeMove_Up);    CHECK(e.Caret().line == 0);
    e.SetCaret(P(0, (int)e.Line(0).size())); e.Move(UI::CodeMove_Right);
    CHECK(e.Caret() == P(4, 0));
    e.Move(UI::CodeMove_Left);
    CHECK(e.Caret() == P(0, (int)e.Line(0).size()));
    // a caret put into a hidden line unfolds what hides it
    e.SetCaret(P(2, 0));
    CHECK(!e.IsLineHidden(2) && !e.IsFolded(0) && !e.IsFolded(1));
    // fold all (the caret leaves the hidden lines), unfold all
    CHECK(e.FoldAll() == 2 && e.IsFolded(0) && e.IsFolded(1) && e.Caret() == P(0, (int)e.Line(0).size()));
    e.UnfoldAll();
    CHECK(e.VisualRowCount() == 9 && !e.IsFolded(0));
    // folds move with the lines above them, and undo moves them back
    CHECK(e.Fold(1));
    e.SetCaret(P(0, 0)); e.InsertText("\n");
    CHECK(e.IsFolded(2) && e.IsLineHidden(3));
    CHECK(e.Undo() && e.IsFolded(1));
    CHECK(e.Unfold(2) && !e.IsFolded(1));                                       // a line it hides names it too
    CHECK(e.ToggleFold(1) && e.IsFolded(1) && e.ToggleFold(1) && !e.IsFolded(1));
    CHECK(e.Fold(2) && e.IsFolded(1));                                          // a line inside: the block around it
    // folding off: no folds
    e.SetFolding(false);
    CHECK(!e.IsFolded(1) && e.VisualRowCount() == 9);
    // Lua: by the block words, and by indent
    CodeEdit l;
    l.SetLanguage(LuaLike());
    l.SetFolding(true);
    l.SetText("function f()\n  if a then\n    b()\n  else\n    c()\n  end\nend\nlocal t = {\n  1,\n}\nx = \"then\"\n--[[ do ]]\nlist:\n  one\n  two\nz");
    CHECK(l.FoldEnd(0) == 6 && l.FoldEnd(1) == 3 && l.FoldEnd(3) == 5 && l.FoldEnd(7) == 9);
    CHECK(l.FoldEnd(10) == -1 && l.FoldEnd(11) == -1);                          // words in strings and comments do not count
    CHECK(l.FoldEnd(12) == 14);                                                 // by indent
    CHECK(l.Fold(1) && l.IsLineHidden(2) && !l.IsLineHidden(3));               // `else` stays in view
    CHECK(l.Fold(12) && l.IsLineHidden(13) && l.IsLineHidden(14) && !l.IsLineHidden(15));
    // the widget: the fold column, the "..." box, the keys, a fold that loses its brace goes
    Rig r;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetFolding(true);
    r.ce.SetText("void A() {\n    if (x) {\n        y();\n    }\n}\nz");
    r.Frames(2);
    const UI::Rect vr = r.ce.ViewRect();
    ClickAt(r, vr.x - r.ce.LineHeight() * 0.45f, r.Y(P(1, 0)));
    CHECK(r.ce.IsFolded(1) && r.ce.VisualRowCount() == 5);
    CHECK(std::fabs(r.ce.PosToScreen(P(3, 0)).y - r.ce.PosToScreen(P(1, 0)).y - r.ce.LineHeight()) < 0.5f);   // line 3 right below line 1
    ClickAt(r, vr.x - r.ce.LineHeight() * 0.45f, r.Y(P(1, 0)));
    CHECK(!r.ce.IsFolded(1));
    CHECK(r.ce.Fold(1));
    r.Frames(1);
    ClickAt(r, r.X(P(1, (int)r.ce.Line(1).size())) + r.ce.CharWidth() * 2.0f, r.Y(P(1, 0)));   // the "..." box
    CHECK(!r.ce.IsFolded(1));
    ClickPos(r, P(1, 3));
    r.sim.ctrl = true; r.sim.shift = true; r.Named(UI::KeyId_LeftBracket);
    CHECK(r.ce.IsFolded(1));
    r.Named(UI::KeyId_RightBracket); r.sim.shift = false;
    CHECK(!r.ce.IsFolded(1));
    r.sim.Hold(UI::KeyId_Alt, true); r.Named(UI::KeyId_LeftBracket);
    CHECK(r.ce.IsFolded(0) && r.ce.IsFolded(1));
    r.Named(UI::KeyId_RightBracket); r.sim.Hold(UI::KeyId_Alt, false); r.sim.ctrl = false; r.Frame();
    CHECK(!r.ce.IsFolded(0) && !r.ce.IsFolded(1));
    CHECK(r.ce.Fold(1));
    r.ce.SetSelection(P(1, 11), P(1, 12));                                     // the '{' of the folded block
    r.ce.DeleteForward();
    r.Frames(1);
    CHECK(!r.ce.IsFolded(1));
}

void MultiCaretTests() {
    CodeEdit e;
    e.SetText("ab\ncd\nef");
    e.SetCaret(P(0, 1)); e.AddCaret(P(1, 1)); e.AddCaret(P(2, 1));
    CHECK(e.CaretCount() == 3 && e.Caret() == P(2, 1));
    CHECK(e.InsertText("X") && e.GetText() == "aXb\ncXd\neXf" && e.CaretCount() == 3);
    std::vector<UI::CodeSelection> s = e.Selections();
    CHECK(s.size() == 3 && s[0].caret == P(0, 2) && s[1].caret == P(1, 2) && s[2].caret == P(2, 2));
    e.BreakUndoGroup();
    e.Type("yz");
    CHECK(e.GetText() == "aXyzb\ncXyzd\neXyzf");
    CHECK(e.Undo() && e.GetText() == "aXb\ncXd\neXf" && e.CaretCount() == 3);   // a word typed at three carets: one step
    CHECK(e.Redo() && e.GetText() == "aXyzb\ncXyzd\neXyzf" && e.CaretCount() == 3);
    CHECK(e.Backspace() && e.GetText() == "aXyb\ncXyd\neXyf");
    CHECK(e.DeleteForward() && e.GetText() == "aXy\ncXy\neXy");
    CHECK(e.NewLine() && e.GetText() == "aXy\n\ncXy\n\neXy\n" && e.CaretCount() == 3);
    CHECK(e.Undo() && e.GetText() == "aXy\ncXy\neXy");
    // many carets on one line; carets on one place merge
    e.SetText("a a a"); e.SetCaret(P(0, 1)); e.AddCaret(P(0, 3)); e.AddCaret(P(0, 5));
    e.InsertText("!");
    CHECK(e.GetText() == "a! a! a!");
    e.AddCaret(P(0, 2));
    CHECK(e.CaretCount() == 3);
    CHECK(e.RemoveCaretAt(P(0, 5)) && e.CaretCount() == 2 && !e.RemoveCaretAt(P(0, 0)));
    e.ClearExtraCarets();
    CHECK(e.CaretCount() == 1);
    // moving: every caret; a selection per caret
    e.SetText("ab\ncd"); e.SetCaret(P(0, 0)); e.AddCaret(P(1, 0));
    e.Move(UI::CodeMove_Right);
    s = e.Selections();
    CHECK(s[0].caret == P(0, 1) && s[1].caret == P(1, 1));
    e.Move(UI::CodeMove_LineEnd, true);
    CHECK(e.InsertText("") && e.GetText() == "a\nc");                         // (empty text: the selections are deleted)
    e.Move(UI::CodeMove_DocStart);
    CHECK(e.CaretCount() == 1);                                                 // both at the start: merged
    // Ctrl+D: the word, then its next places (whole words), wrapping round
    e.SetText("foo bar foo"); e.SetCaret(P(0, 1));
    CHECK(e.SelectNextOccurrence() && e.SelectedText() == "foo" && e.CaretCount() == 1);
    CHECK(e.SelectNextOccurrence() && e.CaretCount() == 2 && e.Anchor() == P(0, 8));
    CHECK(!e.SelectNextOccurrence());
    CHECK(e.InsertText("baz") && e.GetText() == "baz bar baz");
    e.SetText("foo food foo"); e.SetCaret(P(0, 0));
    e.SelectNextOccurrence(); e.SelectNextOccurrence();
    CHECK(e.CaretCount() == 2 && e.Anchor() == P(0, 9));
    e.SetText("x.y x.y"); e.SetSelection(P(0, 0), P(0, 3));                       // not a word: any place
    CHECK(e.SelectNextOccurrence() && e.Anchor() == P(0, 4));
    // copy at many carets: one line each; a paste with as many lines: one each, else the whole text at each
    e.SetText("a1\nb2\nc3"); e.SetSelection(P(0, 0), P(0, 1)); e.AddCaret(P(1, 1), P(1, 0)); e.AddCaret(P(2, 1), P(2, 0));
    CHECK(e.Copy() && std::string(UI::GetClipboardText()) == "a\nb\nc");
    e.SetCaret(P(0, 2)); e.AddCaret(P(1, 2)); e.AddCaret(P(2, 2));
    CHECK(e.Paste() && e.GetText() == "a1a\nb2b\nc3c");
    UI::SetClipboardText("Z");
    CHECK(e.Paste() && e.GetText() == "a1aZ\nb2bZ\nc3cZ");
    e.SetSelection(P(0, 0), P(0, 1)); e.AddCaret(P(1, 1), P(1, 0));
    CHECK(e.Cut() && e.GetText() == "1aZ\n2bZ\nc3cZ" && std::string(UI::GetClipboardText()) == "a\nb");
    // a column selection: the same columns on every line, short lines get a caret at their end
    e.SetText("abcd\nab\nabcdef"); e.SetColumnSelection(P(0, 1), P(2, 3));
    s = e.Selections();
    CHECK(e.CaretCount() == 3 && s[0].Start() == P(0, 1) && s[0].End() == P(0, 3) && s[1].Start() == P(1, 1) && s[1].End() == P(1, 2) && s[2].End() == P(2, 3));
    CHECK(e.Backspace() && e.GetText() == "ad\na\nadef");
    // line commands once per line
    e.SetText("x\ny"); e.SetCaret(P(0, 0)); e.AddCaret(P(0, 1)); e.AddCaret(P(1, 0));
    CHECK(e.ToggleLineComment("//") && e.GetText() == "// x\n// y");
    CHECK(e.DuplicateLines() && e.GetText() == "// x\n// x\n// y\n// y");
    CHECK(e.Undo() && e.Undo() && e.GetText() == "x\ny");
    e.SetText("a\nb\nc\nd"); e.SetCaret(P(1, 0)); e.AddCaret(P(3, 0));
    CHECK(e.MoveLines(true) && e.GetText() == "b\na\nd\nc");
    CHECK(e.MoveLines(false) && e.GetText() == "a\nb\nc\nd");
    e.SetText("ab\ncd"); e.SetCaret(P(0, 0)); e.AddCaret(P(1, 0));
    CHECK(e.Tab() && e.GetText() == "    ab\n    cd");
    // read only: nothing changes
    e.SetReadOnly(true);
    CHECK(!e.InsertText("q") && !e.Backspace() && e.GetText() == "    ab\n    cd");
    e.SetReadOnly(false);
    // SetText and SetCaret drop the extra carets
    e.SetText("q"); CHECK(e.CaretCount() == 1);
}

void DragDropTests() {
    CodeEdit e;
    e.SetText("one two three");
    e.SetSelection(P(0, 4), P(0, 8));                                          // "two "
    CHECK(e.MoveSelectionTo(P(0, 13), false) && e.GetText() == "one threetwo " && e.SelectedText() == "two ");
    CHECK(e.Undo() && e.GetText() == "one two three");                         // one step
    e.SetSelection(P(0, 8), P(0, 13));
    CHECK(e.MoveSelectionTo(P(0, 0), false) && e.GetText() == "threeone two " && e.SelectedText() == "three");
    CHECK(!e.MoveSelectionTo(P(0, 2), false));                                  // inside the selection
    CHECK(e.MoveSelectionTo(P(0, 13), true) && e.GetText() == "threeone two three");   // a copy
    e.SetText("a\nb\nc"); e.SetSelection(P(0, 0), P(1, 0));
    CHECK(e.MoveSelectionTo(P(2, 1), false) && e.GetText() == "b\nca\n" && e.SelectedText() == "a\n");
    // the widget: press in the selection, drag, release
    Rig r;
    r.ce.SetText("one two three");
    r.Frames(2);
    ClickPos(r, P(0, 0));
    r.ce.SetSelection(P(0, 4), P(0, 7));
    r.Frames(1);
    r.sim.Move(r.X(P(0, 5)) + 1, r.Y(P(0, 5))); r.Frame(); r.sim.Down(); r.Frame();
    r.sim.Move(r.X(P(0, 13)) + 2, r.Y(P(0, 13))); r.Frame(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(r.ce.GetText() == "one  threetwo" && r.ce.SelectedText() == "two");
    // a press in the selection with no drag puts the caret there
    r.ce.SetSelection(P(0, 0), P(0, 3));
    r.Frames(1);
    ClickPos(r, P(0, 1));
    CHECK(At(r.ce, 0, 1));
    // Ctrl at the drop: a copy
    r.ce.SetSelection(P(0, 0), P(0, 3));
    r.Frames(30);                                                               // (not a double click)
    r.sim.Move(r.X(P(0, 1)) + 1, r.Y(P(0, 1))); r.Frame(); r.sim.Down(); r.Frame();
    r.sim.Move(r.X(P(0, 13)) + 2, r.Y(P(0, 13))); r.Frame(); r.sim.ctrl = true; r.Frame(); r.sim.Up(); r.Frame(); r.sim.ctrl = false;
    CHECK(r.ce.GetText() == "one  threetwoone");
}

void MultiCaretWidgetTests() {
    Rig r;
    r.ce.SetText("abc\nabc\nabc");
    r.Frames(2);
    ClickPos(r, P(0, 1));
    r.sim.ctrl = true; ClickPos(r, P(2, 1)); r.sim.ctrl = false; r.Frame();
    CHECK(r.ce.CaretCount() == 2);
    r.Type("Z");
    CHECK(r.ce.GetText() == "aZbc\nabc\naZbc");
    r.Key(Key_Backspace);
    CHECK(r.ce.GetText() == "abc\nabc\nabc");
    r.sim.ctrl = true; r.Named(UI::KeyId_Z); r.sim.ctrl = false;
    CHECK(r.ce.GetText() == "aZbc\nabc\naZbc" && r.ce.CaretCount() == 2);
    r.Key(Key_Escape);
    CHECK(r.ce.CaretCount() == 1);
    ClickPos(r, P(1, 0));                                                       // a plain click: one caret
    CHECK(r.ce.CaretCount() == 1 && At(r.ce, 1, 0));
    // Alt+drag: a column selection
    r.sim.Hold(UI::KeyId_Alt, true);
    r.sim.Move(r.X(P(0, 1)) + 1, r.Y(P(0, 1))); r.Frame(); r.sim.Down(); r.Frame();
    r.sim.Move(r.X(P(2, 3)) + 1, r.Y(P(2, 3))); r.Frame(); r.sim.Up(); r.Frame();
    r.sim.Hold(UI::KeyId_Alt, false); r.Frame();
    CHECK(r.ce.CaretCount() == 3 && r.ce.SelectedText() == "Zb");
    r.Type("-");
    CHECK(r.ce.GetText() == "a-c\na-\na-c");
    // Ctrl+D
    r.ce.SetText("x y x");
    r.Frames(1);
    ClickPos(r, P(0, 0));
    r.sim.ctrl = true; r.Named(UI::KeyId_D); r.Named(UI::KeyId_D); r.sim.ctrl = false;
    CHECK(r.ce.CaretCount() == 2 && r.ce.SelectedText() == "x");
    r.Type("w");
    CHECK(r.ce.GetText() == "w y w");
    // the other carets are drawn
    const Bitmap& bmp = r.sim.Draw(12, 14, 20);
    CHECK(PixelAt(bmp, (int)r.X(P(0, 1)), (int)r.Y(P(0, 1))) == C32(r.ce.Palette().caret));
    CHECK(PixelAt(bmp, (int)r.X(P(0, 5)), (int)r.Y(P(0, 5))) == C32(r.ce.Palette().caret));
}

int main() {
    RUN_SUITE("okayui codeedit");
    TextTests();
    TypingTests();
    DeleteTests();
    NewLineTests();
    TabTests();
    MoveTests();
    SelectionTests();
    ClipboardTests();
    UndoTests();
    FindTests();
    HighlightTests();
    BracketTests();
    MarkerTests();
    WidgetTests();
    ExtraTests();
    LineCommandTests();
    LineCommandKeyTests();
    DrawTests();
    ScriptLookTests();
    HostClipboardTests();
    ScriptEditorGapTests();
    AutoCloseTests();
    DedentTests();
    WordWrapTests();
    MultiLineFindTests();
    FoldTests();
    MultiCaretTests();
    DragDropTests();
    MultiCaretWidgetTests();
    FeatureDrawTests();
    PerfTests();
    TEST_MAIN_RESULT();
}
