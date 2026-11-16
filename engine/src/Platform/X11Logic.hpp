#pragma once
// Pure logic for the own X11 backend: protocol encoders / decoders and state machines that need no X server.
// Written by us from the public protocol documents: the Xdnd protocol description (version 5), ICCCM / EWMH
// (the _NET_WORKAREA and _NET_WM_STATE messages, WM_HINTS flags), the RandR extension description (mode timing
// -> refresh rate), the X Resources format (Xft.dpi) and the core cursor font glyph numbers from cursorfont.h.
//
//  * xdnd::Target      the receiving side of drag and drop for one window, as a state machine
//  * ParseXftDpi       the desktop's DPI setting out of the RESOURCE_MANAGER text
//  * BuildDisplays     RandR monitors -> DisplayInfo list (primary first)
//  * CursorGlyph       CursorShape -> core cursor font glyph
//  * Preedit           the text being composed by an input method (XIM preedit callbacks) -> TextEditing events
//  * WrapText, DialogLayout   the plain message box
//  * NetWmStateMessage, urgency helpers
#include "okay/Platform/Platform.hpp"
#include "Exchange.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::platform::x11 {

// ---------------------------------------------------------------------------------------------------------
// Xdnd (receive side)
// ---------------------------------------------------------------------------------------------------------
namespace xdnd {

constexpr int kVersion = 5;
constexpr uint64_t kDataTimeoutMs = 5000;   // the source must hand over the data within this

/// Atom numbers as the X connection assigned them (the logic never talks to the server).
struct Atoms {
    unsigned long uriList = 0;     // "text/uri-list"
    unsigned long actionCopy = 0;  // "XdndActionCopy"
};

struct Enter {
    unsigned long source = 0;
    int version = 0;
    bool moreTypes = false;        // more than 3 types: the list is in the XdndTypeList property of the source
    unsigned long types[3] = {0, 0, 0};
};
struct Position {
    unsigned long source = 0;
    int rootX = 0, rootY = 0;
    unsigned long time = 0;
    unsigned long action = 0;
};

// ClientMessage data.l[] (five longs). Layouts from the protocol description:
//  XdndEnter    l0 source, l1 = version << 24 | (bit 0: more than 3 types), l2..l4 first three types
//  XdndPosition l0 source, l1 0, l2 = x << 16 | y (root coordinates), l3 time, l4 action
//  XdndLeave    l0 source
//  XdndDrop     l0 source, l1 0, l2 time
//  XdndStatus   l0 target, l1 = bit 0 accept | bit 1 want position messages inside the rectangle, l2 = x << 16 | y,
//               l3 = w << 16 | h (rectangle in root coordinates), l4 action
//  XdndFinished l0 target, l1 bit 0 accepted, l2 action (version 5 and later)
inline Enter DecodeEnter(const long d[5]) {
    Enter e;
    e.source = (unsigned long)d[0];
    e.version = (int)(((unsigned long)d[1] >> 24) & 0xff);
    e.moreTypes = (d[1] & 1) != 0;
    for (int i = 0; i < 3; i++) e.types[i] = (unsigned long)d[2 + i];
    return e;
}
inline Position DecodePosition(const long d[5]) {
    Position p;
    p.source = (unsigned long)d[0];
    p.rootX = (int)(((unsigned long)d[2] >> 16) & 0xffff);
    p.rootY = (int)((unsigned long)d[2] & 0xffff);
    p.time = (unsigned long)d[3];
    p.action = (unsigned long)d[4];
    return p;
}
inline void EncodeStatus(unsigned long target, bool accept, bool wantPositions, int rx, int ry, int rw, int rh,
                         unsigned long action, long out[5]) {
    out[0] = (long)target;
    out[1] = (accept ? 1 : 0) | (wantPositions ? 2 : 0);
    out[2] = (long)(((unsigned long)(rx & 0xffff) << 16) | (unsigned long)(ry & 0xffff));
    out[3] = (long)(((unsigned long)(rw & 0xffff) << 16) | (unsigned long)(rh & 0xffff));
    out[4] = (long)action;
}
inline void EncodeFinished(unsigned long target, bool accepted, unsigned long action, long out[5]) {
    out[0] = (long)target;
    out[1] = accepted ? 1 : 0;
    out[2] = accepted ? (long)action : 0;
    out[3] = out[4] = 0;
}
/// What we put in the XdndAware property: our version, capped by the source's (the lower one is used).
inline int NegotiatedVersion(int sourceVersion) { return sourceVersion < kVersion ? sourceVersion : kVersion; }

/// What the caller must do after feeding the state machine one input. Several can be set at once.
struct Reply {
    bool sendStatus = false;       // send XdndStatus to `to`
    unsigned long to = 0;
    bool accept = false;           // status: we take the drop here
    unsigned long action = 0;      // status / finished action atom (0 = XdndActionNone)
    bool requestData = false;      // XConvertSelection(XdndSelection, dataType, ..., time)
    unsigned long dataType = 0;
    unsigned long time = 0;
    bool sendFinished = false;     // send XdndFinished to `to`
    bool finishedOk = false;
    std::vector<Event> events;     // push these to the app's queue
};

class Target {
public:
    enum class State { Idle, Entered, Receiving };

    Target(uint32_t windowId, const Atoms& atoms, uint64_t timeoutMs = kDataTimeoutMs)
        : m_win(windowId), m_atoms(atoms), m_timeout(timeoutMs) {}

    State GetState() const { return m_state; }
    bool Active() const { return m_state != State::Idle; }
    unsigned long Source() const { return m_source; }
    /// The type list is the three in the message, or the whole property when `moreTypes` was set.
    Reply OnEnter(const Enter& e, const std::vector<unsigned long>& typeList) {
        Reply r;
        Reset();   // a new Enter ends whatever was going on (the source may have died)
        m_state = State::Entered;
        m_source = e.source;
        m_version = NegotiatedVersion(e.version);
        m_accepts = false;
        for (unsigned long t : typeList) if (t == m_atoms.uriList && t != 0) m_accepts = true;
        return r;
    }
    /// x, y: the position in this window's own coordinates (the caller translated from the root coordinates).
    Reply OnPosition(const Position& p, int x, int y) {
        Reply r;
        if (m_state != State::Entered || p.source != m_source) return r;   // not ours / not now
        m_lastX = x; m_lastY = y;
        if (m_version >= 1) m_time = p.time;
        r.sendStatus = true; r.to = m_source;
        r.accept = m_accepts;
        r.action = m_accepts ? m_atoms.actionCopy : 0;
        return r;
    }
    Reply OnLeave(unsigned long source) {
        Reply r;
        if (m_state == State::Entered && source == m_source) Reset();
        return r;
    }
    Reply OnDrop(unsigned long source, unsigned long time, uint64_t nowMs) {
        Reply r;
        if (m_state != State::Entered || source != m_source) return r;
        if (!m_accepts) {
            r.sendFinished = true; r.to = m_source; r.finishedOk = false;
            Reset();
            return r;
        }
        m_state = State::Receiving;
        m_deadline = nowMs + m_timeout;
        r.requestData = true; r.dataType = m_atoms.uriList;
        r.time = m_version >= 1 ? time : 0;   // version 0 has no time stamps: CurrentTime
        return r;
    }
    /// The selection transfer ended: `ok` false = the source refused or the property is unusable.
    Reply OnData(bool ok, const std::string& bytes, uint32_t timestampMs = 0) {
        Reply r;
        if (m_state != State::Receiving) return r;
        std::vector<std::string> paths;
        if (ok) paths = ParseUriList(bytes);
        r.sendFinished = true; r.to = m_source;
        r.finishedOk = !paths.empty();
        r.action = r.finishedOk ? m_atoms.actionCopy : 0;
        if (r.finishedOk) {
            r.events = MakeDropEvents(paths, m_win, timestampMs);
            for (Event& e : r.events) { e.x = m_lastX; e.y = m_lastY; }
        }
        Reset();
        return r;
    }
    /// Call now and then: gives up on a source that never sent the data.
    Reply OnTick(uint64_t nowMs) {
        Reply r;
        if (m_state == State::Receiving && nowMs >= m_deadline) {
            r.sendFinished = true; r.to = m_source; r.finishedOk = false;
            Reset();
        }
        return r;
    }

private:
    void Reset() { m_state = State::Idle; m_source = 0; m_accepts = false; m_time = 0; }
    uint32_t m_win;
    Atoms m_atoms;
    uint64_t m_timeout, m_deadline = 0;
    State m_state = State::Idle;
    unsigned long m_source = 0, m_time = 0;
    int m_version = 0, m_lastX = 0, m_lastY = 0;
    bool m_accepts = false;
};

} // namespace xdnd

// ---------------------------------------------------------------------------------------------------------
// Desktop scale and monitors
// ---------------------------------------------------------------------------------------------------------

/// "Xft.dpi:\t120" in the RESOURCE_MANAGER text (one "name: value" per line). 0 when absent or outside 24..960.
/// A later line replaces an earlier one, as in an X resource file. Decimals round to the nearest whole dpi.
inline unsigned ParseXftDpi(const std::string& res) {
    unsigned result = 0;
    size_t pos = 0;
    while (pos <= res.size()) {
        size_t e = res.find('\n', pos);
        if (e == std::string::npos) e = res.size();
        const std::string line = res.substr(pos, e - pos);
        pos = e + 1;
        const std::string key = "Xft.dpi:";
        if (line.compare(0, key.size(), key) != 0) continue;
        size_t i = key.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        double v = 0, frac = 0.1;
        bool digits = false, dot = false;
        for (; i < line.size(); i++) {
            const char c = line[i];
            if (c >= '0' && c <= '9') {
                digits = true;
                if (!dot) v = v * 10 + (c - '0'); else { v += (c - '0') * frac; frac /= 10; }
                if (v > 100000) break;
            } else if (c == '.' && !dot) dot = true;
            else break;
        }
        if (!digits) { result = 0; continue; }
        const unsigned dpi = (unsigned)(v + 0.5);
        result = (dpi >= 24 && dpi <= 960) ? dpi : 0;
    }
    return result;
}

/// Refresh rate of a RandR mode from its timing: dotClock / (hTotal * vTotal). `interlace` doubles it (fields),
/// `doubleScan` halves it. 0 when the timing is empty.
inline int RefreshHzFromTiming(unsigned long dotClock, unsigned hTotal, unsigned vTotal, bool interlace = false, bool doubleScan = false) {
    if (!dotClock || !hTotal || !vTotal) return 0;
    double hz = (double)dotClock / ((double)hTotal * (double)vTotal);
    if (interlace) hz *= 2.0;
    if (doubleScan) hz /= 2.0;
    return (int)(hz + 0.5);
}

struct Monitor {
    int x = 0, y = 0, width = 0, height = 0;
    int mmWidth = 0, mmHeight = 0;
    bool primary = false;
    int refreshHz = 0;
    std::string name;
};

/// Physical pixel density from a size in millimetres; 0 when either number is missing.
inline unsigned PhysicalDpi(int pixels, int mm) { return pixels > 0 && mm > 0 ? (unsigned)(pixels * 25.4 / mm + 0.5) : 0; }

/// RandR monitors -> DisplayInfo, primary first, then left to right, top to bottom. The scale policy: when the
/// desktop sets Xft.dpi (the user's own choice) every monitor uses it; otherwise 96. The physical density is not
/// used because the millimetre sizes a monitor reports are often wrong. `workArea` (x, y, w, h from
/// _NET_WORKAREA, or all zero) is cut down to each monitor; empty areas give the full monitor.
inline std::vector<DisplayInfo> BuildDisplays(std::vector<Monitor> mons, unsigned xftDpi, const int workArea[4] = nullptr) {
    std::stable_sort(mons.begin(), mons.end(), [](const Monitor& a, const Monitor& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    std::vector<DisplayInfo> out;
    bool seenPrimary = false;
    for (const Monitor& m : mons) {
        if (m.width <= 0 || m.height <= 0) continue;
        DisplayInfo d;
        d.x = m.x; d.y = m.y; d.width = m.width; d.height = m.height;
        d.workX = m.x; d.workY = m.y; d.workWidth = m.width; d.workHeight = m.height;
        if (workArea && workArea[2] > 0 && workArea[3] > 0) {
            const int x0 = std::max(m.x, workArea[0]), y0 = std::max(m.y, workArea[1]);
            const int x1 = std::min(m.x + m.width, workArea[0] + workArea[2]), y1 = std::min(m.y + m.height, workArea[1] + workArea[3]);
            if (x1 > x0 && y1 > y0) { d.workX = x0; d.workY = y0; d.workWidth = x1 - x0; d.workHeight = y1 - y0; }
        }
        d.dpi = xftDpi ? xftDpi : 96;
        d.refreshHz = m.refreshHz;
        d.primary = m.primary && !seenPrimary;
        if (d.primary) seenPrimary = true;
        d.name = m.name;
        out.push_back(d);
    }
    if (!out.empty() && !seenPrimary) out[0].primary = true;   // no primary set: the first one
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// Cursors (the core cursor font: no Xcursor library)
// ---------------------------------------------------------------------------------------------------------

/// Glyph numbers of the core cursor font (cursorfont.h); the X11 file static_asserts them against the macros.
inline unsigned CursorGlyph(CursorShape s) {
    switch (s) {
    case CursorShape::Arrow: return 68;        // XC_left_ptr
    case CursorShape::IBeam: return 152;       // XC_xterm
    case CursorShape::Wait: return 150;        // XC_watch
    case CursorShape::Crosshair: return 34;    // XC_crosshair
    case CursorShape::Hand: return 60;         // XC_hand2
    case CursorShape::SizeNS: return 116;      // XC_sb_v_double_arrow
    case CursorShape::SizeWE: return 108;      // XC_sb_h_double_arrow
    case CursorShape::SizeNWSE: return 14;     // XC_bottom_right_corner
    case CursorShape::SizeNESW: return 12;     // XC_bottom_left_corner
    case CursorShape::SizeAll: return 52;      // XC_fleur
    case CursorShape::NotAllowed: return 0;    // XC_X_cursor
    }
    return 68;
}

// ---------------------------------------------------------------------------------------------------------
// Window attention
// ---------------------------------------------------------------------------------------------------------
constexpr long kUrgencyHint = 1L << 8;   // WM_HINTS flags bit (ICCCM)
/// WM_HINTS flags with the urgency bit set or cleared.
inline long WithUrgency(long flags, bool on) { return on ? (flags | kUrgencyHint) : (flags & ~kUrgencyHint); }

/// The EWMH client message data (sent to the root window) that adds / removes up to two _NET_WM_STATE atoms.
/// l0 = 1 add, 0 remove; l1, l2 = the atoms; l3 = 1 (the request comes from a normal application).
inline void NetWmStateMessage(bool add, unsigned long atom1, unsigned long atom2, long out[5]) {
    out[0] = add ? 1 : 0; out[1] = (long)atom1; out[2] = (long)atom2; out[3] = 1; out[4] = 0;
}

// ---------------------------------------------------------------------------------------------------------
// Input method preedit
// ---------------------------------------------------------------------------------------------------------

/// The text an input method is composing, edited by the "preedit draw" callback the way the XIM protocol
/// describes: replace `len` characters from `first` by new text, and set the caret.
class Preedit {
public:
    /// first, len and caret count characters. Out-of-range values are clamped. `utf8` is the replacement text.
    void Draw(int first, int len, const std::string& utf8, int caret) {
        const std::u32string ins = Decode(utf8);
        if (first < 0) first = 0;
        if (first > (int)m_text.size()) first = (int)m_text.size();
        if (len < 0) len = 0;
        if (first + len > (int)m_text.size()) len = (int)m_text.size() - first;
        m_text.replace((size_t)first, (size_t)len, ins);
        if (m_text.size() > kMaxChars) m_text.resize(kMaxChars);
        m_caret = caret < 0 ? 0 : (caret > (int)m_text.size() ? (int)m_text.size() : caret);
    }
    void Clear() { m_text.clear(); m_caret = 0; }
    bool Active() const { return !m_text.empty(); }
    int Caret() const { return m_caret; }
    int Length() const { return (int)m_text.size(); }
    std::string Utf8() const { return Encode(m_text); }
    /// The TextEditing event for the current state (an empty text means "composition ended").
    Event MakeEvent(uint32_t windowId, uint32_t timestampMs = 0) const {
        Event e;
        e.type = EventType::TextEditing; e.windowId = windowId; e.timestampMs = timestampMs;
        e.text = Utf8(); e.editStart = m_caret; e.editLength = 0;
        return e;
    }

    static constexpr size_t kMaxChars = 1024;
    static std::u32string Decode(const std::string& s) {
        std::u32string o;
        size_t i = 0;
        while (i < s.size()) {
            const unsigned char c = (unsigned char)s[i];
            int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
            if (n < 0 || (n > 0 && i + (size_t)n >= s.size())) { o += U'\xFFFD'; i++; continue; }
            char32_t cp = n == 0 ? c : (c & (0x3F >> n));
            bool bad = false;
            for (int k = 1; k <= n; k++) {
                const unsigned char d = (unsigned char)s[i + (size_t)k];
                if ((d & 0xC0) != 0x80) { bad = true; break; }
                cp = (cp << 6) | (d & 0x3F);
            }
            static const char32_t kMin[4] = {0, 0x80, 0x800, 0x10000};
            if (bad || cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { o += U'\xFFFD'; i++; continue; }
            o += cp; i += (size_t)n + 1;
        }
        return o;
    }
    static std::string Encode(const std::u32string& t) {
        std::string s;
        for (char32_t cp : t) {
            if (cp < 0x80) s += (char)cp;
            else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
            else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        }
        return s;
    }
private:
    std::u32string m_text;
    int m_caret = 0;
};

// ---------------------------------------------------------------------------------------------------------
// The plain message box (used when no dialog program is wanted)
// ---------------------------------------------------------------------------------------------------------

/// Break `utf8` into lines of at most `maxCols` characters: at spaces when possible, hard breaks inside a word
/// longer than a line, and every '\n' ends a line. Tabs count as one space. At most `maxLines` lines are
/// returned (the last one ends in "..." when text was cut). Control characters other than '\n' are dropped.
inline std::vector<std::string> WrapText(const std::string& utf8, int maxCols, int maxLines = 40) {
    std::vector<std::string> lines;
    if (maxCols < 4) maxCols = 4;
    std::u32string cur;
    auto flush = [&]() { lines.push_back(Preedit::Encode(cur)); cur.clear(); };
    const std::u32string all = Preedit::Decode(utf8);
    std::u32string word;
    auto putWord = [&]() {
        while (!word.empty()) {
            const size_t need = word.size() + (cur.empty() ? 0 : 1);
            if (cur.size() + need <= (size_t)maxCols) {
                if (!cur.empty()) cur += U' ';
                cur += word; word.clear();
            } else if (!cur.empty()) {
                flush();
            } else {   // a word longer than the line: cut it
                cur = word.substr(0, (size_t)maxCols); word.erase(0, (size_t)maxCols); flush();
            }
        }
    };
    for (char32_t c : all) {
        if (c == U'\n') { putWord(); flush(); }
        else if (c == U' ' || c == U'\t') putWord();
        else if (c < 0x20 || c == 0x7f) continue;
        else word += c;
    }
    putWord();
    if (!cur.empty() || lines.empty()) flush();
    if ((int)lines.size() > maxLines) {
        lines.resize((size_t)maxLines);
        lines.back() += "...";
    }
    return lines;
}

struct DialogLayout {
    int width = 0, height = 0;
    int textX = 0, textY = 0;           // baseline of the first line
    int lineHeight = 0;
    int btnX = 0, btnY = 0, btnW = 0, btnH = 0;
    int winX = 0, winY = 0;             // top-left, centred on the screen (or the parent)
    bool InButton(int x, int y) const { return x >= btnX && x < btnX + btnW && y >= btnY && y < btnY + btnH; }
};

/// Sizes for `lineCount` lines of text with a font cell `charW` x `lineH` (ascent `ascent`), the longest line
/// `maxLineChars` characters wide, centred on the rectangle (ax, ay, aw, ah).
inline DialogLayout LayoutDialog(int lineCount, int maxLineChars, int charW, int lineH, int ascent, int ax, int ay, int aw, int ah) {
    DialogLayout L;
    const int pad = 16, btnW = 8 * charW + 2 * pad, btnH = lineH + 12, gap = 14;
    int textW = maxLineChars * charW;
    if (textW < btnW) textW = btnW;
    L.width = textW + 2 * pad;
    L.height = pad + lineCount * lineH + gap + btnH + pad;
    L.lineHeight = lineH;
    L.textX = pad; L.textY = pad + ascent;
    L.btnW = btnW; L.btnH = btnH;
    L.btnX = (L.width - btnW) / 2; L.btnY = pad + lineCount * lineH + gap;
    L.winX = ax + (aw - L.width) / 2; L.winY = ay + (ah - L.height) / 2;
    if (L.winX < ax) L.winX = ax;
    if (L.winY < ay) L.winY = ay;
    return L;
}

/// What a key press does in the dialog: Return, KP_Enter, Escape, space close it.
inline bool DialogKeyCloses(unsigned long keysym) {
    return keysym == 0xff0d /*Return*/ || keysym == 0xff8d /*KP_Enter*/ || keysym == 0xff1b /*Escape*/ || keysym == 0x20 /*space*/;
}

/// UTF-8 -> Latin-1 for the core font drawing (characters above U+00FF become '?').
inline std::string ToLatin1(const std::string& utf8) {
    std::string o;
    for (char32_t c : Preedit::Decode(utf8)) o += c <= 0xFF ? (char)c : '?';
    return o;
}

} // namespace okay::platform::x11
