// OkayUI::CodeEdit (the multi-line code editor): the editing operations driven directly, then the widget driven headless
// with synthetic mouse and keys (Sim), the highlighter, golden image, and a 100k-line performance check.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <chrono>
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
    DrawTests();
    PerfTests();
    TEST_MAIN_RESULT();
}
