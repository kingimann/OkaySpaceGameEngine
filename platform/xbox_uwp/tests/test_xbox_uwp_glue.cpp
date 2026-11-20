// Linux test for XboxAppGlue.hpp: pointer / character / pad / life-cycle mapping over the fake host. Pure C++. UNVERIFIED on Windows.
#include "XboxAppGlue.hpp"
#include "UwpPlatform.hpp"
#include <cstdio>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
using namespace okay::xbox; using namespace okay::platform;
namespace {
struct Host : UwpHost {
    void* CoreWindowHandle() override { return nullptr; } void PumpEvents() override {} int PadCount() override { return 0; }
    PadDevice* Pad(int) override { return nullptr; } AudioHost* Audio() override { return nullptr; }
};
struct Pad : PadDevice { PadReading r; bool Read(PadReading& o) override { o = r; return true; } void SendVibration(const Vibration&) override {} };
std::vector<Event> Drain(UwpBackend& b) { std::vector<Event> v; Event e; while (b.PollEvent(e)) v.push_back(e); return v; }
int N(const std::vector<Event>& v, EventType t) { int n = 0; for (auto& e : v) if (e.type == t) ++n; return n; }
PointerInput Ptr(PointerKind k, PointerAction a, float x, float y, long long id = 1) { PointerInput p; p.kind = k; p.action = a; p.dipX = x; p.dipY = y; p.id = id; return p; }
}
int main() {
    Host host; PathRoots roots; roots.install = "C:/x/"; roots.local = "/tmp/okay_xbox_glue_local"; roots.temp = "/tmp/okay_xbox_glue_temp";
    UwpBackend b(&host, roots); b.Init(InitFlags()); b.Life().Launched(); b.Life().Resized(960, 540, 192);   // 1920 x 1080
    AppGlue g(&b); Drain(b);
    CHECK(g.ToPixels(10) == 20 && g.ToPixels(0.3f) == 1 && g.ToPixels(0) == 0);   // DIPs -> pixels at 192 dpi (2x), rounded
    { // mouse: left press, right added, left released while right held, right released, wheel
        PointerInput p = Ptr(PointerKind::Mouse, PointerAction::Pressed, 50, 20); p.left = true; g.OnPointer(p);
        p.right = true; g.OnPointer(p);
        p.action = PointerAction::Released; p.left = false; g.OnPointer(p);
        p.right = false; g.OnPointer(p);
        auto ev = Drain(b); CHECK(N(ev, EventType::MouseButtonDown) == 2 && N(ev, EventType::MouseButtonUp) == 2 && g.MouseButtonsHeld() == 0);
        int lefts = 0; for (auto& e : ev) if (e.type == EventType::MouseButtonDown && e.button == MouseLeft) { CHECK(e.x == 100 && e.y == 40); ++lefts; } CHECK(lefts == 1);
        PointerInput w = Ptr(PointerKind::Mouse, PointerAction::Wheel, 0, 0); w.wheelDelta = 240; g.OnPointer(w); w.wheelDelta = 0; g.OnPointer(w);
        ev = Drain(b); CHECK(ev.size() == 1 && ev[0].type == EventType::MouseWheel && ev[0].wheelY == 2.0f);
    }
    { // touch and pen go to the finger tracker; a tap gives FingerDown / FingerUp and one Tap gesture
        g.OnPointer(Ptr(PointerKind::Touch, PointerAction::Pressed, 480, 135, 9)); g.OnPointer(Ptr(PointerKind::Touch, PointerAction::Released, 480, 135, 9));
        auto ev = Drain(b); CHECK(N(ev, EventType::FingerDown) == 1 && N(ev, EventType::FingerUp) == 1);
        for (auto& e : ev) if (e.type == EventType::FingerDown) CHECK(e.fx == 0.5f && e.fy == 0.25f && e.fingerId == 9);
        CHECK(b.TakeGestures().size() == 1);
        g.OnPointer(Ptr(PointerKind::Pen, PointerAction::Pressed, 1, 1, 3)); CHECK(b.Touch().ActiveCount() == 1);
        g.OnPointer(Ptr(PointerKind::Pen, PointerAction::Canceled, 1, 1, 3)); CHECK(b.Touch().ActiveCount() == 0); Drain(b); b.TakeGestures();
    }
    { // focus or visibility lost releases fingers and mouse buttons
        PointerInput p = Ptr(PointerKind::Mouse, PointerAction::Pressed, 5, 5); p.left = true; g.OnPointer(p);
        g.OnPointer(Ptr(PointerKind::Touch, PointerAction::Pressed, 7, 7, 4)); Drain(b);
        g.OnActivated(false); auto ev = Drain(b);
        CHECK(N(ev, EventType::MouseButtonUp) >= 1 && N(ev, EventType::FingerUp) == 1 && g.MouseButtonsHeld() == 0 && b.Touch().ActiveCount() == 0 && !b.Life().Focused());
        g.OnActivated(true); CHECK(b.Life().Focused());
        g.OnPointer(Ptr(PointerKind::Touch, PointerAction::Pressed, 7, 7, 4)); Drain(b);
        g.OnVisibility(false); CHECK(b.Touch().ActiveCount() == 0 && b.Life().Phase() == AppPhase::Hidden); Drain(b);
        g.OnVisibility(true); CHECK(b.Life().Phase() == AppPhase::Active);
    }
    { // characters: text input must be on; control characters dropped; surrogate pairs joined; lone surrogates dropped
        g.OnCharacter('a'); CHECK(Drain(b).empty());
        b.StartTextInput(); g.OnCharacter('a'); g.OnCharacter(8); g.OnCharacter(13); g.OnCharacter(0xE9); g.OnCharacter(0xD83D); g.OnCharacter(0xDE00); g.OnCharacter(0xDE01); g.OnCharacter(0xD83D); g.OnCharacter('b');
        auto ev = Drain(b); CHECK(ev.size() == 4 && ev[0].text == "a" && ev[1].text == "\xC3\xA9" && ev[2].text == "\xF0\x9F\x98\x80" && ev[3].text == "b");
        g.OnComposition("ni", 2); g.OnComposition("ni", 2); g.OnCompositionCommit();
        ev = Drain(b); CHECK(N(ev, EventType::TextEditing) == 2 && N(ev, EventType::TextInput) == 1);
    }
    { // pads: connect by key, same key again does nothing, vanish -> Lost then Removed after the grace time, return inside it = same instance
        std::uint64_t t = 0; b.clockOverride = [&] { return t; }; b.EnableHub(2, 1000);
        Pad pa, pb; Drain(b);
        g.SyncPads({{"A", &pa}, {"B", &pb}}); g.SyncPads({{"A", &pa}, {"B", &pb}});
        auto ev = Drain(b); CHECK(N(ev, EventType::GamepadAdded) == 2 && b.Hub()->PresentCount() == 2);
        const int id = b.Hub()->InstanceId(0);
        t = 100; g.SyncPads({{"B", &pb}}); CHECK(b.Hub()->PresentCount() == 1 && Drain(b).empty());
        t = 500; g.SyncPads({{"A", &pa}, {"B", &pb}}); CHECK(b.Hub()->InstanceId(0) == id && N(Drain(b), EventType::GamepadAdded) == 1);
        t = 600; g.SyncPads({{"B", &pb}}); Drain(b);
        t = 1700; g.SyncPads({{"B", &pb}}); ev = Drain(b); CHECK(N(ev, EventType::GamepadRemoved) == 1 && ev[0].deviceId == id);
        t = 1800; g.SyncPads({}); CHECK(b.Hub()->PresentCount() == 0);
    }
    { // suspend: the save runs before the deferral may complete, fingers end; resume; back and close give a Quit
        int saved = 0; b.onSuspend = [&] { ++saved; };
        g.OnPointer(Ptr(PointerKind::Touch, PointerAction::Pressed, 7, 7, 6)); Drain(b);
        CHECK(g.OnSuspending(4500) && saved == 1 && b.Touch().ActiveCount() == 0 && b.Life().Phase() == AppPhase::Suspended);
        g.OnResuming(); CHECK(b.Life().Phase() != AppPhase::Suspended);
        g.OnBackRequested(); auto ev = Drain(b); CHECK(N(ev, EventType::Quit) >= 1);
        g.OnClosed(); CHECK(b.Life().Phase() == AppPhase::Closing);
    }
    g.OnResized(480, 270, 96); CHECK(b.Life().Size().w == 480 && b.Life().Size().h == 270);
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_glue: all passed\n"); return 0;
}
