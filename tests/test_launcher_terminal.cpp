// The launcher's terminal (launcher/Terminal.cpp and the shell tabs in launcher/CommandLine.cpp): the escape-sequence
// parser against byte streams (cursor moves, erase, colours, scroll regions, the alternate screen, wrapping, UTF-8 and
// wide characters, the title, replies), resizing, the selection and what it copies, the bytes each key sends, and a real
// shell on a pseudo terminal (okaycli::Pty, POSIX only here: printf and echo through /bin/sh, the folder, Tools first on
// PATH, resize, Ctrl+C stopping sleep, exit). Headless. The Windows pseudo console is only compiled, never run, here.
#include "test_framework.hpp"

#include "CommandLine.hpp"
#include "Terminal.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace okayterm;
namespace fs = std::filesystem;

static Terminal Fed(int cols, int rows, const std::string& bytes, std::size_t sb = 100) {
    Terminal t(cols, rows, sb);
    t.Feed(bytes);
    return t;
}

#if !defined(_WIN32)
static bool WaitFor(okaylcli::ShellSession& s, const std::function<bool()>& done, int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        s.Poll();
        if (done()) return true;
        usleep(3000);
    }
    s.Poll();
    return done();
}
static bool Shows(const okaylcli::ShellSession& s, const std::string& text) { return s.Term().AllText().find(text) != std::string::npos; }
static int CountOf(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
    return n;
}
#endif

int main() {
    RUN_SUITE("launcher_terminal");

    // ---- printing, CR / LF, the last-column wrap ----
    {
        Terminal t = Fed(10, 3, "hello\r\nworld");
        CHECK(t.RowText(0) == "hello");
        CHECK(t.RowText(1) == "world");
        CHECK(t.CursorX() == 5 && t.CursorY() == 1);
    }
    {
        Terminal t = Fed(10, 3, "0123456789AB");
        CHECK(t.RowText(0) == "0123456789");
        CHECK(t.RowText(1) == "AB");
        CHECK(t.ScreenLine(0).wrapped);
        CHECK(!t.ScreenLine(1).wrapped);
        CHECK(t.CursorX() == 2 && t.CursorY() == 1);
    }
    {   // exactly a full line, then CR LF: no empty line in between (the wrap waits for the next character)
        Terminal t = Fed(10, 3, "0123456789\r\nx");
        CHECK(t.CursorY() == 1);
        CHECK(t.RowText(1) == "x");
        CHECK(!t.ScreenLine(0).wrapped);
    }
    {   // autowrap off: the last column is overwritten
        Terminal t = Fed(5, 2, "\x1b[?7labcdefg");
        CHECK(t.RowText(0) == "abcdg");
        CHECK(t.CursorY() == 0);
    }
    {   // BS, HT, tab stops (HTS / TBC)
        Terminal t = Fed(20, 2, "ab\bX\tY");
        CHECK(t.RowText(0) == "aX      Y");
        Terminal u = Fed(20, 2, "\x1b[3g\x1b[5G\x1bH\r\tZ");
        CHECK(u.At(4, 0).ch == U'Z');
    }

    // ---- scrolling into the scrollback, and its limit ----
    {
        Terminal t = Fed(10, 3, "a\r\nb\r\nc\r\nd");
        CHECK(t.RowText(0) == "b" && t.RowText(2) == "d");
        CHECK(t.ScrollbackSize() == 1);
        CHECK(t.LineText(t.FirstLine()) == "a");
        CHECK(t.ScreenTop() == 1);
        std::string many;
        for (int i = 0; i < 50; ++i) many += "line" + std::to_string(i) + "\r\n";
        Terminal u = Fed(10, 3, many, 5);
        CHECK(u.ScrollbackSize() == 5);
        CHECK(u.FirstLine() == 50 + 1 - 3 - 5 + 0);   // 51 lines made (the last one empty), 3 on screen, 5 kept
        CHECK(u.LineText(u.FirstLine()) == "line43");
        CHECK(u.LineText(u.ScreenTop()) == "line48");
    }

    // ---- cursor movement ----
    {
        Terminal t = Fed(20, 10, "\x1b[3;5HX");
        CHECK(t.At(4, 2).ch == U'X');
        t.Feed("\x1b[HY");
        CHECK(t.At(0, 0).ch == U'Y');
        t.Feed("\x1b[99;99H");
        CHECK(t.CursorX() == 19 && t.CursorY() == 9);
        t.Feed("\x1b[5;5H\x1b[2A");
        CHECK(t.CursorY() == 2);
        t.Feed("\x1b[3B");
        CHECK(t.CursorY() == 5);
        t.Feed("\x1b[4C");
        CHECK(t.CursorX() == 8);
        t.Feed("\x1b[2D");
        CHECK(t.CursorX() == 6);
        t.Feed("\x1b[A");   // no number = 1
        CHECK(t.CursorY() == 4);
        t.Feed("\x1b[0A");  // 0 = 1 too
        CHECK(t.CursorY() == 3);
        t.Feed("\x1b[12G");
        CHECK(t.CursorX() == 11);
        t.Feed("\x1b[7d");
        CHECK(t.CursorY() == 6);
        t.Feed("\x1b[2E");
        CHECK(t.CursorX() == 0 && t.CursorY() == 8);
        t.Feed("\x1b[3F");
        CHECK(t.CursorY() == 5);
        t.Feed("\x1b[1;1H\x1b[50A\x1b[50D");
        CHECK(t.CursorX() == 0 && t.CursorY() == 0);
        t.Feed("\x1b[4;4H\x1b" "7\x1b[1;1H\x1b" "8");   // DECSC / DECRC
        CHECK(t.CursorX() == 3 && t.CursorY() == 3);
        t.Feed("\x1b[6;6H\x1b[s\x1b[H\x1b[u");          // SCOSC / SCORC
        CHECK(t.CursorX() == 5 && t.CursorY() == 5);
    }

    // ---- erasing (with the current background), inserting, deleting ----
    {
        Terminal t = Fed(10, 3, "abcdefghij\r\nklmnopqrst\r\nuvwxyz");
        t.Feed("\x1b[1;4H\x1b[K");
        CHECK(t.RowText(0) == "abc");
        t.Feed("\x1b[2;4H\x1b[1K");
        CHECK(t.RowText(1) == "    opqrst");
        t.Feed("\x1b[3;1H\x1b[2K");
        CHECK(t.RowText(2).empty());
        Terminal u = Fed(10, 3, "abcdefghij\r\nklmnopqrst\r\nuvwxyz");
        u.Feed("\x1b[2;3H\x1b[J");
        CHECK(u.RowText(0) == "abcdefghij" && u.RowText(1) == "kl" && u.RowText(2).empty());
        Terminal v = Fed(10, 3, "abcdefghij\r\nklmnopqrst\r\nuvwxyz");
        v.Feed("\x1b[2;3H\x1b[1J");
        CHECK(v.RowText(0).empty() && v.RowText(1) == "   nopqrst" && v.RowText(2) == "uvwxyz");
        v.Feed("\x1b[2J");
        CHECK(v.RowText(1).empty() && v.RowText(2).empty());
        Terminal w = Fed(10, 2, "abcdefghij\x1b[1;3H\x1b[41m\x1b[2X");
        CHECK(w.RowText(0) == "ab  efghij");
        CHECK(w.At(2, 0).bg == PaletteColor(1) && w.At(3, 0).bg == PaletteColor(1) && w.At(4, 0).bg == kDefaultColor);
        w.Feed("\x1b[2K");
        CHECK(w.At(9, 0).bg == PaletteColor(1));   // erase paints the current background
        Terminal x = Fed(10, 2, "abcdef\x1b[1;2H\x1b[2@");
        CHECK(x.RowText(0) == "a  bcdef");
        x.Feed("\x1b[3P");
        CHECK(x.RowText(0) == "acdef");
        Terminal y = Fed(10, 4, "1\r\n2\r\n3\r\n4\x1b[2;1H\x1b[L");
        CHECK(y.RowText(1).empty() && y.RowText(2) == "2" && y.RowText(3) == "3");
        y.Feed("\x1b[2M");
        CHECK(y.RowText(1) == "3" && y.RowText(2).empty());
        Terminal z = Fed(10, 2, "ab\x1b[3b");   // REP repeats the last character
        CHECK(z.RowText(0) == "abbbb");
        t.Feed("\x1b[3J");
        Terminal sb = Fed(10, 2, "1\r\n2\r\n3\r\n4");
        CHECK(sb.ScrollbackSize() == 2);
        sb.Feed("\x1b[3J");
        CHECK(sb.ScrollbackSize() == 0);
    }

    // ---- SGR: attributes, 16, 256 and 24-bit colours ----
    {
        Terminal t = Fed(40, 2, "\x1b[1;31mR\x1b[0mn\x1b[4;7mU\x1b[24;27mp\x1b[92mG\x1b[105mB\x1b[39;49mD");
        CHECK(t.At(0, 0).fg == PaletteColor(1) && (t.At(0, 0).attr & kBold));
        CHECK(t.At(1, 0).fg == kDefaultColor && t.At(1, 0).attr == 0);
        CHECK((t.At(2, 0).attr & kUnderline) && (t.At(2, 0).attr & kInverse));
        CHECK(t.At(3, 0).attr == 0);
        CHECK(t.At(4, 0).fg == PaletteColor(10));
        CHECK(t.At(5, 0).bg == PaletteColor(13));
        CHECK(t.At(6, 0).fg == kDefaultColor && t.At(6, 0).bg == kDefaultColor);
        t.Feed("\x1b[38;5;123ma\x1b[48;5;200mb\x1b[38;2;10;20;30mc\x1b[48;2;255;128;0md\x1b[0m");
        CHECK(t.At(7, 0).fg == PaletteColor(123));
        CHECK(t.At(8, 0).bg == PaletteColor(200));
        CHECK(t.At(9, 0).fg == RgbColor(10, 20, 30));
        CHECK(t.At(10, 0).bg == RgbColor(255, 128, 0));
        t.Feed("\x1b[38:2::1:2:3me\x1b[38:2:4:5:6mf\x1b[38:5:9mg\x1b[2;3;9mh\x1b[22;23;29mi\x1b[4:0mj");
        CHECK(t.At(11, 0).fg == RgbColor(1, 2, 3));
        CHECK(t.At(12, 0).fg == RgbColor(4, 5, 6));
        CHECK(t.At(13, 0).fg == PaletteColor(9));
        CHECK((t.At(14, 0).attr & kFaint) && (t.At(14, 0).attr & kItalic) && (t.At(14, 0).attr & kStrike));
        CHECK(!(t.At(15, 0).attr & (kFaint | kItalic | kStrike)));
        CHECK(!(t.At(16, 0).attr & kUnderline));
        t.Feed("\x1b[mk");
        CHECK(t.At(17, 0).fg == kDefaultColor);
        // the colours it is drawn in
        const Scheme& s = DarkScheme();
        std::uint32_t fg = 0, bg = 0;
        CellColors(t.At(0, 0), s, fg, bg);
        CHECK(fg == s.ansi[9]);   // bold red shows bright red
        CHECK(bg == s.bg);
        CellColors(t.At(2, 0), s, fg, bg);
        CHECK(fg == s.bg && bg == s.fg);   // inverse
        CellColors(t.At(9, 0), s, fg, bg);
        CHECK(fg == 0x0A141E);
        CHECK(PaletteRgb(16, s) == 0x000000 && PaletteRgb(231, s) == 0xFFFFFF && PaletteRgb(21, s) == 0x0000FF);
        CHECK(PaletteRgb(232, s) == 0x080808 && PaletteRgb(255, s) == 0xEEEEEE);
        CHECK(PaletteRgb(4, s) == s.ansi[4]);
    }

    // ---- scroll regions ----
    {
        Terminal t = Fed(10, 5, "0\r\n1\r\n2\r\n3\r\n4");
        t.Feed("\x1b[2;4r");
        CHECK(t.ScrollTop() == 1 && t.ScrollBottom() == 3);
        CHECK(t.CursorX() == 0 && t.CursorY() == 0);   // DECSTBM sends the cursor home
        t.Feed("\x1b[4;1H\n");
        CHECK(t.RowText(0) == "0" && t.RowText(1) == "2" && t.RowText(2) == "3" && t.RowText(3).empty() && t.RowText(4) == "4");
        CHECK(t.ScrollbackSize() == 0);   // a region that is not the whole screen never feeds the scrollback
        t.Feed("\x1b[2;1H\x1bM");        // RI at the region's top scrolls it down
        CHECK(t.RowText(1).empty() && t.RowText(2) == "2" && t.RowText(3) == "3" && t.RowText(4) == "4");
        t.Feed("\x1b[3S");
        CHECK(t.RowText(0) == "0" && t.RowText(1).empty() && t.RowText(3).empty() && t.RowText(4) == "4");
        t.Feed("\x1b[?6h\x1b[1;1HO\x1b[?6l");   // origin mode: row 1 is the region's top
        CHECK(t.At(0, 1).ch == U'O');
        t.Feed("\x1b[r");
        CHECK(t.ScrollTop() == 0 && t.ScrollBottom() == 4);
        t.Feed("\x1b[3;1H\x1b[10B");   // CUD inside the region stops at its bottom; with no region at the screen's
        CHECK(t.CursorY() == 4);
    }

    // ---- the alternate screen ----
    {
        Terminal t = Fed(10, 3, "main\r\nline");
        t.Feed("\x1b[?1049h");
        CHECK(t.AltScreen());
        CHECK(t.RowText(0).empty() && t.RowText(1).empty());
        t.Feed("\x1b[Hfull\r\nscreen\r\nprog\r\nmore");
        CHECK(t.RowText(0) == "screen");   // scrolled
        CHECK(t.ScrollbackSize() == 0);    // the alternate screen keeps no scrollback
        CHECK(t.FirstLine() == t.ScreenTop());
        t.Feed("\x1b[?1049l");
        CHECK(!t.AltScreen());
        CHECK(t.RowText(0) == "main" && t.RowText(1) == "line");
        CHECK(t.CursorX() == 4 && t.CursorY() == 1);   // the cursor came back
        t.Feed("\x1b[?47hX\x1b[?47l");
        CHECK(t.RowText(1) == "line");
    }

    // ---- modes, the title, replies ----
    {
        Terminal t(20, 5);
        CHECK(t.CursorVisible() && !t.BracketedPaste() && !t.AppCursorKeys());
        t.Feed("\x1b[?25l\x1b[?2004h\x1b[?1h\x1b=");
        CHECK(!t.CursorVisible() && t.BracketedPaste() && t.AppCursorKeys() && t.AppKeypad());
        t.Feed("\x1b[?25h\x1b[?2004;1l\x1b>");   // several modes in one sequence
        CHECK(t.CursorVisible() && !t.BracketedPaste() && !t.AppCursorKeys() && !t.AppKeypad());
        t.Feed("\x1b]0;My title\x07");
        CHECK(t.Title() == "My title");
        CHECK(t.TakeTitleChanged());
        CHECK(!t.TakeTitleChanged());
        t.Feed("\x1b]2;Caf\xc3\xa9 \x01x\x1b\\");   // ST ends it, UTF-8 kept, controls dropped
        CHECK(t.Title() == "Caf\xc3\xa9 x");
        t.Feed("\x1b]8;;http://example.com\x07link\x1b]8;;\x07");   // other OSC strings are read and ignored
        CHECK(t.RowText(0) == "link");
        t.Feed("\x1bP1$r0m\x1b\\");   // a DCS string is swallowed
        CHECK(t.RowText(0) == "link");
        t.Feed("\x1b[3;4H\x1b[6n");
        CHECK(t.TakeReply() == "\x1b[3;4R");
        t.Feed("\x1b[5n\x1b[c\x1b[>c");
        CHECK(t.TakeReply() == "\x1b[0n\x1b[?1;2c\x1b[>0;10;1c");
        CHECK(t.TakeReply().empty());
        t.Feed("\x07");
        CHECK(t.TakeBell() && !t.TakeBell());
        t.Feed("\x1b[?9001h\x1b[?1004h\x1b[2 q\x1b[8;24;80t");   // ConPTY's win32-input / focus requests and the rest: ignored
        CHECK(t.RowText(0) == "link");
        t.Feed("\x1b" "c");   // RIS
        CHECK(t.RowText(0).empty() && t.Title().empty());
    }

    // ---- sequences split over several reads, broken ones ----
    {
        Terminal t(20, 3);
        t.Feed("\x1b");
        t.Feed("[3");
        t.Feed("1mR");
        CHECK(t.At(0, 0).fg == PaletteColor(1) && t.At(0, 0).ch == U'R');
        t.Feed("\x1b[1;\x18X");   // CAN cancels the sequence
        CHECK(t.At(1, 0).ch == U'X');
        t.Feed("\x1b[12\x1b[0mY");   // ESC starts a new one
        CHECK(t.At(2, 0).ch == U'Y' && t.At(2, 0).fg == kDefaultColor);
        t.Feed("\x1b[1\r\nmZ");   // a control inside a sequence acts at once
        CHECK(t.CursorY() == 1 && t.At(0, 1).ch == U'Z');
        std::string longSeq = "\x1b[";
        for (int i = 0; i < 100; ++i) longSeq += "1;";
        longSeq += "mQ";
        t.Feed(longSeq);   // too many parameters: ignored, no crash
        CHECK(t.At(1, 1).ch == U'Q');
        t.Feed("\x1b(0lqk\x1b(Bq");   // DEC Special Graphics: box corners and lines
        CHECK(t.At(2, 1).ch == 0x250C && t.At(3, 1).ch == 0x2500 && t.At(4, 1).ch == 0x2510 && t.At(5, 1).ch == U'q');
        t.Feed("\x1b#8");   // DECALN
        CHECK(t.RowText(2) == std::string(20, 'E'));
    }

    // ---- UTF-8 and wide characters ----
    {
        Terminal t(10, 3);
        t.Feed("h\xc3\xa9llo");
        CHECK(t.At(1, 0).ch == 0xE9);
        CHECK(t.RowText(0) == "h\xc3\xa9llo");
        CHECK(t.CursorX() == 5);
        t.Feed("\r\n\xe4");   // split in the middle of a character
        t.Feed("\xb8");
        t.Feed("\xad!");
        CHECK(t.At(0, 1).ch == 0x4E2D && t.At(0, 1).width == 2);
        CHECK(t.At(1, 1).width == 0);
        CHECK(t.At(2, 1).ch == U'!');
        CHECK(t.RowText(1) == "\xe4\xb8\xad!");
        t.Feed("\r\n\xff" "a\xc3" "b\xf0\x9f\x98\x80");   // a bad byte, a broken sequence, an emoji
        CHECK(t.At(0, 2).ch == 0xFFFD && t.At(1, 2).ch == U'a' && t.At(2, 2).ch == 0xFFFD && t.At(3, 2).ch == U'b');
        CHECK(t.At(4, 2).ch == 0x1F600 && t.At(4, 2).width == 2);
        t.Feed("e\xcc\x81");   // a combining mark adds no cell
        CHECK(t.At(6, 2).ch == U'e' && t.CursorX() == 7);
        Terminal w = Fed(5, 2, "abcd\xe4\xb8\xad");   // a wide character that does not fit goes to the next line
        CHECK(w.RowText(0) == "abcd" && w.At(0, 1).ch == 0x4E2D && w.ScreenLine(0).wrapped);
        Terminal o = Fed(6, 1, "\xe4\xb8\xad\xe4\xb8\xad\x1b[1;2HX");   // overwriting half a wide character blanks the other half
        CHECK(o.At(0, 0).ch == U' ' && o.At(1, 0).ch == U'X' && o.At(2, 0).ch == 0x4E2D);
    }

    // ---- resizing ----
    {
        Terminal t = Fed(10, 4, "one\r\ntwo\r\nthree\r\nfour");
        t.Resize(10, 2);
        CHECK(t.Rows() == 2);
        CHECK(t.RowText(0) == "three" && t.RowText(1) == "four");
        CHECK(t.ScrollbackSize() == 2 && t.LineText(t.FirstLine()) == "one");
        CHECK(t.CursorY() == 1 && t.CursorX() == 4);
        t.Resize(3, 2);
        CHECK(t.RowText(0) == "thr");
        CHECK(t.CursorX() == 2);
        t.Resize(12, 5);
        CHECK(t.Rows() == 5 && t.Cols() == 12 && (int)t.ScreenLine(4).cells.size() == 12);
        Terminal u = Fed(10, 6, "a\r\nb");   // blank lines under the cursor go first
        u.Resize(10, 3);
        CHECK(u.RowText(0) == "a" && u.RowText(1) == "b" && u.ScrollbackSize() == 0);
        Terminal w = Fed(6, 1, "ab\xe4\xb8\xad");
        w.Resize(3, 1);   // cuts a wide character in half: it goes
        CHECK(w.RowText(0) == "ab");
    }

    // ---- selection and copy ----
    {
        Terminal t = Fed(12, 4, "hello world\r\nsecond\r\n0123456789abcdef");
        const long long top = t.ScreenTop();
        CHECK(!t.HasSelection());
        t.SelectStart({top, 6});
        t.SelectExtend({top + 1, 3});
        CHECK(t.HasSelection());
        CHECK(t.SelectedText() == "world\nsec");
        CHECK(t.IsSelected(top, 6) && t.IsSelected(top + 1, 2) && !t.IsSelected(top + 1, 3) && !t.IsSelected(top, 5));
        t.SelectStart({top + 1, 3});   // backwards works the same
        t.SelectExtend({top, 6});
        CHECK(t.SelectedText() == "world\nsec");
        t.SelectStart({top + 2, 0});   // a line that wrapped by itself is copied as one line
        t.SelectExtend({top + 3, 4});
        CHECK(t.SelectedText() == "0123456789abcdef");
        t.SelectWord({top, 2});
        CHECK(t.SelectedText() == "hello");
        t.SelectWord({top + 1, 1});
        CHECK(t.SelectedText() == "second");
        t.SelectAll();
        CHECK(t.SelectedText() == "hello world\nsecond\n0123456789abcdef");
        CHECK(t.AllText() == t.SelectedText());
        t.ClearSelection();
        CHECK(!t.HasSelection() && t.SelectedText().empty());
        Terminal s = Fed(12, 2, "a\r\nb\r\nc");   // the selection reaches into the scrollback
        s.SelectStart({s.FirstLine(), 0});
        s.SelectExtend({s.EndLine() - 1, 1});
        CHECK(s.SelectedText() == "a\nb\nc");
        Terminal w = Fed(10, 1, "x\xe4\xb8\xady");
        w.SelectStart({w.ScreenTop(), 0});
        w.SelectExtend({w.ScreenTop(), 4});
        CHECK(w.SelectedText() == "x\xe4\xb8\xady");   // the wide character once
        s.ClearAll();
        CHECK(s.ScrollbackSize() == 0 && s.AllText().empty() && s.CursorY() == 0);
    }

    // ---- keys to bytes ----
    {
        Mods none, shift, ctrl, alt, ctrlShift;
        shift.shift = true;
        ctrl.ctrl = true;
        alt.alt = true;
        ctrlShift.ctrl = ctrlShift.shift = true;
        CHECK(KeySequence(Key::Up, none, false) == "\x1b[A");
        CHECK(KeySequence(Key::Down, none, false) == "\x1b[B");
        CHECK(KeySequence(Key::Right, none, false) == "\x1b[C");
        CHECK(KeySequence(Key::Left, none, false) == "\x1b[D");
        CHECK(KeySequence(Key::Up, none, true) == "\x1bOA");   // application cursor keys
        CHECK(KeySequence(Key::Home, none, false) == "\x1b[H");
        CHECK(KeySequence(Key::End, none, true) == "\x1bOF");
        CHECK(KeySequence(Key::Up, ctrl, false) == "\x1b[1;5A");
        CHECK(KeySequence(Key::Left, shift, true) == "\x1b[1;2D");
        CHECK(KeySequence(Key::Right, alt, false) == "\x1b[1;3C");
        CHECK(KeySequence(Key::End, ctrlShift, false) == "\x1b[1;6F");
        CHECK(KeySequence(Key::Insert, none, false) == "\x1b[2~");
        CHECK(KeySequence(Key::Delete, none, false) == "\x1b[3~");
        CHECK(KeySequence(Key::PageUp, none, false) == "\x1b[5~");
        CHECK(KeySequence(Key::PageDown, ctrl, false) == "\x1b[6;5~");
        CHECK(KeySequence(Key::F1, none, false) == "\x1bOP");
        CHECK(KeySequence(Key::F4, none, false) == "\x1bOS");
        CHECK(KeySequence(Key::F2, shift, false) == "\x1b[1;2Q");
        CHECK(KeySequence(Key::F5, none, false) == "\x1b[15~");
        CHECK(KeySequence(Key::F6, none, false) == "\x1b[17~");
        CHECK(KeySequence(Key::F10, none, false) == "\x1b[21~");
        CHECK(KeySequence(Key::F11, none, false) == "\x1b[23~");
        CHECK(KeySequence(Key::F12, shift, false) == "\x1b[24;2~");
        CHECK(KeySequence(Key::Enter, none, false) == "\r");
        CHECK(KeySequence(Key::Enter, alt, false) == "\x1b\r");
        CHECK(KeySequence(Key::Backspace, none, false) == "\x7f");
        CHECK(KeySequence(Key::Backspace, ctrl, false) == "\x08");
        CHECK(KeySequence(Key::Tab, none, false) == "\t");
        CHECK(KeySequence(Key::Tab, shift, false) == "\x1b[Z");
        CHECK(KeySequence(Key::Escape, none, false) == "\x1b");
        CHECK(KeySequence(Key::None, none, false).empty());
        CHECK(CharSequence(U'c', ctrl) == "\x03");
        CHECK(CharSequence(U'C', ctrl) == "\x03");
        CHECK(CharSequence(U'a', ctrl) == "\x01" && CharSequence(U'z', ctrl) == "\x1a");
        CHECK(CharSequence(U' ', ctrl) == std::string(1, '\0'));
        CHECK(CharSequence(U'[', ctrl) == "\x1b" && CharSequence(U'\\', ctrl) == "\x1c" && CharSequence(U']', ctrl) == "\x1d");
        CHECK(CharSequence(U'_', ctrl) == "\x1f");
        CHECK(CharSequence(U'x', alt) == "\x1bx");
        CHECK(CharSequence(U'b', [] { Mods m; m.ctrl = m.alt = true; return m; }()) == "\x1b\x02");
        CHECK(CharSequence(U'q', none) == "q");
        CHECK(CharSequence(0xE9, none) == "\xc3\xa9");
        CHECK(CharSequence(0x1F600, none) == "\xf0\x9f\x98\x80");
        CHECK(PasteSequence("a\nb\r\nc", false) == "a\rb\rc");
        CHECK(PasteSequence("ls\n", true) == "\x1b[200~ls\r\x1b[201~");
        CHECK(PasteSequence("x\x1b[201~rm\n", true) == "\x1b[200~x[201~rm\r\x1b[201~");   // cannot end the paste early
    }

#if !defined(_WIN32)
    // ---- a real shell on a pseudo terminal ----
    const fs::path dir = fs::temp_directory_path() / ("okay_launcher_term_" + std::to_string((long)getpid()));
    const fs::path project = dir / "My Project";
    const fs::path tools = dir / "Tools";
    fs::create_directories(project);
    fs::create_directories(tools);
    {
        std::ofstream f(tools / "okay");
        f << "#!/bin/sh\necho \"fake okay says $1\"\n";
        f.close();
        ::chmod((tools / "okay").c_str(), 0755);
    }
    {
        okaylcli::ShellSession s(60, 10);
        std::string err;
        CHECK(s.StartArgv({"/bin/sh"}, project.string(), tools.string(), err));
        CHECK(s.Running());
        // printf with colours: the grid has the text and the colour
        CHECK(s.Send("printf '\\033[31mred\\033[0m %s\\n' plain\r"));
        CHECK(WaitFor(s, [&] { return Shows(s, "red plain"); }));
        {
            const okayterm::Terminal& t = s.Term();
            bool redCell = false;
            for (long long ln = t.FirstLine(); ln < t.EndLine(); ++ln) {
                const std::string text = t.LineText(ln);
                const std::size_t at = text.find("red plain");
                if (at != std::string::npos && text.find("printf") == std::string::npos)
                    redCell = t.LineAt(ln).cells[at].fg == PaletteColor(1) && t.LineAt(ln).cells[at + 4].fg == kDefaultColor;
            }
            CHECK(redCell);
        }
        // echo, the folder and Tools first on PATH
        CHECK(s.Send("echo \"dir=$PWD\"\r"));
        CHECK(WaitFor(s, [&] { return Shows(s, "dir=" + project.string()); }));
        CHECK(s.Send("okay hello\r"));
        CHECK(WaitFor(s, [&] { return Shows(s, "fake okay says hello"); }));
        CHECK(s.Send("echo \"term=$TERM\"\r"));
        CHECK(WaitFor(s, [&] { return Shows(s, "term=xterm-256color"); }));
        // resize: the program sees the new size
        s.Resize(50, 12);
        CHECK(s.Term().Cols() == 50 && s.Term().Rows() == 12);
        CHECK(s.Send("stty size\r"));
        CHECK(WaitFor(s, [&] { return Shows(s, "12 50"); }));
        // Ctrl+C stops sleep, and the shell carries on
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(s.Send("sleep 30; echo slept\r"));
        WaitFor(s, [] { return false; }, 300);
        CHECK(s.Send(okayterm::CharSequence(U'c', [] { Mods m; m.ctrl = true; return m; }())));
        CHECK(s.Send("echo after-int\r"));
        CHECK(WaitFor(s, [&] { return CountOf(s.Term().AllText(), "after-int") >= 2; }, 5000));   // the echoed line and its output
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
        CHECK(!Shows(s, "\nslept"));
        CHECK(s.Running());
        // a paste goes in as typed text
        CHECK(s.Paste("echo pasted-line\n"));
        CHECK(WaitFor(s, [&] { return CountOf(s.Term().AllText(), "pasted-line") >= 2; }));
        // exit: the session ends and says so
        CHECK(s.Send("exit 7\r"));
        CHECK(WaitFor(s, [&] { return !s.Running(); }));
        s.Poll();
        CHECK(s.ExitCode() == 7);
        CHECK(Shows(s, "exit code 7"));
        CHECK(!s.Send("echo nothing\r"));   // nothing to send to
    }
    {   // colours from a real program: ls --color
        std::ofstream(project / "file.txt") << "x";
        fs::create_directories(project / "Assets");
        okaylcli::ShellSession s(80, 10);
        std::string err;
        CHECK(s.StartArgv({"/bin/sh"}, project.string(), "", err));
        CHECK(s.Send("ls --color=always\r"));
        CHECK(WaitFor(s, [&] { return Shows(s, "Assets") && Shows(s, "file.txt"); }));
        bool coloured = false;
        const okayterm::Terminal& t = s.Term();
        for (long long ln = t.FirstLine(); ln < t.EndLine(); ++ln) {
            const std::string text = t.LineText(ln);
            const std::size_t at = text.find("Assets");
            if (at != std::string::npos && text.find("ls ") == std::string::npos)
                coloured = t.LineAt(ln).cells[at].fg != kDefaultColor;
        }
        CHECK(coloured);   // a folder in ls's colour
        s.Stop();
        CHECK(!s.Running());
        s.Poll();
        CHECK(Shows(s, "stopped"));
    }
    {   // a program that is not there; Stop on a full-screen program brings the main screen back
        okaylcli::ShellSession s;
        std::string err;
        CHECK(!s.StartArgv({(dir / "no-shell").string()}, "", "", err));
        CHECK(!err.empty() && Shows(s, "Could not find"));
        okaylcli::ShellSession f(40, 5);
        CHECK(f.StartArgv({"/bin/sh", "-c", "printf 'before\\n\\033[?1049hinside'; sleep 30"}, "", "", err));
        CHECK(WaitFor(f, [&] { return f.Term().AltScreen(); }));
        f.Stop();
        f.Poll();
        CHECK(!f.Term().AltScreen() && Shows(f, "before"));
    }
    {   // okaycli::Pty directly: a missing folder is a failed start, not a hang
        okaycli::Pty p;
        okaycli::Pty::Options o;
        o.argv = {"/bin/sh", "-c", "echo hi"};
        o.cwd = (dir / "nope").string();
        std::string err;
        CHECK(!p.Start(o, err));
        CHECK(!err.empty() && !p.Running());
    }
    {   // the shell list
        CHECK(okaylcli::DefaultShell() == okaylcli::ShellKind::UserShell);
        CHECK(okaylcli::AvailableShells().back() == okaylcli::ShellKind::Okay);
        std::string err;
        CHECK(!okaylcli::ShellArgv(okaylcli::ShellKind::UserShell, err).empty());
        CHECK(okaylcli::ShellArgv(okaylcli::ShellKind::Okay, err).empty() && !err.empty());
        CHECK(okaylcli::ShellLabel(okaylcli::ShellKind::Okay) == "okay only");
        CHECK(!okaylcli::ShellLabel(okaylcli::ShellKind::UserShell).empty());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
#endif
    TEST_MAIN_RESULT();
}
