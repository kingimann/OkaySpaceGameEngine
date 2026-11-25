// The launcher's terminal emulator. See Terminal.hpp for what it was written from. Our own code.
#include "Terminal.hpp"

#include "okay/Core/Utf8.hpp"   // Next / Append (UTF-8) and CellWidth (how many cells a character takes)

#include <algorithm>

namespace okayterm {

// ---- colours ------------------------------------------------------------------------------------------------------

const Scheme& DarkScheme() {
    // Chosen for the launcher's dark theme (its output box is 0x121317, its accent 0x4296FA): every colour reads on the
    // background, black is a step above it and bright black is a readable grey.
    static const Scheme s = {
        0xDCDEE4, 0x121317, 0xE8EAF0, 0x2F4A73,
        {0x2B2E36, 0xE0605E, 0x7FC67A, 0xE3C26A, 0x4F95F0, 0xC183E6, 0x52C6D0, 0xC8CCD4,
         0x6B7080, 0xFF7B78, 0x9BE395, 0xF5D98A, 0x82B6FF, 0xD9A3F5, 0x7EDDE5, 0xF2F4F8},
    };
    return s;
}

std::uint32_t PaletteRgb(int index, const Scheme& s) {
    index &= 0xFF;
    if (index < 16) return s.ansi[index];
    if (index < 232) {   // 6 levels per channel: 0, then 95 + 40 steps
        const int i = index - 16;
        auto level = [](int v) { return v == 0 ? 0 : 55 + 40 * v; };
        return ((std::uint32_t)level(i / 36) << 16) | ((std::uint32_t)level((i / 6) % 6) << 8) | (std::uint32_t)level(i % 6);
    }
    const std::uint32_t g = (std::uint32_t)(8 + 10 * (index - 232));   // 24 greys from 8 to 238
    return (g << 16) | (g << 8) | g;
}

namespace {
std::uint32_t Resolve(Color c, bool fg, bool bold, const Scheme& s) {
    if (c == kDefaultColor) return fg ? (bold ? s.ansi[15] : s.fg) : s.bg;
    if (IsRgb(c)) return c & 0xFFFFFF;
    int i = (int)(c & 0xFF);
    if (fg && bold && i < 8) i += 8;   // bold shows as the bright colour, as the VT manuals' "bold" did on colour screens
    return PaletteRgb(i, s);
}
std::uint32_t Mix(std::uint32_t a, std::uint32_t b) {   // half way between two colours
    const std::uint32_t r = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF)) / 2, g = (((a >> 8) & 0xFF) + ((b >> 8) & 0xFF)) / 2,
                        bl = ((a & 0xFF) + (b & 0xFF)) / 2;
    return (r << 16) | (g << 8) | bl;
}
} // namespace

void CellColors(const Cell& c, const Scheme& s, std::uint32_t& fg, std::uint32_t& bg) {
    fg = Resolve(c.fg, true, (c.attr & kBold) != 0, s);
    bg = Resolve(c.bg, false, false, s);
    if (c.attr & kInverse) std::swap(fg, bg);
    if (c.attr & kFaint) fg = Mix(fg, bg);
    if (c.attr & kHidden) fg = bg;
}

// ---- set-up -------------------------------------------------------------------------------------------------------

Terminal::Terminal(int cols, int rows, std::size_t scrollbackLimit)
    : cols_(std::max(2, cols)), rows_(std::max(1, rows)), limit_(scrollbackLimit) {
    mainRows_.assign((std::size_t)rows_, BlankLine());
    altRows_.assign((std::size_t)rows_, BlankLine());
    bottom_ = rows_ - 1;
    ResetTabs();
}

Cell Terminal::BlankCell() const {
    Cell c;
    c.bg = cur_.pen.bg;   // erasing paints the current background (as xterm and the Windows console do)
    return c;
}

Line Terminal::BlankLine() const {
    Line l;
    l.cells.assign((std::size_t)cols_, BlankCell());
    return l;
}

void Terminal::ResetTabs() {
    tabs_.assign((std::size_t)cols_, false);
    for (int x = 8; x < cols_; x += 8) tabs_[(std::size_t)x] = true;
}

void Terminal::Reset() {
    cur_ = Cursor();
    saved_ = savedMain_ = Cursor();
    alt_ = false;
    mainRows_.assign((std::size_t)rows_, BlankLine());
    altRows_.assign((std::size_t)rows_, BlankLine());
    top_ = 0;
    bottom_ = rows_ - 1;
    autoWrap_ = true;
    insert_ = appCursor_ = appKeypad_ = bracketedPaste_ = false;
    cursorVisible_ = true;
    title_.clear();
    titleChanged_ = true;
    state_ = State::Ground;
    utf8Have_ = utf8Need_ = 0;
    ResetTabs();
    Touch();
}

void Terminal::ClearAll() {
    scrollback_.clear();
    dropped_ = 0;
    for (Line& l : Screen()) l = BlankLine();
    cur_.x = cur_.y = 0;
    cur_.pendingWrap = false;
    ClearSelection();
    Touch();
}

const Line& Terminal::ScreenLine(int row) const {
    row = std::max(0, std::min(rows_ - 1, row));
    return Screen()[(std::size_t)row];
}

namespace {
std::string CellsText(const std::vector<Cell>& cells, int from, int to, bool trim) {
    std::string out;
    to = std::min(to, (int)cells.size());
    for (int x = std::max(0, from); x < to; ++x) {
        const Cell& c = cells[(std::size_t)x];
        if (c.width == 0) continue;
        okay::utf8::Append(out, c.ch ? (std::uint32_t)c.ch : 0x20u);
    }
    if (trim)
        while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
} // namespace

std::string Terminal::RowText(int row) const { return CellsText(ScreenLine(row).cells, 0, cols_, true); }

long long Terminal::FirstLine() const { return alt_ ? ScreenTop() : dropped_; }

const Line& Terminal::LineAt(long long line) const {
    if (line >= ScreenTop()) return ScreenLine((int)std::min<long long>(line - ScreenTop(), rows_ - 1));
    long long i = line - dropped_;
    if (alt_ || scrollback_.empty() || i < 0) return ScreenLine(0);
    return scrollback_[(std::size_t)std::min<long long>(i, (long long)scrollback_.size() - 1)];
}

std::string Terminal::LineText(long long line) const { return CellsText(LineAt(line).cells, 0, 1 << 20, true); }

// ---- the parser ---------------------------------------------------------------------------------------------------

void Terminal::Feed(const char* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) Byte((unsigned char)data[i]);
    if (n) Touch();
}

void Terminal::Byte(unsigned char b) {
    // CAN and SUB end any sequence (ECMA-48 8.3.6 / 8.3.148); ESC always starts a new one, except as the start of the
    // ST (ESC \) that closes a string.
    if (b == 0x18 || b == 0x1A) {
        if (state_ != State::Ground) { state_ = State::Ground; if (b == 0x1A) Print(0xFFFD); }
        return;
    }
    switch (state_) {
    case State::Ground:
        if (b >= 0x80 || utf8Need_) { Utf8Byte(b); return; }
        if (b == 0x1B) { state_ = State::Escape; inter_.clear(); return; }
        if (b < 0x20 || b == 0x7F) { Control(b); return; }
        Print(b);
        return;
    case State::Escape:
        if (b == 0x1B) return;
        if (b < 0x20) { Control(b); return; }
        if (b == '[') {
            state_ = State::Csi;
            params_.clear(); subParam_.clear(); inter_.clear();
            paramStarted_ = false; privateMark_ = 0; csiBad_ = false;
            return;
        }
        if (b == ']') { state_ = State::Osc; osc_.clear(); return; }
        if (b == 'P' || b == 'X' || b == '^' || b == '_') { state_ = State::Str; return; }   // DCS, SOS, PM, APC: read, ignored
        if (b >= 0x20 && b <= 0x2F) { inter_ += (char)b; state_ = State::EscInter; return; }
        state_ = State::Ground;
        EscDispatch(b);
        return;
    case State::EscInter:
        if (b == 0x1B) { state_ = State::Escape; inter_.clear(); return; }
        if (b < 0x20) { Control(b); return; }
        if (b >= 0x20 && b <= 0x2F) { inter_ += (char)b; return; }
        state_ = State::Ground;
        EscDispatch(b);
        return;
    case State::Csi:
        if (b == 0x1B) { state_ = State::Escape; inter_.clear(); return; }
        if (b < 0x20) { Control(b); return; }   // controls inside a sequence act at once (VT manuals)
        if (b >= '0' && b <= '9') {
            if (!inter_.empty()) { csiBad_ = true; return; }
            if (!paramStarted_) { params_.push_back(-1); subParam_.push_back(false); paramStarted_ = true; }
            int& v = params_.back();
            v = (v < 0 ? 0 : v) * 10 + (b - '0');
            if (v > 65535) v = 65535;
            return;
        }
        if (b == ';' || b == ':') {
            if (!inter_.empty()) { csiBad_ = true; return; }
            if (!paramStarted_) { params_.push_back(-1); subParam_.push_back(false); }
            params_.push_back(-1);
            subParam_.push_back(b == ':');
            paramStarted_ = true;
            if (params_.size() > 32) csiBad_ = true;
            return;
        }
        if (b >= '<' && b <= '?') {   // a private marker belongs right after CSI
            if (params_.empty() && !paramStarted_ && !privateMark_) privateMark_ = (char)b;
            else csiBad_ = true;
            return;
        }
        if (b >= 0x20 && b <= 0x2F) { inter_ += (char)b; return; }
        if (b >= 0x40 && b <= 0x7E) {
            state_ = State::Ground;
            if (!csiBad_) CsiDispatch(b);
            return;
        }
        return;   // DEL and bytes above 0x7F inside a sequence are dropped
    case State::Osc:
        if (b == 0x07) { state_ = State::Ground; OscDispatch(); return; }   // BEL ends it (as xterm and ConPTY use)
        if (b == 0x1B) { state_ = State::OscEsc; return; }
        if (osc_.size() < 4096) osc_ += (char)b;
        return;
    case State::OscEsc:
        if (b == '\\') { state_ = State::Ground; OscDispatch(); return; }
        state_ = State::Escape;   // not ST: the string is dropped and this ESC starts a new sequence
        inter_.clear();
        Byte(b);
        return;
    case State::Str:
        if (b == 0x1B) state_ = State::StrEsc;
        return;
    case State::StrEsc:
        state_ = b == '\\' ? State::Ground : State::Str;
        return;
    }
}

void Terminal::Utf8Byte(unsigned char b) {
    if (utf8Need_ == 0) {
        if (b >= 0xC2 && b <= 0xDF) utf8Need_ = 2;
        else if (b >= 0xE0 && b <= 0xEF) utf8Need_ = 3;
        else if (b >= 0xF0 && b <= 0xF4) utf8Need_ = 4;
        else { Print(0xFFFD); return; }   // a continuation byte on its own, C0 / C1 or F5..FF
        utf8Buf_[0] = b;
        utf8Have_ = 1;
        return;
    }
    if ((b & 0xC0) != 0x80) {   // the sequence broke off: show U+FFFD, then read this byte afresh
        utf8Need_ = utf8Have_ = 0;
        Print(0xFFFD);
        Byte(b);
        return;
    }
    utf8Buf_[utf8Have_++] = b;
    if (utf8Have_ < utf8Need_) return;
    const char* p = (const char*)utf8Buf_;
    const char* end = p + utf8Have_;
    const std::uint32_t cp = okay::utf8::Next(p, end);
    const int used = (int)(p - (const char*)utf8Buf_);
    const int have = utf8Have_;
    unsigned char rest[4];
    for (int i = used; i < have; ++i) rest[i - used] = utf8Buf_[i];
    utf8Need_ = utf8Have_ = 0;
    Print(cp ? (char32_t)cp : U'\xFFFD');
    for (int i = 0; i < have - used; ++i) Utf8Byte(rest[i]);   // a bad second byte (overlong, surrogate): read again
}

namespace {
/// The DEC Special Graphics set (VT100 manual, "Special Graphics Characters"): what 0x5F..0x7E show when it is chosen.
char32_t DecGraphics(char32_t c) {
    static const char32_t map[32] = {
        0x0020, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
        0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
    };
    return c >= 0x5F && c <= 0x7E ? map[c - 0x5F] : c;
}
} // namespace

void Terminal::FixWideAt(int row, int col) {
    std::vector<Cell>& cells = Screen()[(std::size_t)row].cells;
    if (col < 0 || col >= cols_) return;
    Cell& c = cells[(std::size_t)col];
    if (c.width == 2 && col + 1 < cols_) { cells[(std::size_t)col + 1] = BlankCell(); }
    if (c.width == 0 && col > 0) { cells[(std::size_t)col - 1] = BlankCell(); }
}

void Terminal::Print(char32_t cp) {
    const bool graphics = cur_.shiftOut ? cur_.g1Graphics : cur_.g0Graphics;
    if (graphics) cp = DecGraphics(cp);
    int w = okay::utf8::CellWidth((std::uint32_t)cp);
    if (w == 0) return;   // a combining mark: kept off the grid (the base character shows without it)
    if (w == 2 && cols_ < 2) w = 1;
    std::vector<Line>& scr = Screen();
    if (cur_.pendingWrap && autoWrap_) {
        scr[(std::size_t)cur_.y].wrapped = true;
        cur_.x = 0;
        cur_.pendingWrap = false;
        LineFeed();
    }
    if (w == 2 && cur_.x == cols_ - 1) {   // no room for both halves: it goes to the next line
        if (autoWrap_) {
            scr[(std::size_t)cur_.y].cells[(std::size_t)cur_.x] = BlankCell();
            scr[(std::size_t)cur_.y].wrapped = true;
            cur_.x = 0;
            LineFeed();
        } else {
            cur_.x = cols_ - 2;
        }
    }
    std::vector<Cell>& cells = scr[(std::size_t)cur_.y].cells;
    if (insert_) {   // IRM: the rest of the line moves right
        for (int k = 0; k < w; ++k) {
            cells.insert(cells.begin() + cur_.x, BlankCell());
            cells.pop_back();
        }
        if (cells.back().width == 2) cells.back() = BlankCell();
    }
    FixWideAt(cur_.y, cur_.x);
    if (w == 2) FixWideAt(cur_.y, cur_.x + 1);
    Cell c;
    c.ch = cp;
    c.fg = cur_.pen.fg;
    c.bg = cur_.pen.bg;
    c.attr = cur_.pen.attr;
    c.width = (std::uint8_t)w;
    cells[(std::size_t)cur_.x] = c;
    if (w == 2) {
        Cell r = c;
        r.ch = U' ';
        r.width = 0;
        cells[(std::size_t)cur_.x + 1] = r;
    }
    lastPrinted_ = cp;
    cur_.x += w;
    if (cur_.x >= cols_) {
        cur_.x = cols_ - 1;
        cur_.pendingWrap = autoWrap_;
    }
}

void Terminal::Control(unsigned char c) {
    switch (c) {
    case 0x07: bell_ = true; break;   // BEL
    case 0x08:                        // BS
        if (cur_.x > 0) --cur_.x;
        cur_.pendingWrap = false;
        break;
    case 0x09: {                      // HT: the next tab stop, or the last column
        int x = cur_.x + 1;
        while (x < cols_ - 1 && !tabs_[(std::size_t)x]) ++x;
        cur_.x = std::min(x, cols_ - 1);
        cur_.pendingWrap = false;
        break;
    }
    case 0x0A: case 0x0B: case 0x0C: LineFeed(); break;   // LF, VT, FF
    case 0x0D: cur_.x = 0; cur_.pendingWrap = false; break;   // CR
    case 0x0E: cur_.shiftOut = true; break;    // SO: G1
    case 0x0F: cur_.shiftOut = false; break;   // SI: G0
    default: break;   // NUL, DEL and the rest do nothing
    }
}

void Terminal::PushScrollback(const Line& l) {
    if (limit_ == 0) { ++dropped_; return; }
    scrollback_.push_back(l);
    while (scrollback_.size() > limit_) { scrollback_.pop_front(); ++dropped_; }
}

void Terminal::ScrollUp(int top, int bottom, int n) {
    std::vector<Line>& scr = Screen();
    n = std::max(1, std::min(n, bottom - top + 1));
    const bool keep = !alt_ && top == 0 && bottom == rows_ - 1;   // only whole-screen scrolling feeds the scrollback
    for (int k = 0; k < n; ++k) {
        if (keep) PushScrollback(scr[(std::size_t)top]);
        scr.erase(scr.begin() + top);
        scr.insert(scr.begin() + bottom, BlankLine());
    }
}

void Terminal::ScrollDown(int top, int bottom, int n) {
    std::vector<Line>& scr = Screen();
    n = std::max(1, std::min(n, bottom - top + 1));
    for (int k = 0; k < n; ++k) {
        scr.erase(scr.begin() + bottom);
        scr.insert(scr.begin() + top, BlankLine());
    }
}

void Terminal::LineFeed() {
    cur_.pendingWrap = false;
    if (cur_.y == bottom_) ScrollUp(top_, bottom_, 1);
    else if (cur_.y < rows_ - 1) ++cur_.y;
}

void Terminal::ReverseIndex() {
    cur_.pendingWrap = false;
    if (cur_.y == top_) ScrollDown(top_, bottom_, 1);
    else if (cur_.y > 0) --cur_.y;
}

void Terminal::EraseCells(int row, int from, int to) {
    if (row < 0 || row >= rows_) return;
    from = std::max(0, from);
    to = std::min(cols_, to);
    if (from >= to) return;
    FixWideAt(row, from);
    FixWideAt(row, to - 1);
    std::vector<Cell>& cells = Screen()[(std::size_t)row].cells;
    for (int x = from; x < to; ++x) cells[(std::size_t)x] = BlankCell();
}

void Terminal::MoveTo(int x, int y) {
    int minY = 0, maxY = rows_ - 1;
    if (cur_.origin) { y += top_; minY = top_; maxY = bottom_; }
    cur_.x = std::max(0, std::min(cols_ - 1, x));
    cur_.y = std::max(minY, std::min(maxY, y));
    cur_.pendingWrap = false;
}

void Terminal::SetAltScreen(bool on, bool saveCursor, bool clear) {
    if (on == alt_) return;
    if (on) {
        if (saveCursor) savedMain_ = cur_;
        alt_ = true;
        if (clear) for (Line& l : altRows_) l = BlankLine();
    } else {
        if (clear) for (Line& l : altRows_) l = BlankLine();
        alt_ = false;
        if (saveCursor) cur_ = savedMain_;
    }
    top_ = 0;
    bottom_ = rows_ - 1;
    cur_.x = std::min(cur_.x, cols_ - 1);
    cur_.y = std::min(cur_.y, rows_ - 1);
    ClearSelection();
}

void Terminal::EscDispatch(unsigned char f) {
    if (!inter_.empty()) {
        const char i = inter_[0];
        if (i == '(' || i == ')') {   // SCS: choose G0 / G1. '0' = DEC Special Graphics, anything else = ASCII
            const bool g = f == '0';
            (i == '(' ? cur_.g0Graphics : cur_.g1Graphics) = g;
        } else if (i == '#' && f == '8') {   // DECALN: the screen full of E
            for (Line& l : Screen())
                for (Cell& c : l.cells) { c = Cell(); c.ch = U'E'; }
            MoveTo(0, 0);
        }
        return;
    }
    switch (f) {
    case '7': saved_ = cur_; break;               // DECSC
    case '8': cur_ = saved_; cur_.x = std::min(cur_.x, cols_ - 1); cur_.y = std::min(cur_.y, rows_ - 1); break;   // DECRC
    case 'D': LineFeed(); break;                  // IND
    case 'E': cur_.x = 0; LineFeed(); break;      // NEL
    case 'M': ReverseIndex(); break;              // RI
    case 'H': tabs_[(std::size_t)cur_.x] = true; break;   // HTS
    case 'c': Reset(); break;                     // RIS
    case '=': appKeypad_ = true; break;           // DECKPAM
    case '>': appKeypad_ = false; break;          // DECKPNM
    default: break;
    }
}

int Terminal::Param(std::size_t i, int def) const {
    if (i >= params_.size() || params_[i] < 0) return def;
    return params_[i];
}

void Terminal::CsiDispatch(unsigned char f) {
    auto P1 = [&](std::size_t i) { const int v = Param(i, 1); return v == 0 ? 1 : v; };   // a count: 0 means 1
    if (!inter_.empty()) {
        if (inter_ == "!" && f == 'p') {   // DECSTR, a soft reset
            cursorVisible_ = true;
            insert_ = cur_.origin = appCursor_ = appKeypad_ = false;
            autoWrap_ = true;
            cur_.pen = Pen();
            top_ = 0; bottom_ = rows_ - 1;
            saved_ = Cursor();
        }
        return;   // CSI Ps SP q (cursor shape) and the rest: not needed for the grid
    }
    if (privateMark_ == '?' && (f == 'h' || f == 'l')) { SetMode(f == 'h'); return; }
    if (privateMark_ == '>' && f == 'c') { reply_ += "\x1b[>0;10;1c"; return; }   // DA2
    if (privateMark_ && f != 'h' && f != 'l') return;
    switch (f) {
    case '@': {   // ICH
        const int n = std::min(P1(0), cols_ - cur_.x);
        std::vector<Cell>& cells = Screen()[(std::size_t)cur_.y].cells;
        FixWideAt(cur_.y, cur_.x);
        for (int k = 0; k < n; ++k) { cells.insert(cells.begin() + cur_.x, BlankCell()); cells.pop_back(); }
        if (cells.back().width == 2) cells.back() = BlankCell();
        cur_.pendingWrap = false;
        break;
    }
    case 'A': {   // CUU: stops at the region's top when it starts inside it
        const int stop = cur_.y >= top_ ? top_ : 0;
        cur_.y = std::max(stop, cur_.y - P1(0));
        cur_.pendingWrap = false;
        break;
    }
    case 'B': {   // CUD: stops at the region's bottom when it starts inside it
        const int target = cur_.y + P1(0);
        const int stop = cur_.y <= bottom_ ? bottom_ : rows_ - 1;
        cur_.y = std::min(target, stop);
        cur_.pendingWrap = false;
        break;
    }
    case 'C': cur_.x = std::min(cols_ - 1, cur_.x + P1(0)); cur_.pendingWrap = false; break;   // CUF
    case 'D': cur_.x = std::max(0, std::min(cols_ - 1, cur_.x) - P1(0)); cur_.pendingWrap = false; break;   // CUB
    case 'E': cur_.x = 0; cur_.y = std::min(cur_.y <= bottom_ ? bottom_ : rows_ - 1, cur_.y + P1(0)); cur_.pendingWrap = false; break;   // CNL
    case 'F': cur_.x = 0; cur_.y = std::max(cur_.y >= top_ ? top_ : 0, cur_.y - P1(0)); cur_.pendingWrap = false; break;   // CPL
    case 'G': case '`': cur_.x = std::max(0, std::min(cols_ - 1, P1(0) - 1)); cur_.pendingWrap = false; break;   // CHA, HPA
    case 'H': case 'f': MoveTo(P1(1) - 1, P1(0) - 1); break;   // CUP, HVP
    case 'I': for (int k = 0; k < P1(0); ++k) Control(0x09); break;   // CHT
    case 'Z': {   // CBT
        for (int k = 0; k < P1(0) && cur_.x > 0; ++k) {
            int x = cur_.x - 1;
            while (x > 0 && !tabs_[(std::size_t)x]) --x;
            cur_.x = x;
        }
        cur_.pendingWrap = false;
        break;
    }
    case 'J': {   // ED
        const int mode = Param(0, 0);
        if (mode == 0) {
            EraseCells(cur_.y, cur_.x, cols_);
            for (int y = cur_.y + 1; y < rows_; ++y) EraseCells(y, 0, cols_);
        } else if (mode == 1) {
            for (int y = 0; y < cur_.y; ++y) EraseCells(y, 0, cols_);
            EraseCells(cur_.y, 0, cur_.x + 1);
        } else if (mode == 2) {
            for (int y = 0; y < rows_; ++y) { EraseCells(y, 0, cols_); Screen()[(std::size_t)y].wrapped = false; }
        } else if (mode == 3 && !alt_) {   // xterm's "erase saved lines": the scrollback
            scrollback_.clear();
            ClearSelection();
        }
        break;
    }
    case 'K': {   // EL
        const int mode = Param(0, 0);
        if (mode == 0) { EraseCells(cur_.y, cur_.x, cols_); Screen()[(std::size_t)cur_.y].wrapped = false; }
        else if (mode == 1) EraseCells(cur_.y, 0, cur_.x + 1);
        else if (mode == 2) EraseCells(cur_.y, 0, cols_);
        break;
    }
    case 'L':   // IL: only inside the scroll region
        if (cur_.y >= top_ && cur_.y <= bottom_) { ScrollDown(cur_.y, bottom_, P1(0)); cur_.x = 0; }
        cur_.pendingWrap = false;
        break;
    case 'M':   // DL
        if (cur_.y >= top_ && cur_.y <= bottom_) {
            std::vector<Line>& scr = Screen();
            const int n = std::min(P1(0), bottom_ - cur_.y + 1);
            for (int k = 0; k < n; ++k) { scr.erase(scr.begin() + cur_.y); scr.insert(scr.begin() + bottom_, BlankLine()); }
            cur_.x = 0;
        }
        cur_.pendingWrap = false;
        break;
    case 'P': {   // DCH
        const int n = std::min(P1(0), cols_ - cur_.x);
        std::vector<Cell>& cells = Screen()[(std::size_t)cur_.y].cells;
        FixWideAt(cur_.y, cur_.x);
        if (cur_.x + n < cols_) FixWideAt(cur_.y, cur_.x + n);
        cells.erase(cells.begin() + cur_.x, cells.begin() + cur_.x + n);
        cells.insert(cells.end(), (std::size_t)n, BlankCell());
        cur_.pendingWrap = false;
        break;
    }
    case 'S': ScrollUp(top_, bottom_, P1(0)); break;     // SU
    case 'T': ScrollDown(top_, bottom_, P1(0)); break;   // SD
    case 'X': EraseCells(cur_.y, cur_.x, cur_.x + P1(0)); cur_.pendingWrap = false; break;   // ECH
    case 'a': cur_.x = std::min(cols_ - 1, cur_.x + P1(0)); cur_.pendingWrap = false; break;   // HPR
    case 'b': for (int k = 0, n = std::min(P1(0), 65535); k < n; ++k) Print(lastPrinted_); break;   // REP
    case 'c': if (Param(0, 0) == 0) reply_ += "\x1b[?1;2c"; break;   // DA: a VT100 with the advanced video option
    case 'd': MoveTo(cur_.x, P1(0) - 1); break;   // VPA
    case 'e': MoveTo(cur_.x, cur_.y - (cur_.origin ? top_ : 0) + P1(0)); break;   // VPR
    case 'g': {   // TBC
        const int mode = Param(0, 0);
        if (mode == 0) tabs_[(std::size_t)cur_.x] = false;
        else if (mode == 3) std::fill(tabs_.begin(), tabs_.end(), false);
        break;
    }
    case 'h': case 'l': SetMode(f == 'h'); break;
    case 'm': Sgr(); break;
    case 'n':   // DSR
        if (Param(0, 0) == 5) reply_ += "\x1b[0n";
        else if (Param(0, 0) == 6)
            reply_ += "\x1b[" + std::to_string(cur_.y - (cur_.origin ? top_ : 0) + 1) + ";" + std::to_string(cur_.x + 1) + "R";
        break;
    case 'r': {   // DECSTBM
        const int t = P1(0) - 1, b = Param(1, 0) == 0 ? rows_ - 1 : Param(1, rows_) - 1;
        if (t < b && b < rows_) { top_ = t; bottom_ = b; MoveTo(0, 0); }
        break;
    }
    case 's': if (params_.empty()) saved_ = cur_; break;   // SCOSC
    case 'u': if (params_.empty()) { cur_ = saved_; cur_.x = std::min(cur_.x, cols_ - 1); cur_.y = std::min(cur_.y, rows_ - 1); } break;   // SCORC
    default: break;   // window operations (t) and the rest change nothing on the grid
    }
}

void Terminal::SetMode(bool on) {
    for (std::size_t i = 0; i < std::max<std::size_t>(1, params_.size()); ++i) {
        const int m = Param(i, 0);
        if (privateMark_ != '?') {
            if (m == 4) insert_ = on;   // IRM
            continue;
        }
        switch (m) {
        case 1: appCursor_ = on; break;                  // DECCKM
        case 6: cur_.origin = on; MoveTo(0, 0); break;   // DECOM
        case 7: autoWrap_ = on; if (!on) cur_.pendingWrap = false; break;   // DECAWM
        case 25: cursorVisible_ = on; break;             // DECTCEM
        case 47: SetAltScreen(on, false, false); break;
        case 1047: SetAltScreen(on, false, !on); break;
        case 1048: if (on) saved_ = cur_; else cur_ = saved_; break;
        case 1049: SetAltScreen(on, true, true); break;
        case 2004: bracketedPaste_ = on; break;
        default: break;   // mouse reports, focus events, win32-input-mode and the rest: not taken up
        }
    }
}

void Terminal::Sgr() {
    if (params_.empty()) { cur_.pen = Pen(); return; }
    Pen& p = cur_.pen;
    for (std::size_t i = 0; i < params_.size(); ++i) {
        const int v = Param(i, 0);
        // the sub-parameters joined to this one with ':' (38:2::r:g:b, 4:3)
        std::size_t subEnd = i + 1;
        while (subEnd < params_.size() && subParam_[subEnd]) ++subEnd;
        const std::size_t subs = subEnd - i - 1;
        if (v == 38 || v == 48 || v == 58) {
            Color c = kDefaultColor;
            bool ok = false;
            if (subs > 0) {   // colon form: 38:5:n, 38:2:r:g:b or 38:2:cs:r:g:b
                const int kind = Param(i + 1, -1);
                if (kind == 5 && subs >= 2) { c = PaletteColor(Param(i + 2, 0)); ok = true; }
                else if (kind == 2 && subs >= 4) {
                    const std::size_t r = subs >= 5 ? i + 3 : i + 2;
                    c = RgbColor(Param(r, 0), Param(r + 1, 0), Param(r + 2, 0));
                    ok = true;
                }
                i = subEnd - 1;
            } else {          // semicolon form: 38;5;n or 38;2;r;g;b
                const int kind = Param(i + 1, -1);
                if (kind == 5 && i + 2 < params_.size()) { c = PaletteColor(Param(i + 2, 0)); ok = true; i += 2; }
                else if (kind == 2 && i + 4 < params_.size()) {
                    c = RgbColor(Param(i + 2, 0), Param(i + 3, 0), Param(i + 4, 0));
                    ok = true;
                    i += 4;
                } else {
                    i = params_.size();   // not understood: the rest of the list cannot be read safely
                }
            }
            if (ok && v == 38) p.fg = c;
            if (ok && v == 48) p.bg = c;
            continue;
        }
        if (subs > 0) {   // 4:0 is "no underline", 4:n a style of underline
            if (v == 4) { if (Param(i + 1, 1) == 0) p.attr &= ~kUnderline; else p.attr |= kUnderline; }
            i = subEnd - 1;
            continue;
        }
        switch (v) {
        case 0: p = Pen(); break;
        case 1: p.attr |= kBold; break;
        case 2: p.attr |= kFaint; break;
        case 3: p.attr |= kItalic; break;
        case 4: case 21: p.attr |= kUnderline; break;
        case 5: case 6: p.attr |= kBlink; break;
        case 7: p.attr |= kInverse; break;
        case 8: p.attr |= kHidden; break;
        case 9: p.attr |= kStrike; break;
        case 22: p.attr &= ~(kBold | kFaint); break;
        case 23: p.attr &= ~kItalic; break;
        case 24: p.attr &= ~kUnderline; break;
        case 25: p.attr &= ~kBlink; break;
        case 27: p.attr &= ~kInverse; break;
        case 28: p.attr &= ~kHidden; break;
        case 29: p.attr &= ~kStrike; break;
        case 39: p.fg = kDefaultColor; break;
        case 49: p.bg = kDefaultColor; break;
        default:
            if (v >= 30 && v <= 37) p.fg = PaletteColor(v - 30);
            else if (v >= 40 && v <= 47) p.bg = PaletteColor(v - 40);
            else if (v >= 90 && v <= 97) p.fg = PaletteColor(v - 90 + 8);
            else if (v >= 100 && v <= 107) p.bg = PaletteColor(v - 100 + 8);
            break;
        }
    }
}

void Terminal::OscDispatch() {
    const std::size_t semi = osc_.find(';');
    if (semi == std::string::npos) return;
    const std::string code = osc_.substr(0, semi);
    if (code == "0" || code == "2") {   // the window title (OSC 1, the icon name, is not shown)
        std::string t = okay::utf8::Sanitize(osc_.substr(semi + 1));
        std::string clean;
        for (char c : t) if ((unsigned char)c >= 0x20 && c != 0x7F) clean += c;
        if (clean.size() > 256) clean = okay::utf8::TruncateBytes(clean, 256);
        if (clean != title_) { title_ = clean; titleChanged_ = true; }
    }
}

// ---- size ---------------------------------------------------------------------------------------------------------

void Terminal::Resize(int cols, int rows) {
    cols = std::max(2, cols);
    rows = std::max(1, rows);
    if (cols == cols_ && rows == rows_) return;
    auto fitWidth = [&](Line& l) {
        if ((int)l.cells.size() > cols) {
            l.cells.resize((std::size_t)cols);
            if (l.cells.back().width == 2) l.cells.back() = Cell();   // half a wide character would be left
        }
        while ((int)l.cells.size() < cols) l.cells.push_back(Cell());
    };
    cols_ = cols;
    for (Line& l : mainRows_) fitWidth(l);
    for (Line& l : altRows_) fitWidth(l);
    // Rows: the main screen gives its top lines to the scrollback (blank lines under the cursor go first); the
    // alternate screen just loses them.
    auto fitRows = [&](std::vector<Line>& scr, bool isMain, int& cursorY) {
        while ((int)scr.size() > rows) {
            const int last = (int)scr.size() - 1;
            bool blank = last > cursorY;
            if (blank)
                for (const Cell& c : scr[(std::size_t)last].cells) if (c.ch != U' ' || c.bg != kDefaultColor) { blank = false; break; }
            if (blank) { scr.pop_back(); continue; }
            if (isMain) PushScrollback(scr.front());
            scr.erase(scr.begin());
            if (cursorY > 0) --cursorY;
        }
        while ((int)scr.size() < rows) {
            Line l;
            l.cells.assign((std::size_t)cols_, Cell());
            scr.push_back(l);
        }
    };
    int mainY = alt_ ? savedMain_.y : cur_.y;
    int altY = alt_ ? cur_.y : 0;
    fitRows(mainRows_, true, mainY);
    fitRows(altRows_, false, altY);
    rows_ = rows;
    if (alt_) { cur_.y = altY; savedMain_.y = std::min(mainY, rows_ - 1); savedMain_.x = std::min(savedMain_.x, cols_ - 1); }
    else cur_.y = mainY;
    cur_.x = std::min(cur_.x, cols_ - 1);
    cur_.y = std::max(0, std::min(cur_.y, rows_ - 1));
    cur_.pendingWrap = false;
    saved_.x = std::min(saved_.x, cols_ - 1);
    saved_.y = std::min(saved_.y, rows_ - 1);
    top_ = 0;
    bottom_ = rows_ - 1;
    ResetTabs();
    Touch();
}

// ---- the selection ------------------------------------------------------------------------------------------------

void Terminal::SelectStart(Pos p) { selecting_ = true; anchor_ = head_ = p; Touch(); }
void Terminal::SelectExtend(Pos p) { if (selecting_) { head_ = p; Touch(); } }

void Terminal::SelectAll() {
    selecting_ = true;
    anchor_ = {FirstLine(), 0};
    long long last = EndLine() - 1;
    while (last > FirstLine() && LineText(last).empty()) --last;   // not the empty rows under the prompt
    head_ = {last, (int)LineAt(last).cells.size()};
    Touch();
}

void Terminal::SelectWord(Pos p) {
    const Line& l = LineAt(p.line);
    const int n = (int)l.cells.size();
    if (p.col < 0 || p.col >= n) { SelectStart(p); return; }
    auto wordy = [&](int x) {
        const char32_t c = l.cells[(std::size_t)x].ch;
        if (l.cells[(std::size_t)x].width == 0) return true;
        return c > 0x7F || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' || c == ':' || c == '~';
    };
    int a = p.col, b = p.col + 1;
    if (wordy(p.col)) {
        while (a > 0 && wordy(a - 1)) --a;
        while (b < n && wordy(b)) ++b;
    }
    selecting_ = true;
    anchor_ = {p.line, a};
    head_ = {p.line, b};
    Touch();
}

bool Terminal::IsSelected(long long line, int col) const {
    if (!HasSelection()) return false;
    const Pos s = std::min(anchor_, head_), e = std::max(anchor_, head_);
    const Pos at{line, col};
    return !(at < s) && at < e;
}

std::string Terminal::SelectedText() const {
    if (!HasSelection()) return "";
    Pos s = std::min(anchor_, head_), e = std::max(anchor_, head_);
    if (s.line < FirstLine()) s = {FirstLine(), 0};
    if (e.line >= EndLine()) e = {EndLine() - 1, 1 << 20};
    std::string out;
    for (long long ln = s.line; ln <= e.line; ++ln) {
        const Line& l = LineAt(ln);
        const int from = ln == s.line ? s.col : 0;
        const bool last = ln == e.line;
        const int to = last ? e.col : (int)l.cells.size();
        out += CellsText(l.cells, from, to, !last || to >= (int)l.cells.size());
        if (!last && !l.wrapped) out += '\n';
    }
    return out;
}

std::string Terminal::AllText() const {
    std::string out;
    long long last = EndLine() - 1;
    while (last > FirstLine() && LineText(last).empty()) --last;
    for (long long ln = FirstLine(); ln <= last; ++ln) {
        const Line& l = LineAt(ln);
        out += CellsText(l.cells, 0, (int)l.cells.size(), true);
        if (ln != last && !l.wrapped) out += '\n';
    }
    return out;
}

// ---- input: the bytes sent to the shell -------------------------------------------------------------------------

std::string KeySequence(Key k, Mods m, bool appCursor) {
    const int mod = 1 + (m.shift ? 1 : 0) + (m.alt ? 2 : 0) + (m.ctrl ? 4 : 0);
    const std::string esc = "\x1b";
    auto letter = [&](char c) {   // arrows, Home, End, F1..F4
        if (mod > 1) return esc + "[1;" + std::to_string(mod) + c;
        const bool ss3 = (c >= 'P' && c <= 'S') || appCursor;
        return esc + (ss3 ? "O" : "[") + c;
    };
    auto tilde = [&](int n) {
        if (mod > 1) return esc + "[" + std::to_string(n) + ";" + std::to_string(mod) + "~";
        return esc + "[" + std::to_string(n) + "~";
    };
    const std::string altPrefix = m.alt ? esc : std::string();
    switch (k) {
    case Key::None: return "";
    case Key::Up: return letter('A');
    case Key::Down: return letter('B');
    case Key::Right: return letter('C');
    case Key::Left: return letter('D');
    case Key::Home: return letter('H');
    case Key::End: return letter('F');
    case Key::Insert: return tilde(2);
    case Key::Delete: return tilde(3);
    case Key::PageUp: return tilde(5);
    case Key::PageDown: return tilde(6);
    case Key::F1: return letter('P');
    case Key::F2: return letter('Q');
    case Key::F3: return letter('R');
    case Key::F4: return letter('S');
    case Key::F5: return tilde(15);
    case Key::F6: return tilde(17);
    case Key::F7: return tilde(18);
    case Key::F8: return tilde(19);
    case Key::F9: return tilde(20);
    case Key::F10: return tilde(21);
    case Key::F11: return tilde(23);
    case Key::F12: return tilde(24);
    case Key::Enter: return altPrefix + "\r";
    case Key::Backspace: return altPrefix + (m.ctrl ? "\x08" : "\x7f");
    case Key::Tab: return m.shift ? esc + "[Z" : altPrefix + "\t";
    case Key::Escape: return altPrefix + esc;
    }
    return "";
}

std::string CharSequence(char32_t ch, Mods m) {
    std::string out;
    if (m.alt) out += '\x1b';
    if (m.ctrl) {
        if (ch >= 'a' && ch <= 'z') { out += (char)(ch - 'a' + 1); return out; }
        if (ch >= 'A' && ch <= 'Z') { out += (char)(ch - 'A' + 1); return out; }
        if (ch == ' ' || ch == '@' || ch == '2') { out += '\0'; return out; }
        if (ch >= '[' && ch <= '_') { out += (char)(ch - '[' + 0x1B); return out; }
        if (ch == '/') { out += '\x1f'; return out; }
        if (ch == '?') { out += '\x7f'; return out; }
    }
    okay::utf8::Append(out, (std::uint32_t)ch);
    return out;
}

std::string PasteSequence(const std::string& text, bool bracketed) {
    std::string body;
    body.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') { body += '\r'; if (i + 1 < text.size() && text[i + 1] == '\n') ++i; continue; }
        if (c == '\n') { body += '\r'; continue; }
        if (c == '\x1b' && bracketed) continue;
        body += c;
    }
    if (!bracketed) return body;
    return "\x1b[200~" + body + "\x1b[201~";
}

} // namespace okayterm
