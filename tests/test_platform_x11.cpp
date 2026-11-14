// The pure logic of the own X11 backend (Platform/X11Logic.hpp): Xdnd receive state machine and message layouts,
// Xft.dpi, RandR monitors -> displays, cursor glyphs, input-method preedit, message box layout.
// No X server needed; the same code against a real server is in test_platform_xdnd.cpp / test_platform_window.cpp.
#include "test_framework.hpp"
#include "Platform/X11Logic.hpp"

using namespace okay::platform;
using namespace okay::platform::x11;

namespace {

constexpr unsigned long kUri = 301, kCopy = 302, kOther = 303, kSrc = 0x400001;

xdnd::Atoms A() { xdnd::Atoms a; a.uriList = kUri; a.actionCopy = kCopy; return a; }

void TestDndMessages() {
    RUN_SUITE("xdnd messages");
    long d[5] = {(long)kSrc, (5L << 24) | 1, 11, 12, 13};
    xdnd::Enter e = xdnd::DecodeEnter(d);
    CHECK(e.source == kSrc && e.version == 5 && e.moreTypes && e.types[0] == 11 && e.types[2] == 13);
    d[1] = 3L << 24;
    e = xdnd::DecodeEnter(d);
    CHECK(e.version == 3 && !e.moreTypes);
    long p[5] = {(long)kSrc, 0, (300L << 16) | 217, 123456, (long)kCopy};
    xdnd::Position pos = xdnd::DecodePosition(p);
    CHECK(pos.rootX == 300 && pos.rootY == 217 && pos.time == 123456 && pos.action == kCopy);
    long st[5];
    xdnd::EncodeStatus(77, true, false, 0, 0, 0, 0, kCopy, st);
    CHECK(st[0] == 77 && st[1] == 1 && st[2] == 0 && st[3] == 0 && st[4] == (long)kCopy);
    xdnd::EncodeStatus(77, false, true, 10, 20, 30, 40, 0, st);
    CHECK(st[1] == 2 && st[2] == ((10L << 16) | 20) && st[3] == ((30L << 16) | 40) && st[4] == 0);
    long fin[5];
    xdnd::EncodeFinished(77, true, kCopy, fin);
    CHECK(fin[0] == 77 && fin[1] == 1 && fin[2] == (long)kCopy);
    xdnd::EncodeFinished(77, false, kCopy, fin);
    CHECK(fin[1] == 0 && fin[2] == 0);                      // a refused drop names no action
    CHECK(xdnd::NegotiatedVersion(3) == 3 && xdnd::NegotiatedVersion(5) == 5 && xdnd::NegotiatedVersion(9) == 5 && xdnd::NegotiatedVersion(0) == 0);
}

void TestDndFlow() {
    RUN_SUITE("xdnd state machine");
    xdnd::Target t(55, A(), 1000);
    CHECK(!t.Active());
    xdnd::Enter en; en.source = kSrc; en.version = 5;
    // Enter with a uri list among the types.
    auto r = t.OnEnter(en, {kOther, kUri});
    CHECK(!r.sendStatus && t.GetState() == xdnd::Target::State::Entered);
    xdnd::Position ps; ps.source = kSrc; ps.time = 999;
    r = t.OnPosition(ps, 10, 20);
    CHECK(r.sendStatus && r.to == kSrc && r.accept && r.action == kCopy);
    // A position from another window is ignored.
    ps.source = 0x999; r = t.OnPosition(ps, 1, 1);
    CHECK(!r.sendStatus);
    ps.source = kSrc;
    // Drop starts the data transfer, with the time of the drop message.
    r = t.OnDrop(kSrc, 5000, 100);
    CHECK(r.requestData && r.dataType == kUri && r.time == 5000 && t.GetState() == xdnd::Target::State::Receiving);
    // A drop from another source while receiving is ignored.
    CHECK(!t.OnDrop(0x999, 1, 100).requestData);
    // Data arrives: events and Finished.
    r = t.OnData(true, "# files\r\nfile:///tmp/a%20b.txt\r\nfile:///tmp/c.png\r\nhttp://example.com/x\r\n", 42);
    CHECK(r.sendFinished && r.finishedOk && r.to == kSrc && r.action == kCopy);
    CHECK(r.events.size() == 4);                           // DropBegin, 2 files (the http line is skipped), DropComplete
    CHECK(r.events[0].type == EventType::DropBegin && r.events[3].type == EventType::DropComplete);
    CHECK(r.events[1].type == EventType::DropFile && r.events[1].text == "/tmp/a b.txt" && r.events[2].text == "/tmp/c.png");
    CHECK(r.events[1].windowId == 55 && r.events[1].x == 10 && r.events[1].y == 20 && r.events[1].timestampMs == 42);
    CHECK(!t.Active());

    // No uri list offered: status says no, drop is finished as refused at once.
    t.OnEnter(en, {kOther});
    ps.source = kSrc;
    r = t.OnPosition(ps, 0, 0);
    CHECK(r.sendStatus && !r.accept && r.action == 0);
    r = t.OnDrop(kSrc, 1, 0);
    CHECK(r.sendFinished && !r.finishedOk && !r.requestData && !t.Active());

    // Leave ends it; a leave from a stranger does not.
    t.OnEnter(en, {kUri});
    t.OnLeave(0x777);
    CHECK(t.Active());
    t.OnLeave(kSrc);
    CHECK(!t.Active());
    CHECK(!t.OnDrop(kSrc, 1, 0).requestData);               // a drop with no Enter is ignored

    // The source refuses / sends rubbish: Finished(false), no events.
    t.OnEnter(en, {kUri}); t.OnDrop(kSrc, 1, 0);
    r = t.OnData(false, "", 0);
    CHECK(r.sendFinished && !r.finishedOk && r.events.empty());
    t.OnEnter(en, {kUri}); t.OnDrop(kSrc, 1, 0);
    r = t.OnData(true, "http://only.remote/x\n", 0);       // nothing local in it
    CHECK(r.sendFinished && !r.finishedOk && r.events.empty());
    CHECK(!t.OnData(true, "file:///x\n", 0).sendFinished);  // data nobody asked for

    // Timeout: the source never answers.
    t.OnEnter(en, {kUri}); t.OnDrop(kSrc, 1, 5000);
    CHECK(!t.OnTick(5999).sendFinished);
    r = t.OnTick(6000);
    CHECK(r.sendFinished && !r.finishedOk && !t.Active());

    // A new Enter in the middle of a transfer starts over (the old source died).
    t.OnEnter(en, {kUri}); t.OnDrop(kSrc, 1, 0);
    xdnd::Enter en2; en2.source = 0x500; en2.version = 4;
    t.OnEnter(en2, {kUri});
    CHECK(t.GetState() == xdnd::Target::State::Entered && t.Source() == 0x500);
    // Version 0 has no time stamps: the request uses CurrentTime (0).
    xdnd::Enter en0; en0.source = kSrc; en0.version = 0;
    t.OnEnter(en0, {kUri});
    CHECK(t.OnDrop(kSrc, 12345, 0).time == 0);
    // Hostile: a flood of files is capped by the list parser, not by us; an empty type list accepts nothing.
    t.OnEnter(en, {});
    CHECK(!t.OnPosition(ps, 0, 0).accept);
}

void TestScaleAndMonitors() {
    RUN_SUITE("xft dpi and monitors");
    CHECK(ParseXftDpi("Xft.dpi:\t144\nXft.antialias: 1\n") == 144);
    CHECK(ParseXftDpi("Xft.antialias: 1\nXft.dpi: 120.4\n") == 120);
    CHECK(ParseXftDpi("Xft.dpi: 100\nXft.dpi: 110\n") == 110);        // later wins
    CHECK(ParseXftDpi("Xft.dpi: 100\nXft.dpi: junk\n") == 0);         // later junk wins too: unusable
    CHECK(ParseXftDpi("") == 0 && ParseXftDpi("Xft.dpi:") == 0);
    CHECK(ParseXftDpi("Xft.dpi: 5\n") == 0 && ParseXftDpi("Xft.dpi: 5000\n") == 0);
    CHECK(ParseXftDpi("xft.dpi: 100\n") == 0);                        // names are case-sensitive
    CHECK(ParseXftDpi("Xft.dpi: 99999999999999999999\n") == 0);
    CHECK(ParseXftDpi("*Xft.dpi: 100\n") == 0);                       // loose bindings are not read
    CHECK(RefreshHzFromTiming(148500000, 2200, 1125) == 60);
    CHECK(RefreshHzFromTiming(74250000, 2200, 1125, true) == 60);     // 1080i: 30 frames, 60 fields
    CHECK(RefreshHzFromTiming(0, 2200, 1125) == 0 && RefreshHzFromTiming(1, 0, 1) == 0);
    CHECK(PhysicalDpi(3840, 596) == 164);
    CHECK(PhysicalDpi(1920, 0) == 0);

    std::vector<Monitor> m(3);
    m[0].x = 1920; m[0].width = 2560; m[0].height = 1440; m[0].name = "DP-2"; m[0].refreshHz = 144;
    m[1].x = 0; m[1].width = 1920; m[1].height = 1080; m[1].name = "HDMI-1"; m[1].primary = true; m[1].refreshHz = 60;
    m[2].x = 0; m[2].width = 0; m[2].height = 0; m[2].name = "ghost";   // disabled output: no size
    int wa[4] = {0, 28, 4480, 1412};                                    // a panel at the top of the whole desktop
    auto d = BuildDisplays(m, 120, wa);
    CHECK(d.size() == 2);
    CHECK(d[0].name == "HDMI-1" && d[0].primary && d[0].dpi == 120 && d[0].refreshHz == 60);
    CHECK(d[1].name == "DP-2" && !d[1].primary && d[1].x == 1920);
    CHECK(d[0].workY == 28 && d[0].workHeight == 1052 && d[0].workX == 0 && d[0].workWidth == 1920);
    CHECK(d[1].workY == 28 && d[1].workHeight == 1412 && d[1].workWidth == 2560);
    d = BuildDisplays(m, 0, nullptr);
    CHECK(d[0].dpi == 96 && d[0].workWidth == 1920 && d[0].workHeight == 1080);
    m[1].primary = false;                                               // none primary: the leftmost
    d = BuildDisplays(m, 0);
    CHECK(d[0].primary && d[0].name == "HDMI-1" && !d[1].primary);
    CHECK(BuildDisplays({}, 96).empty());
    int bad[4] = {5000, 5000, 100, 100};                                // work area outside every monitor: full monitor
    CHECK(BuildDisplays(m, 96, bad)[0].workWidth == 1920);
}

void TestCursorAndAttention() {
    RUN_SUITE("cursor glyphs, attention");
    CHECK(CursorGlyph(CursorShape::Arrow) == 68 && CursorGlyph(CursorShape::IBeam) == 152 && CursorGlyph(CursorShape::Wait) == 150);
    CHECK(CursorGlyph(CursorShape::Hand) == 60 && CursorGlyph(CursorShape::SizeAll) == 52 && CursorGlyph(CursorShape::NotAllowed) == 0);
    // Every shape has a glyph and all the resize shapes differ.
    CHECK(CursorGlyph(CursorShape::SizeNS) != CursorGlyph(CursorShape::SizeWE));
    CHECK(CursorGlyph(CursorShape::SizeNWSE) != CursorGlyph(CursorShape::SizeNESW));
    CHECK(WithUrgency(0x13, true) == (0x13 | 256) && WithUrgency(0x113, false) == 0x13);
    CHECK(WithUrgency(WithUrgency(0, true), true) == 256);
    long m[5];
    NetWmStateMessage(true, 900, 0, m);
    CHECK(m[0] == 1 && m[1] == 900 && m[2] == 0 && m[3] == 1);
    NetWmStateMessage(false, 900, 901, m);
    CHECK(m[0] == 0 && m[2] == 901);
}

void TestPreedit() {
    RUN_SUITE("input method preedit");
    Preedit p;
    CHECK(!p.Active());
    p.Draw(0, 0, "ni", 2);
    CHECK(p.Utf8() == "ni" && p.Caret() == 2 && p.Active());
    p.Draw(0, 2, "\xE4\xBD\xA0", 1);                                // "ni" replaced by one CJK character
    CHECK(p.Utf8() == "\xE4\xBD\xA0" && p.Length() == 1 && p.Caret() == 1);
    p.Draw(1, 0, "h", 2);                                           // insert at the end
    CHECK(p.Utf8() == "\xE4\xBD\xA0h" && p.Caret() == 2);
    p.Draw(0, 1, "", 0);                                            // delete the first character
    CHECK(p.Utf8() == "h" && p.Caret() == 0);
    // Out-of-range values are clamped, never crash.
    p.Draw(99, 99, "z", 99);
    CHECK(p.Utf8() == "hz" && p.Caret() == 2);
    p.Draw(-5, -5, "", -3);
    CHECK(p.Utf8() == "hz" && p.Caret() == 0);
    Event e = p.MakeEvent(7, 9);
    CHECK(e.type == EventType::TextEditing && e.windowId == 7 && e.text == "hz" && e.editStart == 0 && e.timestampMs == 9);
    p.Clear();
    e = p.MakeEvent(7);
    CHECK(e.text.empty() && !p.Active());
    // Bad UTF-8 becomes U+FFFD, overlong forms are refused, and the length is capped.
    p.Draw(0, 0, "a\xFF" "b", 3);
    CHECK(p.Length() == 3 && p.Utf8() == "a\xEF\xBF\xBD" "b");
    p.Clear(); p.Draw(0, 0, "\xC0\x80", 0);
    CHECK(p.Utf8() == "\xEF\xBF\xBD\xEF\xBF\xBD");
    p.Clear(); p.Draw(0, 0, std::string(5000, 'x'), 0);
    CHECK(p.Length() == (int)Preedit::kMaxChars);
    p.Clear(); p.Draw(0, 0, "\xF0\x9F\x98\x80", 1);                // 4-byte emoji is one character
    CHECK(p.Length() == 1);
    p.Clear(); p.Draw(0, 0, "\xE4\xBD", 0);                         // truncated sequence at the end
    CHECK(p.Length() == 2);
}

void TestDialog() {
    RUN_SUITE("message box layout");
    auto l = WrapText("Hello world, this is a long message", 12);
    CHECK(l.size() == 3 && l[0] == "Hello world," && l[1] == "this is a" && l[2] == "long message");
    l = WrapText("line one\nline two\n\nafter blank", 40);
    CHECK(l.size() == 4 && l[0] == "line one" && l[1] == "line two" && l[2] == "" && l[3] == "after blank");
    l = WrapText("abcdefghijklmnopqrstuvwxyz", 10);
    CHECK(l.size() == 3 && l[0] == "abcdefghij" && l[2] == "uvwxyz");
    l = WrapText("", 20);
    CHECK(l.size() == 1 && l[0].empty());
    l = WrapText("a\tb\x01" "c", 20);
    CHECK(l.size() == 1 && l[0] == "a bc");
    std::string many; for (int i = 0; i < 200; i++) many += "word\n";
    l = WrapText(many, 20, 10);
    CHECK(l.size() == 10 && l.back() == "word...");
    CHECK(WrapText("x", -5).size() == 1);                           // absurd width is clamped
    CHECK(ToLatin1("caf\xC3\xA9 \xE4\xBD\xA0") == "caf\xE9 ?");

    DialogLayout L = LayoutDialog(3, 30, 7, 14, 11, 0, 0, 1280, 720);
    CHECK(L.width == 30 * 7 + 32 && L.height == 16 + 3 * 14 + 14 + (14 + 12) + 16);
    CHECK(L.winX == (1280 - L.width) / 2 && L.winY == (720 - L.height) / 2);
    CHECK(L.btnX + L.btnW / 2 == L.width / 2);                      // button centred
    CHECK(L.InButton(L.btnX, L.btnY) && L.InButton(L.btnX + L.btnW - 1, L.btnY + L.btnH - 1));
    CHECK(!L.InButton(L.btnX - 1, L.btnY) && !L.InButton(L.btnX, L.btnY + L.btnH));
    // A narrow message still has room for the button; a screen smaller than the box keeps it at the corner.
    L = LayoutDialog(1, 2, 7, 14, 11, 0, 0, 100, 50);
    CHECK(L.width >= L.btnW + 32 && L.winX == 0 && L.winY == 0);
    CHECK(DialogKeyCloses(0xff0d) && DialogKeyCloses(0xff1b) && !DialogKeyCloses('a'));
}

} // namespace

int main() {
    TestDndMessages();
    TestDndFlow();
    TestScaleAndMonitors();
    TestCursorAndAttention();
    TestPreedit();
    TestDialog();
    TEST_MAIN_RESULT();
}
