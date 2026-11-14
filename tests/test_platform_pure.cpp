// The pure logic behind the own Windows (and future macOS) backend: UTF-16 handling, key tables, DPI
// arithmetic, click counting, raw-mouse accumulation, XInput mapping and controller hot-plug tracking.
// No display, no OS: runs anywhere (Linux CI included).
#include "test_framework.hpp"
#include "Platform/PlatformText.hpp"
#include "Platform/Win32Logic.hpp"
#include "Platform/MacLogic.hpp"
#include "Platform/AudioRing.hpp"
#include <cstring>
#include <vector>
#include <map>
#include <set>

using namespace okay::platform;

namespace {

std::u16string U16(std::initializer_list<unsigned> v) { std::u16string s; for (unsigned c : v) s += (char16_t)c; return s; }

void TestUtf() {
    RUN_SUITE("utf8 / utf16");
    using namespace text;
    CHECK(Utf16ToUtf8(u"abc", 3) == "abc");
    CHECK(Utf16ToUtf8(u"é", 1) == "\xC3\xA9");
    CHECK(Utf16ToUtf8(u"€", 1) == "\xE2\x82\xAC");
    const std::u16string rocket = U16({0xD83D, 0xDE80});   // U+1F680
    CHECK(Utf16ToUtf8(rocket.data(), 2) == "\xF0\x9F\x9A\x80");
    // Lone surrogates never produce ill-formed UTF-8.
    const std::u16string lone = U16({'a', 0xD83D, 'b', 0xDE80, 'c'});
    CHECK(Utf16ToUtf8(lone.data(), lone.size()) == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" "c");
    const std::u16string tail = U16({'a', 0xD83D});
    CHECK(Utf16ToUtf8(tail.data(), 2) == "a\xEF\xBF\xBD");
    // Round trips.
    for (const char* s : {"", "hello", "caf\xC3\xA9", "\xE2\x82\xAC 5", "\xF0\x9F\x9A\x80 go", "line1\nline2"}) {
        const std::u16string w = Utf8ToUtf16(s);
        CHECK(Utf16ToUtf8(w.data(), w.size()) == s);
    }
    CHECK(Utf8ToUtf16("\xF0\x9F\x9A\x80") == rocket);
    // Malformed input becomes U+FFFD, one per bad byte, and never reads past the end.
    CHECK(Utf8ToUtf16("\xFF") == U16({0xFFFD}));
    CHECK(Utf8ToUtf16("\xC3") == U16({0xFFFD}));                 // truncated
    CHECK(Utf8ToUtf16("\xE2\x82") == U16({0xFFFD, 0xFFFD}));      // truncated 3-byte
    CHECK(Utf8ToUtf16("\xC0\x80") == U16({0xFFFD, 0xFFFD}));      // overlong NUL
    CHECK(Utf8ToUtf16("\xED\xA0\x80") == U16({0xFFFD, 0xFFFD, 0xFFFD}));   // encoded surrogate
    CHECK(Utf8ToUtf16("\xF4\x90\x80\x80") == U16({0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD}));   // > U+10FFFF
    CHECK(Utf8ToUtf16("a\xC3(b") == U16({'a', 0xFFFD, '(', 'b'}));
}

void TestAssembler() {
    RUN_SUITE("Utf16Assembler (WM_CHAR)");
    using namespace text;
    Utf16Assembler a;
    std::string out;
    CHECK(a.Feed('a', out) && out == "a");
    out.clear();
    CHECK(!a.Feed(0xD83D, out) && out.empty() && a.Pending());   // held until the low half arrives
    CHECK(a.Feed(0xDE80, out) && out == "\xF0\x9F\x9A\x80" && !a.Pending());
    out.clear();
    // Control characters (Backspace, Tab, Enter, Esc, DEL) are not text.
    for (char16_t c : {0x08, 0x09, 0x0A, 0x0D, 0x1B, 0x7F, 0x85}) CHECK(!a.Feed(c, out));
    CHECK(out.empty());
    // A lone low surrogate: one replacement character, no state left behind.
    CHECK(a.Feed(0xDE80, out) && out == "\xEF\xBF\xBD");
    out.clear();
    // High surrogate followed by an ordinary character: the orphan is replaced, the character is kept
    // (the old code kept the stale high surrogate and glued it to the next surrogate pair).
    CHECK(!a.Feed(0xD83D, out));
    CHECK(a.Feed('x', out) && out == "\xEF\xBF\xBD" "x" && !a.Pending());
    out.clear();
    // Two high surrogates in a row: first becomes U+FFFD, second is held.
    CHECK(!a.Feed(0xD83D, out));
    CHECK(a.Feed(0xD83D, out) && out == "\xEF\xBF\xBD" && a.Pending());
    out.clear();
    CHECK(a.Feed(0xDE80, out) && out == "\xF0\x9F\x9A\x80");
    out.clear();
    // High surrogate then a control character: replacement only.
    CHECK(!a.Feed(0xD83D, out));
    CHECK(a.Feed(0x0D, out) && out == "\xEF\xBF\xBD");
    a.Reset();
    CHECK(!a.Pending());
}

void TestCrlf() {
    RUN_SUITE("clipboard line ends");
    using namespace text;
    CHECK(ToCrlf("a\nb") == "a\r\nb");
    CHECK(ToCrlf("a\r\nb") == "a\r\nb");   // already CR LF: not doubled
    CHECK(ToCrlf("\n\n") == "\r\n\r\n");
    CHECK(ToCrlf("") == "");
    CHECK(FromCrlf("a\r\nb\r\n") == "a\nb\n");
    CHECK(FromCrlf("a\rb") == "a\rb");     // a lone CR is kept
    CHECK(FromCrlf(ToCrlf("x\ny\n")) == "x\ny\n");
}

void TestKeys() {
    RUN_SUITE("Win32 scan codes and virtual keys");
    using namespace win32;
    // Set 1 scan codes of a US keyboard.
    CHECK(KeyFromScan(0x01, false) == Key::Escape);
    CHECK(KeyFromScan(0x02, false) == Key::N1);
    CHECK(KeyFromScan(0x0B, false) == Key::N0);
    CHECK(KeyFromScan(0x10, false) == Key::Q);
    CHECK(KeyFromScan(0x11, false) == Key::W);
    CHECK(KeyFromScan(0x1E, false) == Key::A);
    CHECK(KeyFromScan(0x2C, false) == Key::Z);
    CHECK(KeyFromScan(0x32, false) == Key::M);
    CHECK(KeyFromScan(0x1C, false) == Key::Return);
    CHECK(KeyFromScan(0x1C, true) == Key::KpEnter);
    CHECK(KeyFromScan(0x1D, false) == Key::LCtrl);
    CHECK(KeyFromScan(0x1D, true) == Key::RCtrl);
    CHECK(KeyFromScan(0x38, false) == Key::LAlt);
    CHECK(KeyFromScan(0x38, true) == Key::RAlt);   // AltGr
    CHECK(KeyFromScan(0x2A, false) == Key::LShift);
    CHECK(KeyFromScan(0x36, false) == Key::RShift);
    CHECK(KeyFromScan(0x39, false) == Key::Space);
    CHECK(KeyFromScan(0x3B, false) == Key::F1);
    CHECK(KeyFromScan(0x44, false) == Key::F10);
    CHECK(KeyFromScan(0x57, false) == Key::F11);
    CHECK(KeyFromScan(0x58, false) == Key::F12);
    CHECK(KeyFromScan(0x45, false) == Key::Pause);
    CHECK(KeyFromScan(0x45, true) == Key::NumLock);
    CHECK(KeyFromScan(0x46, false) == Key::ScrollLock);
    CHECK(KeyFromScan(0x47, true) == Key::Home);
    CHECK(KeyFromScan(0x47, false) == Key::Kp7);
    CHECK(KeyFromScan(0x53, true) == Key::Delete);
    CHECK(KeyFromScan(0x53, false) == Key::KpPeriod);
    CHECK(KeyFromScan(0x48, true) == Key::Up);
    CHECK(KeyFromScan(0x50, true) == Key::Down);
    CHECK(KeyFromScan(0x4B, true) == Key::Left);
    CHECK(KeyFromScan(0x4D, true) == Key::Right);
    CHECK(KeyFromScan(0x5B, true) == Key::LGui);
    CHECK(KeyFromScan(0x5C, true) == Key::RGui);
    CHECK(KeyFromScan(0x37, true) == Key::PrintScreen);
    CHECK(KeyFromScan(0x35, true) == Key::KpDivide);
    CHECK(KeyFromScan(0x37, false) == Key::KpMultiply);
    CHECK(KeyFromScan(0x4A, false) == Key::KpMinus);
    CHECK(KeyFromScan(0x4E, false) == Key::KpPlus);
    CHECK(KeyFromScan(0x56, false) == Key::Unknown);   // the ISO "102nd key" has no Key value
    CHECK(KeyFromScan(-1, false) == Key::Unknown);
    CHECK(KeyFromScan(0x200, false) == Key::Unknown);
    CHECK(KeyFromScan(0x7F, true) == Key::Unknown);
    // Every non-extended scan code maps to a distinct Key (no key shadows another).
    std::set<int> seen;
    int mapped = 0;
    for (int s = 0; s < 89; ++s) {
        const Key k = KeyFromScan(s, false);
        if (k == Key::Unknown) continue;
        ++mapped;
        CHECK(seen.insert((int)k).second);
    }
    CHECK(mapped == 85);
    // VkForKey: letters/digits/F-keys are the documented ranges, and distinct keys have distinct VKs
    // (KpEnter shares VK_RETURN with Return: Windows tells them apart by the extended flag only).
    CHECK(VkForKey(Key::A) == 'A' && VkForKey(Key::Z) == 'Z');
    CHECK(VkForKey(Key::N1) == '1' && VkForKey(Key::N9) == '9' && VkForKey(Key::N0) == '0');
    CHECK(VkForKey(Key::F1) == 0x70 && VkForKey(Key::F12) == 0x7B);
    CHECK(VkForKey(Key::Kp0) == 0x60 && VkForKey(Key::Kp9) == 0x69);
    CHECK(VkForKey(Key::LShift) == 0xA0 && VkForKey(Key::RCtrl) == 0xA3 && VkForKey(Key::RAlt) == 0xA5);
    CHECK(VkForKey(Key::Unknown) == 0);
    std::map<int, int> vks;
    for (int k = 1; k < 512; ++k) {
        const int vk = VkForKey((Key)k);
        if (!vk) continue;
        CHECK(vks.insert({vk, k}).second || vk == 0x0D);
    }
    // Every key the engine's own tables use has a virtual key: letters, digits, punctuation, arrows, mods.
    for (int k = (int)Key::A; k <= (int)Key::Slash; ++k) {
        if (k == 50) continue;   // USB 50 (non-US #) is not in Key
        CHECK(VkForKey((Key)k) != 0);
    }
    // Scan code -> Key -> VK agree for the whole letter block.
    CHECK(VkForKey(KeyFromScan(0x1E, false)) == 'A');
    CHECK(VkForKey(KeyFromScan(0x2C, false)) == 'Z');
    CHECK(VkForKey(KeyFromScan(0x10, false)) == 'Q');
}

void TestDpi() {
    RUN_SUITE("DPI arithmetic");
    using namespace win32;
    CHECK(ScaleForDpi(100, 96) == 100);
    CHECK(ScaleForDpi(100, 144) == 150);
    CHECK(ScaleForDpi(100, 192) == 200);
    CHECK(ScaleForDpi(101, 120) == 126);    // 126.25
    CHECK(ScaleForDpi(1, 144) == 2);        // 1.5 rounds half away from zero, like MulDiv
    CHECK(ScaleForDpi(-1, 144) == -2);
    CHECK(ScaleForDpi(100, 0) == 100);      // unknown DPI = 96
    CHECK(ScaleForDpi(16384, 480) == 81920);   // no overflow at the largest window and 500%
    CHECK(UnscaleForDpi(150, 144) == 100);
    CHECK(UnscaleForDpi(200, 192) == 100);
    CHECK(UnscaleForDpi(100, 0) == 100);
    for (unsigned dpi : {96u, 120u, 144u, 168u, 192u, 240u}) for (int v : {1, 7, 100, 1280, 3840}) CHECK(UnscaleForDpi(ScaleForDpi(v, dpi), dpi) == v);
    CHECK_NEAR(DpiScaleFactor(96), 1.0, 1e-6);
    CHECK_NEAR(DpiScaleFactor(144), 1.5, 1e-6);
    CHECK_NEAR(DpiScaleFactor(0), 1.0, 1e-6);
}

void TestClicks() {
    RUN_SUITE("double-click counting");
    win32::ClickCounter c;
    CHECK(c.Down(1, 10, 10, 1000, 500, 2) == 1);
    CHECK(c.Down(1, 11, 10, 1300, 500, 2) == 2);
    CHECK(c.Down(1, 11, 11, 1600, 500, 2) == 3);
    CHECK(c.Down(1, 11, 11, 2200, 500, 2) == 1);   // too slow
    CHECK(c.Down(3, 11, 11, 2300, 500, 2) == 1);   // other button
    CHECK(c.Down(3, 40, 11, 2400, 500, 2) == 1);   // too far
    CHECK(c.Down(3, 40, 11, 2500, 500, 2) == 2);
    c.Reset();
    CHECK(c.Down(3, 40, 11, 2600, 500, 2) == 1);
    // The 32-bit message-time clock wraps every 49 days.
    CHECK(c.Down(1, 0, 0, 0xFFFFFF00u, 500, 2) == 1);
    CHECK(c.Down(1, 0, 0, 0x00000050u, 500, 2) == 2);
}

void TestRawMouse() {
    RUN_SUITE("raw mouse accumulation");
    win32::RawMouseAccumulator r;
    int dx = 9, dy = 9;
    r.Take(dx, dy);
    CHECK(dx == 0 && dy == 0);
    r.Add(0, 3, -2); r.Add(0, 4, 5);
    r.Take(dx, dy);
    CHECK(dx == 7 && dy == 3);
    r.Take(dx, dy);
    CHECK(dx == 0 && dy == 0);   // taking clears
    r.Add(1, 1000, 2000);        // first absolute sample: no delta
    r.Take(dx, dy);
    CHECK(dx == 0 && dy == 0);
    r.Add(1, 1010, 1995); r.Add(1, 1020, 1995);
    r.Take(dx, dy);
    CHECK(dx == 20 && dy == -5);
    r.ResetAbsolute();
    r.Add(1, 5000, 5000);        // after a reset (new device / focus) the jump is not motion
    r.Take(dx, dy);
    CHECK(dx == 0 && dy == 0);
}

void TestXInput() {
    RUN_SUITE("XInput mapping");
    using namespace win32;
    CHECK(XiButtonMask(GamepadButton::A) == 0x1000 && XiButtonMask(GamepadButton::Y) == 0x8000);
    CHECK(XiButtonMask(GamepadButton::DpadUp) == 1 && XiButtonMask(GamepadButton::DpadRight) == 8);
    CHECK(XiButtonMask(GamepadButton::Guide) == 0);
    std::set<int> masks;
    for (int b = 0; b <= (int)GamepadButton::DpadRight; ++b) {
        const uint16_t m = XiButtonMask((GamepadButton)b);
        if (m) CHECK(masks.insert(m).second && (m & (m - 1)) == 0);   // distinct single bits
    }
    CHECK(masks.size() == 14);
    CHECK(XiFlipY(0) == 0 && XiFlipY(100) == -100 && XiFlipY(-100) == 100);
    CHECK(XiFlipY(-32768) == 32767 && XiFlipY(32767) == -32767);
    CHECK(XiTrigger(0) == 0 && XiTrigger(255) == 32767);
    CHECK(XiTrigger(128) > 16000 && XiTrigger(128) < 16500);
    int16_t prev = -1;
    for (int t = 0; t < 256; ++t) { const int16_t v = XiTrigger((uint8_t)t); CHECK(v >= prev); prev = v; }
}

void TestPadTracker() {
    RUN_SUITE("controller hot-plug tracker");
    using namespace win32;
    bool plugged[4] = {false, true, false, true};
    int probes[4] = {0, 0, 0, 0};
    auto probe = [&](int s) { ++probes[s]; return plugged[s]; };
    PadTracker t(1000);
    CHECK(!t.Scanned());
    std::vector<PadTracker::Change> ch;
    t.Poll(0, probe, ch);
    CHECK(t.Scanned());
    CHECK(ch.size() == 2);
    CHECK(ch[0].added && ch[0].slot == 1 && ch[0].instance == 1 && ch[0].deviceIndex == 0);
    CHECK(ch[1].added && ch[1].slot == 3 && ch[1].instance == 2 && ch[1].deviceIndex == 1);
    CHECK(t.Count() == 2 && t.SlotOfIndex(0) == 1 && t.SlotOfIndex(1) == 3 && t.SlotOfIndex(2) == -1);
    CHECK(t.IndexOfSlot(3) == 1);
    // Cost: connected slots are read every poll, empty slots at most once per interval.
    ch.clear();
    for (int ms = 10; ms < 1000; ms += 10) t.Poll((uint64_t)ms, probe, ch);
    CHECK(ch.empty());
    CHECK(probes[0] == 1 && probes[2] == 1);          // the empty slots: only the first scan
    CHECK(probes[1] == 100 && probes[3] == 100);      // the connected ones: every poll
    t.Poll(1000, probe, ch);
    CHECK(probes[0] == 2 && probes[2] == 2);          // due again
    // Hot-plug: slot 0 appears, slot 1 disappears.
    plugged[0] = true; plugged[1] = false;
    ch.clear();
    t.Poll(1010, probe, ch);                          // slot 1 is read now and is gone; slot 0 is not due
    CHECK(ch.size() == 1 && !ch[0].added && ch[0].slot == 1 && ch[0].instance == 1);
    ch.clear();
    t.Poll(2000, probe, ch);
    CHECK(ch.size() == 1 && ch[0].added && ch[0].slot == 0 && ch[0].instance == 3 && ch[0].deviceIndex == 0);
    CHECK(t.Count() == 2 && t.SlotOfIndex(0) == 0 && t.SlotOfIndex(1) == 3);
    // The same slot plugged in again is a new device (a new instance id).
    plugged[0] = false;
    ch.clear();
    t.Poll(2010, probe, ch);
    CHECK(ch.size() == 1 && !ch[0].added && ch[0].instance == 3);
    plugged[0] = true;
    t.Poll(3500, probe, ch);
    CHECK(ch.size() == 2 && ch[1].added && ch[1].slot == 0 && ch[1].instance == 4);
    // A failed state read marks the slot lost without waiting for the next poll.
    ch.clear();
    t.MarkLost(3, ch);
    CHECK(ch.size() == 1 && !ch[0].added && ch[0].slot == 3 && !t.Connected(3) && t.Instance(3) == 0);
    t.MarkLost(3, ch);
    t.MarkLost(9, ch);
    CHECK(ch.size() == 1);
}

void TestMac() {
    RUN_SUITE("macOS key table and helpers");
    using namespace mac;
    // The ANSI layout, in kVK order 0x00..0x2F (a famously irregular sequence): a s d f h g z x c v (ISO) b q w e r y t
    // 1 2 3 4 6 5 = 9 7 - 8 0 ] o u [ i p (Return) l j ' k ; \ , / n m .
    CHECK(KeyFromVirtualKey(0x00) == Key::A && KeyFromVirtualKey(0x01) == Key::S && KeyFromVirtualKey(0x06) == Key::Z);
    CHECK(KeyFromVirtualKey(0x0B) == Key::B && KeyFromVirtualKey(0x0C) == Key::Q && KeyFromVirtualKey(0x2D) == Key::N);
    CHECK(KeyFromVirtualKey(0x0A) == Key::Unknown);   // ISO section key
    CHECK(KeyFromVirtualKey(0x24) == Key::Return && KeyFromVirtualKey(0x30) == Key::Tab && KeyFromVirtualKey(0x31) == Key::Space);
    CHECK(KeyFromVirtualKey(0x33) == Key::Backspace && KeyFromVirtualKey(0x35) == Key::Escape && KeyFromVirtualKey(0x75) == Key::Delete);
    CHECK(KeyFromVirtualKey(0x7B) == Key::Left && KeyFromVirtualKey(0x7C) == Key::Right);
    CHECK(KeyFromVirtualKey(0x7D) == Key::Down && KeyFromVirtualKey(0x7E) == Key::Up);
    CHECK(KeyFromVirtualKey(0x7A) == Key::F1 && KeyFromVirtualKey(0x78) == Key::F2 && KeyFromVirtualKey(0x63) == Key::F3);
    CHECK(KeyFromVirtualKey(0x76) == Key::F4 && KeyFromVirtualKey(0x60) == Key::F5 && KeyFromVirtualKey(0x6F) == Key::F12);
    CHECK(KeyFromVirtualKey(0x37) == Key::LGui && KeyFromVirtualKey(0x36) == Key::RGui);
    CHECK(KeyFromVirtualKey(0x38) == Key::LShift && KeyFromVirtualKey(0x3C) == Key::RShift);
    CHECK(KeyFromVirtualKey(0x3A) == Key::LAlt && KeyFromVirtualKey(0x3D) == Key::RAlt);
    CHECK(KeyFromVirtualKey(0x3B) == Key::LCtrl && KeyFromVirtualKey(0x3E) == Key::RCtrl);
    CHECK(KeyFromVirtualKey(0x52) == Key::Kp0 && KeyFromVirtualKey(0x5C) == Key::Kp9 && KeyFromVirtualKey(0x4C) == Key::KpEnter);
    CHECK(KeyFromVirtualKey(0x80) == Key::Unknown && KeyFromVirtualKey(0xFFFF) == Key::Unknown);
    // Injective: no two virtual keys give the same Key. The 26 letters and the ten digits are all present.
    std::set<int> seen;
    int letterCount = 0, digitCount = 0;
    for (unsigned vk = 0; vk < 0x80; ++vk) {
        const Key k = KeyFromVirtualKey(vk);
        if (k == Key::Unknown) continue;
        CHECK(seen.insert((int)k).second);
        if ((int)k >= (int)Key::A && (int)k <= (int)Key::Z) ++letterCount;
        if ((int)k >= (int)Key::N1 && (int)k <= (int)Key::N0) ++digitCount;
    }
    CHECK(letterCount == 26 && digitCount == 10);
    CHECK(ModsFromNsFlags(0) == 0);
    CHECK(ModsFromNsFlags(kNsShift | kNsCommand) == (ModShift | ModGui));
    CHECK(ModsFromNsFlags(kNsControl | kNsOption | kNsCapsLock) == (ModCtrl | ModAlt | ModCaps));
    CHECK(ModsFromNsFlags(kNsNumericPad) == 0);   // "key is on the numpad", not NumLock
    CHECK_NEAR(WheelNotches(3.0, false), 3.0, 1e-6);
    CHECK_NEAR(WheelNotches(25.0, true), 2.5, 1e-6);
}


void TestRing() {
    RUN_SUITE("audio ring");
    AudioRing r;
    uint8_t in[10000], out[10000];
    for (int i = 0; i < 10000; ++i) in[i] = (uint8_t)(i * 7 + 3);
    CHECK(r.Size() == 0 && r.Pop(out, 5) == 0);
    r.Push(in, 100);
    CHECK(r.Size() == 100);
    CHECK(r.Pop(out, 40) == 40 && std::memcmp(out, in, 40) == 0);
    r.Push(in + 100, 9000);   // grows while the head is not at 0
    CHECK(r.Size() == 9060);
    CHECK(r.Pop(out, 9060) == 9060 && std::memcmp(out, in + 40, 9060) == 0);
    CHECK(r.Size() == 0);
    // wrap-around in a fixed small ring
    AudioRing w(64);
    for (int round = 0; round < 50; ++round) {
        w.Push(in + round, 40);
        uint8_t o2[40];
        CHECK(w.Pop(o2, 40) == 40 && std::memcmp(o2, in + round, 40) == 0);
    }
    // capacity drops the OLDEST bytes
    AudioRing c(100);
    c.Push(in, 80); c.Push(in + 80, 80);
    CHECK(c.Size() == 100);
    CHECK(c.Pop(out, 100) == 100 && std::memcmp(out, in + 60, 100) == 0);
    c.Push(in, 1000);   // bigger than the capacity: the newest 100 survive
    CHECK(c.Size() == 100 && c.Pop(out, 100) == 100 && std::memcmp(out, in + 900, 100) == 0);
    c.Push(in, 10); c.Clear();
    CHECK(c.Size() == 0);
    // playback period: silence for the part the queue cannot supply
    AudioRing q;
    q.Push(in, 6);
    uint8_t buf[16];
    std::memset(buf, 0xAA, sizeof(buf));
    const size_t real = FillPlaybackPeriod(buf, 16, false, [](uint8_t*, size_t) {}, q);
    CHECK(real == 6 && std::memcmp(buf, in, 6) == 0 && buf[6] == 0 && buf[15] == 0);
    int cbBytes = 0;
    FillPlaybackPeriod(buf, 16, true, [&](uint8_t* b, size_t n) { cbBytes = (int)n; b[0] = 9; }, q);
    CHECK(cbBytes == 16 && buf[0] == 9 && buf[1] == 0);
}

} // namespace

int main() {
    TestUtf();
    TestAssembler();
    TestCrlf();
    TestKeys();
    TestDpi();
    TestClicks();
    TestRawMouse();
    TestXInput();
    TestPadTracker();
    TestMac();
    TestRing();
    TEST_MAIN_RESULT();
}
