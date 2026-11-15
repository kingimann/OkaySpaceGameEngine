// Headless tests for okay::text::Buffer (the code editor's data model): every feature on its own, hostile inputs (huge lines,
// invalid UTF-8, a 1 MB paste against a time budget, the undo cap, offset overflow, slow-search needles), and a randomised
// differential test against a trivially correct std::string model. OKAY_TEXT_FUZZ_ITERS scales the random test (default
// 4000 steps per seed), OKAY_TEXT_FUZZ_SEED changes the seed.
#include "test_framework.hpp"
#include "okay/Text/TextBuffer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace okay::text;

namespace {

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ull + 88172645463325252ull) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (std::uint32_t)n); }
};

double Ms(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

// Sanitizers slow everything down a lot; budgets are multiplied.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
constexpr double kSlow = 20.0;
#else
constexpr double kSlow = 1.0;
#endif

bool Consistent(const Buffer& b) {
    const std::string t = b.Text();
    if (t.size() != b.ByteSize()) return false;
    if (!Buffer::IsValidUtf8(t)) return false;
    std::size_t nl = 0;
    for (char c : t) if (c == '\n') ++nl;
    if (b.LineCount() != nl + 1) return false;
    if (t.find('\r') != std::string::npos) return false;
    for (std::size_t i = 0; i < b.LineCount(); ++i) if (b.Line(i).find('\n') != std::string_view::npos) return false;
    for (const Selection& s : b.Selections()) if (b.Clamp(s.anchor) != s.anchor || b.Clamp(s.head) != s.head) return false;
    return true;
}

void TestBasics() {
    RUN_SUITE("basics");
    Buffer b;
    CHECK(b.LineCount() == 1 && b.Text().empty() && b.ByteSize() == 0 && b.Line(0).empty() && b.Line(5).empty());
    CHECK(b.SetText("one\ntwo\r\nthree\rfour"));
    CHECK(b.Text() == "one\ntwo\nthree\nfour" && b.LineCount() == 4 && b.Line(1) == "two" && b.Line(3) == "four");
    CHECK(b.SetText("x\n") && b.LineCount() == 2 && b.Line(1).empty());       // a trailing newline is a real empty last line
    CHECK(b.SetText("") && b.LineCount() == 1);

    b.SetText("hello world\nsecond line\nthird");
    auto e = b.Insert(Pos{0, 5}, ",");
    CHECK(e && *e == (Pos{0, 6}) && b.Line(0) == "hello, world");
    e = b.Insert(Pos{1, 0}, "a\nb\nc");
    CHECK(e && *e == (Pos{3, 1}) && b.LineCount() == 5 && b.Line(1) == "a" && b.Line(2) == "b" && b.Line(3) == "csecond line");
    CHECK(b.Erase(Pos{1, 0}, Pos{3, 1}) && b.Line(1) == "second line" && b.LineCount() == 3);
    CHECK(!b.Erase(Pos{1, 2}, Pos{1, 2}));                                    // nothing removed
    CHECK(b.Erase(Pos{1, 3}, Pos{1, 0}) && b.Line(1) == "ond line");          // either order
    auto r = b.Replace(Pos{0, 0}, Pos{0, 5}, "HI\nthere");
    CHECK(r && *r == (Pos{1, 5}) && b.Line(0) == "HI" && b.Line(1) == "there, world");
    CHECK(Consistent(b));
}

void TestUtf8() {
    RUN_SUITE("utf8 and sanitising");
    CHECK(Buffer::Sanitize("a\r\nb\rc\nd") == "a\nb\nc\nd");
    CHECK(Buffer::Sanitize(std::string("a\0b", 3)) == "a\xEF\xBF\xBD" "b");
    CHECK(Buffer::Sanitize("\xFF") == "\xEF\xBF\xBD");
    CHECK(Buffer::Sanitize("\xC0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD");        // overlong
    CHECK(Buffer::Sanitize("\xED\xA0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");  // surrogate
    CHECK(Buffer::Sanitize("\xF4\x90\x80\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");  // above U+10FFFF
    CHECK(Buffer::Sanitize("\xE2\x82") == "\xEF\xBF\xBD\xEF\xBF\xBD");        // truncated
    CHECK(Buffer::Sanitize("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80") == "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK(Buffer::IsValidUtf8("\xC3\xA9") && !Buffer::IsValidUtf8("\xC3") && Buffer::IsValidUtf8(""));

    Buffer b;
    b.SetText("a\xC3\xA9" "b\xE2\x82\xAC" "c");                              // a e-acute b euro c
    CHECK(b.Clamp(Pos{0, 2}) == (Pos{0, 1}));                                  // inside e-acute: start of it
    CHECK(b.Clamp(Pos{0, 5}) == (Pos{0, 4}));                                  // inside the euro
    CHECK(b.Clamp(Pos{9, 9}) == (Pos{0, b.LineLength(0)}));
    CHECK(b.Next(Pos{0, 1}) == (Pos{0, 3}) && b.Prev(Pos{0, 3}) == (Pos{0, 1}));
    CHECK(b.Next(Pos{0, 4}) == (Pos{0, 7}) && b.Prev(Pos{0, 7}) == (Pos{0, 4}));
    CHECK(b.CodepointCount(0) == 5);
    b.SetText("ab\ncd");
    CHECK(b.Next(Pos{0, 2}) == (Pos{1, 0}) && b.Prev(Pos{1, 0}) == (Pos{0, 2}) && b.Prev(Pos{0, 0}) == (Pos{0, 0}) && b.Next(Pos{1, 2}) == (Pos{1, 2}));
    b.SetText("x");
    b.Insert(Pos{0, 1}, "\xFF\xC3");                                           // broken input repaired, never stored
    CHECK(Buffer::IsValidUtf8(b.Text()) && b.Text() == "x\xEF\xBF\xBD\xEF\xBF\xBD");
}

void TestOffsetsColumns() {
    RUN_SUITE("offsets and columns");
    Buffer b;
    b.SetText("abc\n\nde\xC3\xA9");
    CHECK(b.OffsetOf(Pos{0, 0}) == 0 && b.OffsetOf(Pos{0, 3}) == 3 && b.OffsetOf(Pos{1, 0}) == 4 && b.OffsetOf(Pos{2, 0}) == 5);
    CHECK(b.OffsetOf(Pos{2, 4}) == 9 && b.ByteSize() == 9);
    for (std::size_t o = 0; o <= b.ByteSize(); ++o) {
        const Pos p = b.PosOf(o);
        const std::string t = b.Text();
        const bool boundary = o == t.size() || (t[o] & 0xC0) != 0x80;
        if (boundary) CHECK(b.OffsetOf(p) == o);
    }
    CHECK(b.PosOf(1000) == b.EndPos() && b.PosOf((std::size_t)-1) == b.EndPos());
    CHECK(b.OffsetOf(Pos{(std::size_t)-1, (std::size_t)-1}) == b.ByteSize());   // overflow-sized positions are clamped
    CHECK(b.OffsetOf(Pos{0, (std::size_t)-1}) == 3);

    Options o; o.tabSize = 4;
    Buffer t(o);
    t.SetText("\tab\n\xC3\xA9x\n  \tz");
    CHECK(t.DisplayColumn(0, 1) == 4 && t.DisplayColumn(0, 3) == 6);
    CHECK(t.DisplayColumn(1, 2) == 1 && t.DisplayColumn(2, 3) == 4 && t.DisplayColumn(2, 100) == 5);
    CHECK(t.ByteForDisplayColumn(0, 0) == 0 && t.ByteForDisplayColumn(0, 3) == 0 && t.ByteForDisplayColumn(0, 4) == 1 && t.ByteForDisplayColumn(0, 99) == 3);
    CHECK(t.ByteForDisplayColumn(1, 1) == 2);                                   // after the two-byte e-acute
    for (std::size_t line = 0; line < t.LineCount(); ++line)
        for (std::size_t c = 0; c <= t.LineLength(line); ++c) {
            if (c < t.LineLength(line) && (t.Line(line)[c] & 0xC0) == 0x80) continue;
            CHECK(t.ByteForDisplayColumn(line, t.DisplayColumn(line, c)) == c || t.Line(line)[c - 1] == '\t');
        }
    Options o2; o2.tabSize = 1000;
    CHECK(Buffer(o2).GetOptions().tabSize == 16);
    Options o3; o3.tabSize = -5;
    CHECK(Buffer(o3).GetOptions().tabSize == 1);
}

void TestUndo() {
    RUN_SUITE("undo, redo, groups");
    Buffer b;
    b.SetText("abc");
    b.SetCursor(Pos{0, 3});
    b.Insert(Pos{0, 3}, "d");
    b.Insert(Pos{0, 4}, "e");
    CHECK(b.Text() == "abcde" && b.UndoDepth() == 2);
    CHECK(b.Undo() && b.Text() == "abcd");
    CHECK(b.Redo() && b.Text() == "abcde" && !b.Redo());
    CHECK(b.Undo() && b.Undo() && b.Text() == "abc" && !b.Undo() && !b.CanUndo() && b.CanRedo());
    b.Insert(Pos{0, 0}, "X");                                                   // a new edit drops redo
    CHECK(!b.CanRedo() && b.Text() == "Xabc");

    b.SetText("one");
    b.BeginGroup();
    b.Insert(Pos{0, 3}, " two");
    b.BeginGroup();                                                             // nesting joins the outer group
    b.Insert(Pos{0, 7}, " three\nfour");
    b.EndGroup();
    CHECK(b.InGroup() && !b.Undo() && !b.CanUndo());                            // undo refused while a group is open
    b.EndGroup();
    CHECK(!b.InGroup() && b.UndoDepth() == 1 && b.Text() == "one two three\nfour");
    CHECK(b.Undo() && b.Text() == "one" && b.Redo() && b.Text() == "one two three\nfour");
    b.EndGroup(); b.EndGroup();                                                 // stray ends are harmless
    CHECK(b.UndoDepth() == 1);
    b.BeginGroup(); b.EndGroup();
    CHECK(b.UndoDepth() == 1);                                                  // an empty group is not a step

    // selections come back with undo and redo
    b.SetText("hello world");
    b.SetSelections({Selection{Pos{0, 0}, Pos{0, 5}}});
    b.TypeText("bye");
    CHECK(b.Text() == "bye world" && b.Selections().size() == 1 && b.Selections()[0].head == (Pos{0, 3}) && b.Selections()[0].Empty());
    CHECK(b.Undo() && b.Text() == "hello world" && b.Selections()[0] == (Selection{Pos{0, 0}, Pos{0, 5}}));
    CHECK(b.Redo() && b.Selections()[0].head == (Pos{0, 3}));

    // undo of a multi-line erase and an edit that crosses lines
    b.SetText("a\nb\nc\nd");
    b.Erase(Pos{0, 1}, Pos{2, 1});
    CHECK(b.Text() == "a\nd" && b.Undo() && b.Text() == "a\nb\nc\nd" && b.Redo() && b.Text() == "a\nd");

    // the depth cap: only the newest 5 groups are kept
    Options o; o.undoGroups = 5;
    Buffer c(o);
    for (int i = 0; i < 100; ++i) c.Insert(c.EndPos(), "x");
    CHECK(c.UndoDepth() == 5);
    int n = 0; while (c.Undo()) ++n;
    CHECK(n == 5 && c.ByteSize() == 95);
    // the byte cap
    Options ob; ob.undoBytes = 1000;
    Buffer d(ob);
    for (int i = 0; i < 100; ++i) d.Insert(d.EndPos(), std::string(100, 'y'));
    CHECK(d.UndoDepth() <= 10 && d.UndoDepth() >= 1 && d.ByteSize() == 10000);
    d.SetOptions([&] { Options q = ob; q.undoGroups = 2; return q; }());
    CHECK(d.UndoDepth() == 2);
    c.ClearHistory();
    CHECK(!c.CanUndo() && !c.CanRedo());
}

void TestSelectionsAndMultiCursor() {
    RUN_SUITE("selections and several carets");
    Buffer b;
    b.SetText("abc\ndef\nghi");
    b.SetSelections({Selection{Pos{9, 9}, Pos{9, 9}}, Selection{Pos{0, 1}, Pos{0, 1}}, Selection{Pos{0, 1}, Pos{0, 1}}});
    CHECK(b.Selections().size() == 2 && b.Selections()[0].head == (Pos{0, 1}) && b.Selections()[1].head == (Pos{2, 3}));   // sorted, merged, clamped
    b.SetSelections({Selection{Pos{0, 0}, Pos{0, 3}}, Selection{Pos{0, 2}, Pos{1, 1}}});
    CHECK(b.Selections().size() == 1 && b.Selections()[0].Lo() == (Pos{0, 0}) && b.Selections()[0].Hi() == (Pos{1, 1}));
    b.SetSelections({});
    CHECK(b.Selections().size() == 1);

    b.SetText("abc\ndef\nghi");
    b.SetSelections({Selection{Pos{0, 1}, Pos{0, 1}}, Selection{Pos{1, 1}, Pos{1, 1}}, Selection{Pos{2, 1}, Pos{2, 1}}});
    CHECK(b.TypeText("XY"));
    CHECK(b.Text() == "aXYbc\ndXYef\ngXYhi" && b.Selections().size() == 3);
    CHECK(b.Selections()[0].head == (Pos{0, 3}) && b.Selections()[1].head == (Pos{1, 3}) && b.Selections()[2].head == (Pos{2, 3}));
    CHECK(b.UndoDepth() == 1 && b.Undo() && b.Text() == "abc\ndef\nghi" && b.Selections().size() == 3);
    b.Redo();
    CHECK(b.Backspace() && b.Text() == "aXbc\ndXef\ngXhi");
    CHECK(b.DeleteForward() && b.Text() == "aXc\ndXf\ngXi");
    // two carets on one line: edits do not disturb each other
    b.SetText("a b c");
    b.SetSelections({Selection{Pos{0, 1}, Pos{0, 1}}, Selection{Pos{0, 3}, Pos{0, 3}}});
    b.TypeText("\n");
    CHECK(b.Text() == "a\n b\n c" && b.Selections()[0].head == (Pos{1, 0}) && b.Selections()[1].head == (Pos{2, 0}));
    // selections replaced, non-empty ones deleted by Backspace
    b.SetText("one two three");
    b.SetSelections({Selection{Pos{0, 0}, Pos{0, 3}}, Selection{Pos{0, 8}, Pos{0, 13}}});
    CHECK(b.SelectedText() == "one\nthree");
    CHECK(b.Backspace() && b.Text() == " two ");
    // backspace at a line start joins lines; at the very start nothing happens
    b.SetText("ab\ncd");
    b.SetCursor(Pos{1, 0});
    CHECK(b.Backspace() && b.Text() == "abcd" && b.Selections()[0].head == (Pos{0, 2}));
    b.SetCursor(Pos{0, 0});
    CHECK(!b.Backspace() && b.Text() == "abcd");
    b.SetCursor(Pos{0, 4});
    CHECK(!b.DeleteForward());
    // carets next to each other with Backspace do not eat the same character twice
    b.SetText("abcd");
    b.SetSelections({Selection{Pos{0, 2}, Pos{0, 2}}, Selection{Pos{0, 3}, Pos{0, 3}}});
    CHECK(b.Backspace() && b.Text() == "ad" && Consistent(b));
    // edits move the carets
    b.SetText("abc\ndef");
    b.SetSelections({Selection{Pos{1, 2}, Pos{1, 2}}});
    b.Insert(Pos{0, 1}, "\n\n");
    CHECK(b.Selections()[0].head == (Pos{3, 2}));
    b.Erase(Pos{0, 0}, Pos{2, 0});
    CHECK(b.Selections()[0].head == (Pos{1, 2}) && b.Text() == "bc\ndef");
    // size limit refuses typing for every caret
    Options o; o.maxBytes = 10;
    Buffer c(o);
    c.SetText("12345");
    c.SetSelections({Selection{Pos{0, 1}, Pos{0, 1}}, Selection{Pos{0, 3}, Pos{0, 3}}, Selection{Pos{0, 5}, Pos{0, 5}}});
    CHECK(!c.TypeText("ab") && c.Text() == "12345");
    CHECK(c.TypeText("a") && c.Text() == "1a23a45a");
}

void TestWords() {
    RUN_SUITE("words");
    Buffer b;
    b.SetText("foo_bar  baz(qux) \xC3\xA9t\xC3\xA9\n  next");
    CHECK(b.NextWordEnd(Pos{0, 0}) == (Pos{0, 9}));                             // "foo_bar  "
    CHECK(b.NextWordEnd(Pos{0, 9}) == (Pos{0, 12}));                            // "baz"
    CHECK(b.NextWordEnd(Pos{0, 12}) == (Pos{0, 13}));                           // "("
    CHECK(b.PrevWordStart(Pos{0, 9}) == (Pos{0, 0}));
    CHECK(b.PrevWordStart(Pos{0, 12}) == (Pos{0, 9}));
    CHECK(b.PrevWordStart(Pos{0, 13}) == (Pos{0, 12}));
    CHECK(b.PrevWordStart(Pos{1, 0}) == (Pos{0, b.LineLength(0)}) && b.NextWordEnd(Pos{0, b.LineLength(0)}) == (Pos{1, 0}));
    CHECK(b.PrevWordStart(Pos{0, 0}) == (Pos{0, 0}) && b.NextWordEnd(b.EndPos()) == b.EndPos());
    const Range w = b.WordAt(Pos{0, 2});
    CHECK(w.begin == (Pos{0, 0}) && w.end == (Pos{0, 7}));
    CHECK(b.WordAt(Pos{0, 7}).begin == (Pos{0, 0}));                            // right after a word counts
    CHECK(b.WordAt(Pos{0, 8}).Empty());                                         // between blanks
    const Range u = b.WordAt(Pos{0, b.LineLength(0) - 1});                      // a word with non-ASCII letters stays whole
    CHECK(u.end.col == b.LineLength(0) && Buffer::IsValidUtf8(b.Slice(u.begin, u.end)));
    b.SetText("one two three");
    b.SetCursor(Pos{0, 7});
    CHECK(b.DeleteWord(false) && b.Text() == "one  three");
    CHECK(b.DeleteWord(true) && b.Text() == "one three");
}

void TestIndent() {
    RUN_SUITE("indent");
    Options o; o.tabSize = 4; o.insertSpaces = true;
    Buffer b(o);
    b.SetText("a\n\n  b\n\tc");
    CHECK(b.IndentUnit() == "    " && b.IndentBytes(2) == 2 && b.IndentOf(3) == "\t" && b.IndentBytes(99) == 0);
    CHECK(b.IndentLines(0, 99));
    CHECK(b.Text() == "    a\n\n      b\n    \tc" && b.UndoDepth() == 1);       // the empty line is skipped
    CHECK(b.OutdentLines(0, 3));
    CHECK(b.Text() == "a\n\n  b\n\tc");                                          // spaces up to 4, or one tab
    CHECK(!b.OutdentLines(0, 0) && !b.IndentLines(50, 60) && !b.IndentLines(3, 1));
    CHECK(b.Undo() && b.Text() == "    a\n\n      b\n    \tc");
    Options t; t.insertSpaces = false;
    Buffer c(t);
    c.SetText("x");
    c.IndentLines(0, 0);
    CHECK(c.Text() == "\tx");

    Buffer d(o);
    d.SetText("  if (x) {");
    CHECK(d.NewlineText(Pos{0, 10}) == "\n      ");                              // indent 2 plus one level after '{'
    CHECK(d.NewlineText(Pos{0, 5}) == "\n  ");                                    // not after a bracket
    CHECK(d.NewlineText(Pos{0, 1}) == "\n ");                                     // inside the indent: only what is before the caret
    d.SetText("f(  ");
    CHECK(d.NewlineText(Pos{0, 4}) == "\n    ");                                 // blanks after the bracket still count
    d.SetText("");
    CHECK(d.NewlineText(Pos{0, 0}) == "\n");
}

void TestBrackets() {
    RUN_SUITE("brackets");
    Buffer b;
    b.SetText("f(a[1], {\n  b\n})");
    auto m = b.MatchBracket(Pos{0, 1});
    CHECK(m && m->open == (Pos{0, 1}) && m->close == (Pos{2, 1}));
    m = b.MatchBracket(Pos{0, 2});                                                // caret after '(' also counts
    CHECK(m && m->open == (Pos{0, 1}));
    m = b.MatchBracket(Pos{2, 2});                                                // after ')'
    CHECK(m && m->close == (Pos{2, 1}) && m->open == (Pos{0, 1}));
    m = b.MatchBracket(Pos{2, 0});                                                // on '}'
    CHECK(m && m->open == (Pos{0, 8}));
    m = b.MatchBracket(Pos{0, 4});                                                // after '[': the pair of '[' and ']'
    CHECK(m && m->open == (Pos{0, 3}) && m->close == (Pos{0, 5}));
    CHECK(!b.MatchBracket(Pos{1, 3}));                                            // on 'b' with no bracket next to it
    b.SetText("(  ]  )");
    m = b.MatchBracket(Pos{0, 0});
    CHECK(m && m->close == (Pos{0, 6}));                                          // only the same kind counts
    b.SetText("(((");
    CHECK(!b.MatchBracket(Pos{0, 0}));                                            // unbalanced
    b.SetText("(\")\" )");                                                        // the ')' in the string is skipped
    CHECK(b.MatchBracket(Pos{0, 0})->close == (Pos{0, 2}));
    m = b.MatchBracket(Pos{0, 0}, [](const Pos& p) { return p.col >= 1 && p.col <= 3; });
    CHECK(m && m->close == (Pos{0, 5}));
    CHECK(!b.MatchBracket(Pos{0, 0}, [](const Pos&) { return true; }));
    // a scan limit stops a hostile scan
    b.SetText("(" + std::string(100000, 'x') + ")");
    CHECK(!b.MatchBracket(Pos{0, 0}, SkipFn(), 1000));
    CHECK(b.MatchBracket(Pos{0, 0}, SkipFn(), 1000000));
    std::string deep(50000, '(');
    deep += std::string(50000, ')');
    b.SetText(deep);
    CHECK(b.MatchBracket(Pos{0, 0}) && b.MatchBracket(Pos{0, 0})->close == (Pos{0, 99999}));
}

void TestFind() {
    RUN_SUITE("find and replace");
    Buffer b;
    b.SetText("Foo foo FOO\nbar foobar\nfoo");
    auto r = b.Find("foo", Pos{0, 0});
    CHECK(r && r->begin == (Pos{0, 4}) && r->end == (Pos{0, 7}));
    r = b.Find("foo", Pos{0, 5});
    CHECK(r && r->begin == (Pos{1, 4}));
    r = b.Find("foo", Pos{2, 1});
    CHECK(r && r->begin == (Pos{0, 4}));                                          // wrapped
    CHECK(!b.Find("foo", Pos{2, 1}, FindFlags::MatchCase, false, false));
    r = b.Find("foo", Pos{0, 0}, FindFlags::None);
    CHECK(r && r->begin == (Pos{0, 0}));                                          // case folded
    r = b.Find("foo", Pos{1, 4}, FindFlags::None, true);
    CHECK(r && r->begin == (Pos{0, 8}));                                          // backwards: the last one before
    r = b.Find("foo", Pos{0, 0}, FindFlags::None, true);
    CHECK(r && r->begin == (Pos{2, 0}));                                          // backwards wrap
    CHECK(!b.Find("foo", Pos{0, 0}, FindFlags::None, true, false));
    r = b.Find("foo", Pos{0, 0}, FindFlags::WholeWord | FindFlags::MatchCase);
    CHECK(r && r->begin == (Pos{0, 4}));
    CHECK(b.FindAll("foo", FindFlags::WholeWord).size() == 4);                    // Foo foo FOO foo; "foobar" is not whole
    CHECK(b.FindAll("foo", FindFlags::None).size() == 5);
    CHECK(b.FindAll("foo", FindFlags::MatchCase).size() == 3);
    CHECK(!b.Find("", Pos{0, 0}) && b.FindAll("").empty() && b.FindAll("zzz").empty());
    CHECK(b.FindAll("foo", FindFlags::None, 2).size() == 2);
    // across lines
    r = b.Find("FOO\nbar", Pos{0, 0}, FindFlags::MatchCase);
    CHECK(r && r->begin == (Pos{0, 8}) && r->end == (Pos{1, 3}));
    CHECK(b.Find("FOO\r\nbar", Pos{0, 0}) == r);                                  // the needle is normalised too
    // overlapping matches are not reported twice
    b.SetText("aaaa");
    CHECK(b.FindAll("aa").size() == 2);
    // UTF-8 needle
    b.SetText("caf\xC3\xA9 caf\xC3\xA9");
    CHECK(b.FindAll("\xC3\xA9").size() == 2 && b.Find("\xC3\xA9", Pos{0, 5})->begin == (Pos{0, 9}));
    // replace all: one undo step
    b.SetText("x foo y foo\nfoo");
    CHECK(b.ReplaceAll("foo", "[bar\nbaz]") == 3);
    CHECK(b.Text() == "x [bar\nbaz] y [bar\nbaz]\n[bar\nbaz]" && b.UndoDepth() == 1);
    CHECK(b.Undo() && b.Text() == "x foo y foo\nfoo");
    CHECK(b.ReplaceAll("foo", "") == 3 && b.Text() == "x  y \n");
    CHECK(b.ReplaceAll("nothing", "z") == 0);
    b.SetText("a-a-a");
    CHECK(b.ReplaceAll("-", "--") == 2 && b.Text() == "a--a--a");
    b.SetText("one\ntwo");
    CHECK(b.ReplaceAll("e\ntw", "E TW") == 1 && b.Text() == "onE TWo");
    // the size limit refuses a growing replace and changes nothing
    Options o; o.maxBytes = 12;
    Buffer c(o);
    c.SetText("a a a a");
    CHECK(c.ReplaceAll("a", "bbb") == 0 && c.Text() == "a a a a");
    CHECK(c.ReplaceAll("a", "bb") == 4 && c.Text() == "bb bb bb bb" && c.ByteSize() == 11);
}

void TestEditorSupport() {
    RUN_SUITE("what a widget needs: options, reopen, line marks, search per line");
    {   // input repair options
        Options o; o.badByte = "?"; o.dropControls = true;
        Buffer b(o);
        b.SetText(std::string("a\x01\x7f" "b\xFF" "c\0d", 9));
        CHECK(b.Text() == "ab?cd");
        CHECK(b.Repair("x\ty\r\nz\x02") == "x\ty\nz");
        CHECK(Buffer::Sanitize("a\x01") == "a\x01");                              // the defaults keep other controls
    }
    {   // reopen the newest step: typing a word is one undo step
        Buffer b;
        b.SetCursor(Pos{0, 0});
        b.Insert(Pos{0, 0}, "a");
        CHECK(b.ReopenLastGroup() && b.InGroup() && !b.ReopenLastGroup());
        b.Insert(Pos{0, 1}, "b");
        b.EndGroup();
        CHECK(b.UndoDepth() == 1 && b.Text() == "ab");
        CHECK(b.Undo() && b.Text().empty() && b.UndoDepth() == 0);
        Buffer c;
        CHECK(!c.ReopenLastGroup());
        c.Insert(Pos{0, 0}, "x");
        c.Undo();
        CHECK(!c.ReopenLastGroup());                                              // nothing to reopen once undone
        // the selections a step restores can be updated after the fact
        Buffer d;
        d.SetText("hello");
        d.SetCursor(Pos{0, 5});
        d.Insert(Pos{0, 5}, "!");
        d.SetCursor(Pos{0, 2});
        d.RecordSelectionsInLastGroup();
        d.SetCursor(Pos{0, 0});
        d.Undo();
        d.Redo();
        CHECK(d.Selections()[0].head == (Pos{0, 2}));
    }
    {   // line marks follow edits, undo and redo; dropped ones do not come back
        Buffer b;
        b.SetText("0\n1\n2\n3\n4\n5");
        const std::size_t set = b.NewMarkSet();
        b.SetMarks(set, {LineMark{1, 10}, LineMark{3, 11}, LineMark{3, 12}, LineMark{5, 13}});
        b.Insert(Pos{2, 1}, "\nx\ny");                                           // two lines added after line 2
        CHECK(b.Marks(set).size() == 4 && b.Marks(set)[0].line == 1 && b.Marks(set)[1].line == 5 && b.Marks(set)[2].line == 5 && b.Marks(set)[3].line == 7);
        CHECK(b.Marks(set)[1].tag == 11 && b.Marks(set)[2].tag == 12);              // order and tags kept
        b.Erase(Pos{2, 0}, Pos{4, 1});                                             // two lines removed
        CHECK(b.Marks(set).size() == 4 && b.Marks(set)[1].line == 3 && b.Marks(set)[3].line == 5);
        b.Erase(Pos{1, 0}, Pos{4, 0});                                             // three lines removed: marks on removed lines go
        bool any1 = false; for (const LineMark& m : b.Marks(set)) if (m.tag == 11 || m.tag == 12) any1 = true;
        CHECK(!any1 && b.Marks(set).size() == 2 && b.Marks(set)[0].line == 1 && b.Marks(set)[1].tag == 13);
        const auto before = b.Marks(set);
        b.Undo();                                                                  // undo adds the lines back; the dropped marks stay dropped
        CHECK(b.Marks(set).size() == 2 && b.Marks(set)[1].line == before[1].line + 3);
        b.Redo();
        CHECK(b.Marks(set).size() == 2 && b.Marks(set)[1].line == before[1].line);
        // a replace of three lines by one drops the marks on the last two of the range; by three, none
        b.SetText("a\nb\nc\nd");
        b.SetMarks(set, {LineMark{0, 1}, LineMark{1, 2}, LineMark{2, 3}, LineMark{3, 4}});
        b.Replace(Pos{0, 0}, Pos{2, 1}, "z");
        CHECK(b.Marks(set).size() == 2 && b.Marks(set)[0].tag == 1 && b.Marks(set)[1].tag == 4 && b.Marks(set)[1].line == 1);
        b.SetText("a\nb\nc\nd");
        b.SetMarks(set, {LineMark{0, 1}, LineMark{1, 2}, LineMark{2, 3}, LineMark{3, 4}});
        b.Replace(Pos{0, 0}, Pos{2, 1}, "x\ny\nz");
        CHECK(b.Marks(set).size() == 4 && b.Marks(set)[3].line == 3);
        CHECK(b.Marks(99).empty());
        b.SetMarks(99, {LineMark{1, 1}});                                          // an unknown set is ignored
        // a second set is independent
        const std::size_t s2 = b.NewMarkSet();
        b.SetMarks(s2, {LineMark{2, 7}});
        b.Insert(Pos{0, 0}, "\n");
        CHECK(b.Marks(s2)[0].line == 3 && b.Marks(set)[0].line == 0 && b.Marks(set)[1].line == 2);
    }
    {   // bracket search limits and priority
        Buffer b;
        std::string t = "(\n";
        for (int i = 0; i < 50; ++i) t += "x\n";
        t += ")";
        b.SetText(t);
        CHECK(b.MatchBracket(Pos{0, 0}, SkipFn(), 1u << 20, 100));
        CHECK(!b.MatchBracket(Pos{0, 0}, SkipFn(), 1u << 20, 10));
        CHECK(b.MatchBracket(Pos{0, 0})->at == (Pos{0, 0}));
        CHECK(b.MatchBracket(Pos{51, 1})->at == (Pos{51, 0}));
        b.SetText("()");
        // the bracket right at the caret wins even when it is rejected: no fallback to the one before it
        CHECK(!b.MatchBracket(Pos{0, 1}, [](const Pos& p) { return p.col == 1; }));
    }
    {   // matches on one line for drawing
        Buffer b;
        b.SetText("aaaa ab AB\nab");
        CHECK((b.FindInLine(0, "aa") == std::vector<std::size_t>{0, 2}));
        CHECK((b.FindInLine(0, "ab", FindFlags::MatchCase) == std::vector<std::size_t>{5}));
        CHECK((b.FindInLine(0, "ab", FindFlags::None) == std::vector<std::size_t>{5, 8}));
        CHECK(b.FindInLine(0, "").empty() && b.FindInLine(0, "a\nb").empty() && b.FindInLine(9, "a").empty());
    }
}

void TestTokenizer() {
    RUN_SUITE("tokenizer hook");
    Buffer b;
    int calls = 0;
    // state 1 = inside a block comment that started on an earlier line; a line "/*" opens it, "*/" closes it
    b.SetTokenizer([&](std::string_view line, std::uint32_t in, std::vector<Token>& out) -> std::uint32_t {
        ++calls;
        std::uint32_t st = in;
        if (line.find("/*") != std::string_view::npos) st = 1;
        if (line.find("*/") != std::string_view::npos) st = 0;
        out.push_back(Token{0, line.size(), (int)(in || st ? 2 : 1)});
        return st;
    });
    b.SetText("a\n/* c\nb\n*/\nd");
    CHECK(b.LineTokens(0).size() == 1 && b.LineTokens(0)[0].kind == 1 && b.LineTokens(0)[0].end == 1);
    calls = 0;
    CHECK(b.LineTokens(2)[0].kind == 2 && b.LineTokens(4)[0].kind == 1);
    CHECK(calls == 3 + 3);                                                        // line 2: states of 0 and 1, then itself; line 4: states of 2 and 3, then itself
    calls = 0;
    (void)b.LineTokens(4);
    CHECK(calls == 0);                                                            // the last line asked for is kept
    (void)b.LineTokens(2);
    CHECK(calls == 1);                                                            // another line: states are cached, only the line runs
    b.Insert(Pos{3, 0}, "x");                                                     // states of lines 0..3 stay valid, 3 and below re-run
    calls = 0;
    (void)b.LineTokens(4);
    CHECK(calls == 2);                                                            // the state of line 3, then line 4
    b.Erase(Pos{1, 0}, Pos{1, 2});                                                // the comment opener is gone
    CHECK(b.LineTokens(2)[0].kind == 1 && b.LineTokens(3)[0].kind == 1);
    b.Insert(Pos{0, 1}, "\n/*");                                                  // line count changed
    CHECK(b.LineTokens(2)[0].kind == 2);
    CHECK(b.LineTokens(999).empty());
    b.SetTokenizer(nullptr);
    CHECK(b.LineTokens(0).empty());
    b.SetTokenizer([](std::string_view, std::uint32_t, std::vector<Token>& out) -> std::uint32_t {
        out.push_back(Token{50, 9, 0}); out.push_back(Token{1000, 2000, 0}); return 0;   // hostile: out of range
    });
    b.SetText("ab");
    for (const Token& t : b.LineTokens(0)) CHECK(t.begin <= t.end && t.end <= 2);
}

void TestHostile() {
    RUN_SUITE("hostile inputs");
    // 1 MB paste, many short lines, within a time budget; undo it too
    {
        std::string big;
        while (big.size() < (1u << 20)) big += "line of some code { x = 1; } // c\xC3\xA9\r\n";
        Buffer b;
        auto t0 = std::chrono::steady_clock::now();
        auto e = b.Insert(Pos{0, 0}, big);
        const double ins = Ms(t0);
        CHECK(e && b.ByteSize() > 900000);
        CHECK(ins < 2000.0 * kSlow);
        t0 = std::chrono::steady_clock::now();
        CHECK(b.Undo() && b.ByteSize() == 0 && b.Redo());
        CHECK(Ms(t0) < 2000.0 * kSlow);
        t0 = std::chrono::steady_clock::now();
        CHECK(b.FindAll("code").size() > 1000);
        CHECK(b.ReplaceAll("code", "text") > 1000);
        CHECK(Ms(t0) < 3000.0 * kSlow);
        CHECK(Consistent(b));
        std::printf("  1 MB paste: %.1f ms\n", ins);
    }
    // a 4 MB single line: middle inserts, erases, finds
    {
        Buffer b;
        b.SetText(std::string(4u << 20, 'a'));
        CHECK(b.LineCount() == 1);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i) b.Insert(Pos{0, 2000000}, "xy");
        for (int i = 0; i < 200; ++i) b.Erase(Pos{0, 2000000}, Pos{0, 2000002});
        CHECK(b.ByteSize() == (4u << 20));
        CHECK(b.DisplayColumn(0, 4u << 20) == (4u << 20));
        CHECK(!b.Find("b", Pos{0, 0}, FindFlags::None, false, true));
        CHECK(Ms(t0) < 4000.0 * kSlow);
        CHECK(Consistent(b));
    }
    // a needle built to make a naive search quadratic: KMP is linear
    {
        Buffer b;
        b.SetText(std::string(2u << 20, 'a'));
        auto t0 = std::chrono::steady_clock::now();
        CHECK(!b.Find(std::string(5000, 'a') + "b", Pos{0, 0}));
        CHECK(Ms(t0) < 2000.0 * kSlow);
    }
    // 100 000 lines and bulk edits
    {
        std::string t;
        for (int i = 0; i < 100000; ++i) t += "x\n";
        Buffer b;
        CHECK(b.SetText(t) && b.LineCount() == 100001);
        auto t0 = std::chrono::steady_clock::now();
        CHECK(b.ReplaceAll("x", "yy") == 100000);                                // one linear edit, not 100000 shifts
        CHECK(b.IndentLines(0, 1000000));
        CHECK(Ms(t0) < 5000.0 * kSlow);
        CHECK(b.Undo() && b.Undo() && b.Text() == t);
    }
    // overflow-sized positions and offsets everywhere
    {
        const std::size_t M = (std::size_t)-1;
        Buffer b;
        b.SetText("abc\ndef");
        CHECK(b.Insert(Pos{M, M}, "Z") && b.Text() == "abc\ndefZ");
        CHECK(b.Erase(Pos{M, M}, Pos{1, 3}) && b.Text() == "abc\ndef");
        CHECK(b.Erase(Pos{M, M}, Pos{M - 1, 0}) == false);                       // both clamp to the end: nothing to erase
        CHECK(b.Slice(Pos{M, M}, Pos{0, M}) == "\ndef");
        b.Replace(Pos{M, 0}, Pos{0, M}, "q");
        CHECK(b.Text() == "abcq");
        CHECK(b.IndentLines(M - 1, M) == false && b.OutdentLines(M, M) == false);
        CHECK(b.LineTokens(M).empty());
        b.SetSelections({Selection{Pos{M, M}, Pos{M, M}}});
        CHECK(b.TypeText("!") && b.Text() == "abcq!");
        CHECK(b.ByteForDisplayColumn(M, M) == 0 && b.DisplayColumn(M, M) == 0 && b.Line(M).empty());
        Options o; o.maxBytes = M;                                               // limit arithmetic must not wrap
        Buffer c(o);
        c.SetText("abc");
        CHECK(c.Insert(Pos{0, 0}, "d") && c.Text() == "dabc");
        Options z; z.maxBytes = 0;
        Buffer d(z);
        CHECK(!d.SetText("a") && !d.Insert(Pos{0, 0}, "a") && d.ByteSize() == 0);
        CHECK(d.Insert(Pos{0, 0}, "").has_value());                              // inserting nothing is not a refusal
    }
    // the size limit
    {
        Options o; o.maxBytes = 100;
        Buffer b(o);
        CHECK(b.Insert(Pos{0, 0}, std::string(100, 'a')));
        CHECK(!b.Insert(Pos{0, 0}, "b") && b.ByteSize() == 100);
        CHECK(b.Replace(Pos{0, 0}, Pos{0, 1}, "b").has_value() && b.ByteSize() == 100);   // same size is fine
        CHECK(!b.Replace(Pos{0, 0}, Pos{0, 1}, "bb").has_value());
        CHECK(b.Erase(Pos{0, 0}, Pos{0, 50}) && b.Insert(Pos{0, 0}, std::string(50, 'c')));
        CHECK(!b.SetText(std::string(101, 'x')) && b.ByteSize() == 100);          // a refused SetText changes nothing
    }
    // junk text of every kind goes through every entry point and keeps the invariants
    {
        Rng rng(7);
        Buffer b;
        for (int i = 0; i < 300; ++i) {
            std::string junk;
            const int n = rng.Int(40);
            for (int k = 0; k < n; ++k) junk.push_back((char)rng.Int(256));
            b.Insert(Pos{(std::size_t)rng.Int(5), (std::size_t)rng.Int(20)}, junk);
            if (rng.Int(3) == 0) b.Erase(Pos{(std::size_t)rng.Int(5), (std::size_t)rng.Int(20)}, Pos{(std::size_t)rng.Int(5), (std::size_t)rng.Int(20)});
            if (rng.Int(7) == 0) b.Undo();
            if (rng.Int(9) == 0) b.TypeText(junk);
            CHECK(Consistent(b));
        }
    }
}

// ---- the differential test: a std::string is the model --------------------------------------------------------------
std::string Piece(Rng& r) {
    static const char* const kP[] = {"a", "b", "ab", "foo", "Foo", " ", "  ", "\t", "\n", "\r\n", "\r", "\xC3\xA9", "\xE2\x82\xAC",
                                     "\xF0\x9F\x98\x80", "{", "}", "(", ")", "\n\n", "line\n", "\xC3", "\xFF", "x_y"};
    return kP[r.Int((int)(sizeof(kP) / sizeof(kP[0])))];
}
std::string RandText(Rng& r) {
    std::string s;
    const int n = r.Int(6);
    for (int i = 0; i < n; ++i) s += Piece(r);
    return s;
}
std::size_t Boundary(const std::string& t, Rng& r) {            // a random character boundary offset in the model
    for (;;) {
        const std::size_t o = (std::size_t)r.Int((int)t.size() + 1);
        if (o == t.size() || (t[o] & 0xC0) != 0x80) return o;
    }
}
std::string RandNeedle(Rng& r) {
    static const char* const kN[] = {"a", "b", "ab", "aa", "foo", "\n", "a\nb", "\xC3\xA9", "x_y", "line\n", "  "};
    return kN[r.Int((int)(sizeof(kN) / sizeof(kN[0])))];
}

void TestDifferential() {
    RUN_SUITE("random differential test against a std::string model");
    int iters = 4000;
    if (const char* e = std::getenv("OKAY_TEXT_FUZZ_ITERS")) iters = std::atoi(e);
    std::uint64_t seed0 = 1;
    if (const char* e = std::getenv("OKAY_TEXT_FUZZ_SEED")) seed0 = (std::uint64_t)std::atoll(e);
    for (std::uint64_t seed = seed0; seed < seed0 + 6; ++seed) {
        Rng rng(seed);
        Options bo; bo.undoGroups = (std::size_t)1 << 30; bo.undoBytes = (std::size_t)1 << 30;
        Buffer b(bo);
        std::string model;
        std::vector<std::string> undo, redo;
        bool ok = true;
        for (int step = 0; step < iters && ok; ++step) {
            const int op = rng.Int(10);
            const std::string before = model;
            bool changed = false;
            if (op <= 2) {                                                        // insert
                const std::string s = RandText(rng);
                const std::size_t o = Boundary(model, rng);
                const std::string san = Buffer::Sanitize(s);
                b.Insert(b.PosOf(o), s);
                if (!san.empty()) { model.insert(o, san); changed = true; }
            } else if (op <= 4) {                                                 // erase
                std::size_t o1 = Boundary(model, rng), o2 = Boundary(model, rng);
                if (o1 > o2) std::swap(o1, o2);
                b.Erase(b.PosOf(o1), b.PosOf(o2));
                if (o2 > o1) { model.erase(o1, o2 - o1); changed = true; }
            } else if (op == 5) {                                                 // replace
                std::size_t o1 = Boundary(model, rng), o2 = Boundary(model, rng);
                if (o1 > o2) std::swap(o1, o2);
                const std::string s = RandText(rng), san = Buffer::Sanitize(s);
                b.Replace(b.PosOf(o1), b.PosOf(o2), s);
                if (o2 > o1 || !san.empty()) { model.replace(o1, o2 - o1, san); changed = true; }
            } else if (op == 6) {                                                 // undo
                const bool did = b.Undo();
                CHECK(did == !undo.empty());
                if (!undo.empty()) { redo.push_back(model); model = undo.back(); undo.pop_back(); }
                continue;
            } else if (op == 7) {                                                 // redo
                const bool did = b.Redo();
                CHECK(did == !redo.empty());
                if (!redo.empty()) { undo.push_back(model); model = redo.back(); redo.pop_back(); }
                continue;
            } else if (op == 8) {                                                 // find against std::string
                const std::string n = RandNeedle(rng);
                const std::size_t o = Boundary(model, rng);
                const auto f = b.Find(n, b.PosOf(o), FindFlags::MatchCase, false, false);
                const std::size_t m = model.find(n, o);
                CHECK(f.has_value() == (m != std::string::npos));
                if (f && m != std::string::npos) CHECK(b.OffsetOf(f->begin) == m && b.OffsetOf(f->end) == m + n.size());
                const auto g = b.Find(n, b.PosOf(o), FindFlags::MatchCase, true, false);
                const std::size_t rm = o == 0 ? std::string::npos : model.rfind(n, o - 1);
                CHECK(g.has_value() == (rm != std::string::npos));
                if (g && rm != std::string::npos) CHECK(b.OffsetOf(g->begin) == rm);
                std::size_t cnt = 0;
                for (std::size_t p = model.find(n); p != std::string::npos; p = model.find(n, p + n.size())) ++cnt;
                CHECK(b.FindAll(n).size() == cnt);
                continue;
            } else {                                                              // replace all
                const std::string n = RandNeedle(rng), s = RandText(rng), san = Buffer::Sanitize(s);
                std::string m2;
                std::size_t cnt = 0, p = 0;
                for (;;) {
                    const std::size_t h = model.find(n, p);
                    if (h == std::string::npos) { m2.append(model, p, std::string::npos); break; }
                    m2.append(model, p, h - p); m2 += san; p = h + n.size(); ++cnt;
                }
                const std::size_t got = b.ReplaceAll(n, s);
                CHECK(got == cnt);
                if (cnt) { model = m2; changed = true; }                           // even identical text is an undo step
            }
            if (changed) { undo.push_back(before); redo.clear(); }
            CHECK(b.Text() == model);
            if (b.Text() != model) { ok = false; break; }
            CHECK(Consistent(b));
            // offsets round trip at a random boundary, line count agrees with the model
            const std::size_t o = Boundary(model, rng);
            CHECK(b.OffsetOf(b.PosOf(o)) == o);
            std::size_t nl = 0;
            for (char c : model) if (c == '\n') ++nl;
            CHECK(b.LineCount() == nl + 1);
            if (b.UndoDepth() != undo.size()) { CHECK(b.UndoDepth() == undo.size()); ok = false; }
        }
        // undo everything, then redo everything
        while (b.Undo()) {}
        CHECK(b.Text().empty());
        while (b.Redo()) {}
        CHECK(b.Text() == (redo.empty() ? model : redo.front()));   // the newest state the log reaches
    }
}

}  // namespace

int main() {
    TestBasics();
    TestUtf8();
    TestOffsetsColumns();
    TestUndo();
    TestSelectionsAndMultiCursor();
    TestWords();
    TestIndent();
    TestBrackets();
    TestFind();
    TestTokenizer();
    TestEditorSupport();
    TestHostile();
    TestDifferential();
    TEST_MAIN_RESULT();
}
