// OkayUI::CodeEdit (the multi-line code editor): the editing operations driven directly, then the widget driven headless
// with synthetic mouse and keys (Sim), the highlighter, golden image, and a 100k-line performance check.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>

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
    e.SetFind("a\nb");
    CHECK(e.FindText().empty());
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
int ChR(unsigned c) { return (int)(c >> 24); }
int ChG(unsigned c) { return (int)((c >> 16) & 255); }
int ChB(unsigned c) { return (int)((c >> 8) & 255); }
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
    r.ce.SetMarkers({UI::CodeMarker{4, 0, -1, "unknown name", 0}, UI::CodeMarker{8, 0, -1, "extra brace", 0}});
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
    // a breakpoint washes its whole row, the gutter included (as ImGuiColorTextEdit fills the line); no dot
    CHECK(PixelAt(bmp, (int)(it.x + 2), (int)(rowY(2) + lh * 0.5f)) != C32(pal.gutter));
    CHECK(PixelAt(bmp, (int)(it.x + 2), (int)(rowY(1) + lh * 0.5f)) == C32(pal.gutter));
    CHECK(PixelAt(bmp, (int)(vr.x + vr.w - 3), (int)(rowY(2) + 1)) != PixelAt(bmp, (int)(vr.x + vr.w - 3), (int)(rowY(1) + 1)));
    // an error washes its whole row too, gutter included
    CHECK(PixelAt(bmp, (int)(it.x + 2), (int)(rowY(4) + 2)) != C32(pal.gutter));
    CHECK(PixelAt(bmp, (int)(it.x + 2), (int)(rowY(5) + 2)) == C32(pal.gutter));
    CHECK(PixelAt(bmp, (int)(vr.x + vr.w - 3), (int)(rowY(4) + 2)) != PixelAt(bmp, (int)(vr.x + vr.w - 3), (int)(rowY(6) + 2)));
    {   // the error's message is written after the end of the line, four characters on, in the error colour (clipped to the view)
        const float mx0 = r.ce.PosToScreen(P(8, (int)r.ce.Line(8).size())).x + 4.0f * r.ce.CharWidth();
        int red = 0, before = 0;
        for (int y = (int)rowY(8); y < (int)(rowY(8) + lh); ++y) {
            for (int x = (int)mx0; x < (int)(vr.x + vr.w); ++x) {
                const unsigned c = PixelAt(bmp, x, y);
                if (ChR(c) > ChG(c) + 40) ++red;
            }
            for (int x = (int)(mx0 - 3.0f * r.ce.CharWidth()); x < (int)mx0 - 1; ++x) {
                const unsigned c = PixelAt(bmp, x, y);
                if (ChR(c) > ChG(c) + 40) ++before;
            }
        }
        CHECK(red > 10);
        CHECK(before == 0);   // nothing between the end of the code and the message
    }
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


// The code font (launcher look): JetBrains Mono, the font the script editor's ImGui widget uses. Every character advances the
// same whole number of pixels, the text is drawn in runs (no per-character centring), each glyph lies in its own column, and
// the row is the font height times the spacing without rounding (as ImGuiColorTextEdit). At 16 px JetBrains Mono's advance
// (600 of 1320 units of ascent minus descent) is 7.27 px, drawn as 7: the same 7 px ImGui draws with PixelSnapH.
void CodeFontTests() {
    Rig r;
    UI::UseLauncherLook(true);
    r.ce.SetHighlighter(nullptr);
    r.ce.SetLineSpacing(1.55f);
    r.ce.SetText("iiiiWWWW\nmlmlmlml\n0123456789");
    r.Frame();
    CHECK(r.ce.MonospaceFont());
    CHECK(r.ce.CharWidth() == 7.0f);                                  // 16 px text (launcher look's text scale 2 x 8)
    CHECK(std::fabs(r.ce.LineHeight() - 16.0f * 1.55f) < 1e-4f);      // 24.8, not rounded
    UI::PushCodeFont();
    CHECK(UI::CodeFontActive());
    CHECK(UI::CalcTextSize("iiiiWWWW").x == UI::CalcTextSize("mlmlmlml").x);   // fixed width while pushed
    UI::PopCodeFont();
    CHECK(!UI::CodeFontActive());
    CHECK(UI::CalcTextSize("iiii").x != UI::CalcTextSize("WWWW").x);           // the UI font again
    const UI::DrawData data = UI::GetDrawData();
    const auto* v = static_cast<const UI::DrawVertex*>(data.vertices);
    const float top = r.ce.PosToScreen(P(0, 0)).y, x0 = r.ce.PosToScreen(P(0, 0)).x, cw = r.ce.CharWidth(), lh = r.ce.LineHeight();
    for (int line = 0; line < 3; ++line) {
        std::vector<float> centres;
        const float y0 = top + line * lh, y1 = y0 + lh;
        for (int c = 0; c < data.cmdCount; ++c) {
            const UI::DrawCmd& cmd = data.cmds[c];
            if (cmd.texture != data.atlasTexture) continue;
            for (int i = cmd.idxOffset; i + 5 < cmd.idxOffset + cmd.idxCount; i += 6) {
                float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
                for (int k = 0; k < 6; ++k) {
                    const UI::DrawVertex& q = v[data.indices[i + k]];
                    mnx = std::min(mnx, q.x); mxx = std::max(mxx, q.x); mny = std::min(mny, q.y); mxy = std::max(mxy, q.y);
                }
                const float cy = (mny + mxy) * 0.5f;
                const bool glyph = mxx - mnx >= 2.0f && mxx - mnx < cw * 2.0f && mnx < x0 + 20.0f * cw;   // not the row's 1 px box
                if (glyph && cy >= y0 && cy < y1 && mnx >= x0 - 1.0f) centres.push_back((mnx + mxx) * 0.5f);
            }
        }
        std::sort(centres.begin(), centres.end());
        const size_t want = line == 2 ? 10 : 8;
        CHECK(centres.size() == want);
        bool inCells = centres.size() == want;
        for (size_t k = 0; k < centres.size() && k < want; ++k)
            if (!(centres[k] >= x0 + (float)k * cw && centres[k] < x0 + (float)(k + 1) * cw)) inCells = false;
        CHECK(inCells);
    }
    // the gutter: " N " in characters plus the margin (one row tall with breakpoints shown); the numbers end two characters left of the text
    CHECK(std::fabs(r.ce.GutterWidth() - (std::floor(lh + 0.5f) + 3.0f * cw)) < 1e-3f);
    r.ce.SetLeftMargin(22.0f);
    r.ce.SetPadding(8.0f, 8.0f);
    r.Frame();
    CHECK(std::fabs(r.ce.GutterWidth() - (8.0f + 22.0f + 3.0f * cw)) < 1e-3f);
    CHECK(r.ce.ViewRect().y == r.ce.ItemRect().y + 8.0f);
    CHECK(r.ce.PosToScreen(P(0, 0)).x == r.ce.ViewRect().x);
    // no code font: the UI font as before (rounded rows)
    r.ce.SetUseCodeFont(false);
    r.Frame();
    CHECK(!r.ce.MonospaceFont());
    CHECK(r.ce.LineHeight() == std::floor(r.ce.LineHeight()));
    UI::UseLauncherLook(false);
    r.Frame();
    CHECK(r.ce.MonospaceFont());
    CHECK(r.ce.LineHeight() == std::floor(r.ce.LineHeight()));        // the 8x8 font: whole pixels
}

// The current line: a fill and a 1 px box across the whole row (gutter included) whenever there is no selection, dimmer
// without the keyboard; a warning marker uses the warning colours and has no underline.
void RowLookTests() {
    Rig r(560, 340);
    r.w = 520; r.h = 300;
    UI::CodePalette pal = UI::DefaultCodePalette();
    pal.currentLine = UI::RGBA(255, 255, 255, 40); pal.currentLineInactive = UI::RGBA(255, 255, 255, 20);
    pal.currentLineEdge = UI::RGBA(255, 0, 255, 255);
    r.ce.SetPalette(pal);
    r.ce.SetHighlighter(nullptr);
    r.ce.SetText("one\ntwo\nthree\nfour");
    UI::CodeMarker warn; warn.line = 3; warn.message = "careful"; warn.warning = true; warn.underline = false;
    r.ce.SetMarkers({warn});
    r.Frame();
    ClickPos(r, P(1, 1));
    const UI::Rect it = r.ce.ItemRect();
    const float y1 = r.ce.PosToScreen(P(1, 0)).y, lh = r.ce.LineHeight();
    const unsigned edge = C32(pal.currentLineEdge);
    unsigned focusedFill = 0;
    {
        const Bitmap& bmp = r.sim.Draw(12, 14, 20);
        int edgeTop = 0;
        for (int x = (int)it.x; x < (int)(it.x + 40); ++x) if (PixelAt(bmp, x, (int)y1) == edge) ++edgeTop;
        CHECK(edgeTop > 30);                                          // the box runs through the gutter
        CHECK(PixelAt(bmp, (int)(it.x + 3), (int)(y1 + lh * 0.5f)) != PixelAt(bmp, (int)(it.x + 3), (int)(r.ce.PosToScreen(P(2, 0)).y + lh * 0.5f)));
        focusedFill = PixelAt(bmp, (int)(it.x + 5), (int)(y1 + lh * 0.5f));
    }
    ClickAt(r, 5, 330);                                               // away: no keyboard
    {
        const Bitmap& b2 = r.sim.Draw(12, 14, 20);
        CHECK(PixelAt(b2, (int)it.x + 3, (int)y1) == edge);           // still boxed
        CHECK(PixelAt(b2, (int)(it.x + 5), (int)(y1 + lh * 0.5f)) != focusedFill);   // a dimmer fill
    }
    // a selection: no current-line box
    r.ce.SetSelection(P(0, 0), P(1, 2));
    r.Frame();
    const Bitmap& b3 = r.sim.Draw(12, 14, 20);
    CHECK(PixelAt(b3, (int)it.x + 3, (int)y1) != edge);
    // the warning: an amber message after the line
    const float y3 = r.ce.PosToScreen(P(3, 0)).y;
    const float mx0 = r.ce.PosToScreen(P(3, 4)).x + 4.0f * r.ce.CharWidth();
    int amber = 0, red = 0;
    for (int y = (int)y3; y < (int)(y3 + lh); ++y) for (int x = (int)mx0; x < (int)(mx0 + 60); ++x) {
        const unsigned c = PixelAt(b3, x, y);
        const int R = ChR(c), G = ChG(c), B = ChB(c);
        if (R > B + 40 && G > B + 20) ++amber;
        if (R > G + 60) ++red;
    }
    CHECK(amber > 10);
    CHECK(red == 0);
    // no underline under the warning's text
    int under = 0;
    const float x3 = r.ce.PosToScreen(P(3, 0)).x;
    for (int y = (int)(y3 + lh - 4); y < (int)(y3 + lh); ++y) for (int x = (int)x3; x < (int)(x3 + 4 * r.ce.CharWidth()); ++x) {
        const unsigned c = PixelAt(b3, x, y);
        if (ChR(c) > ChG(c) + 60) ++under;
    }
    CHECK(under == 0);
}


// Geometry capture (OkayUI::BeginGeometryCapture / EndGeometryCapture): what a CodeEdit draws is taken out of the frame whole.
// Drawn back from the capture it gives the same pixels as the frame drawn normally; the frame keeps what came before and after.
void CaptureTests() {
    for (int look = 0; look < 2; ++look) {
        Reset();
        if (look == 1) UI::UseLauncherLook(true);
        Sim sim(500, 300);
        CodeEdit ce;
        ce.SetLanguage(UI::OkayScriptLanguage());
        ce.SetText(kSample);
        ce.SetMarkers({UI::CodeMarker{4, 0, -1, "unknown name", 0}});
        UI::CapturedGeometry geo;
        bool capture = false;
        auto ui = [&] {
            UI::BareNextWindow();                                                        // a window before, in the main batch
            if (UI::Begin("before", 0, 0, 10, 10)) UI::GetWindowDrawList().AddRectFilled(UI::Vec2{0, 0}, UI::Vec2{8, 8}, UI::RGBA(255, 0, 0));
            UI::End();
            if (capture) UI::BeginGeometryCapture();
            UI::BareNextWindow();
            if (UI::Begin("code", 20, 20, 460, 260)) ce.Draw("ed", 460, 260);
            UI::End();
            if (capture) UI::EndGeometryCapture(geo);
            UI::BareNextWindow();                                                        // and one after
            if (UI::Begin("after", 490, 290, 10, 10)) UI::GetWindowDrawList().AddRectFilled(UI::Vec2{492, 292}, UI::Vec2{500, 300}, UI::RGBA(0, 255, 0));
            UI::End();
        };
        sim.Frames(2, ui);
        const Bitmap normal = sim.Draw(0, 0, 0);
        const int fullIdx = UI::GetDrawData().indexCount;
        capture = true;
        sim.Frame(ui);
        const UI::DrawData left = UI::GetDrawData();
        CHECK(!geo.indices.empty() && !geo.cmds.empty());
        CHECK(left.indexCount + (int)geo.indices.size() == fullIdx);   // moved out, nothing lost or doubled
        int sum = 0; bool inRange = true;
        for (const UI::DrawCmd& c : geo.cmds) { sum += c.idxCount; if (c.idxOffset < 0 || c.idxOffset + c.idxCount > (int)geo.indices.size()) inRange = false; }
        for (int ix : geo.indices) if (ix < 0 || ix >= (int)geo.vertices.size()) inRange = false;
        CHECK(sum == (int)geo.indices.size() && inRange);
        if (look == 1) {   // the launcher look's text samples the atlas
            bool atlas = false;
            for (const UI::DrawCmd& c : geo.cmds) if (c.texture && c.texture == left.atlasTexture) atlas = true;
            CHECK(atlas);
        }
        // what is left: the two corner squares, and no code
        Bitmap rest(500, 300); rest.Clear(0, 0, 0);
        UI::soft::Render(left, rest);
        CHECK(PixelAt(rest, 2, 2) == Rgba(255, 0, 0) && PixelAt(rest, 497, 297) == Rgba(0, 255, 0));
        CHECK(CountNot(rest, 20, 20, 480, 280, Rgba(0, 0, 0)) == 0);
        // the capture drawn after what was left gives the normal frame's pixels inside the code's rectangle
        UI::DrawData cd = left;
        cd.vertices = geo.vertices.data(); cd.vertexCount = (int)geo.vertices.size();
        cd.indices = geo.indices.data(); cd.indexCount = (int)geo.indices.size();
        cd.cmds = geo.cmds.data(); cd.cmdCount = (int)geo.cmds.size();
        UI::soft::Render(cd, rest);
        int differ = 0;
        for (int y = 20; y < 280; ++y) for (int x = 20; x < 480; ++x) if (PixelAt(rest, x, y) != PixelAt(normal, x, y)) ++differ;
        CHECK(differ == 0);
        // input still reaches the editor while it is captured: a click focuses it
        sim.Click(200, 100, ui);
        CHECK(ce.Focused());
        // an EndGeometryCapture without a Begin gives nothing and takes nothing
        capture = false;
        sim.Frame([&] { ui(); UI::EndGeometryCapture(geo); });
        CHECK(geo.indices.empty() && UI::GetDrawData().indexCount > 0);
        UI::UseLauncherLook(false);
    }
}

// A proportional font (the launcher look's Roboto): every glyph lands in its own cell, so the caret and selection line up.
// Also overwrite mode's caret and the hover help on the bare widget.
void ProportionalAndOverwriteTests() {
    {
        Rig r;
        UI::UseLauncherLook(true);
        r.ce.SetUseCodeFont(false);                   // the launcher look's own Roboto, not the code font
        r.ce.SetHighlighter(nullptr);
        r.ce.SetText("iiiiWWWW\nmlmlmlml");
        r.Frame();
        CHECK(!r.ce.MonospaceFont());
        CHECK(r.ce.CharWidth() >= UI::CalcTextSize("0").x);
        const UI::DrawData data = UI::GetDrawData();
        const UI::DrawData* dd = &data;
        const auto* v = static_cast<const UI::DrawVertex*>(dd->vertices);
        const float top = r.ce.PosToScreen(P(0, 0)).y, x0 = r.ce.PosToScreen(P(0, 0)).x, cw = r.ce.CharWidth();
        for (int line = 0; line < 2; ++line) {
            // the glyph quads of this line: textured triangles inside its row and right of the gutter
            std::vector<float> centres;
            const float y0 = top + line * r.ce.LineHeight(), y1 = y0 + r.ce.LineHeight();
            for (int c = 0; c < dd->cmdCount; ++c) {
                const UI::DrawCmd& cmd = dd->cmds[c];
                if (!cmd.texture || cmd.texture != dd->atlasTexture) continue;
                for (int i = cmd.idxOffset; i + 5 < cmd.idxOffset + cmd.idxCount; i += 6) {   // two triangles per glyph
                    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
                    for (int k = 0; k < 6; ++k) {
                        const UI::DrawVertex& q = v[dd->indices[i + k]];
                        mnx = std::min(mnx, q.x); mxx = std::max(mxx, q.x); mny = std::min(mny, q.y); mxy = std::max(mxy, q.y);
                    }
                    const float cy = (mny + mxy) * 0.5f;
                    const bool glyph = mxx - mnx >= 2.0f && mxx - mnx < cw * 2.0f && mnx < x0 + 20.0f * cw;   // not the row's 1 px box
                    if (glyph && cy >= y0 && cy < y1 && mnx >= x0 - 1.0f) centres.push_back((mnx + mxx) * 0.5f);
                }
            }
            std::sort(centres.begin(), centres.end());
            CHECK(centres.size() == 8);
            bool inCells = centres.size() == 8;
            for (size_t k = 0; k < centres.size() && k < 8; ++k)
                if (!(centres[k] >= x0 + (float)k * cw && centres[k] < x0 + (float)(k + 1) * cw)) inCells = false;
            CHECK(inCells);
        }
        UI::UseLauncherLook(false);
        r.Frame();
        CHECK(r.ce.MonospaceFont());                  // the classic 8x8 font
        CHECK(r.ce.CharWidth() == UI::CalcTextSize("0").x);
    }
    {   // overwrite caret: a block one cell wide instead of the 2-pixel bar
        Rig r;
        r.ce.SetHighlighter(nullptr);
        r.ce.SetText("abc");
        r.Frame();                                    // the geometry the click needs
        ClickPos(r, P(0, 1));
        CHECK(r.ce.Focused() && At(r.ce, 0, 1));
        r.Frame();
        const Bitmap bar = r.sim.Draw();
        r.ce.SetOverwrite(true);
        r.Frame();
        const Bitmap block = r.sim.Draw();
        const int cx = (int)r.X(P(0, 1)), cy = (int)r.Y(P(0, 1));
        // 4 pixels right of the caret's left edge: part of the block, not of the bar
        const int fx = cx + (int)(r.ce.CharWidth() * 0.6f);
        int changed = 0;
        for (int y = cy - 3; y <= cy + 3; ++y) if (PixelAt(bar, fx, y) != PixelAt(block, fx, y)) ++changed;
        CHECK(changed >= 5);
        CHECK(r.ce.TypeText("X") && r.ce.Line(0) == "aXc");
        r.ce.SetReadOnly(true);
        CHECK(!r.ce.TypeText("Y") && r.ce.Line(0) == "aXc");
    }
    {   // hover help on the bare widget: no tooltip while dragging or over an error message's line
        Rig r;
        r.ce.SetHighlighter(nullptr);
        r.ce.SetWordInfo({{"foo", "foo() does a thing"}});
        r.ce.SetText("foo bar\nfoo");
        r.Frame();
        CHECK(r.ce.WordAtScreen(r.X(P(0, 1)) + 1.0f, r.Y(P(0, 1))) == "foo");
        CHECK(r.ce.WordAtScreen(r.X(P(0, 5)) + 1.0f, r.Y(P(0, 5))) == "bar");
        CHECK(r.ce.WordAtScreen(r.X(P(0, 3)) + 1.0f, r.Y(P(0, 3))) == "");
        CHECK(r.ce.WordAtScreen(-5.0f, -5.0f) == "" && r.ce.WordAtScreen(NAN, NAN) == "");
        auto tipPixels = [&](CodePos at) {
            r.sim.Move(r.X(at) + 1.0f, r.Y(at)); r.Frame(); r.Frame();
            const Bitmap a = r.sim.Draw();
            r.sim.Move(r.X(P(0, 3)) + 1.0f, r.Y(P(0, 3))); r.Frame(); r.Frame();   // over the space: no tooltip
            const Bitmap b = r.sim.Draw();
            int d = 0;
            for (int y = 0; y < (int)r.sim.H; ++y) for (int x = 0; x < (int)r.sim.W; ++x) if (PixelAt(a, x, y) != PixelAt(b, x, y)) ++d;
            return d;
        };
        CHECK(tipPixels(P(0, 1)) > 100);              // "foo": a tooltip (plus the moved pointer's nothing)
        CHECK(tipPixels(P(0, 5)) < 20);               // "bar": no help, no tooltip
        UI::CodeMarker m; m.line = 1; m.message = "oops";
        r.ce.SetMarkers({m});
        // on line 1 the error message wins: same pixels whether or not the word has help
        r.sim.Move(r.X(P(1, 1)) + 1.0f, r.Y(P(1, 1))); r.Frame(); r.Frame();
        const Bitmap withHelp = r.sim.Draw();
        r.ce.SetWordInfo({});
        r.Frame(); r.Frame();
        const Bitmap noHelp = r.sim.Draw();
        int d = 0;
        for (int y = 0; y < (int)r.sim.H; ++y) for (int x = 0; x < (int)r.sim.W; ++x) if (PixelAt(withHelp, x, y) != PixelAt(noHelp, x, y)) ++d;
        CHECK(d == 0);
    }
}


// The launcher look's glyph atlas: Roboto and JetBrains Mono at six sizes first; the first time code is drawn past the largest
// (the script editor's code at 16 px x UI scale 0.7 to 2 x zoom 0.7 to 3, so up to 96 px) it grows, between two frames, to nine
// code sizes. Checked: the sizes, the memory, that every scale and zoom then draws from a bake at least as large, that UI text
// looks the same after the growth, and the growth path itself (height doubled until it fits, else the UI font alone).
void AtlasTests() {
    UI::UseLauncherLook(true);
    auto say = [](const char* what, const UI::FontAtlasInfo& a) {
        std::cout << "  " << what << ": " << a.width << " x " << a.height << ", glyphs in " << a.usedRows << " rows, " << a.grows
                  << " doubling(s), " << a.uiSizes << " UI sizes (to " << a.largestUiPx << " px), " << a.codeSizes << " code sizes (to "
                  << a.largestCodePx << " px), " << (double)a.bytes / (1024.0 * 1024.0) << " MB RGBA, built in " << a.buildMs << " ms\n";
    };
    auto upscaledPairs = [](float& worst) {
        int n = 0; worst = 0.0f;
        for (int u = 70; u <= 200; u += 5)
            for (int z = 7; z <= 30; ++z) {
                const float px = 16.0f * (float)u / 100.0f * (float)z / 10.0f, bake = UI::CodeFontBakedSizeFor(px);
                if (bake < px - 0.5f) ++n;
                worst = std::max(worst, px / bake);
            }
        return n;
    };
    const UI::FontAtlasInfo a = UI::GetFontAtlasInfo();
    say("atlas at start", a);
    CHECK(a.ok && a.codeSizes == 6 && a.uiSizes == 6 && a.largestCodePx == 46.0f);
    CHECK(a.width == 1024 && a.height == 1024 && a.usedRows > 0 && a.usedRows <= a.height - 4);
    CHECK(a.bytes == 1024ull * 1024ull * 4ull);
    // UI text drawn before the growth, to compare with after
    Sim sim(320, 120);
    auto ui = [] { if (UI::Begin("A", 5, 5, 300, 100)) { UI::Text("Roboto 0123 Wgjy"); UI::PushCodeFont(); UI::Text("mono 0123 {}"); UI::PopCodeFont(); } UI::End(); };
    sim.Frames(2, ui);
    const Bitmap before = sim.Draw();
    float worst = 0.0f;
    const int upBefore = upscaledPairs(worst);   // (asking for 96 px code here is what asks the atlas to grow)
    std::cout << "  before growing: " << upBefore << " of 648 UI scale x zoom pairs drawn larger than baked (up to x" << worst << ")\n";
    CHECK(upBefore > 0);
    CHECK(UI::GetFontAtlasInfo().height == 1024);                // not mid-frame: only at the next BeginFrame
    sim.Frames(2, ui);
    const UI::FontAtlasInfo g = UI::GetFontAtlasInfo();
    say("atlas after growing", g);
    CHECK(g.ok && g.codeSizes == 9 && g.largestCodePx == 96.0f && g.width == 1024 && g.height == 2048 && g.grows == 1);
    CHECK(g.usedRows > a.usedRows && g.usedRows <= g.height - 4);
    const int upAfter = upscaledPairs(worst);
    std::cout << "  after growing: " << upAfter << " pairs drawn larger than baked (largest ratio " << worst << ")\n";
    CHECK(upAfter == 0);
    const Bitmap after = sim.Draw();
    int diff = 0;
    for (int y = 0; y < before.h; ++y) for (int x = 0; x < before.w; ++x) diff += PixelAt(before, x, y) != PixelAt(after, x, y);
    CHECK(diff == 0);                                             // the UI and code text at 16 px look exactly the same
    if (diff) std::cerr << "  pixels changed by the growth: " << diff << "\n";
    sim.Frames(2, ui);
    CHECK(UI::GetFontAtlasInfo().height == 2048);                 // grown once, stays
    // the growth path on its own: start small, doubled until the code font fits
    const UI::FontAtlasInfo s6 = UI::TestBuildFontAtlas(128, 4096, 6);
    CHECK(s6.ok && s6.height == 1024 && s6.grows == 3 && s6.codeSizes == 6 && s6.usedRows == a.usedRows);
    const UI::FontAtlasInfo s9 = UI::TestBuildFontAtlas(256, 4096, 9);
    CHECK(s9.ok && s9.height == 2048 && s9.grows == 3 && s9.codeSizes == 9 && s9.usedRows == g.usedRows);
    // no room for the code font: the UI font alone (the code font falls back to it)
    const UI::FontAtlasInfo n = UI::TestBuildFontAtlas(1024, 1024, 9);
    CHECK(n.ok && n.codeSizes == 0 && n.uiSizes == 6 && n.height == 1024);
    // no room for anything: no atlas (the classic look's 8x8 font is used then)
    CHECK(!UI::TestBuildFontAtlas(64, 64, 6).ok);
    CHECK(UI::GetFontAtlasInfo().height == 2048 && UI::GetFontAtlasInfo().codeSizes == 9);   // the real atlas is untouched
    UI::UseLauncherLook(false);
}

// Step 30: what ImGuiColorTextEdit does that ours did not (docs/own_ui_code_editor.md, step 30).
void ParityTests() {
    {   // AltGr (Ctrl+Alt on Windows) types: { [ @ on many keyboards come with it. Ctrl alone does not type.
        Rig r;
        r.ce.SetText("");
        r.Frames(2);
        ClickPos(r, P(0, 0));
        r.sim.ctrl = true; r.sim.Hold(UI::KeyId_Alt, true);
        r.Type("{");
        r.sim.Hold(UI::KeyId_Alt, false);
        r.Type("x");
        r.sim.ctrl = false;
        CHECK(r.ce.GetText() == "{");
    }
    {   // Alt+Backspace undoes, as in the ImGui editor; Backspace alone still deletes
        Rig r;
        r.ce.SetText("ab");
        r.Frames(2);
        ClickPos(r, P(0, 2));
        r.Type("c");
        CHECK(r.ce.GetText() == "abc");
        r.sim.Hold(UI::KeyId_Alt, true); r.Key(Key_Backspace); r.sim.Hold(UI::KeyId_Alt, false); r.Frame();
        CHECK(r.ce.GetText() == "ab");
        r.Key(Key_Backspace);
        CHECK(r.ce.GetText() == "a");
    }
    {   // DropTypedText: the next Draw ignores the text (the host used it); the one after types again
        Rig r;
        r.ce.SetText("");
        r.Frames(2);
        ClickPos(r, P(0, 0));
        r.ce.DropTypedText();
        r.Type(")");
        CHECK(r.ce.GetText().empty());
        r.Type(")");
        CHECK(r.ce.GetText() == ")");
    }
    {   // without bracket indent, Enter copies the indent only (the ImGui editor's rule); with it, one more level after '{'
        CodeEdit e;
        e.SetText("  if {}");
        e.SetBracketIndent(false);
        e.SetCaret(P(0, 6));
        CHECK(e.NewLine());
        CHECK(e.GetText() == "  if {\n  }");
        CHECK(e.Caret() == P(1, 2));
        e.SetText("  if {}");
        e.SetBracketIndent(true);
        e.SetCaret(P(0, 6));
        CHECK(e.NewLine());
        CHECK(e.GetText() == "  if {\n      \n  }");
    }
    {   // the wheel: the step the host gives (an ImGui child scrolls 5 font heights), none with Ctrl (the host's zoom)
        Rig r;
        std::string t; for (int i = 0; i < 300; ++i) t += "line " + std::to_string(i) + "\n";
        r.ce.SetText(t);
        r.Frames(2);
        r.sim.Move(r.X(P(2, 1)), r.Y(P(2, 1))); r.Frame();
        r.sim.wheel = -1.0f; r.Frame();
        CHECK(std::fabs(r.ce.ScrollY() - 3.0f * r.ce.LineHeight()) < 0.01f);   // default: three rows
        r.ce.SetWheelStep(80.0f, 32.0f);
        r.sim.wheel = -1.0f; r.Frame();
        CHECK(std::fabs(r.ce.ScrollY() - (3.0f * r.ce.LineHeight() + 80.0f)) < 0.01f);
        const float y0 = r.ce.ScrollY();
        r.sim.ctrl = true; r.sim.wheel = -1.0f; r.Frame(); r.sim.ctrl = false; r.Frame();
        CHECK(r.ce.ScrollY() == y0);                                              // Ctrl+wheel: no scroll
    }
    {   // a drag after a double click extends by whole words, after a triple click by whole lines
        Rig r;
        r.ce.SetHighlighter(nullptr);
        r.ce.SetText("alpha beta gamma\ndelta epsilon\nzeta");
        r.Frames(2);
        r.sim.Move(r.X(P(0, 7)) + 1, r.Y(P(0, 7))); r.Frame();
        r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame();
        CHECK(r.ce.SelectedText() == "beta");
        r.sim.Move(r.X(P(0, 13)) + 1, r.Y(P(0, 13))); r.Frame();                  // into "gamma": the whole word joins
        CHECK(r.ce.SelectedText() == "beta gamma");
        r.sim.Move(r.X(P(0, 1)) + 1, r.Y(P(0, 1))); r.Frame();                    // back into "alpha": from its start to the end of "beta"
        CHECK(r.ce.SelectedText() == "alpha beta" && r.ce.Caret() == P(0, 0));
        r.sim.Up(); r.Frame();
        for (int i = 0; i < 40; ++i) r.Frame();
        r.sim.Move(r.X(P(1, 2)) + 1, r.Y(P(1, 2))); r.Frame();
        r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame();
        CHECK(r.ce.SelectedText() == "delta epsilon\n");
        r.sim.Move(r.X(P(2, 1)) + 1, r.Y(P(2, 1))); r.Frame();                    // down a line: both lines
        CHECK(r.ce.SelectedText() == "delta epsilon\nzeta");
        r.sim.Move(r.X(P(0, 3)) + 1, r.Y(P(0, 3))); r.Frame();                    // up a line: from line 0 to the end of line 1
        CHECK(r.ce.SelectedText() == "alpha beta gamma\ndelta epsilon\n");
        r.sim.Up(); r.Frame();
    }
    {   // the host draws the tooltip: Hover() says what is under the pointer, and OkayUI draws none itself
        Rig r;
        r.ce.SetHighlighter(nullptr);
        r.ce.SetWordInfo({{"foo", "foo() does a thing"}});
        r.ce.SetText("foo bar\nfoo");
        UI::CodeMarker m; m.line = 1; m.message = "oops"; m.warning = true;
        r.ce.SetMarkers({m});
        r.Frame();
        r.sim.Move(r.X(P(0, 1)) + 1.0f, r.Y(P(0, 1))); r.Frame(); r.Frame();
        CHECK(r.ce.Hover().kind == UI::CodeHover::Info && r.ce.Hover().text == "foo() does a thing" && r.ce.Hover().line == 0);
        CHECK(r.ce.Hover().tooltip == "foo() does a thing");
        const Bitmap own = r.sim.Draw();
        r.ce.SetHostDrawsTooltip(true);
        r.Frame(); r.Frame();
        CHECK(r.ce.Hover().kind == UI::CodeHover::Info);                          // still reported
        const Bitmap hosted = r.sim.Draw();
        r.sim.Move(r.X(P(0, 3)) + 1.0f, r.Y(P(0, 3))); r.Frame(); r.Frame();      // over the space: nothing
        CHECK(r.ce.Hover().kind == UI::CodeHover::None && r.ce.Hover().tooltip.empty());
        const Bitmap none = r.sim.Draw();
        int dOwn = 0, dHosted = 0;
        for (int y = 0; y < (int)r.sim.H; ++y)
            for (int x = 0; x < (int)r.sim.W; ++x) { dOwn += PixelAt(own, x, y) != PixelAt(none, x, y); dHosted += PixelAt(hosted, x, y) != PixelAt(none, x, y); }
        CHECK(dOwn > 100);      // OkayUI's own tooltip
        CHECK(dHosted < 20);    // none drawn by OkayUI when the host draws it
        r.sim.Move(r.X(P(1, 1)) + 1.0f, r.Y(P(1, 1))); r.Frame(); r.Frame();      // the warning's line: the marker wins
        CHECK(r.ce.Hover().kind == UI::CodeHover::Warning && r.ce.Hover().line == 1 && r.ce.Hover().text == "oops");
        CHECK(r.ce.Hover().tooltip == "Warning at line 2: oops");
    }
}
void LineCommandTests() {
    RUN_SUITE("codeedit: toggle comment, move lines, duplicate lines");
    using UI::CodePos;
    {   // toggle comment: comment, then uncomment, on one line and on a block with a blank line and different indents
        UI::CodeEdit ce; ce.SetText("a\n    b\n\n  c\nd");
        ce.SetSelection(CodePos{0, 0}, CodePos{3, 3});
        CHECK(ce.ToggleLineComment("//"));
        CHECK(ce.GetText() == "// a\n//     b\n\n//   c\nd");           // at the smallest indent (column 0 here)
        CHECK(ce.ToggleLineComment("//"));
        CHECK(ce.GetText() == "a\n    b\n\n  c\nd");                       // back to the start, exactly
        CHECK(ce.Undo() && ce.GetText() == "// a\n//     b\n\n//   c\nd");   // one undo step each
        CHECK(ce.Undo() && ce.GetText() == "a\n    b\n\n  c\nd");
        CHECK(!ce.CanUndo());
        UI::CodeEdit c2; c2.SetText("    x = 1;\n    y = 2;");
        c2.SetCaret(CodePos{0, 8});
        CHECK(c2.ToggleLineComment("//"));
        CHECK(c2.GetText() == "    // x = 1;\n    y = 2;");
        CHECK(c2.Caret().line == 0 && c2.Caret().col == 11);                  // the caret stays on the same text
        CHECK(c2.ToggleLineComment("//") && c2.GetText() == "    x = 1;\n    y = 2;" && c2.Caret().col == 8);
        // mixed: one commented, one not: the lines all get commented
        UI::CodeEdit c3; c3.SetText("// a\nb"); c3.SelectAll();
        CHECK(c3.ToggleLineComment("//") && c3.GetText() == "// // a\n// b");
        // a selection ending at the start of a line leaves that line alone
        UI::CodeEdit c4; c4.SetText("a\nb\nc"); c4.SetSelection(CodePos{0, 0}, CodePos{2, 0});
        CHECK(c4.ToggleLineComment("#") && c4.GetText() == "# a\n# b\nc");
        // nothing to do: blank lines only, read only, empty prefix
        UI::CodeEdit c5; c5.SetText("\n  \n"); c5.SelectAll();
        CHECK(!c5.ToggleLineComment("//"));
        UI::CodeEdit c6; c6.SetText("a"); c6.SetReadOnly(true);
        CHECK(!c6.ToggleLineComment("//") && !c6.MoveLines(true) && !c6.DuplicateLines());
        UI::CodeEdit c7; c7.SetText("a"); CHECK(!c7.ToggleLineComment(""));
    }
    {   // move lines
        UI::CodeEdit ce; ce.SetText("one\ntwo\nthree\nfour");
        ce.SetCaret(CodePos{1, 2});
        CHECK(ce.MoveLines(false));
        CHECK(ce.GetText() == "two\none\nthree\nfour" && ce.Caret().line == 0 && ce.Caret().col == 2);
        CHECK(!ce.MoveLines(false));                                          // already first
        CHECK(ce.MoveLines(true) && ce.MoveLines(true));
        CHECK(ce.GetText() == "one\nthree\ntwo\nfour" && ce.Caret().line == 2);
        ce.SetSelection(CodePos{1, 1}, CodePos{2, 2});                        // a two-line block moves together
        CHECK(ce.MoveLines(true));
        CHECK(ce.GetText() == "one\nfour\nthree\ntwo");
        CHECK(ce.Anchor().line == 2 && ce.Caret().line == 3);
        CHECK(!ce.MoveLines(true));                                           // the block touches the last line
        CHECK(ce.Undo() && ce.GetText() == "one\nthree\ntwo\nfour");
        CHECK(ce.Redo() && ce.GetText() == "one\nfour\nthree\ntwo");
        UI::CodeEdit one; one.SetText("only"); CHECK(!one.MoveLines(true) && !one.MoveLines(false));
    }
    {   // duplicate lines
        UI::CodeEdit ce; ce.SetText("a\nb\nc");
        ce.SetCaret(CodePos{1, 1});
        CHECK(ce.DuplicateLines());
        CHECK(ce.GetText() == "a\nb\nb\nc" && ce.Caret().line == 2 && ce.Caret().col == 1);
        ce.SetSelection(CodePos{0, 0}, CodePos{1, 1});
        CHECK(ce.DuplicateLines());
        CHECK(ce.GetText() == "a\nb\na\nb\nb\nc");
        CHECK(ce.Anchor().line == 2 && ce.Caret().line == 3);                 // the selection is on the copy
        CHECK(ce.Undo() && ce.GetText() == "a\nb\nb\nc");
        UI::CodeEdit last; last.SetText("x\ny"); last.SetCaret(CodePos{1, 0});
        CHECK(last.DuplicateLines() && last.GetText() == "x\ny\ny");
    }
    {   // keys: Alt+Up / Alt+Down move lines, Ctrl+/ toggles, Ctrl+Shift+D duplicates (through Draw)
        Reset();
        Sim sim(400, 300);
        UI::CodeEdit ce; ce.SetText("a\nb\nc");
        auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 400, 300)) ce.Draw("ce", 380, 280); UI::End(); };
        sim.Frames(2, ui);
        ce.Focus(); ce.SetCaret(CodePos{1, 0});
        sim.Frame(ui);
        sim.Hold(UI::KeyId_Alt, true); sim.Press(Key_Up); sim.Frame(ui); sim.Hold(UI::KeyId_Alt, false);
        CHECK(ce.GetText() == "b\na\nc");
        sim.Hold(UI::KeyId_Ctrl, true); sim.Hold(UI::KeyId_Slash, true); sim.Frame(ui); sim.Hold(UI::KeyId_Slash, false); sim.Hold(UI::KeyId_Ctrl, false);
        CHECK(ce.GetText() == "// b\na\nc");
        sim.Frame(ui);
        sim.Hold(UI::KeyId_Ctrl, true); sim.Hold(UI::KeyId_Shift, true); sim.shift = true; sim.Hold(UI::KeyId_D, true); sim.Frame(ui);
        CHECK(ce.GetText().size() > std::string("// b\na\nc").size());
    }
}

} // namespace

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
    DrawTests();
    ProportionalAndOverwriteTests();
    CodeFontTests();
    RowLookTests();
    CaptureTests();
    ParityTests();
    AtlasTests();
    PerfTests();
    TEST_MAIN_RESULT();
}
