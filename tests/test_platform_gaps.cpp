// Pure logic added for the SDL2 phase-out: controller mapping database, event queue, clipboard / drop
// helpers, DPI and multi-monitor maths. No display, no OS: runs anywhere.
#include "test_framework.hpp"
#include "Platform/PadMapping.hpp"
#include "Platform/EventQueue.hpp"
#include "Platform/Exchange.hpp"
#include "Platform/ScaleMath.hpp"

using namespace okay::platform;

namespace {

const char* kXbox = "030000005e0400008e02000014010000,X360 Controller,a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,"
                    "guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,"
                    "righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Linux,";

void TestPadParse() {
    RUN_SUITE("pad mapping parse");
    pad::Mapping m;
    CHECK(pad::ParseMappingLine(kXbox, m));
    CHECK(m.name == "X360 Controller");
    CHECK(m.platform == "Linux");
    CHECK(m.button[(int)GamepadButton::A].kind == pad::Binding::Button && m.button[0].index == 0);
    CHECK(m.button[(int)GamepadButton::DpadUp].kind == pad::Binding::Hat && m.button[(int)GamepadButton::DpadUp].hatMask == 1);
    CHECK(m.axis[(int)GamepadAxis::TriggerLeft].kind == pad::Binding::Axis && m.axis[4].index == 2);
    CHECK(pad::GuidToString(m.guid) == "030000005e0400008e02000014010000");
    CHECK(!pad::ParseMappingLine("# comment", m));
    CHECK(!pad::ParseMappingLine("", m));
    CHECK(!pad::ParseMappingLine("nothex,Name,a:b0", m));
    CHECK(!pad::ParseMappingLine("030000005e0400008e02000014010000,,a:b0", m));        // no name
    CHECK(!pad::ParseMappingLine("030000005e0400008e02000014010000", m));              // no fields
    CHECK(!pad::ParseMappingLine("030000005e0400008e0200001401000,Short,a:b0", m));    // 31 digits
    // Modifiers and hostile values.
    pad::Binding b;
    CHECK(pad::ParseBinding("+a2", b) && b.half == 1 && !b.invert);
    CHECK(pad::ParseBinding("-a1~", b) && b.half == -1 && b.invert);
    CHECK(pad::ParseBinding("a4~", b) && b.invert && b.half == 0);
    CHECK(pad::ParseBinding("h3.8", b) && b.index == 3 && b.hatMask == 8);
    CHECK(!pad::ParseBinding("h0", b));
    CHECK(!pad::ParseBinding("h0.0", b));
    CHECK(!pad::ParseBinding("h0.16", b));
    CHECK(!pad::ParseBinding("b256", b));
    CHECK(!pad::ParseBinding("b99999999999", b));
    CHECK(!pad::ParseBinding("x3", b));
    CHECK(!pad::ParseBinding("b", b));
    CHECK(!pad::ParseBinding("b1z", b));
    CHECK(!pad::ParseBinding("+", b));
    // A bad value leaves only that control unmapped.
    CHECK(pad::ParseMappingLine("030000005e0400008e02000014010000,Pad,a:bX,b:b1,misc1:b9,platform:Windows", m));
    CHECK(!m.button[0].Valid() && m.button[1].Valid() && m.platform == "Windows");
}

void TestPadApply() {
    RUN_SUITE("pad mapping apply");
    pad::Mapping m; pad::ParseMappingLine(kXbox, m);
    pad::RawState r; r.axes.assign(6, 0); r.buttons.assign(11, 0); r.hats.assign(1, 0);
    pad::PadState s = pad::Apply(m, r);
    CHECK(s.axis[(int)GamepadAxis::LeftX] == 0);
    CHECK(s.axis[(int)GamepadAxis::TriggerLeft] == 16383);   // plain axis at rest 0 -> middle of 0..32767 (raw triggers rest at -32768 on real pads)
    r.axes[2] = -32768; r.axes[5] = 32767; r.axes[0] = 32767; r.axes[1] = -32768;
    s = pad::Apply(m, r);
    CHECK(s.axis[(int)GamepadAxis::TriggerLeft] == 0);
    CHECK(s.axis[(int)GamepadAxis::TriggerRight] == 32767);
    CHECK(s.axis[(int)GamepadAxis::LeftX] == 32767 && s.axis[(int)GamepadAxis::LeftY] == -32768);
    r.buttons[0] = 1; r.buttons[7] = 1; r.hats[0] = 1 | 2;
    s = pad::Apply(m, r);
    CHECK(s.button[(int)GamepadButton::A] && s.button[(int)GamepadButton::Start] && !s.button[(int)GamepadButton::B]);
    CHECK(s.button[(int)GamepadButton::DpadUp] && s.button[(int)GamepadButton::DpadRight] && !s.button[(int)GamepadButton::DpadDown]);
    // Half axes and inversion.
    pad::Mapping h; pad::ParseMappingLine("00000000000000000000000000000001,Half,lefttrigger:+a0,righttrigger:-a0,lefty:a1~,leftx:b3", h);
    r.axes = {16384, 10000}; r.buttons.assign(4, 0);
    s = pad::Apply(h, r);
    CHECK(s.axis[4] == 16383 || s.axis[4] == 16384);       // +a0 at half travel
    CHECK(s.axis[5] == 0);                                  // -a0: positive travel gives nothing
    CHECK(s.axis[1] == -10001);                             // inverted
    CHECK(s.axis[0] == -32768);                             // button-fed stick, not pressed
    r.axes[0] = -32768; r.buttons[3] = 1;
    s = pad::Apply(h, r);
    CHECK(s.axis[5] == 32767 && s.axis[4] == 0 && s.axis[0] == 32767);
    // Inverting -32768 does not overflow.
    pad::Mapping iv; pad::ParseMappingLine("00000000000000000000000000000002,Inv,a:a0~", iv);
    r.axes = {-32768};
    CHECK(pad::Apply(iv, r).button[0]);
    // A mapping that points past the device reads as idle, never out of range.
    pad::Mapping far; pad::ParseMappingLine("00000000000000000000000000000003,Far,a:b200,leftx:a200,dpup:h50.1,lefttrigger:a99", far);
    r.axes.clear(); r.buttons.clear(); r.hats.clear();
    s = pad::Apply(far, r);
    CHECK(!s.button[0] && !s.button[11] && s.axis[0] == 0 && s.axis[4] == 0);
}

void TestPadDb() {
    RUN_SUITE("pad mapping db");
    pad::MappingDb db;
    std::string text = std::string("# header\r\n") + kXbox + "\r\n" +
        "030000005e0400008e02000014010000,X360 Win,a:b0,platform:Windows,\n"
        "garbage line\n"
        "0300ffff6e0400008e02000014010000,Generic,a:b1,\n";
    CHECK(db.AddText(text) == 3);
    CHECK(db.Size() == 3);
    pad::Guid g; pad::ParseGuid("030000005e0400008e02000014010000", g);
    const pad::Mapping* m = db.Find(g, "Linux");
    CHECK(m && m->name == "X360 Controller");
    m = db.Find(g, "Windows");
    CHECK(m && m->name == "X360 Win");
    m = db.Find(g, "Mac OS X");   // no entry for the platform: no match (a platform-less one would match)
    CHECK(m == nullptr);
    // Same device, different name checksum (bytes 2-3).
    pad::Guid g2 = g; g2.b[2] = 0x12; g2.b[3] = 0x34;
    m = db.Find(g2, "Linux");
    CHECK(m && m->name == "X360 Controller");
    pad::Guid other; pad::ParseGuid("03000000deadbeef00000000000000ff", other);
    CHECK(db.Find(other, "Linux") == nullptr);
    // Platform-less entry matches everywhere; a later line replaces an earlier one.
    pad::Guid ga; pad::ParseGuid("0300ffff6e0400008e02000014010000", ga);
    CHECK(db.Find(ga, "Linux") && db.Find(ga, "Windows"));
    db.AddText("0300ffff6e0400008e02000014010000,Generic2,a:b2\n");
    CHECK(db.Size() == 3 && db.Find(ga, "Linux")->name == "Generic2");
    CHECK(db.AddText("a\nb\nc\n", 2) == 0);
    // Linux GUID builder at the SDL2 positions.
    const pad::Guid lg = pad::MakeLinuxGuid(3, 0x045e, 0x028e, 0x0114);
    CHECK(pad::GuidToString(lg) == "030000005e0400008e02000014010000");
    CHECK(pad::GuidSameDevice(lg, pad::MakeLinuxGuid(3, 0x045e, 0x028e, 0x0114, 0xabcd)));
    CHECK(!pad::GuidSameDevice(lg, pad::MakeLinuxGuid(3, 0x045e, 0x028f, 0x0114)));
}

Event Mv(uint32_t w, int x, int y, int dx, int dy) { Event e; e.type = EventType::MouseMove; e.windowId = w; e.x = x; e.y = y; e.dx = dx; e.dy = dy; return e; }
Event Ev(EventType t, uint32_t w = 1) { Event e; e.type = t; e.windowId = w; return e; }

void TestQueue() {
    RUN_SUITE("event queue");
    EventQueue q(8);
    q.Push(Mv(1, 1, 1, 1, 1)); q.Push(Mv(1, 3, 4, 2, 3)); q.Push(Mv(2, 9, 9, 1, 1));
    CHECK(q.Size() == 2);
    Event e;
    CHECK(q.Poll(e) && e.x == 3 && e.y == 4 && e.dx == 3 && e.dy == 4);
    CHECK(q.Poll(e) && e.windowId == 2);
    CHECK(!q.Poll(e));
    // A click between two moves is not reordered or merged over.
    q.Push(Mv(1, 1, 1, 1, 0)); q.Push(Ev(EventType::MouseButtonDown)); q.Push(Mv(1, 2, 1, 1, 0));
    CHECK(q.Size() == 3);
    q.Clear();
    Event w1 = Ev(EventType::MouseWheel); w1.wheelY = 1; Event w2 = w1; w2.wheelY = 2;
    q.Push(w1); q.Push(w2);
    CHECK(q.Size() == 1 && q.Poll(e) && e.wheelY == 3);
    Event w3 = w1; w3.wheelFlipped = true;
    q.Push(w1); q.Push(w3);
    CHECK(q.Size() == 2);
    q.Clear();
    Event r1 = Ev(EventType::WindowResized); r1.width = 100; r1.height = 100; Event r2 = r1; r2.width = 200;
    q.Push(r1); q.Push(r2);
    CHECK(q.Size() == 1 && q.Poll(e) && e.width == 200);
    // Full queue: keys alternate with moves so nothing merges; the oldest move goes first.
    q.Clear();
    for (int i = 0; i < 4; i++) { q.Push(Mv(1, i, i, 1, 1)); q.Push(Ev(EventType::KeyDown)); }
    CHECK(q.Size() == 8);
    q.Push(Ev(EventType::KeyUp));
    CHECK(q.Size() == 8 && q.Dropped() == 1);
    CHECK(q.Poll(e) && e.type == EventType::KeyDown);   // the first move was dropped
    // All keys, no move to drop: the new event is dropped and counted; Quit is queued once there is room.
    EventQueue k(4);
    for (int i = 0; i < 6; i++) k.Push(Ev(EventType::KeyDown));
    CHECK(k.Size() == 4 && k.Dropped() == 2);
    CHECK(k.Capacity() == 4 && EventQueue(0).Capacity() == 2);
    // Destroying a window removes its events but keeps Quit.
    EventQueue d;
    d.Push(Ev(EventType::KeyDown, 1)); d.Push(Ev(EventType::KeyDown, 2)); d.Push(Ev(EventType::Quit, 1));
    d.DropWindow(1);
    CHECK(d.Size() == 2);
    // Bound holds under a flood.
    EventQueue f(64);
    for (int i = 0; i < 100000; i++) { f.Push(Ev(i % 2 ? EventType::KeyDown : EventType::KeyUp)); if (i % 3 == 0) f.Push(Mv(1, i, i, 1, 1)); }
    CHECK(f.Size() <= 64);
}

void TestExchange() {
    RUN_SUITE("clipboard and drop");
    MemoryClipboardProvider c;
    CHECK(c.GetText().empty() && !c.Has(kMimeText) && c.ChangeCount() == 0);
    CHECK(c.SetText("h\xC3\xA9llo"));
    CHECK(c.GetText() == "h\xC3\xA9llo" && c.Has(kMimeText) && c.ChangeCount() == 1);
    CHECK(!c.SetText("bad\xFF"));
    CHECK(c.GetText() == "h\xC3\xA9llo" && c.ChangeCount() == 1);   // a refused write changes nothing
    CHECK(c.Set(kMimeUriList, {'x'}));
    CHECK(!c.Has(kMimeText) && c.Formats().size() == 1);
    CHECK(!c.Set("", {'x'}));
    CHECK(!c.Set("a/b", std::vector<uint8_t>(MemoryClipboardProvider::kMaxBytes + 1)));
    c.Clear(); c.Clear();
    CHECK(c.ChangeCount() == 3 && c.Formats().empty());
    CHECK(IsValidUtf8("") && IsValidUtf8("\xF0\x9F\x9A\x80") && IsValidUtf8("\xE2\x82\xAC"));
    CHECK(!IsValidUtf8("\xC0\x80") && !IsValidUtf8("\xED\xA0\x80") && !IsValidUtf8("\xF4\x90\x80\x80") && !IsValidUtf8("\xE2\x82") && !IsValidUtf8("\x80"));
    // URIs.
    CHECK(FileUriToPath("file:///home/a%20b.txt") == "/home/a b.txt");
    CHECK(FileUriToPath("file://localhost/tmp/x") == "/tmp/x");
    CHECK(FileUriToPath("file:///C:/Users/me/a.png") == "C:/Users/me/a.png");
    CHECK(FileUriToPath("file:///tmp/caf%C3%A9") == "/tmp/caf\xC3\xA9");
    CHECK(FileUriToPath("file:///tmp/a?x=1#f") == "/tmp/a");
    CHECK(FileUriToPath("http://example.com/a").empty());
    CHECK(FileUriToPath("file://otherhost/a").empty());
    CHECK(FileUriToPath("file:///a%zz").empty());
    CHECK(FileUriToPath("file:///a%2").empty());
    CHECK(FileUriToPath("file:///a%00b").empty());
    CHECK(FileUriToPath("file://").empty() && FileUriToPath("").empty());
    const auto l = ParseUriList("# comment\r\nfile:///a/b\r\n\r\nhttp://x/y\r\nfile:///c%20d\n");
    CHECK(l.size() == 2 && l[0] == "/a/b" && l[1] == "/c d");
    CHECK(ParseUriList("file:///a\nfile:///b\nfile:///c\n", 2).size() == 2);
    // Drop events.
    CHECK(MakeDropEvents({}, 1).empty());
    const auto ev = MakeDropEvents(l, 7, 55);
    CHECK(ev.size() == 4 && ev[0].type == EventType::DropBegin && ev[1].type == EventType::DropFile && ev[1].text == "/a/b" &&
          ev[2].text == "/c d" && ev[3].type == EventType::DropComplete && ev[3].text.empty() && ev[2].windowId == 7);
}

void TestScale() {
    RUN_SUITE("dpi and monitors");
    CHECK_NEAR(scale::FromDpi(96), 1.0f, 1e-6);
    CHECK_NEAR(scale::FromDpi(144), 1.5f, 1e-6);
    CHECK_NEAR(scale::FromDpi(0), 1.0f, 1e-6);
    CHECK_NEAR(scale::Snap(1.30f), 1.25f, 1e-6);
    CHECK_NEAR(scale::Snap(1.375f), 1.5f, 1e-6);
    CHECK_NEAR(scale::Snap(100.0f), 4.0f, 1e-6);
    CHECK_NEAR(scale::Snap(0.0f), 0.5f, 1e-6);
    CHECK_NEAR(scale::Snap(std::nanf("")), 1.0f, 1e-6);
    CHECK_NEAR(scale::Snap(1.0f / 0.0f), 1.0f, 1e-6);
    CHECK(scale::ToPhysical(1, 1.5f) == 2 && scale::ToPhysical(-1, 1.5f) == -2 && scale::ToPhysical(100, 1.25f) == 125);
    CHECK(scale::ToLogical(125, 1.25f) == 100 && scale::ToLogical(7, 0.0f) == 7);
    CHECK_NEAR(scale::FramebufferScale(800, 600, 1600, 1200), 2.0f, 1e-6);
    CHECK_NEAR(scale::FramebufferScale(0, 600, 0, 900), 1.5f, 1e-6);
    CHECK_NEAR(scale::FramebufferScale(0, 0, 10, 10), 1.0f, 1e-6);
    CHECK(scale::FontPixels(13, 1.5f) == 20 && scale::FontPixels(2, 1.0f) == 6);
    CHECK(scale::RescaleWindowExtent(800, 1.0f, 1.5f) == 1200 && scale::RescaleWindowExtent(800, 0, 1.5f) == 800);
    std::vector<DisplayInfo> d(2);
    d[0].x = 0; d[0].y = 0; d[0].width = 1920; d[0].height = 1080; d[0].workX = 0; d[0].workY = 0; d[0].workWidth = 1920; d[0].workHeight = 1040;
    d[1].x = 1920; d[1].y = 0; d[1].width = 2560; d[1].height = 1440; d[1].workX = 1920; d[1].workY = 0; d[1].workWidth = 2560; d[1].workHeight = 1400;
    CHECK(scale::DisplayForRect(d, 100, 100, 800, 600) == 0);
    CHECK(scale::DisplayForRect(d, 1500, 100, 800, 600) == 0);          // 420 px on the first, 380 on the second
    CHECK(scale::DisplayForRect(d, 1700, 100, 800, 600) == 1);          // 220 on the first, 580 on the second
    CHECK(scale::DisplayForRect(d, 9000, 100, 100, 100) == 1);          // off every monitor: nearest
    CHECK(scale::DisplayForRect(d, -3000, 0, 100, 100) == 0);
    CHECK(scale::DisplayForRect({}, 0, 0, 1, 1) == -1);
    CHECK(scale::DisplayForPoint(d, 1919, 5) == 0 && scale::DisplayForPoint(d, 1920, 5) == 1 && scale::DisplayForPoint(d, 5000, 5) == -1);
    int x = 1300, y = 700, w = 800, h = 600;                             // mostly on monitor 0, sticks out right and under the taskbar
    CHECK(scale::FitToWorkArea(d, x, y, w, h) && x + w <= 1920 && y + h <= 1040 && x == 1120 && y == 440);
    x = 9000; y = 9000; w = 5000; h = 5000;                              // a monitor that is gone, bigger than any
    CHECK(scale::FitToWorkArea(d, x, y, w, h) && w == 2560 && h == 1400 && x == 1920 && y == 0);
    std::vector<DisplayInfo> none; x = 5; w = 5;
    CHECK(!scale::FitToWorkArea(none, x, y, w, h) && x == 5);
}

} // namespace

int main() {
    TestPadParse();
    TestPadApply();
    TestPadDb();
    TestQueue();
    TestExchange();
    TestScale();
    TEST_MAIN_RESULT();
}
