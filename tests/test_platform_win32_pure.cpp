// Pure tests for the Windows-backend parts that need no Windows: display and DPI maths, icon pixel
// preparation, the input-method composition state machine and the HID report descriptor parser.
// Expected values are worked out by hand from the Microsoft and USB HID documents.
#include "HidParser.hpp"
#include "Win32Display.hpp"
#include "PadMap.hpp"
#include "Win32Hid.hpp"
#include "Win32Icon.hpp"
#include "Win32Ime.hpp"
#include "Win32XInput.hpp"
#include "Win32Messages.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/WindowLogic.hpp"
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
    CHECK(ValidDpi(144) == 144 && ValidDpi(0) == 96 && ValidDpi(-5) == 96 && ValidDpi(47) == 96 && ValidDpi(5000) == 96);
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
    // Centring and fullscreen on a chosen display (v: 0 main 1920x1080 with a 40 px task bar, 1 left at -1280, 2 right at 1920).
    v[0].ux = 0; v[0].uy = 0; v[0].uw = 1920; v[0].uh = 1040;
    CHECK(WorkArea(v[0]).h == 1040 && WorkArea(v[1]).x == -1280 && WorkArea(v[1]).h == 1024);   // no work area given: whole display
    Rect c = CentredRect(WorkArea(v[0]), 800, 600);
    CHECK(c.x == 560 && c.y == 220 && c.w == 800 && c.h == 600);
    c = CentredRect(WorkArea(v[1]), 800, 600);                          // left display: negative coordinates
    CHECK(c.x == -1280 + 240 && c.y == 212);
    c = CentredRect(WorkArea(v[0]), 3000, 2000);                        // bigger than the display: top-left, not off screen
    CHECK(c.x == 0 && c.y == 0);
    CHECK(CentredRect(WorkArea(v[2]), 1920, 100).x == 1920);            // exactly as wide: left edge
    Rect f = FullscreenRect(v[2]);
    CHECK(f.x == 1920 && f.y == 0 && f.w == 1920 && f.h == 1080);
    CHECK(DisplayForNewWindow(v, true, -100, 50) == 1 && DisplayForNewWindow(v, false, -100, 50) == 0 && DisplayForNewWindow({}, true, 0, 0) == -1);
    // Taskbar flash: stop clears, briefly flashes window and taskbar a few times, until-focused keeps going until foreground.
    CHECK(PlanFlash(FlashMode::Stop).flags == 0 && PlanFlash(FlashMode::Stop).count == 0);
    CHECK(PlanFlash(FlashMode::Briefly).flags == 3 && PlanFlash(FlashMode::Briefly).count == 3);
    CHECK(PlanFlash(FlashMode::UntilFocused).flags == (3u | 0xCu) && PlanFlash(FlashMode::UntilFocused).count == 0);
}

static void PadMapT() {
    using namespace padmap;
    // Source syntax.
    Source s;
    CHECK(detail::ParseSource("b3", s) && s.kind == Kind::Button && s.index == 3 && !s.invert);
    CHECK(detail::ParseSource(" -A1 ", s) && s.kind == Kind::Axis && s.index == 1 && s.invert && s.half == Half::Full);
    CHECK(detail::ParseSource("a4+", s) && s.half == Half::Positive && detail::ParseSource("a4-", s) && s.half == Half::Negative);
    CHECK(detail::ParseSource("h0.Left", s) && s.kind == Kind::Hat && s.hatBit == 8 && detail::ParseSource("h1.up", s) && s.index == 1 && s.hatBit == 1);
    CHECK(!detail::ParseSource("", s) && !detail::ParseSource("x1", s) && !detail::ParseSource("a", s) && !detail::ParseSource("b-1", s) && !detail::ParseSource("-b1", s) &&
          !detail::ParseSource("h0", s) && !detail::ParseSource("h0.diag", s) && !detail::ParseSource("a1x", s) && !detail::ParseSource("a99999", s) && !detail::ParseSource("-h0.up", s));
    uint16_t h = 0;
    CHECK(detail::ParseHex16("054c", h) && h == 0x054C && detail::ParseHex16("0xFFFF", h) && h == 0xFFFF && !detail::ParseHex16("10000", h) && !detail::ParseHex16("", h) && !detail::ParseHex16("0x", h) && !detail::ParseHex16("12g4", h));

    // The text file.
    const char* txt =
        "# example\n[pad]\nvid = 054C\npid = 09cc   # comment after a value\nname = Test   Pad\n"
        "a = b1\nb = b2\nleftx = a0\nlefty = -a1\nlefttrigger = a4\nrighttrigger = a5+\ndpup = h0.up\ndpdown = h0.down\nstart = b9\n"
        "bogus = b1\nx = q7\n"
        "[PAD]\nvid = 1234\nname = no pid\n"
        "[pad]\nvid=1234\npid=5678\nleftx = a2\n"
        "[pad]\nvid=1234\npid=5678\nleftx = a3\ntrailing junk line\n";
    Database db; std::vector<std::string> errs;
    CHECK(db.Parse(txt, &errs) == 3 && db.Size() == 2);                 // 054c:09cc, and 1234:5678 twice (replaced), the pid-less one skipped
    CHECK(errs.size() == 4);                                            // bogus key, bad source, entry without pid, junk line
    const Entry* e = db.Find(0x054C, 0x09CC);
    CHECK(e && e->name == "Test   Pad" && e->map.buttons[(int)GamepadButton::A].index == 1 && e->map.axes[(int)GamepadAxis::LeftY].invert);
    CHECK(e && e->map.buttons[(int)GamepadButton::Start].index == 9 && e->map.buttons[(int)GamepadButton::DpadDown].hatBit == 4);
    CHECK(db.Find(0x1234, 0x5678) && db.Find(0x1234, 0x5678)->map.axes[0].index == 3);   // the later entry replaced the earlier
    CHECK(!db.Find(0x1234, 0x0000) && !db.Find(1, 1));
    CHECK(Database().Parse("key = value\n[pad]\nvid = zz\n") == 0);   // key before [pad]; bad vid, no entry
    CHECK(Database().Parse("") == 0 && Database().Parse("\n\n[pad]") == 0);
    CHECK(Database().Parse(std::string((1u << 20) + 1, 'x')) == 0);     // over 1 MB
    Database d2; CHECK(d2.Parse("[pad]\r\nvid=1\r\npid=2\r\na=b0\r\n") == 1 && d2.Find(1, 2)->map.buttons[0].index == 0);   // CRLF, and no final newline

    // Evaluating a map.
    PadMap m = e->map;
    int16_t axes[6] = {-32768, 32767, 0, 0, 32767, 0};
    uint8_t hats[1] = {1 | 2};   // up + right
    RawView r; r.axes = axes; r.axisCount = 6; r.buttons = (1u << 1) | (1u << 9); r.buttonCount = 12; r.hats = hats; r.hatCount = 1;
    CHECK(StandardAxis(m, GamepadAxis::LeftX, r) == -32768);
    CHECK(StandardAxis(m, GamepadAxis::LeftY, r) == -32767);            // inverted 32767; flipping -32768 clamps, 32767 -> -32767
    CHECK(StandardAxis(m, GamepadAxis::TriggerLeft, r) == 32767);       // full range -> (v+32768)/2 = 32767
    CHECK(StandardAxis(m, GamepadAxis::TriggerRight, r) == 0);          // positive half of 0
    axes[4] = -32768; CHECK(StandardAxis(m, GamepadAxis::TriggerLeft, r) == 0);
    axes[4] = 0;      CHECK(StandardAxis(m, GamepadAxis::TriggerLeft, r) == 16384);
    axes[5] = 20000;  CHECK(StandardAxis(m, GamepadAxis::TriggerRight, r) == 20000);
    CHECK(StandardAxis(m, GamepadAxis::RightX, r) == 0);                // not mapped
    CHECK(StandardButton(m, GamepadButton::A, r) && !StandardButton(m, GamepadButton::B, r) && StandardButton(m, GamepadButton::Start, r));
    CHECK(StandardButton(m, GamepadButton::DpadUp, r) && !StandardButton(m, GamepadButton::DpadDown, r) && !StandardButton(m, GamepadButton::DpadLeft, r));
    r.axisCount = 2;                                                    // the pad reports fewer axes than the map names: reads 0, never out of range
    CHECK(StandardAxis(m, GamepadAxis::TriggerLeft, r) == 0 && StandardAxis(m, GamepadAxis::TriggerRight, r) == 0);
    r.buttonCount = 1; r.hatCount = 0;
    CHECK(!StandardButton(m, GamepadButton::A, r) && !StandardButton(m, GamepadButton::DpadUp, r));
    CHECK(StandardAxis(m, (GamepadAxis)9, r) == 0 && !StandardButton(m, (GamepadButton)99, r));
    // Negative half, axis as a button, button as a trigger.
    PadMap q; q.axes[(int)GamepadAxis::TriggerLeft] = Source{Kind::Axis, 0, false, Half::Negative, 0};
    q.buttons[(int)GamepadButton::X] = Source{Kind::Axis, 1, false, Half::Full, 0};
    q.axes[(int)GamepadAxis::TriggerRight] = Source{Kind::Button, 0, false, Half::Full, 0};
    int16_t a2[2] = {-30000, 20000}; RawView r2; r2.axes = a2; r2.axisCount = 2; r2.buttons = 1; r2.buttonCount = 1;
    CHECK(StandardAxis(q, GamepadAxis::TriggerLeft, r2) == 30000 && StandardButton(q, GamepadButton::X, r2) && StandardAxis(q, GamepadAxis::TriggerRight, r2) == 32767);
    a2[1] = 10000; CHECK(!StandardButton(q, GamepadButton::X, r2));
    a2[0] = -32768; CHECK(StandardAxis(q, GamepadAxis::TriggerLeft, r2) == 32767);   // never overflows

    // The generic guess.
    PadMap g = GuessMap(6, 12, 1);
    int16_t ga[6] = {100, 200, 300, 400, 32767, -32768}; uint8_t gh[1] = {4};
    RawView gr; gr.axes = ga; gr.axisCount = 6; gr.buttons = 1u << 4; gr.buttonCount = 12; gr.hats = gh; gr.hatCount = 1;
    CHECK(StandardAxis(g, GamepadAxis::LeftY, gr) == 200 && StandardAxis(g, GamepadAxis::RightY, gr) == 400);
    CHECK(StandardAxis(g, GamepadAxis::TriggerLeft, gr) == 32767 && StandardAxis(g, GamepadAxis::TriggerRight, gr) == 0);
    CHECK(StandardButton(g, GamepadButton::LeftShoulder, gr) && !StandardButton(g, GamepadButton::A, gr) && StandardButton(g, GamepadButton::DpadDown, gr));
    CHECK(GuessMap(2, 4, 0).axes[(int)GamepadAxis::RightX].kind == Kind::None && GuessMap(2, 4, 0).buttons[(int)GamepadButton::Start].kind == Kind::None);
    CHECK(GuessMap(4, 12, 0).buttons[(int)GamepadButton::DpadUp].kind == Kind::None && GuessMap(4, 12, 0).axes[(int)GamepadAxis::RightY].index == 3);
    CHECK(!GuessMap(0, 0, 0).Any() && GuessMap(2, 0, 0).Any());

    // Product names.
    CHECK(CleanProductName("  Wireless\tController \n ") == "Wireless Controller");
    CHECK(CleanProductName("").empty() && CleanProductName(" \t\n").empty());
    CHECK(CleanProductName(std::string(100, 'x')).size() == 63);
    CHECK(CleanProductName(std::string(62, 'x') + "\xC3\xA9\xC3\xA9") == std::string(62, 'x'));   // 2-byte character would straddle byte 63: dropped whole
    CHECK(FallbackPadName(0x054C, 0x9CC) == "HID game controller 054C:09CC");
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
    // Clause ranges (GCS_COMPCLAUSE: UTF-16 offsets, first 0, last = length) become code point boundaries.
    {
        Machine mc;
        mc.Start();
        const std::u16string comp = u"a\xD83D\xDE00" u"bc";            // 5 units, 4 code points
        auto e = mc.Composition(kGcsCompStr | kGcsCursorPos | kGcsCompClause, comp, 1, {0, 3, 5});
        CHECK(e.size() == 1 && e[0].clauses == std::vector<int>({0, 2, 4}) && e[0].cursor == 1);
        e = mc.Composition(kGcsCompStr | kGcsCursorPos | kGcsCompClause, comp, 1, {0, 3, 5});   // identical: nothing new
        CHECK(e.empty());
        e = mc.Composition(kGcsCompStr | kGcsCursorPos | kGcsCompClause, comp, 1, {0, 1, 5});   // clause moved: new event
        CHECK(e.size() == 1 && e[0].clauses == std::vector<int>({0, 1, 4}));
        e = mc.Composition(kGcsCompStr | kGcsCursorPos, comp, 1, {0, 1, 5});                    // flag not set: no clauses
        CHECK(e.size() == 1 && e[0].clauses.empty());
        // Bad offsets (past the end, going backwards) are clamped and kept in order.
        CHECK(ClausesFromUtf16(u"abc", {0, 9, 2}) == std::vector<int>({0, 3, 3}));
        CHECK(ClausesFromUtf16(u"abc", {}).empty());
        // The clause boundaries reach the application through Event::clauses (empty by default).
        okay::platform::Event ev;
        CHECK(ev.clauses.empty());
        ev.type = okay::platform::EventType::TextEditing; ev.clauses = ClausesFromUtf16(u"abc", {0, 1, 3});
        CHECK(ev.clauses == std::vector<int>({0, 1, 3}));
    }
    // Cancel: Escape in the IME ends the composition with an empty string and no result -> cleared once.
    {
        Machine mc;
        mc.Start();
        mc.Composition(kGcsCompStr, u"ka", 2);
        auto e = mc.Composition(kGcsCompStr, u"", 0);                                // composition string emptied
        CHECK(e.size() == 1 && e[0].text.empty() && e[0].clauses.empty());
        CHECK(mc.End().empty());
    }
    // Focus loss while composing clears the text once and ends the composition; with nothing showing it does nothing.
    {
        Machine mc;
        mc.Start();
        mc.Composition(kGcsCompStr, u"ka", 2);
        auto e = mc.FocusLost();
        CHECK(e.size() == 1 && e[0].text.empty() && !mc.Active());
        CHECK(mc.FocusLost().empty());
        CHECK(mc.End().empty());
        Machine idle;
        CHECK(idle.FocusLost().empty());
        // After focus returns a new composition starts clean.
        mc.Start();
        e = mc.Composition(kGcsCompStr, u"ka", 2);
        CHECK(e.size() == 1 && e[0].text == "ka");
    }
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


// The Raw Input / HidP side: caps -> usage layout -> pad state. Caps are written by hand the way HidP_GetButtonCaps /
// HidP_GetValueCaps describe the same descriptors the parser tests above use.
static void HidP() {
    using namespace win32hid;
    CHECK(IsXInputDevicePath("\\\\?\\HID#VID_045E&PID_028E&IG_00#8&1234&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"));
    CHECK(IsXInputDevicePath("\\\\?\\hid#vid_045e&pid_028e&ig_00#x") && !IsXInputDevicePath("\\\\?\\HID#VID_054C&PID_09CC#7&2&0&0000#{...}") && !IsXInputDevicePath(""));
    CHECK(!IsXInputDevicePath("\\\\?\\HID#VID_1234&PID_5678#BIG_00"));   // "IG_" must start a token, not end a word
    uint16_t v = 0, p = 0;
    CHECK(ParseVidPid("\\\\?\\HID#VID_054C&PID_09CC#7&2&0&0000#{x}", v, p) && v == 0x054C && p == 0x09CC);
    CHECK(ParseVidPid("\\\\?\\hid#vid_045e&pid_028e&ig_00#x", v, p) && v == 0x045E && p == 0x028E);
    CHECK(!ParseVidPid("\\\\?\\HID#VID_05&PID_09CC", v, p) && !ParseVidPid("", v, p) && !ParseVidPid("VID_ZZZZ&PID_0001", v, p));
    CHECK(SignedFromRaw(0xFF, 8, true) == -1 && SignedFromRaw(0x80, 8, true) == -128 && SignedFromRaw(0x7F, 8, true) == 127 && SignedFromRaw(0xFF, 8, false) == 255);
    CHECK(SignedFromRaw(0xFFFF, 16, true) == -1 && SignedFromRaw(0xFFFFFFFFu, 32, true) == -1 && SignedFromRaw(5, 0, true) == 5);

    // A pad: X, Y 8-bit 0..255 (logical max reported as -1 by Windows for a 0xFF written in one byte), Rz 16-bit signed,
    // a hat 1..8, a range of 12 buttons and one more button on the vendor-less page that must be ignored.
    std::vector<ButtonCap> bc;
    bc.push_back({1, kPageButton, true, 1, 12});
    bc.push_back({1, 0x0C, false, 0x40, 0x40});
    std::vector<ValueCap> vc;
    vc.push_back({1, kPageGenericDesktop, false, 0x30, 0x30, 8, 1, 0, -1});
    vc.push_back({1, kPageGenericDesktop, false, 0x31, 0x31, 8, 1, 0, 255});
    vc.push_back({1, kPageGenericDesktop, false, 0x35, 0x35, 16, 1, -32768, 32767});
    vc.push_back({1, kPageGenericDesktop, false, 0x39, 0x39, 4, 1, 1, 8});
    vc.push_back({1, 0xFF00, false, 0x01, 0x01, 8, 1, 0, 255});     // vendor page: not an axis
    const UsageLayout l = BuildLayout(bc, vc);
    CHECK(l.buttons.size() == 12 && l.buttons[0] == 1 && l.buttons[11] == 12);
    CHECK(l.axes.size() == 3 && l.axes[0].usage == 0x30 && l.axes[0].hi == 255 && !l.axes[0].isSigned);   // -1 fixed to 255
    CHECK(l.axes[2].usage == 0x35 && l.axes[2].isSigned && l.axes[2].lo == -32768 && l.axes[2].bitSize == 16);
    CHECK(l.hats.size() == 1 && l.hats[0].lo == 1 && l.hats[0].hi == 8 && l.Usable());

    PadState s = Evaluate(l, {1, 3, 12, 40}, {0, 255, (uint32_t)0x8000}, {3});
    CHECK(s.buttons == ((1u << 0) | (1u << 2) | (1u << 11)));          // usage 40 is outside the 12 buttons
    CHECK(s.axes[0] == -32768 && s.axes[1] == 32767 && s.axes[2] == -32768 /* 0x8000 as signed 16-bit = -32768 */);
    CHECK(s.hats[0] == 2);                                              // value 3 with lo 1 = index 2 = right
    s = Evaluate(l, {}, {128, 128, 0}, {0});                            // hat value 0 is outside 1..8: centred
    CHECK(s.buttons == 0 && s.hats[0] == 0 && s.axes[2] == 0 && s.axes[0] >= 0 && s.axes[0] <= 256);
    s = Evaluate(l, {}, {}, {});                                         // missing readings read as the low end / centre, never crash
    CHECK(s.buttons == 0 && s.hats[0] == 0);

    // Usage ranges of values: X..Rz as one range cap, report count equals the range; and a cap that is an array of values is skipped.
    std::vector<ValueCap> vr;
    vr.push_back({2, kPageGenericDesktop, true, 0x30, 0x32, 8, 3, 0, 255});
    vr.push_back({2, kPageGenericDesktop, false, 0x33, 0x33, 8, 4, 0, 255});   // ReportCount 4 for one usage: array, skipped
    const UsageLayout lr = BuildLayout({}, vr);
    CHECK(lr.axes.size() == 3 && lr.axes[1].usage == 0x31 && lr.axes[1].reportId == 2 && lr.Usable());
    // Limits: more than 8 axes, 32 buttons, 2 hats are cut; a hostile range cannot blow up.
    std::vector<ButtonCap> many; many.push_back({0, kPageButton, true, 1, 65535});
    std::vector<ValueCap> manyv; manyv.push_back({0, kPageGenericDesktop, true, 0x30, 0x39, 8, 10, 0, 255});
    const UsageLayout lm = BuildLayout(many, manyv);
    CHECK(lm.buttons.size() == 32 && lm.axes.size() <= 8 && lm.hats.size() <= 2);
    CHECK(BuildLayout({{0, kPageButton, true, 9, 1}}, {}).buttons.empty());   // reversed range
    CHECK(!BuildLayout({}, {}).Usable());
    // A degenerate axis (max <= min) still yields a layout and reads 0.
    std::vector<ValueCap> deg; deg.push_back({0, kPageGenericDesktop, false, 0x30, 0x30, 8, 1, 5, 5}); deg.push_back({0, kPageGenericDesktop, false, 0x31, 0x31, 8, 1, 0, 255});
    const UsageLayout ld = BuildLayout({}, deg);
    CHECK(Evaluate(ld, {}, {5, 0}, {}).axes[0] == 0);
}

static void WindowLogic() {
    CHECK(UiScaleFromDpi(96) == 1.0f && UiScaleFromDpi(120) == 1.25f && UiScaleFromDpi(144) == 1.5f && UiScaleFromDpi(192) == 2.0f);
    CHECK(UiScaleFromDpi(288) == 2.0f && UiScaleFromDpi(72) == 1.0f && UiScaleFromDpi(0) == 1.0f && UiScaleFromDpi(-4) == 1.0f);
    CHECK(UiScaleFromDpi(108) == 1.25f && UiScaleFromDpi(107) == 1.0f);   // 1.125 rounds up to 1.25, 1.115 down to 1.0
    CHECK(UiScaleFromDpi(132) == 1.5f && UiScaleFromDpi(131) == 1.25f);   // 1.375 rounds up, just under it down
    CHECK(ShouldReclaimInputFocus(2, 1, false, 0, 7, 7));
    CHECK(ShouldReclaimInputFocus(3, 0, false, 0, 7, 7));
    CHECK(!ShouldReclaimInputFocus(1, 1, false, 0, 7, 7));    // no pop-out was open
    CHECK(!ShouldReclaimInputFocus(2, 2, false, 0, 7, 7));    // still open
    CHECK(!ShouldReclaimInputFocus(2, 1, true, 0, 7, 7));     // already has the keyboard
    CHECK(!ShouldReclaimInputFocus(2, 1, false, 9, 7, 7));    // another window has it
    CHECK(!ShouldReclaimInputFocus(2, 1, false, 0, 8, 7));    // pointer elsewhere
    CHECK(!ShouldReclaimInputFocus(2, 1, false, 0, 0, 0));    // no window id
    CHECK(IsActiveForFrameRate(true, false, false) && IsActiveForFrameRate(false, true, false) && IsActiveForFrameRate(false, false, true) && !IsActiveForFrameRate(false, false, false));
    CHECK(NeedsAttentionFlash(true, true) && NeedsAttentionFlash(false, false) && NeedsAttentionFlash(true, false) && !NeedsAttentionFlash(false, true));
    CHECK(IsBackgroundWindow(false, false) && IsBackgroundWindow(true, true) && !IsBackgroundWindow(true, false));
}

// XInput hot-plug tracker and rumble timer (Win32XInput.hpp). Before this, the own Windows backend sent no event when an
// XInput pad was plugged in or pulled out after start, asked every empty slot on every call, and a rumble never stopped.
static void XInputSlots() {
    using namespace win32xi;
    SlotTracker t;
    CHECK(t.FullScanDue(0) && t.SlotsToAsk(0) == 0xFu && !t.Scanned());
    t.Update(0xF, 0x5, 10);                       // first scan: slots 0 and 2 connected, silently
    SlotChange c;
    CHECK(t.Scanned() && t.Mask() == 0x5 && t.Count() == 2 && !t.PopChange(c));
    CHECK(t.SlotOfIndex(0) == 0 && t.SlotOfIndex(1) == 2 && t.SlotOfIndex(2) == -1 && t.SlotOfIndex(-1) == -1);
    CHECK(t.IndexOfSlot(0) == 0 && t.IndexOfSlot(2) == 1 && t.IndexOfSlot(1) == -1 && t.IndexOfSlot(9) == -1);
    // Between full scans only the connected slots are asked.
    CHECK(!t.FullScanDue(500) && t.SlotsToAsk(500) == 0x5);
    t.Update(0x5, 0x4, 500);                      // slot 0 stopped answering: removed
    CHECK(t.PopChange(c) && !c.added && c.slot == 0 && c.instanceId == 1 && !t.PopChange(c));
    CHECK(t.Mask() == 0x4 && t.IndexOfSlot(2) == 0);
    // A full scan is due after a second, or at once after a device-change hint.
    CHECK(t.FullScanDue(1010) && t.SlotsToAsk(1010) == 0xFu);
    CHECK(!t.FullScanDue(600));
    t.DeviceChangeHint();
    CHECK(t.FullScanDue(600) && t.SlotsToAsk(600) == 0xFu);
    t.Update(0xF, 0xE, 600);                      // slots 1 and 3 came; 2 stays
    CHECK(t.PopChange(c) && c.added && c.slot == 1 && c.instanceId == 2);
    CHECK(t.PopChange(c) && c.added && c.slot == 3 && c.instanceId == 4 && !t.PopChange(c));
    CHECK(t.Mask() == 0xE && t.IndexOfSlot(1) == 0 && t.IndexOfSlot(3) == 2 && !t.FullScanDue(700));
    // Removals are listed before additions in one scan (slot 1 out, slot 0 in).
    t.Update(0xF, 0xD, 1700);
    CHECK(t.PopChange(c) && !c.added && c.slot == 1);
    CHECK(t.PopChange(c) && c.added && c.slot == 0 && !t.PopChange(c));
    // Answers outside the asked slots are ignored; unasked slots keep their state.
    t.Update(0x1, 0xE, 1800);
    CHECK(t.PopChange(c) && !c.added && c.slot == 0 && t.Mask() == 0xC);
    // A clock that went backwards forces a full scan instead of waiting forever.
    CHECK(t.FullScanDue(5));

    RumbleTimer r;
    uint16_t lo = 1, hi = 1;
    r.Start(0, 30000, 60000, 250, 1000, lo, hi);
    CHECK(lo == 30000 && hi == 60000 && r.Active(0));
    CHECK(r.Expired(1249) == 0 && r.Expired(1250) == 0x1 && !r.Active(0) && r.Expired(2000) == 0);   // once
    r.Start(1, 100, 0, 0, 0, lo, hi);             // zero duration: off now, nothing to stop later
    CHECK(lo == 0 && hi == 0 && !r.Active(1) && r.Expired(10) == 0);
    r.Start(2, 0, 0, 500, 0, lo, hi);             // zero strength: off
    CHECK(lo == 0 && hi == 0 && !r.Active(2));
    r.Start(3, 1, 1, 100, 0, lo, hi);
    r.Start(3, 5, 5, 400, 50, lo, hi);            // a new request replaces the old end time
    CHECK(r.Expired(100) == 0 && r.Expired(450) == 0x8);
    r.Start(1, 9, 9, 100, 0, lo, hi); r.Forget(1);
    CHECK(r.Expired(1000) == 0);
    r.Start(7, 9, 9, 100, 0, lo, hi);             // a slot that does not exist sends nothing
    CHECK(lo == 0 && hi == 0);
}

// Mouse click counting and the system-menu keys (Win32Messages.hpp). Before: every MouseButtonDown said clicks = 1 (no
// double-click on the own Windows backend), and Alt or F10 alone put the window into menu mode (the next key, e.g. Space,
// opened the system menu and the game stopped), and Alt+letter beeped.
static void Win32Msgs() {
    using namespace win32msg;
    ClickCounter c;
    CHECK(c.Down(1, 100, 100, 1000, 500, 4, 4) == 1);
    CHECK(c.Up(1) == 1);
    CHECK(c.Down(1, 101, 99, 1300, 500, 4, 4) == 2);          // inside the time and the 4 x 4 box: a double-click
    CHECK(c.Up(1) == 2);
    CHECK(c.Down(1, 102, 100, 1700, 500, 4, 4) == 3);         // triple (box and time from the last click)
    CHECK(c.Down(1, 102, 100, 2300, 500, 4, 4) == 1);         // too late
    CHECK(c.Down(1, 106, 100, 2400, 500, 4, 4) == 1);         // too far (more than half the box sideways)
    CHECK(c.Down(3, 106, 100, 2500, 500, 4, 4) == 1);         // another button starts again
    CHECK(c.Up(1) == 1);                                      // the up of a button that is not the counted one
    CHECK(c.Down(3, 106, 102, 2600, 500, 4, 4) == 2);
    // The message clock is 32 bits and wraps after 49.7 days.
    ClickCounter w;
    CHECK(w.Down(1, 0, 0, 0xFFFFFF00u, 500, 4, 4) == 1 && w.Down(1, 0, 0, 0x00000010u, 500, 4, 4) == 2);
    // A zero or silly double-click time from the system: 500 ms.
    ClickCounter z;
    CHECK(z.Down(1, 0, 0, 0, 0, 4, 4) == 1 && z.Down(1, 0, 0, 400, 0, 4, 4) == 2);

    // WM_SYSCOMMAND SC_KEYMENU (0xF100, low 4 bits are the system's): swallowed for Alt or F10 alone (lParam 0) and
    // Alt+letter; Alt+Space (lParam ' ') still opens the window menu. Other system commands pass.
    CHECK(SwallowSysCommand(0xF100, 0) && SwallowSysCommand(0xF102, 0) && SwallowSysCommand(0xF100, 'f'));
    CHECK(!SwallowSysCommand(0xF100, ' ') && !SwallowSysCommand(0xF060 /* SC_CLOSE */, 0) && !SwallowSysCommand(0xF020 /* SC_MINIMIZE */, 0));
    // WM_MENUCHAR: MNC_CLOSE in the high word (no beep for Alt+key with no menu).
    CHECK(MenuCharReply() == (1 << 16));
}

int main() {
    Display(); Icon(); Ime(); Hid(); HidP(); PadMapT(); WindowLogic(); XInputSlots(); Win32Msgs();
    std::printf("platform_win32_pure: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
