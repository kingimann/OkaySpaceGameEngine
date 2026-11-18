// Pure tests for the Windows-backend parts that need no Windows: display and DPI maths, icon pixel
// preparation, the input-method composition state machine and the HID report descriptor parser.
// Expected values are worked out by hand from the Microsoft and USB HID documents.
#include "HidParser.hpp"
#include "Win32Display.hpp"
#include "Win32Icon.hpp"
#include "Win32Ime.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace okay::platform;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void Display() {
    using namespace win32disp;
    CHECK(ScaleFromDpi(96) == 1.0f && ScaleFromDpi(144) == 1.5f && ScaleFromDpi(192) == 2.0f && ScaleFromDpi(0) == 1.0f && ScaleFromDpi(5000) == 1.0f);
    CHECK(ScaleLength(100, 144) == 150 && ScaleLength(-100, 144) == -150 && ScaleLength(1, 120) == 1 && ScaleLength(3, 144) == 5 && ScaleLength(100, 0) == 100 && ScaleLength(0, 192) == 0);
    CHECK(DpiFromWParam(0x00A000A0) == 160 && DpiFromWParam(0x0060) == 96);
    const Rect r = FromLtrb(10, 20, 110, 70);
    CHECK(r.x == 10 && r.y == 20 && r.w == 100 && r.h == 50);
    CHECK(FromLtrb(10, 10, 5, 5).w == 0 && FromLtrb(10, 10, 5, 5).h == 0);
    CHECK(Contains(r, 10, 20) && Contains(r, 109, 69) && !Contains(r, 110, 20) && !Contains(r, 9, 20) && !Contains(r, 50, 70));
    CHECK(RefreshFromDevMode(60) == 60 && RefreshFromDevMode(0) == 0 && RefreshFromDevMode(1) == 0 && RefreshFromDevMode(100000) == 0);
    std::vector<DisplayInfo> v(3);
    v[0].x = 1920; v[0].w = 1920; v[0].h = 1080; v[0].name = "right";
    v[1].x = 0; v[1].w = 1920; v[1].h = 1080; v[1].primary = true; v[1].name = "main";
    v[2].x = -1280; v[2].w = 1280; v[2].h = 1024; v[2].name = "left";
    OrderDisplays(v);
    CHECK(v[0].name == "main" && v[1].name == "left" && v[2].name == "right");
    CHECK(DisplayForPoint(v, 100, 100) == 0 && DisplayForPoint(v, -5, 5) == 1 && DisplayForPoint(v, 3000, 10) == 2);
    CHECK(DisplayForPoint(v, 5000, 500) == 2 && DisplayForPoint(v, -9000, 500) == 1);   // outside all: the nearest centre
    CHECK(DisplayForPoint({}, 0, 0) == -1);
}

static void Icon() {
    using namespace win32icon;
    const uint8_t px[12] = {10, 20, 30, 255, 40, 50, 60, 0, 70, 80, 90, 128};
    CHECK(ValidIcon(px, 3, 1) && !ValidIcon(nullptr, 3, 1) && !ValidIcon(px, 0, 1) && !ValidIcon(px, 513, 1) && !ValidIcon(px, 1, -2));
    const auto bgra = ToBgra(px, 3, 1);
    CHECK(bgra.size() == 12 && bgra[0] == 30 && bgra[1] == 20 && bgra[2] == 10 && bgra[3] == 255 && bgra[8] == 90 && bgra[11] == 128);
    CHECK(MaskStride(1) == 2 && MaskStride(16) == 2 && MaskStride(17) == 4 && MaskStride(32) == 4);
    const auto m = AndMask(px, 3, 1);                  // pixel 2 has alpha 0: transparent; pixel 3 has alpha 128: opaque
    CHECK(m.size() == 2 && m[0] == 0x40 && m[1] == 0);
    uint8_t wide[20 * 4];
    for (int i = 0; i < 20; ++i) { wide[i * 4] = wide[i * 4 + 1] = wide[i * 4 + 2] = 1; wide[i * 4 + 3] = i == 17 ? 0 : 255; }
    const auto m2 = AndMask(wide, 20, 1);
    CHECK(m2.size() == 4 && m2[2] == 0x40 && m2[0] == 0 && m2[1] == 0 && m2[3] == 0);   // pixel 17 = byte 2, bit 1 from the left
    // Resize: identical size keeps every pixel; 2x2 -> 1x1 averages weighted by alpha; a transparent border does not darken.
    const uint8_t two[16] = {255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0};
    const auto same = Resize(px, 3, 1, 3, 1);     // same size: every pixel kept; a fully transparent pixel loses its (invisible) colour
    CHECK(same.size() == 12 && std::memcmp(same.data(), px, 4) == 0 && std::memcmp(same.data() + 8, px + 8, 4) == 0 && same[3 + 4] == 0 && same[4] == 0);
    const auto one = Resize(two, 2, 2, 1, 1);
    CHECK(one.size() == 4 && one[0] == 255 && one[1] == 0 && one[2] == 0 && one[3] == 64);
    const auto up = Resize(two, 2, 2, 4, 4);           // growing: each source pixel covers a 2x2 block
    CHECK(up.size() == 64 && up[0] == 255 && up[3] == 255 && up[(2 * 4 + 0) * 4 + 3] == 0 && up[(0 * 4 + 2) * 4 + 3] == 0);
    const auto zero = Resize(two, 2, 2, 1, 1);
    CHECK(zero.size() == 4);
}

static void Ime() {
    using namespace win32ime;
    CHECK(FromUtf16(u"ni", 2).text == "ni" && FromUtf16(u"ni", 2).cursor == 2);
    CHECK(FromUtf16(u"\x4F60", 1).text == "\xE4\xBD\xA0" && FromUtf16(u"\x4F60", 1).cursor == 1);
    CHECK(FromUtf16(u"\xD83D\xDE00", 2).text == "\xF0\x9F\x98\x80" && FromUtf16(u"\xD83D\xDE00", 2).cursor == 1);     // one code point in two units
    CHECK(FromUtf16(u"a\xD83D" u"b", 3).text == "a\xEF\xBF\xBD" "b");                                                  // lone surrogate
    CHECK(FromUtf16(u"abc", 99).cursor == 3 && FromUtf16(u"abc", -4).cursor == 0 && FromUtf16(u"", 0).text.empty());
    CHECK(FromUtf16(u"\x00E4", 0).text == "\xC3\xA4");
    Machine m;
    CHECK(!m.Active());
    m.Start();
    CHECK(m.Active());
    auto ev = m.Composition(kGcsCompStr | kGcsCursorPos, u"ni", 2);
    CHECK(ev.size() == 1 && ev[0].text == "ni" && ev[0].cursor == 2);
    ev = m.Composition(kGcsCompStr | kGcsCursorPos, u"ni", 2);                       // same again: nothing new
    CHECK(ev.empty());
    ev = m.Composition(kGcsCompStr | kGcsCursorPos, u"ni", 1);                       // caret moved: new event
    CHECK(ev.size() == 1 && ev[0].cursor == 1);
    ev = m.Composition(kGcsCompStr, u"\x4F60", 0);                                   // no cursor flag: caret at the end
    CHECK(ev.size() == 1 && ev[0].text == "\xE4\xBD\xA0" && ev[0].cursor == 1);
    ev = m.Composition(kGcsResultStr, u"", 0);                                       // committed: composing text cleared
    CHECK(ev.size() == 1 && ev[0].text.empty());
    CHECK(m.End().empty());                                                          // already cleared: no second empty event
    CHECK(!m.Active());
    // End while text is showing clears it once.
    m.Start();
    m.Composition(kGcsCompStr, u"a", 1);
    ev = m.End();
    CHECK(ev.size() == 1 && ev[0].text.empty());
    CHECK(m.End().empty());
    // An IME that skips the start message still works, and a message with neither flag does nothing.
    Machine m2;
    CHECK(m2.Composition(0, u"", 0).empty());
    ev = m2.Composition(kGcsCompStr, u"k", 1);
    CHECK(m2.Active() && ev.size() == 1 && ev[0].text == "k");
    // Result and a new composition in one message (flags both set): the composition string wins and is shown.
    ev = m2.Composition(kGcsResultStr | kGcsCompStr, u"\x3042", 1);
    CHECK(ev.size() == 1 && ev[0].text == "\xE3\x81\x82");
    // Empty composition string right after Start sends nothing (nothing was showing).
    Machine m3;
    m3.Start();
    CHECK(m3.Composition(kGcsCompStr, u"", 0).empty());
}

static std::vector<uint8_t> Bytes(std::initializer_list<int> l) { std::vector<uint8_t> v; for (int b : l) v.push_back((uint8_t)b); return v; }

static void Hid() {
    using namespace hid;
    // A classic pad: two 8-bit signed axes, a hat on 4 bits (1..8, 0 = centre) with 4 padding bits, 8 buttons. 4 bytes, no report id.
    const auto d = Bytes({0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
                          0xC0, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
                          0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0});
    const Descriptor ds = Parse(d.data(), d.size());
    CHECK(ds.ok && !ds.usesReportIds && ds.fields.size() == 4);
    CHECK(ds.fields[0].bitOffset == 0 && ds.fields[0].count == 2 && ds.fields[0].bitSize == 8 && ds.fields[0].usages[0] == kUsageX && ds.fields[0].usages[1] == kUsageY &&
          ds.fields[0].logicalMin == -127 && ds.fields[0].logicalMax == 127 && ds.fields[0].variable && !ds.fields[0].constant);
    CHECK(ds.fields[1].bitOffset == 16 && ds.fields[1].bitSize == 4 && ds.fields[1].usages[0] == kUsageHat && ds.fields[1].logicalMin == 1 && ds.fields[1].logicalMax == 8);
    CHECK(ds.fields[2].bitOffset == 20 && ds.fields[2].constant);
    CHECK(ds.fields[3].bitOffset == 24 && ds.fields[3].count == 8 && ds.fields[3].usagePage == kPageButton && ds.fields[3].usages[0] == 1 && ds.fields[3].usages[7] == 8);
    CHECK(ds.ReportBits(0) == 32);
    const PadLayout l = LayoutFromFields(ds);
    CHECK(l.Usable() && l.axes.size() == 2 && l.hats.size() == 1 && l.buttons.size() == 8);
    CHECK(l.axes[0].usage == kUsageX && l.axes[0].isSigned && l.buttons[2].bitOffset == 26);
    const uint8_t rep[4] = {0x7F, 0x81, 0x03, 0x05};   // X=127, Y=-127, hat=3 (right), buttons 1 and 3
    CHECK(ReadBits(rep, 4, 0, 8, true) == 127 && ReadBits(rep, 4, 8, 8, true) == -127 && ReadBits(rep, 4, 16, 4, false) == 3 && ReadBits(rep, 4, 24, 1, false) == 1 && ReadBits(rep, 4, 25, 1, false) == 0 && ReadBits(rep, 4, 26, 1, false) == 1);
    CHECK(NormalizeAxis(127, -127, 127) == 32767 && NormalizeAxis(-127, -127, 127) == -32768 && NormalizeAxis(0, -127, 127) == 0);
    CHECK(NormalizeAxis(500, -127, 127) == 32767 && NormalizeAxis(-500, -127, 127) == -32768 && NormalizeAxis(5, 3, 3) == 0);
    CHECK(NormalizeAxis(0, 0, 255) == -32768 && NormalizeAxis(255, 0, 255) == 32767 && NormalizeAxis(128, 0, 255) == 128);   // 128/255 of the range: 32896 - 32768
    CHECK(HatBits(1, 1, 8) == 1 && HatBits(2, 1, 8) == 3 && HatBits(3, 1, 8) == 2 && HatBits(4, 1, 8) == 6 && HatBits(5, 1, 8) == 4 && HatBits(6, 1, 8) == 12 && HatBits(7, 1, 8) == 8 && HatBits(8, 1, 8) == 9);
    CHECK(HatBits(0, 1, 8) == 0 && HatBits(9, 1, 8) == 0 && HatBits(0, 0, 7) == 1 && HatBits(2, 0, 7) == 2 && HatBits(7, 0, 7) == 9);
    CHECK(HatBits(1, 0, 3) == 2 && HatBits(3, 0, 3) == 8 && HatBits(4, 0, 3) == 0);   // four-position hat: up, right, down, left
    CHECK(ReadBits(rep, 4, 30, 8, false) == 0 && ReadBits(rep, 4, 0, 0, false) == 0 && ReadBits(rep, 4, 0, 33, false) == 0);   // too short / bad size read 0

    // Report ids, an unsigned 0..255 axis (2-byte logical max), usage range for buttons, an array field, an Output item that takes no input bits.
    const auto d2 = Bytes({0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
                           0x05, 0x08, 0x09, 0x01, 0x91, 0x02, 0x75, 0x08, 0x95, 0x01, 0x91, 0x03,
                           0x85, 0x02, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
                           0x05, 0x07, 0x09, 0x04, 0x09, 0x05, 0x75, 0x08, 0x95, 0x04, 0x81, 0x00, 0xC0});
    const Descriptor d2p = Parse(d2.data(), d2.size());
    CHECK(d2p.ok && d2p.usesReportIds && d2p.fields.size() == 4);
    CHECK(d2p.fields[0].reportId == 1 && d2p.fields[0].logicalMax == 255 && d2p.fields[0].bitOffset == 0 && d2p.ReportBits(1) == 8);
    CHECK(d2p.fields[1].reportId == 2 && d2p.fields[1].bitOffset == 0 && d2p.fields[1].count == 3 && d2p.fields[1].usages[2] == 3);
    CHECK(d2p.fields[3].bitOffset == 8 && !d2p.fields[3].variable && d2p.fields[3].usages.size() == 4 && d2p.fields[3].usages[0] == 4 && d2p.fields[3].usages[3] == 5);   // last usage repeats
    CHECK(d2p.ReportBits(2) == 40 && d2p.ReportBits(7) == 0);
    // Push and pop restore the global state.
    const auto d3 = Bytes({0x05, 0x01, 0x09, 0x30, 0x75, 0x08, 0x95, 0x01, 0xA4, 0x75, 0x10, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x81, 0x02, 0xB4, 0x09, 0x31, 0x81, 0x02});
    const Descriptor d3p = Parse(d3.data(), d3.size());
    CHECK(d3p.ok && d3p.fields.size() == 2 && d3p.fields[0].bitSize == 16 && d3p.fields[0].logicalMax == 65535 && d3p.fields[1].bitSize == 8 && d3p.fields[1].bitOffset == 16);
    // Malformed input: truncated item, unbalanced pop, absurd sizes, long item skipped, empty. Nothing crashes, found fields are kept.
    CHECK(!Parse(Bytes({0x05}).data(), 1).ok && Parse(nullptr, 0).fields.empty() && Parse(nullptr, 0).ok);
    CHECK(!Parse(Bytes({0xB4}).data(), 1).ok);
    const auto big = Bytes({0x75, 0xFF, 0x95, 0xFF, 0x81, 0x02});
    CHECK(!Parse(big.data(), big.size()).ok);
    const auto lng = Bytes({0xFE, 0x02, 0x10, 0xAA, 0xBB, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02});
    CHECK(Parse(lng.data(), lng.size()).ok && Parse(lng.data(), lng.size()).fields.size() == 1);
    CHECK(!Parse(Bytes({0xFE, 0x05, 0x10}).data(), 3).ok);
    // Random bytes never crash the parser (also run under ASan and UBSan).
    unsigned rng = 99;
    for (int round = 0; round < 3000; ++round) {
        std::vector<uint8_t> junk((size_t)(rng % 90));
        for (auto& b : junk) { rng = rng * 1664525u + 1013904223u; b = (uint8_t)(rng >> 24); }
        rng = rng * 1664525u + 1013904223u;
        const Descriptor p = Parse(junk.data(), junk.size());
        const PadLayout pl = LayoutFromFields(p);
        (void)pl.Usable();
    }
    CHECK(true);
}

int main() {
    Display(); Icon(); Ime(); Hid();
    std::printf("platform_win32_pure: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
