#pragma once
// The launcher's terminal emulator: the screen grid, its scrollback, the parser for the escape sequences a shell and a
// pseudo console write, the selection, and the bytes each key sends. No ImGui and no process, so tests/test_launcher_cli.cpp
// checks it headless; the Command line page (CommandLineUI.cpp) draws it and CommandLine.cpp's ShellSession feeds it from
// the shell's pseudo terminal (okaycli::Pty in tools/okay-cli/Process.hpp).
//
// Our own code and structure, written from the published standards and documents, not from any terminal's code:
//   * ECMA-48 (5th edition, 1991): the C0 controls, the CSI / OSC / DCS / SOS / PM / APC string syntax (parameter,
//     intermediate and final bytes), cursor movement (CUU..CHA, CUP, HVP, VPA, HPA, CNL, CPL, CHT, CBT), editing
//     (ICH, DCH, IL, DL, ECH, ED, EL, REP), SGR, SM / RM, DSR and DA;
//   * the DEC VT100 and VT220 manuals: DECSC / DECRC, IND / NEL / RI / HTS, DECSTBM scroll regions, DECOM, DECAWM and the
//     last-column wrap, DECTCEM, DECCKM application cursor keys, DECKPAM / DECKPNM, the DEC Special Graphics character
//     set, DECALN, DECSTR and the cursor and function key codes;
//   * Microsoft's "Console Virtual Terminal Sequences" page: what ConPTY writes (256-colour and 24-bit SGR, the
//     alternate screen 1049, bracketed paste 2004, the window title through OSC 0 / 2) and the input sequences it reads,
//     including the modifier number in CSI 1 ; m X.
// UTF-8 is decoded with engine/include/okay/Core/Utf8.hpp and character widths come from its CellWidth (one table).
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace okayterm {

// ---- colours ------------------------------------------------------------------------------------------------------

/// A cell's colour: one of the 256 palette entries (0..255), a 24-bit colour (kRgbFlag | 0xRRGGBB) or the default.
using Color = std::uint32_t;
constexpr Color kDefaultColor = 0xFFFFFFFFu;
constexpr Color kRgbFlag = 0x01000000u;
inline Color PaletteColor(int index) { return (Color)(index & 0xFF); }
inline Color RgbColor(int r, int g, int b) { return kRgbFlag | ((Color)(r & 0xFF) << 16) | ((Color)(g & 0xFF) << 8) | (Color)(b & 0xFF); }
inline bool IsRgb(Color c) { return c != kDefaultColor && (c & kRgbFlag) != 0; }

/// The colours a terminal is drawn with (0xRRGGBB). DarkScheme matches the launcher's dark theme.
struct Scheme {
    std::uint32_t fg, bg, cursor, selection;
    std::uint32_t ansi[16];   ///< 0-7 normal, 8-15 bright
};
const Scheme& DarkScheme();
/// 0xRRGGBB of palette entry `index`: 0-15 from the scheme, 16-231 the 6 x 6 x 6 colour cube, 232-255 the grey ramp.
std::uint32_t PaletteRgb(int index, const Scheme& s);

// ---- cells, lines -------------------------------------------------------------------------------------------------

enum Attr : std::uint16_t {
    kBold = 1, kFaint = 2, kItalic = 4, kUnderline = 8, kBlink = 16, kInverse = 32, kHidden = 64, kStrike = 128,
};

struct Cell {
    char32_t ch = U' ';
    Color fg = kDefaultColor, bg = kDefaultColor;
    std::uint16_t attr = 0;
    std::uint8_t width = 1;   ///< 1 a normal cell, 2 the left half of a wide character, 0 its right half
    bool operator==(const Cell& o) const { return ch == o.ch && fg == o.fg && bg == o.bg && attr == o.attr && width == o.width; }
};

struct Line {
    std::vector<Cell> cells;
    bool wrapped = false;   ///< the text went on to the next line by itself (no newline): copied as one line
};

/// The colours a cell is drawn in (0xRRGGBB), after bold-as-bright, faint, inverse and hidden.
void CellColors(const Cell& c, const Scheme& s, std::uint32_t& fg, std::uint32_t& bg);

/// A place in the text: `line` counts every line the terminal ever kept (scrollback first), so it stays the same
/// while new output scrolls the screen; `col` is a cell column.
struct Pos {
    long long line = 0;
    int col = 0;
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
};

// ---- the terminal -------------------------------------------------------------------------------------------------

class Terminal {
public:
    explicit Terminal(int cols = 80, int rows = 24, std::size_t scrollbackLimit = 5000);

    /// What the program wrote. Sequences split across calls are carried over.
    void Feed(const char* data, std::size_t n);
    void Feed(const std::string& s) { Feed(s.data(), s.size()); }
    /// A new size. Text is kept (cut or padded at the right); when it gets shorter, lines from the top go to the
    /// scrollback so the cursor's line stays on the screen.
    void Resize(int cols, int rows);
    /// Everything back to the start (RIS): screen, modes, title. The scrollback is kept.
    void Reset();
    /// The page's Clear: the screen and the scrollback, cursor home.
    void ClearAll();

    int Cols() const { return cols_; }
    int Rows() const { return rows_; }
    int CursorX() const { return cur_.x; }
    int CursorY() const { return cur_.y; }
    bool CursorVisible() const { return cursorVisible_; }
    bool AltScreen() const { return alt_; }
    bool AppCursorKeys() const { return appCursor_; }
    bool AppKeypad() const { return appKeypad_; }
    bool BracketedPaste() const { return bracketedPaste_; }
    bool AutoWrap() const { return autoWrap_; }
    int ScrollTop() const { return top_; }
    int ScrollBottom() const { return bottom_; }
    const std::string& Title() const { return title_; }
    /// True once after the title changed.
    bool TakeTitleChanged() { const bool t = titleChanged_; titleChanged_ = false; return t; }
    /// True once after a bell (BEL).
    bool TakeBell() { const bool b = bell_; bell_ = false; return b; }
    /// Bytes the terminal answers the program with (cursor position report, device attributes). Send them back.
    std::string TakeReply() { std::string r; r.swap(reply_); return r; }

    /// The screen (the alternate one while a full-screen program uses it).
    const Line& ScreenLine(int row) const;
    const Cell& At(int col, int row) const { return ScreenLine(row).cells[(std::size_t)col]; }
    /// One screen row as UTF-8 with the spaces at its end trimmed.
    std::string RowText(int row) const;

    // The view: the scrollback then the screen, numbered by Pos::line. While the alternate screen is up there is no
    // scrollback in the view (as in other terminals).
    std::size_t ScrollbackSize() const { return scrollback_.size(); }
    long long FirstLine() const;                 ///< the oldest line in the view
    long long ScreenTop() const { return dropped_ + (long long)scrollback_.size(); }   ///< the screen's row 0
    long long EndLine() const { return ScreenTop() + rows_; }   ///< one past the last line
    const Line& LineAt(long long line) const;    ///< FirstLine() <= line < EndLine()
    std::string LineText(long long line) const;

    // The selection: from an anchor to where the mouse is, in reading order; the cell at the end is not included.
    void SelectStart(Pos p);
    void SelectExtend(Pos p);
    void SelectWord(Pos p);   ///< a double click: the run of letters, digits and path characters under p
    void SelectAll();
    void ClearSelection() { selecting_ = false; anchor_ = head_ = Pos(); ++revision_; }
    bool HasSelection() const { return selecting_ && !(anchor_ == head_); }
    bool IsSelected(long long line, int col) const;
    /// The selected text: lines joined with "\n" (none where a line wrapped by itself), spaces at line ends trimmed,
    /// the right half of a wide character skipped.
    std::string SelectedText() const;
    /// Every line in the view, as SelectedText would copy it.
    std::string AllText() const;

    /// Goes up whenever anything visible changes.
    unsigned Revision() const { return revision_; }

private:
    struct Pen { Color fg = kDefaultColor, bg = kDefaultColor; std::uint16_t attr = 0; };
    struct Cursor {
        int x = 0, y = 0;
        Pen pen;
        bool pendingWrap = false;   ///< printed in the last column: the next character goes to the next line first
        bool origin = false;        ///< DECOM: rows count from the scroll region's top
        bool g0Graphics = false, g1Graphics = false, shiftOut = false;   ///< DEC Special Graphics in G0 / G1, SO active
    };
    enum class State { Ground, Escape, EscInter, Csi, Osc, OscEsc, Str, StrEsc };

    std::vector<Line>& Screen() { return alt_ ? altRows_ : mainRows_; }
    const std::vector<Line>& Screen() const { return alt_ ? altRows_ : mainRows_; }
    Line BlankLine() const;
    Cell BlankCell() const;
    void Byte(unsigned char b);
    void Utf8Byte(unsigned char b);
    void Print(char32_t cp);
    void Control(unsigned char c);
    void EscDispatch(unsigned char final);
    void CsiDispatch(unsigned char final);
    void OscDispatch();
    void Sgr();
    void SetMode(bool on);
    int Param(std::size_t i, int def) const;
    void LineFeed();
    void ReverseIndex();
    void ScrollUp(int top, int bottom, int n);
    void ScrollDown(int top, int bottom, int n);
    void EraseCells(int row, int from, int to);   ///< [from, to) with the pen's background
    void FixWideAt(int row, int col);              ///< overwriting half of a wide character blanks the other half
    void MoveTo(int x, int y);                     ///< y relative to the origin when DECOM is on; clamped
    void SetAltScreen(bool on, bool saveCursor, bool clear);
    void PushScrollback(const Line& l);
    void ResetTabs();
    void Touch() { ++revision_; }

    int cols_, rows_;
    std::size_t limit_;
    std::vector<Line> mainRows_, altRows_;
    std::deque<Line> scrollback_;
    long long dropped_ = 0;   ///< lines that fell off the front of the scrollback (keeps Pos::line stable)
    bool alt_ = false;
    Cursor cur_, saved_, savedMain_;
    int top_ = 0, bottom_ = 0;
    std::vector<bool> tabs_;
    bool autoWrap_ = true, insert_ = false, cursorVisible_ = true, appCursor_ = false, appKeypad_ = false, bracketedPaste_ = false;
    char32_t lastPrinted_ = U' ';
    std::string title_, reply_;
    bool titleChanged_ = false, bell_ = false;

    // the parser
    State state_ = State::Ground;
    std::vector<int> params_;      ///< -1 = left out
    std::vector<bool> subParam_;   ///< true = joined to the one before with ':' (SGR 38:2:r:g:b)
    bool paramStarted_ = false;
    char privateMark_ = 0;         ///< '?', '>', '<' or '=' right after CSI
    std::string inter_;            ///< intermediate bytes (0x20..0x2F)
    bool csiBad_ = false;          ///< too many parameters or a misplaced byte: the sequence is read and ignored
    std::string osc_;
    unsigned char utf8Buf_[4] = {0, 0, 0, 0};
    int utf8Have_ = 0, utf8Need_ = 0;

    // the selection
    bool selecting_ = false;
    Pos anchor_, head_;
    unsigned revision_ = 0;
};

// ---- input: the bytes sent to the shell -------------------------------------------------------------------------

enum class Key {
    None, Up, Down, Left, Right, Home, End, PageUp, PageDown, Insert, Delete,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, Enter, Backspace, Tab, Escape,
};
struct Mods {
    bool shift = false, ctrl = false, alt = false;
};
/// The bytes a key sends: arrows and Home / End as CSI A..D / H / F (ESC O ... in application cursor mode), the editing
/// keys and F5..F12 as CSI n ~, F1..F4 as ESC O P..S, with the modifier number (1 + Shift 1 + Alt 2 + Ctrl 4) as
/// CSI 1 ; m X or CSI n ; m ~ when a modifier is held. Enter is CR, Backspace DEL (Ctrl+Backspace BS), Shift+Tab CSI Z.
/// Alt adds ESC before a key that has no modifier form. "" for Key::None.
std::string KeySequence(Key k, Mods m, bool appCursor);
/// The bytes a typed character sends: UTF-8, with Ctrl a C0 control (Ctrl+A = 0x01 .. Ctrl+Z = 0x1A, Ctrl+Space and
/// Ctrl+@ = 0x00, Ctrl+[ \ ] ^ _ = 0x1B..0x1F) and with Alt an ESC first.
std::string CharSequence(char32_t ch, Mods m);
/// Pasted text as the shell should get it: line ends as CR; with bracketed paste on, inside ESC [ 200 ~ ... ESC [ 201 ~
/// with any ESC in the text dropped, so pasted text can never end the paste early or send a key sequence.
std::string PasteSequence(const std::string& text, bool bracketed);

} // namespace okayterm
