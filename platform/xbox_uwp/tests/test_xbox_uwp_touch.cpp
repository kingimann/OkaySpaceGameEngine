// Linux test for XboxTouch.hpp (touch tracker, tap / drag thresholds, text composition) and its wiring into UwpBackend over a fake host.
#include "XboxTouch.hpp"
#include "XboxUwpBackend.hpp"
#include "UwpPlatform.hpp"
#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
using namespace okay::xbox;
using K = TouchEvent::Kind;

static int Count(const std::vector<TouchEvent>& v, K k) { int n = 0; for (auto& e : v) if (e.kind == k) ++n; return n; }

static void TrackerTests() {
    { TouchTracker t; auto d = t.Feed(7, PointerPhase::Down, 100, 100, 1000); CHECK(d.size() == 1 && d[0].kind == K::RawDown && d[0].primary && t.PrimaryId() == 7);
      auto u = t.Feed(7, PointerPhase::Up, 103, 102, 1200); CHECK(Count(u, K::Tap) == 1 && Count(u, K::RawUp) == 1 && t.ActiveCount() == 0 && t.PrimaryId() == -1); }
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); auto u = t.Feed(1, PointerPhase::Up, 0, 0, 301); CHECK(Count(u, K::Tap) == 0); }   // too slow
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); auto u = t.Feed(1, PointerPhase::Up, 0, 0, 300); CHECK(Count(u, K::Tap) == 1); }    // exactly on the limit
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); auto m = t.Feed(1, PointerPhase::Move, 10, 0, 10); CHECK(Count(m, K::DragStart) == 0 && Count(m, K::RawMove) == 1);   // 10 px is inside the slop
      m = t.Feed(1, PointerPhase::Move, 11, 0, 20); CHECK(Count(m, K::DragStart) == 1);
      m = t.Feed(1, PointerPhase::Move, 30, 5, 30); CHECK(Count(m, K::DragMove) == 1 && Count(m, K::DragStart) == 0);
      m = t.Feed(1, PointerPhase::Move, 30, 5, 31); CHECK(m.empty());                                                                      // no movement, no event
      auto u = t.Feed(1, PointerPhase::Up, 30, 5, 100); CHECK(Count(u, K::DragEnd) == 1 && Count(u, K::Tap) == 0 && u[1].startX == 0 && u[1].x == 30); }
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); t.Feed(1, PointerPhase::Move, 50, 0, 5); auto c = t.Feed(1, PointerPhase::Cancel, 50, 0, 6); CHECK(Count(c, K::DragCancel) == 1 && Count(c, K::RawCancel) == 1 && Count(c, K::DragEnd) == 0); }
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); auto c = t.Feed(1, PointerPhase::Cancel, 0, 0, 5); CHECK(Count(c, K::Tap) == 0 && Count(c, K::RawCancel) == 1); }
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); t.Feed(2, PointerPhase::Down, 50, 50, 5);   // two fingers: no taps, no drags; the older one stays primary
      CHECK(t.PrimaryId() == 1 && t.ActiveCount() == 2);
      auto m = t.Feed(1, PointerPhase::Move, 90, 0, 6); CHECK(Count(m, K::DragStart) == 0);
      auto u = t.Feed(2, PointerPhase::Up, 50, 50, 8); CHECK(Count(u, K::Tap) == 0 && t.PrimaryId() == 1);
      u = t.Feed(1, PointerPhase::Up, 90, 0, 9); CHECK(Count(u, K::Tap) == 0 && Count(u, K::DragEnd) == 0); }
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); t.Feed(2, PointerPhase::Down, 5, 5, 1); t.Feed(1, PointerPhase::Up, 0, 0, 2); CHECK(t.PrimaryId() == 2); }   // primary passes on
    { TouchTracker t; CHECK(t.Feed(9, PointerPhase::Move, 1, 1, 0).empty() && t.Feed(9, PointerPhase::Up, 1, 1, 0).empty()); }                        // unknown id
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); auto r = t.Feed(1, PointerPhase::Down, 5, 5, 3); CHECK(Count(r, K::RawCancel) == 1 && Count(r, K::RawDown) == 1 && t.ActiveCount() == 1); }   // repeated Down restarts
    { TouchTracker t; t.Feed(1, PointerPhase::Down, 0, 0, 0); t.Feed(2, PointerPhase::Down, 5, 5, 1); auto c = t.CancelAll(); CHECK(Count(c, K::RawCancel) == 2 && t.ActiveCount() == 0); }
    { TouchConfig c; c.tapSlopPx = 2; c.dragSlopPx = 2; c.tapMaxMs = 50; TouchTracker t(c); t.Feed(1, PointerPhase::Down, 0, 0, 0); CHECK(Count(t.Feed(1, PointerPhase::Move, 3, 0, 1), K::DragStart) == 1); }
}

static void CompositionTests() {
    TextComposition c;
    CHECK(!c.Active() && c.Update("ni", 2) && c.Active() && c.Text() == "ni" && c.Caret() == 2);
    CHECK(!c.Update("ni", 2));                                              // no change
    CHECK(c.Update("\xE4\xBD\xA0", 99) && c.Caret() == 1);                  // one code point, caret clamped
    CHECK(c.Update("ab", -4) && c.Caret() == 0);
    CHECK(c.Commit() == "ab" && !c.Active() && c.Text().empty());
    c.Update("x", 1); CHECK(c.Commit("final") == "final");
    c.Update("x", 1); CHECK(c.Update("", 0) && !c.Active()); CHECK(!c.Cancel());
    CHECK(TextComposition::CodePoints("a\xC3\xA9\xE2\x82\xAC") == 3);
}

namespace {
struct Host : UwpHost {
    int touch = 0;
    void* CoreWindowHandle() override { return this; }
    void PumpEvents() override {}
    int PadCount() override { return 0; }
    PadDevice* Pad(int) override { return nullptr; }
    AudioHost* Audio() override { return nullptr; }
    int TouchDevices() override { return touch; }
};
std::vector<okay::platform::Event> Drain(UwpBackend& b) { std::vector<okay::platform::Event> v; okay::platform::Event e; while (b.PollEvent(e)) v.push_back(e); return v; }
int CountT(const std::vector<okay::platform::Event>& v, okay::platform::EventType t) { int n = 0; for (auto& e : v) if (e.type == t) ++n; return n; }
}

static void BackendTouchTests() {
    using namespace okay::platform;
    Host host; PathRoots roots; roots.install = "C:/x/"; roots.local = "/tmp/okay_xbox_touch_local"; roots.temp = "/tmp/okay_xbox_touch_temp";
    UwpBackend b(&host, roots); b.Init(InitFlags()); b.Life().Launched(); b.Life().Resized(960, 540, 192);   // 1920 x 1080
    Drain(b);
    CHECK(b.TouchDeviceCount() == 0); host.touch = 2; CHECK(b.TouchDeviceCount() == 2);
    // a tap: finger events with 0..1 coordinates, the mouse follows the primary finger, one Tap gesture
    // PushTouch takes DIPs (CoreWindow PointerPoint.Position); the window is at 192 DPI (2x), so 480 x 135 DIPs is pixel 960 x 270
    b.PushTouch(5, PointerPhase::Down, 480.0f, 135.0f); b.PushTouch(5, PointerPhase::Up, 480.0f, 135.0f);
    { auto ev = Drain(b); CHECK(CountT(ev, EventType::FingerDown) == 1 && CountT(ev, EventType::FingerUp) == 1 && CountT(ev, EventType::MouseButtonDown) == 1 && CountT(ev, EventType::MouseButtonUp) == 1);
      for (auto& e : ev) if (e.type == EventType::FingerDown) CHECK(e.fingerId == 5 && e.fx == 0.5f && e.fy == 0.25f);
      for (auto& e : ev) if (e.type == EventType::MouseButtonDown) CHECK(e.button == MouseLeft && e.x == 960 && e.y == 270);
      auto g = b.TakeGestures(); CHECK(g.size() == 1 && g[0].kind == K::Tap && b.TakeGestures().empty()); }
    // a drag: motion events, the mouse moves, the left button is held until the finger lifts
    b.PushTouch(1, PointerPhase::Down, 50.0f, 50.0f); b.PushTouch(1, PointerPhase::Move, 70.0f, 50.0f);
    { std::uint32_t m = b.MouseState(nullptr, nullptr); CHECK(m & MouseMask(MouseLeft)); auto ev = Drain(b); CHECK(CountT(ev, EventType::FingerMotion) == 1 && CountT(ev, EventType::MouseMove) >= 1); }
    b.PushTouch(1, PointerPhase::Up, 70.0f, 50.0f); Drain(b); CHECK(!(b.MouseState(nullptr, nullptr) & MouseMask(MouseLeft)));
    { auto g = b.TakeGestures(); CHECK(g.size() == 2 && g[0].kind == K::DragStart && g[1].kind == K::DragEnd); }
    // a second finger does not touch the mouse button
    b.PushTouch(1, PointerPhase::Down, 5.0f, 5.0f); b.PushTouch(2, PointerPhase::Down, 250.0f, 250.0f); Drain(b);
    b.PushTouch(2, PointerPhase::Up, 250.0f, 250.0f); { auto ev = Drain(b); CHECK(CountT(ev, EventType::FingerUp) == 1 && CountT(ev, EventType::MouseButtonUp) == 0); }
    // losing focus cancels the held finger and releases the button
    b.CancelTouches(); { auto ev = Drain(b); CHECK(CountT(ev, EventType::FingerUp) == 1 && CountT(ev, EventType::MouseButtonUp) == 1); } CHECK(b.Touch().ActiveCount() == 0);
    b.TakeGestures();

    // DIPs to pixels at 2x, including a fractional DIP and a point outside the window (pointer capture): the mouse and the finger
    // get pixels, fx / fy are pixel / window pixels
    b.PushTouch(8, PointerPhase::Down, 100.25f, 33.5f);
    { auto ev = Drain(b); for (auto& e : ev) { if (e.type == EventType::FingerDown) CHECK(e.x == 201 && e.y == 67 && e.fx == 201.0f / 1920.0f && e.fy == 67.0f / 1080.0f);
                                               if (e.type == EventType::MouseButtonDown) CHECK(e.x == 201 && e.y == 67); } }
    b.PushTouch(8, PointerPhase::Move, -10.0f, 600.0f); { auto ev = Drain(b); for (auto& e : ev) if (e.type == EventType::FingerMotion) CHECK(e.x == -20 && e.y == 1200); }
    b.PushTouch(8, PointerPhase::Up, -10.0f, 600.0f); Drain(b); b.TakeGestures();

    // the main finger changes: 1 down (presses the mouse), 2 down, 1 up (releases it; 2 becomes the tracker's primary), 2 moves, 2 up.
    // Mouse downs and ups must pair, no up without a held button, and the mouse must not move with no button held.
    b.PushTouch(1, PointerPhase::Down, 10.0f, 10.0f); b.PushTouch(2, PointerPhase::Down, 200.0f, 200.0f); b.PushTouch(1, PointerPhase::Up, 10.0f, 10.0f);
    b.PushTouch(2, PointerPhase::Move, 260.0f, 260.0f); b.PushTouch(2, PointerPhase::Up, 260.0f, 260.0f);
    { auto ev = Drain(b); int held = 0, downs = 0, ups = 0, strayUps = 0, freeMoves = 0;
      for (auto& e : ev) {
          if (e.type == EventType::MouseButtonDown) { ++downs; ++held; }
          else if (e.type == EventType::MouseButtonUp) { ++ups; if (held == 0) ++strayUps; else --held; }
          else if (e.type == EventType::MouseMove && held == 0 && downs > 0) ++freeMoves;
      }
      CHECK(downs == 1 && ups == 1 && strayUps == 0 && freeMoves == 0 && CountT(ev, EventType::FingerUp) == 2); }
    CHECK(!(b.MouseState(nullptr, nullptr) & MouseMask(MouseLeft)));
    // after every finger lifted, the next finger takes the mouse again
    b.PushTouch(3, PointerPhase::Down, 20.0f, 20.0f); b.PushTouch(3, PointerPhase::Up, 20.0f, 20.0f);
    { auto ev = Drain(b); CHECK(CountT(ev, EventType::MouseButtonDown) == 1 && CountT(ev, EventType::MouseButtonUp) == 1); }
    b.TakeGestures();

    // suspend with a finger down and a composition active: the finger is cancelled (button released), the composition is cleared
    b.StartTextInput(); b.PushComposition("ab", 2); b.PushTouch(4, PointerPhase::Down, 30.0f, 30.0f); Drain(b);
    CHECK(b.MouseState(nullptr, nullptr) & MouseMask(MouseLeft));
    b.Life().Suspending(1000, 5000); b.ServiceLifecycle();
    { auto ev = Drain(b); CHECK(CountT(ev, EventType::FingerUp) == 1 && CountT(ev, EventType::MouseButtonUp) == 1 && CountT(ev, EventType::WindowFocusLost) == 1);
      int cleared = 0; for (auto& e : ev) if (e.type == EventType::TextEditing && e.text.empty()) ++cleared; CHECK(cleared == 1); }
    CHECK(!(b.MouseState(nullptr, nullptr) & MouseMask(MouseLeft)) && b.Touch().ActiveCount() == 0);
    b.PushCompositionCommit(); CHECK(Drain(b).empty());   // nothing left to commit after resume
    b.Life().Resuming(); b.ServiceLifecycle(); Drain(b); b.StopTextInput(); Drain(b); b.TakeGestures();

    // text composition: nothing unless text input is on; TextEditing while composing; commit gives TextInput
    b.PushComposition("ni", 2); CHECK(Drain(b).empty());
    b.StartTextInput(); b.SetTextInputRect(10, 20, 300, 40); CHECK(b.TextInputRect()[0] == 10 && b.TextInputRect()[3] == 40);
    b.PushComposition("ni", 2); b.PushComposition("ni", 2); b.PushComposition("\xE4\xBD\xA0", 1);
    { auto ev = Drain(b); CHECK(ev.size() == 2 && ev[0].type == EventType::TextEditing && ev[0].text == "ni" && ev[0].x == 2 && ev[1].text == "\xE4\xBD\xA0" && ev[1].x == 1); }
    b.PushCompositionCommit(); { auto ev = Drain(b); CHECK(ev.size() == 2 && ev[0].type == EventType::TextEditing && ev[0].text.empty() && ev[1].type == EventType::TextInput && ev[1].text == "\xE4\xBD\xA0"); }
    b.PushCompositionCommit(); CHECK(Drain(b).empty());                                  // nothing composed, nothing committed
    b.PushComposition("zz", 2); Drain(b); b.StopTextInput(); { auto ev = Drain(b); CHECK(ev.size() == 1 && ev[0].type == EventType::TextEditing && ev[0].text.empty()); }   // stopping clears the composition
    b.PushComposition("q", 1); CHECK(Drain(b).empty());
}

int main() {
    TrackerTests(); CompositionTests(); BackendTouchTests();
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_touch: all passed\n");
    return 0;
}
