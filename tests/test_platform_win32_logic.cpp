// The plain-logic parts of the own Windows backend (engine/include/okay/Platform/Win32Logic.hpp and
// Win32AudioLogic.hpp), checked with no Windows and no display: key code translation, UTF-8 / UTF-16
// and clipboard text, WM_CHAR assembly, tick and DPI maths, click counting, the event queue, XInput
// mapping, hot-plug and rumble timing, and the audio byte queue, buffer plan and format checks.
//
// This test is also built with MinGW and run by the Windows CI job on a real Windows machine (the
// logic is plain C++, so it must give the same answers there). Expected numbers below are written by
// hand from the Windows documentation, not produced by the code under test.
#include "test_framework.hpp"
#include "okay/Platform/Win32Logic.hpp"
#include "okay/Platform/Win32AudioLogic.hpp"

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <set>
#include <string>
#include <vector>

using namespace okay::platform;
using namespace okay::platform::wl;

namespace {

std::string Bytes(std::initializer_list<int> v) { std::string s; for (int b : v) s.push_back((char)b); return s; }

void TestKeys() {
    RUN_SUITE("scan code to key");
    // Set-1 make codes of a US-style keyboard, written from the layout, not from the table.
    const struct { int scan; Key k; } plain[] = {
        {0x01, Key::Escape}, {0x02, Key::N1}, {0x0A, Key::N9}, {0x0B, Key::N0}, {0x0C, Key::Minus}, {0x0D, Key::Equals},
        {0x0E, Key::Backspace}, {0x0F, Key::Tab},
        {0x10, Key::Q}, {0x11, Key::W}, {0x12, Key::E}, {0x13, Key::R}, {0x14, Key::T}, {0x15, Key::Y}, {0x16, Key::U},
        {0x17, Key::I}, {0x18, Key::O}, {0x19, Key::P}, {0x1A, Key::LeftBracket}, {0x1B, Key::RightBracket}, {0x1C, Key::Return},
        {0x1D, Key::LCtrl}, {0x1E, Key::A}, {0x1F, Key::S}, {0x20, Key::D}, {0x21, Key::F}, {0x22, Key::G}, {0x23, Key::H},
        {0x24, Key::J}, {0x25, Key::K}, {0x26, Key::L}, {0x27, Key::Semicolon}, {0x28, Key::Apostrophe}, {0x29, Key::Grave},
        {0x2A, Key::LShift}, {0x2B, Key::Backslash}, {0x2C, Key::Z}, {0x2D, Key::X}, {0x2E, Key::C}, {0x2F, Key::V},
        {0x30, Key::B}, {0x31, Key::N}, {0x32, Key::M}, {0x33, Key::Comma}, {0x34, Key::Period}, {0x35, Key::Slash},
        {0x36, Key::RShift}, {0x37, Key::KpMultiply}, {0x38, Key::LAlt}, {0x39, Key::Space}, {0x3A, Key::CapsLock},
        {0x3B, Key::F1}, {0x3C, Key::F2}, {0x44, Key::F10}, {0x45, Key::Pause}, {0x46, Key::ScrollLock},
        {0x47, Key::Kp7}, {0x48, Key::Kp8}, {0x49, Key::Kp9}, {0x4A, Key::KpMinus}, {0x4B, Key::Kp4}, {0x4C, Key::Kp5},
        {0x4D, Key::Kp6}, {0x4E, Key::KpPlus}, {0x4F, Key::Kp1}, {0x50, Key::Kp2}, {0x51, Key::Kp3}, {0x52, Key::Kp0},
        {0x53, Key::KpPeriod}, {0x57, Key::F11}, {0x58, Key::F12},
    };
    for (const auto& p : plain) CHECK(KeyFromScan(p.scan, false) == p.k);
    const struct { int scan; Key k; } ext[] = {
        {0x1C, Key::KpEnter}, {0x1D, Key::RCtrl}, {0x35, Key::KpDivide}, {0x37, Key::PrintScreen}, {0x38, Key::RAlt},
        {0x45, Key::NumLock}, {0x47, Key::Home}, {0x48, Key::Up}, {0x49, Key::PageUp}, {0x4B, Key::Left}, {0x4D, Key::Right},
        {0x4F, Key::End}, {0x50, Key::Down}, {0x51, Key::PageDown}, {0x52, Key::Insert}, {0x53, Key::Delete},
        {0x5B, Key::LGui}, {0x5C, Key::RGui},
    };
    for (const auto& p : ext) CHECK(KeyFromScan(p.scan, true) == p.k);
    // The fake Shifts Windows sends around the extended cursor block, and nonsense, are unknown.
    CHECK(KeyFromScan(0x2A, true) == Key::Unknown);
    CHECK(KeyFromScan(0x36, true) == Key::Unknown);
    CHECK(KeyFromScan(0x00, false) == Key::Unknown);
    CHECK(KeyFromScan(0x59, false) == Key::Unknown);
    CHECK(KeyFromScan(-1, false) == Key::Unknown);
    CHECK(KeyFromScan(0x1234, true) == Key::Unknown);

    // Every letter, digit and F key of the seam has a scan code and comes back as itself.
    for (int k = (int)Key::A; k <= (int)Key::Z; ++k) {
        bool e = true;
        const int s = ScanFromKey((Key)k, &e);
        CHECK(s > 0 && !e && KeyFromScan(s, e) == (Key)k);
    }
    for (int k = (int)Key::N1; k <= (int)Key::N0; ++k) { bool e = true; const int s = ScanFromKey((Key)k, &e); CHECK(s >= 2 && s <= 0x0B && !e); }
    for (int k = (int)Key::F1; k <= (int)Key::F12; ++k) CHECK(ScanFromKey((Key)k) > 0);
    bool e = false;
    CHECK(ScanFromKey(Key::Up, &e) == 0x48 && e);
    CHECK(ScanFromKey(Key::Kp8, &e) == 0x48 && !e);
    CHECK(ScanFromKey(Key::RCtrl, &e) == 0x1D && e);
    CHECK(ScanFromKey(Key::LCtrl, &e) == 0x1D && !e);
    CHECK(ScanFromKey(Key::Pause, &e) == 0x45 && !e);
    CHECK(ScanFromKey(Key::Unknown) == -1);
    CHECK(ScanFromKey((Key)300) == -1);

    // No two scan codes give the same key (apart from none).
    std::set<int> seen;
    int dup = 0;
    for (int ext2 = 0; ext2 < 2; ++ext2)
        for (int s = 1; s < 0x59; ++s) {
            const Key k = KeyFromScan(s, ext2 != 0);
            if (k != Key::Unknown && !seen.insert((int)k).second) ++dup;
        }
    CHECK(dup == 0);

    // The MapVirtualKey form: 0xE0 prefix = extended.
    CHECK(KeyFromScanEx(0x001E) == Key::A);
    CHECK(KeyFromScanEx(0xE048) == Key::Up);
    CHECK(KeyFromScanEx(0xE11D) == Key::Pause);
    CHECK(KeyFromScanEx(0x0000) == Key::Unknown);

    // lParam of WM_KEYDOWN, built from the documented bit layout.
    const std::uintptr_t lp = (std::uintptr_t)3 | ((std::uintptr_t)0x48 << 16) | (std::uintptr_t(1) << 24) | (std::uintptr_t(1) << 30);
    const KeyLParam d = DecodeKeyLParam(lp);
    CHECK(d.repeatCount == 3 && d.scan == 0x48 && d.extended && d.wasDown && !d.released);
    const KeyLParam u = DecodeKeyLParam((std::uintptr_t)1 | ((std::uintptr_t)0x1E << 16) | (std::uintptr_t(1) << 30) | (std::uintptr_t(1) << 31));
    CHECK(u.scan == 0x1E && !u.extended && u.wasDown && u.released);
    CHECK(DecodeKeyLParam(0).repeatCount == 0);

    // System keys: only Alt+F4 and Alt+Space reach DefWindowProc.
    CHECK(PassSysKeyToDefault(0x73) && PassSysKeyToDefault(0x20));
    CHECK(!PassSysKeyToDefault(0x12 /*VK_MENU*/) && !PassSysKeyToDefault(0x79 /*VK_F10*/) && !PassSysKeyToDefault(0x0D /*VK_RETURN*/));

    // Key state and release on focus loss.
    KeyState ks;
    ks.Down(Key::W); ks.Down(Key::LShift); ks.Down((Key)511);
    CHECK(ks.IsDown(Key::W) && ks.IsDown(Key::LShift) && !ks.IsDown(Key::A));
    ks.Up(Key::W);
    CHECK(!ks.IsDown(Key::W));
    ks.Down((Key)600);   // out of range: ignored
    CHECK(!ks.IsDown((Key)600));
    const std::vector<Key> rel = ks.ReleaseAll();
    CHECK(rel.size() == 2 && rel[0] == Key::LShift && rel[1] == (Key)511);
    CHECK(!ks.IsDown(Key::LShift) && ks.ReleaseAll().empty());
}

void TestText() {
    RUN_SUITE("UTF-8 / UTF-16, clipboard, WM_CHAR");
    // Hand-written code units.
    CHECK(Utf8ToUtf16("A") == std::u16string(u"A"));
    CHECK(Utf8ToUtf16(Bytes({0xC3, 0xA4})) == std::u16string(u"ä"));                    // a with diaeresis
    CHECK(Utf8ToUtf16(Bytes({0xE2, 0x82, 0xAC})) == std::u16string(u"€"));              // euro sign
    CHECK(Utf8ToUtf16(Bytes({0xF0, 0x9F, 0x98, 0x80})) == (std::u16string() + char16_t(0xD83D) + char16_t(0xDE00)));   // U+1F600
    CHECK(Utf8ToUtf16(Bytes({0xF4, 0x8F, 0xBF, 0xBF})) == (std::u16string() + char16_t(0xDBFF) + char16_t(0xDFFF)));   // U+10FFFF
    CHECK(Utf8ToUtf16("").empty());
    // Bad input: each bad sequence is one U+FFFD, the good text around it survives.
    CHECK(Utf8ToUtf16(Bytes({'a', 0x80, 'b'})) == std::u16string(u"a�b"));              // stray continuation
    CHECK(Utf8ToUtf16(Bytes({0xC0, 0xAF})) .front() == 0xFFFD);                              // overlong "/"
    CHECK(Utf8ToUtf16(Bytes({0xC1, 0xBF})).front() == 0xFFFD);
    CHECK(Utf8ToUtf16(Bytes({0xE0, 0x80, 0x80})).front() == 0xFFFD);                         // overlong
    CHECK(Utf8ToUtf16(Bytes({0xED, 0xA0, 0x80})).front() == 0xFFFD);                         // encoded surrogate
    CHECK(Utf8ToUtf16(Bytes({0xF4, 0x90, 0x80, 0x80})).front() == 0xFFFD);                   // past U+10FFFF
    CHECK(Utf8ToUtf16(Bytes({0xF8})) == std::u16string(u"�"));
    CHECK(Utf8ToUtf16(Bytes({0xE2, 0x82})) == std::u16string(u"�"));                    // truncated at the end
    CHECK(Utf8ToUtf16(Bytes({0xE2, 0x82, 'x'})) == std::u16string(u"�x"));              // truncated, then text
    // UTF-16 to UTF-8.
    CHECK(Utf16ToUtf8(std::u16string(u"hällo €")) == "h\xC3\xA4llo \xE2\x82\xAC");
    CHECK(Utf16ToUtf8((std::u16string() + char16_t(0xD83D) + char16_t(0xDE00))) == "\xF0\x9F\x98\x80");
    CHECK(Utf16ToUtf8((std::u16string() + char16_t(0xD83D) + char16_t('x'))) == "\xEF\xBF\xBD" "x");   // lone high surrogate
    CHECK(Utf16ToUtf8((std::u16string() + char16_t(0xDE00))) == "\xEF\xBF\xBD");                    // lone low surrogate
    CHECK(Utf16ToUtf8(nullptr, 5).empty());
    // Round trip over every scalar value in steps, plus the edges.
    int bad = 0;
    for (std::uint32_t cp = 0x20; cp <= 0x10FFFF; cp += (cp < 0x1000 ? 1 : 97)) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        std::string u8; AppendUtf8(u8, cp);
        if (Utf16ToUtf8(Utf8ToUtf16(u8)) != u8) ++bad;
    }
    CHECK(bad == 0);
    // Hostile bytes never crash and always give well-formed UTF-16 that re-encodes to valid UTF-8.
    std::uint32_t seed = 12345;
    int notStable = 0;
    for (int i = 0; i < 3000; ++i) {
        std::string s;
        const int n = (int)(seed = seed * 1664525u + 1013904223u) % 12;
        for (int j = 0; j < n; ++j) s.push_back((char)((seed = seed * 1664525u + 1013904223u) >> 24));
        const std::u16string w = Utf8ToUtf16(s);
        const std::string back = Utf16ToUtf8(w);
        if (Utf8ToUtf16(back) != w) ++notStable;   // re-encoding must be a fixed point
    }
    CHECK(notStable == 0);

    // Bounded length of a UTF-16 string without its terminator.
    const char16_t full[4] = {u'a', u'b', u'c', u'd'};
    CHECK(BoundedLength16(full, 4) == 4 && BoundedLength16(full, 2) == 2 && BoundedLength16(nullptr, 9) == 0);
    const char16_t zt[4] = {u'a', 0, u'c', 0};
    CHECK(BoundedLength16(zt, 4) == 1);

    // Clipboard line ends.
    CHECK(ToClipboardNewlines("a\nb") == "a\r\nb");
    CHECK(ToClipboardNewlines("a\r\nb") == "a\r\nb");
    CHECK(ToClipboardNewlines("\n\n") == "\r\n\r\n");
    CHECK(ToClipboardNewlines("") == "");
    CHECK(ToClipboardNewlines("no newline") == "no newline");
    CHECK(FromClipboardNewlines("a\r\nb\r\n") == "a\nb\n");
    CHECK(FromClipboardNewlines("a\rb") == "a\rb");
    CHECK(FromClipboardNewlines("\r\n\r\n") == "\n\n");
    CHECK(FromClipboardNewlines("a\nb") == "a\nb");
    CHECK(FromClipboardNewlines(ToClipboardNewlines("x\ny\n\nz")) == "x\ny\n\nz");

    // WM_CHAR assembly.
    CharAssembler ca;
    CHECK(ca.Push('a') == "a");
    CHECK(ca.Push(0x00E4) == "\xC3\xA4");
    CHECK(ca.Push(0x20AC) == "\xE2\x82\xAC");
    CHECK(ca.Push(' ') == " ");
    CHECK(ca.Push('\b').empty() && ca.Push('\t').empty() && ca.Push('\r').empty() && ca.Push(0x1B).empty());   // controls are not text
    CHECK(ca.Push(0x01).empty() && ca.Push(0x7F).empty() && ca.Push(0x85).empty());      // Ctrl+A, DEL, a C1 control
    CHECK(ca.Push(0xD83D).empty());                                                      // high half: wait
    CHECK(ca.Push(0xDE00) == "\xF0\x9F\x98\x80");                                        // pair completes
    CHECK(ca.Push(0xD83D).empty());
    CHECK(ca.Push('x') == "\xEF\xBF\xBD" "x");                                           // high half then a plain letter
    CHECK(ca.Push(0xDE00) == "\xEF\xBF\xBD");                                            // lone low half
    CHECK(ca.Push(0xD83D).empty());
    ca.Reset();
    CHECK(ca.Push(0xDE00) == "\xEF\xBF\xBD");                                            // Reset forgot the high half
}

void TestTimeDpiClicks() {
    RUN_SUITE("ticks, DPI, centring, clicks");
    CHECK(TicksToMs(0, 10000000) == 0);
    CHECK(TicksToMs(10000000, 10000000) == 1000);
    CHECK(TicksToMs(15000, 10000000) == 1);                // 1.5 ms floors to 1
    CHECK(TicksToMs(25000, 10000000) == 2);
    CHECK(TicksToMs(5, 0) == 0);                           // no frequency: no division
    // A counter that would overflow ticks * 1000: 1 GHz, three years of uptime.
    const std::uint64_t secs = 3ull * 365 * 86400;
    CHECK(TicksToMs(secs * 1000000000ull, 1000000000ull) == secs * 1000ull);
    CHECK(TicksToMs(secs * 1000000000ull + 500000, 1000000000ull) == secs * 1000ull);   // 0.5 ms floors to 0
    // Monotonic: more ticks never give fewer milliseconds.
    std::uint64_t prev = 0; bool mono = true;
    for (std::uint64_t t = 0; t < 5000000; t += 12345) { const auto m = TicksToMs(t, 3000000); if (m < prev) mono = false; prev = m; }
    CHECK(mono);

    CHECK_NEAR(ScaleFromDpi(96), 1.0, 1e-6);
    CHECK_NEAR(ScaleFromDpi(120), 1.25, 1e-6);
    CHECK_NEAR(ScaleFromDpi(144), 1.5, 1e-6);
    CHECK_NEAR(ScaleFromDpi(192), 2.0, 1e-6);
    CHECK_NEAR(ScaleFromDpi(0), 1.0, 1e-6);
    CHECK_NEAR(ScaleFromDpi(1), 0.25, 1e-6);
    CHECK_NEAR(ScaleFromDpi(100000), 8.0, 1e-6);
    CHECK(ScaleToPixels(100, 96) == 100 && ScaleToPixels(100, 144) == 150 && ScaleToPixels(100, 192) == 200);
    CHECK(ScaleToPixels(1, 120) == 1);                     // 1.25 rounds to 1
    CHECK(ScaleToPixels(2, 120) == 3);                     // 2.5 rounds half away from zero
    CHECK(ScaleToPixels(-2, 120) == -3);
    CHECK(ScaleToPixels(7, 0) == 7);
    CHECK(ScaleFromPixels(150, 144) == 100 && ScaleFromPixels(200, 192) == 100 && ScaleFromPixels(-150, 144) == -100);
    for (int v = 1; v <= 400; ++v) {                         // to pixels and back comes home for common scales
        for (unsigned dpi : {96u, 120u, 144u, 168u, 192u, 288u}) if (ScaleFromPixels(ScaleToPixels(v, dpi), dpi) != v) { CHECK(false); }
    }
    int w = 0, h = 0;
    ResizeForDpiChange(800, 600, 96, 192, w, h);
    CHECK(w == 1600 && h == 1200);
    ResizeForDpiChange(1600, 1200, 192, 96, w, h);
    CHECK(w == 800 && h == 600);
    ResizeForDpiChange(1, 1, 192, 96, w, h);
    CHECK(w == 1 && h == 1);                                // never below one pixel
    ResizeForDpiChange(800, 600, 0, 96, w, h);
    CHECK(w == 800 && h == 600);                            // unknown old DPI: unchanged
    int x = 0, y = 0;
    CenteredOrigin(0, 0, 1920, 1080, 800, 600, x, y);
    CHECK(x == 560 && y == 240);
    CenteredOrigin(1920, 0, 1280, 1024, 640, 480, x, y);    // a second monitor to the right
    CHECK(x == 2240 && y == 272);
    CenteredOrigin(0, 0, 800, 600, 2000, 2000, x, y);       // bigger than the screen: top-left stays on screen
    CHECK(x == 0 && y == 0);

    ClickTracker c;                                         // 500 ms, 4 x 4 pixel rectangle
    CHECK(c.Press(1, 100, 100, 1000) == 1);
    CHECK(c.Press(1, 101, 99, 1300) == 2);                 // inside 2 px of the first, 300 ms later
    CHECK(c.Press(1, 100, 100, 1500) == 3);                // triple
    CHECK(c.Press(1, 100, 100, 2100) == 1);                // 600 ms later: new series
    CHECK(c.Press(2, 100, 100, 2200) == 1);                // other button
    CHECK(c.Press(2, 100, 100, 2300) == 2);
    CHECK(c.Press(2, 103, 100, 2400) == 1);                // 3 px away: outside half of 4
    CHECK(c.Press(2, 103, 100, 2410) == 2);
    c.Reset();
    CHECK(c.Press(2, 103, 100, 2420) == 1);
    ClickTracker wrap;                                      // the 32-bit millisecond clock wraps
    CHECK(wrap.Press(1, 5, 5, 0xFFFFFFF0u) == 1);
    CHECK(wrap.Press(1, 5, 5, 0x00000010u) == 2);          // 32 ms later across the wrap
    ClickTracker slow(1000, 10, 10);
    CHECK(slow.Press(1, 0, 0, 0) == 1 && slow.Press(1, 5, 5, 900) == 2 && slow.Press(1, 6, 0, 1900) == 3 && slow.Press(1, 6, 0, 3000) == 1);
    ClickTracker many;
    int last = 0;
    for (int i = 0; i < 400; ++i) last = many.Press(1, 0, 0, (std::uint32_t)(i * 10));
    CHECK(last == 255);                                     // saturates instead of wrapping to 0
}

Event Ev(EventType t, std::uint32_t win = 1) { Event e; e.type = t; e.windowId = win; return e; }

void TestEventQueue() {
    RUN_SUITE("event queue");
    EventQueue q(8);
    Event out;
    CHECK(!q.Pop(out) && q.Empty());
    Event m1 = Ev(EventType::MouseMove); m1.x = 10; m1.y = 10; m1.dx = 3; m1.dy = -1; m1.timestampMs = 5;
    Event m2 = m1; m2.x = 14; m2.y = 12; m2.dx = 4; m2.dy = 2; m2.timestampMs = 9;
    q.Push(m1); q.Push(m2);
    CHECK(q.Size() == 1);
    CHECK(q.Pop(out) && out.type == EventType::MouseMove && out.x == 14 && out.y == 12 && out.dx == 7 && out.dy == 1 && out.timestampMs == 9);
    // Moves of different windows, or with another event between, are not merged.
    Event m3 = m1; m3.windowId = 2;
    q.Push(m1); q.Push(m3); q.Push(Ev(EventType::KeyDown)); q.Push(m1);
    CHECK(q.Size() == 4);
    while (q.Pop(out)) {}
    // Resize keeps the newest size.
    Event r1 = Ev(EventType::WindowResized); r1.width = 640; r1.height = 480;
    Event r2 = r1; r2.width = 800; r2.height = 600;
    q.Push(r1); q.Push(r2); q.Push(Ev(EventType::WindowFocusLost)); q.Push(r1);
    CHECK(q.Size() == 3);
    CHECK(q.Pop(out) && out.width == 800 && out.height == 600);
    CHECK(q.Pop(out) && out.type == EventType::WindowFocusLost);
    CHECK(q.Pop(out) && out.width == 640);
    // Order is kept.
    for (int i = 0; i < 5; ++i) { Event e = Ev(EventType::KeyDown); e.deviceId = i; q.Push(e); }
    for (int i = 0; i < 5; ++i) CHECK(q.Pop(out) && out.deviceId == i);

    // Full queue: mouse moves go first, then ordinary events; key up, button up and quit stay.
    EventQueue f(4);
    f.Push(Ev(EventType::KeyUp)); f.Push(Ev(EventType::MouseMove)); f.Push(Ev(EventType::KeyDown)); f.Push(Ev(EventType::Quit));
    CHECK(f.Size() == 4);
    f.Push(Ev(EventType::MouseButtonDown));                  // evicts the oldest mouse move
    CHECK(f.Size() == 4 && f.Dropped() == 1);
    CHECK(f.Pop(out) && out.type == EventType::KeyUp);
    CHECK(f.Pop(out) && out.type == EventType::KeyDown);     // the move is gone
    CHECK(f.Pop(out) && out.type == EventType::Quit);
    CHECK(f.Pop(out) && out.type == EventType::MouseButtonDown);
    EventQueue g(4);
    for (int i = 0; i < 4; ++i) g.Push(Ev(i % 2 ? EventType::KeyUp : EventType::MouseButtonUp));
    g.Push(Ev(EventType::KeyDown));                          // nothing can be evicted: the new event is refused
    CHECK(g.Size() == 4 && g.Dropped() == 1);
    g.Push(Ev(EventType::KeyUp));                            // so is a new precious one, rather than losing an old one
    CHECK(g.Size() == 4 && g.Dropped() == 2);
    EventQueue h(5);                                         // a flood of unreadable events stays bounded
    for (int i = 0; i < 100000; ++i) { Event e = Ev(EventType::KeyDown); e.deviceId = i; h.Push(e); }
    CHECK(h.Size() == 5);
    h.Clear();
    CHECK(h.Empty());
    EventQueue tiny(0);                                      // a silly capacity is raised
    for (int i = 0; i < 10; ++i) tiny.Push(Ev(EventType::KeyDown));
    CHECK(tiny.Size() == 4);
}

void TestPads() {
    RUN_SUITE("XInput mapping, hot-plug, rumble");
    CHECK(xinput::kA == 0x1000 && xinput::kY == 0x8000 && xinput::kDpadUp == 1 && xinput::kStart == 0x10 && xinput::kBack == 0x20);
    const std::uint16_t all = 0xFFFF;
    for (int b = 0; b <= (int)GamepadButton::DpadRight; ++b) {
        const bool g = (GamepadButton)b == GamepadButton::Guide;
        CHECK(PadButtonDown(all, (GamepadButton)b) == !g);
        CHECK(!PadButtonDown(0, (GamepadButton)b));
    }
    CHECK(PadButtonDown(0x1000, GamepadButton::A) && !PadButtonDown(0x1000, GamepadButton::B));
    CHECK(PadButtonDown(0x2000, GamepadButton::B) && PadButtonDown(0x4000, GamepadButton::X) && PadButtonDown(0x8000, GamepadButton::Y));
    CHECK(PadButtonDown(0x0100, GamepadButton::LeftShoulder) && PadButtonDown(0x0200, GamepadButton::RightShoulder));
    CHECK(PadButtonDown(0x0040, GamepadButton::LeftStick) && PadButtonDown(0x0080, GamepadButton::RightStick));
    CHECK(PadButtonDown(0x0001, GamepadButton::DpadUp) && PadButtonDown(0x0002, GamepadButton::DpadDown) &&
          PadButtonDown(0x0004, GamepadButton::DpadLeft) && PadButtonDown(0x0008, GamepadButton::DpadRight));
    // Several buttons at once do not disturb each other.
    CHECK(PadButtonDown(0x1010, GamepadButton::A) && PadButtonDown(0x1010, GamepadButton::Start) && !PadButtonDown(0x1010, GamepadButton::Back));

    CHECK(PadStickY(0) == -1 && PadStickY(-1) == 0);
    CHECK(PadStickY(32767) == -32768 && PadStickY(-32768) == 32767);
    for (int v = -32768; v <= 32767; v += 37) CHECK(PadStickY(PadStickY((std::int16_t)v)) == v);   // an involution: no value is lost
    CHECK(PadStickY(100) < 0 && PadStickY(-100) >= 0);       // up on XInput (+) is up on the seam (-)
    CHECK(PadTrigger(0) == 0 && PadTrigger(255) == 32767 && PadTrigger(128) == 16448 && PadTrigger(1) == 128);
    int prev = -1; bool inc = true;
    for (int t = 0; t < 256; ++t) { const int v = PadTrigger((std::uint8_t)t); if (v <= prev) inc = false; prev = v; }
    CHECK(inc);

    PadSlotTracker pt;
    auto ch = pt.Update(0);
    CHECK(ch.empty() && pt.Count() == 0 && pt.SlotOfIndex(0) == -1);
    ch = pt.Update(0b0101);                                   // slots 0 and 2 appear
    CHECK(ch.size() == 2 && ch[0].added && ch[0].slot == 0 && ch[0].index == 0 && ch[0].instanceId == 1 &&
          ch[1].added && ch[1].slot == 2 && ch[1].index == 1 && ch[1].instanceId == 2);
    CHECK(pt.Count() == 2 && pt.SlotOfIndex(0) == 0 && pt.SlotOfIndex(1) == 2 && pt.SlotOfIndex(2) == -1);
    CHECK(pt.IndexOfSlot(2) == 1 && pt.IndexOfSlot(1) == -1 && pt.IndexOfSlot(9) == -1);
    CHECK(pt.InstanceOfSlot(0) == 1 && pt.InstanceOfSlot(2) == 2 && pt.InstanceOfSlot(7) == 0);
    CHECK(pt.Update(0b0101).empty());                         // nothing new
    ch = pt.Update(0b0100);                                   // slot 0 unplugged
    CHECK(ch.size() == 1 && !ch[0].added && ch[0].slot == 0 && ch[0].instanceId == 1);
    CHECK(pt.SlotOfIndex(0) == 2);                            // slot 2 is now the first pad
    ch = pt.Update(0b0101);                                   // slot 0 plugged in again: a new instance id
    CHECK(ch.size() == 1 && ch[0].added && ch[0].slot == 0 && ch[0].instanceId == 3 && ch[0].index == 0);
    ch = pt.Update(0xFFFFFFFFu);                              // bits past slot 3 are ignored
    CHECK(pt.Mask() == 0xF && ch.size() == 2);
    ch = pt.Update(0);
    CHECK(ch.size() == 4 && pt.Count() == 0);
    for (const auto& c : ch) CHECK(!c.added && c.instanceId > 0);

    IntervalGate gate(1000);
    CHECK(gate.Due(5000) && !gate.Due(5500) && !gate.Due(5999) && gate.Due(6000) && !gate.Due(6001) && gate.Due(7500));
    IntervalGate zero(0);
    CHECK(zero.Due(1) && zero.Due(1));

    RumbleSchedule rs;
    rs.Set(1, 1000, 0, 300, 10000);
    CHECK(rs.Active(1) && !rs.Active(0));
    CHECK(rs.Expired(10299) == 0 && rs.Active(1));
    CHECK(rs.Expired(10300) == 2u && !rs.Active(1));          // slot 1 is bit 1; reported once
    CHECK(rs.Expired(99999) == 0);
    rs.Set(0, 0, 5000, 100, 0);
    rs.Set(3, 1, 1, 100, 0);
    CHECK(rs.Expired(100) == 9u);                             // slots 0 and 3
    rs.Set(2, 1, 1, 100, 0);
    rs.Set(2, 0, 0, 100, 0);                                  // strength 0 stops it
    CHECK(!rs.Active(2) && rs.Expired(1000) == 0);
    rs.Set(2, 9, 9, 0, 0);                                    // zero length: nothing starts
    CHECK(!rs.Active(2));
    rs.Set(0, 9, 9, 500, 0); rs.Cancel(0);
    CHECK(!rs.Active(0) && rs.Expired(1000) == 0);
    rs.Set(-1, 1, 1, 10, 0); rs.Set(4, 1, 1, 10, 0);          // bad slots are ignored
    CHECK(rs.Expired(100) == 0);
}

void TestAudio() {
    RUN_SUITE("audio queue, plan, format");
    // Byte queue against a plain deque as the reference, on a long pseudo-random run of pushes and pops.
    ByteFifo f;
    std::deque<std::uint8_t> ref;
    std::uint32_t s = 99;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s >> 8; };
    int mism = 0;
    for (int step = 0; step < 4000; ++step) {
        if (rnd() % 3 != 0) {
            const std::size_t n = rnd() % 700;
            std::vector<std::uint8_t> d(n);
            for (auto& b : d) b = (std::uint8_t)rnd();
            f.Push(d.data(), n);
            ref.insert(ref.end(), d.begin(), d.end());
        } else {
            const std::size_t n = rnd() % 900;
            std::vector<std::uint8_t> o(n);
            const std::size_t got = f.Pop(o.data(), n);
            const std::size_t want = std::min(n, ref.size());
            if (got != want) ++mism;
            for (std::size_t i = 0; i < got; ++i) { if (o[i] != ref.front()) ++mism; ref.pop_front(); }
        }
        if (f.Size() != ref.size()) ++mism;
    }
    CHECK(mism == 0);
    f.Clear();
    CHECK(f.Size() == 0 && f.Pop(nullptr, 10) == 0);
    const std::uint8_t five[5] = {1, 2, 3, 4, 5};
    f.Push(five, 5);
    CHECK(f.Pop(nullptr, 2) == 2 && f.Size() == 3);           // null output = discard
    std::uint8_t o3[3] = {};
    CHECK(f.Pop(o3, 9) == 3 && o3[0] == 3 && o3[2] == 5);
    CHECK(f.Push(nullptr, 0) == 0 && f.Size() == 0);

    ByteFifo cap(10);                                         // capture: the newest bytes stay
    std::uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(cap.Push(a, 8) == 0 && cap.Size() == 8);
    CHECK(cap.Push(a, 8) == 6 && cap.Size() == 10);            // 6 old bytes dropped
    std::uint8_t o10[10] = {};
    CHECK(cap.Pop(o10, 10) == 10);
    const std::uint8_t want10[10] = {7, 8, 1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(std::memcmp(o10, want10, 10) == 0);
    std::uint8_t big[25]; for (int i = 0; i < 25; ++i) big[i] = (std::uint8_t)i;
    CHECK(cap.Push(a, 4) == 0);
    CHECK(cap.Push(big, 25) == 4 + 15 && cap.Size() == 10);    // alone it passes the cap: only its last 10 bytes remain
    CHECK(cap.Pop(o10, 10) == 10 && o10[0] == 15 && o10[9] == 24);

    // Wrap-around: a head in the middle of the buffer.
    ByteFifo ring;
    std::vector<std::uint8_t> blk(3000, 7);
    ring.Push(blk.data(), blk.size());
    ring.Pop(nullptr, 2900);
    std::vector<std::uint8_t> blk2(3000);
    for (std::size_t i = 0; i < blk2.size(); ++i) blk2[i] = (std::uint8_t)(i * 3);
    ring.Push(blk2.data(), blk2.size());                      // crosses the end of the 4096-byte ring
    std::vector<std::uint8_t> got(3100);
    CHECK(ring.Pop(got.data(), 3100) == 3100);
    bool same = true;
    for (int i = 0; i < 100; ++i) if (got[(size_t)i] != 7) same = false;
    for (std::size_t i = 0; i < 3000; ++i) if (got[100 + i] != blk2[i]) same = false;
    CHECK(same);

    // Playback fill: queued bytes, then silence.
    ByteFifo pq;
    std::uint8_t src[6] = {9, 9, 9, 9, 9, 9};
    pq.Push(src, 6);
    std::uint8_t buf[10]; std::memset(buf, 0x55, sizeof(buf));
    CHECK(FillFromFifo(pq, buf, 10) == 6);
    CHECK(buf[5] == 9 && buf[6] == 0 && buf[9] == 0);
    CHECK(FillFromFifo(pq, buf, 10) == 0 && buf[0] == 0);       // empty queue: all silence, never stale data

    AudioSpec sp; sp.freq = 48000; sp.channels = 2; sp.format = SampleFormat::S16; sp.frames = 1024;
    CHECK(FrameBytes(sp) == 4);
    sp.format = SampleFormat::F32;
    CHECK(FrameBytes(sp) == 8);
    sp.format = SampleFormat::S16;
    BufferPlan p = PlanBuffers(sp);
    CHECK(p.frames == 1024 && p.bytes == 4096 && p.count == 3 && p.latencyMs == 64);   // 60 ms / 21.3 ms = 2.8 -> 3
    sp.frames = 256;
    p = PlanBuffers(sp);
    CHECK(p.frames == 256 && p.count == 8 && p.bytes == 1024 && p.latencyMs == 42);    // 2880 / 256 = 11.25 -> capped at 8
    sp.frames = 0;
    CHECK(PlanBuffers(sp).frames == 1024);                     // no hint: 1024
    sp.frames = 1;
    CHECK(PlanBuffers(sp).frames == 64);                       // too small: raised
    sp.frames = 100000;
    CHECK(PlanBuffers(sp).frames == 16384);                    // too big: capped
    sp.frames = 1000;
    CHECK(PlanBuffers(sp).frames == 1008);                     // multiple of 16
    sp.frames = 1024; sp.freq = 8000; sp.channels = 1;
    p = PlanBuffers(sp);
    CHECK(p.count == 3 && p.bytes == 2048);                    // 8 kHz mono 16-bit
    sp.freq = 0;
    CHECK(PlanBuffers(sp).count >= 3);                         // a missing rate does not divide by zero
    for (int fr : {64, 100, 480, 1024, 4096}) for (int fq : {8000, 22050, 44100, 48000, 96000, 192000}) {
        AudioSpec w; w.frames = fr; w.freq = fq; w.channels = 2;
        const BufferPlan q = PlanBuffers(w);
        CHECK(q.count >= 3 && q.count <= 8 && q.frames % 16 == 0 && q.bytes == q.frames * 4);
    }

    AudioSpec st; st.freq = 44100; st.channels = 2; st.format = SampleFormat::S16;
    WaveFormatFields wf = WaveFormatFor(st);
    CHECK(wf.tag == 1 && wf.channels == 2 && wf.samplesPerSec == 44100 && wf.bitsPerSample == 16 && wf.blockAlign == 4 && wf.avgBytesPerSec == 176400);
    st.format = SampleFormat::F32; st.channels = 1; st.freq = 48000;
    wf = WaveFormatFor(st);
    CHECK(wf.tag == 3 && wf.channels == 1 && wf.bitsPerSample == 32 && wf.blockAlign == 4 && wf.avgBytesPerSec == 192000);

    AudioSpec want, got2;
    std::string why;
    want.freq = 48000; want.channels = 2;
    CHECK(NegotiateSpec(want, got2, why) && got2.freq == 48000 && got2.channels == 2 && got2.frames == 1024);
    want.channels = 6;
    CHECK(!NegotiateSpec(want, got2, why) && why.find("channel") != std::string::npos);
    want.allowChange = true;
    CHECK(NegotiateSpec(want, got2, why) && got2.channels == 2);
    want.channels = 0;
    CHECK(NegotiateSpec(want, got2, why) && got2.channels == 1);
    want.allowChange = false; want.channels = 2; want.freq = 4000;
    CHECK(!NegotiateSpec(want, got2, why) && why.find("sample rate") != std::string::npos);
    want.allowChange = true;
    CHECK(NegotiateSpec(want, got2, why) && got2.freq == 8000);
    want.freq = 500000;
    CHECK(NegotiateSpec(want, got2, why) && got2.freq == 192000);

    // Device names: the 31-character cut Windows keeps.
    const std::string longName = "Speakers (Realtek High Definition Audio)";   // 40 characters
    const std::string cut = longName.substr(0, 31);
    CHECK(WaveNameMatches("Speakers", "Speakers") && !WaveNameMatches("Speakers", "Headphones"));
    CHECK(WaveNameMatches(longName, cut));                      // the full name finds the cut one
    CHECK(!WaveNameMatches(longName, cut.substr(0, 30)));       // a 30-char name is not a cut
    CHECK(!WaveNameMatches("Speakers (Other Device) of some length", cut));
    CHECK(!WaveNameMatches(cut, longName));
    const std::vector<std::string> devs = {"Speakers", cut, "Headset"};
    CHECK(ResolveWaveDevice(nullptr, devs) == -1 && ResolveWaveDevice("", devs) == -1 && ResolveWaveDevice("default", devs) == -1);
    CHECK(ResolveWaveDevice("Headset", devs) == 2 && ResolveWaveDevice(longName.c_str(), devs) == 1 && ResolveWaveDevice("Missing", devs) == -2);
    CHECK(ResolveWaveDevice("Speakers", {}) == -2);
}

} // namespace

int main() {
    TestKeys();
    TestText();
    TestTimeDpiClicks();
    TestEventQueue();
    TestPads();
    TestAudio();
    TEST_MAIN_RESULT();
}
