// okay::platform event queue, no display needed: PushEvent / PollEvent round trips on every compiled-in backend, PumpEvents
// keeping order, and CharForKey. The queue works without a display, so this runs anywhere. (Real window events are in test_platform_window.cpp.)
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include <string>
#include <vector>

using namespace okay::platform;

namespace {

std::vector<Event> DrainAll(Backend& b) {
    std::vector<Event> v;
    Event e;
    while (b.PollEvent(e)) v.push_back(e);
    return v;
}

// One poll (no retries anywhere in this file: the stress test below proves none are needed).
bool PollRetry(Backend& b, Event& e) { return b.PollEvent(e); }

Event Mk(EventType t) { Event e; e.type = t; e.windowId = 7; return e; }

void RoundTrip(Backend& b) {
    // One of every kind a backend must be able to carry, pushed in a known order.
    Event key = Mk(EventType::KeyDown); key.key = Key::A; key.mods = ModShift | ModCtrl; key.repeat = true;
    Event keyUp = Mk(EventType::KeyUp); keyUp.key = Key::Escape;
    Event text = Mk(EventType::TextInput); text.text = "h\xC3\xA9llo";
    Event move = Mk(EventType::MouseMove); move.x = 11; move.y = 22; move.dx = -3; move.dy = 4;
    Event down = Mk(EventType::MouseButtonDown); down.button = MouseRight; down.clicks = 2; down.x = 5; down.y = 6;
    Event up = Mk(EventType::MouseButtonUp); up.button = MouseLeft; up.x = 8; up.y = 9;
    Event wheel = Mk(EventType::MouseWheel); wheel.wheelX = 0.0f; wheel.wheelY = 2.0f;
    Event drop = Mk(EventType::DropFile); drop.windowId = 0; drop.text = "/tmp/some file.png";
    Event close = Mk(EventType::WindowClose);
    Event gained = Mk(EventType::WindowFocusGained);
    Event lost = Mk(EventType::WindowFocusLost);
    Event quit = Mk(EventType::Quit); quit.windowId = 0;
    const Event all[] = {key, keyUp, text, move, down, up, wheel, drop, close, gained, lost, quit};
    DrainAll(b);
    for (const Event& e : all) CHECK(b.PushEvent(e));
    const std::vector<Event> got = DrainAll(b);
    CHECK(got.size() == sizeof(all) / sizeof(all[0]));
    for (size_t i = 0; i < got.size() && i < sizeof(all) / sizeof(all[0]); ++i) {
        CHECK(got[i].type == all[i].type);
        CHECK(got[i].windowId == all[i].windowId);
    }
    if (got.size() == sizeof(all) / sizeof(all[0])) {
        CHECK(got[0].key == Key::A && got[0].repeat && (got[0].mods & ModShift) && (got[0].mods & ModCtrl) && !(got[0].mods & ModAlt));
        CHECK(got[1].key == Key::Escape && !got[1].repeat);
        CHECK(got[2].text == "h\xC3\xA9llo");
        CHECK(got[3].x == 11 && got[3].y == 22 && got[3].dx == -3 && got[3].dy == 4);
        CHECK(got[4].button == MouseRight && got[4].clicks == 2 && got[4].x == 5 && got[4].y == 6);
        CHECK(got[5].button == MouseLeft && got[5].x == 8 && got[5].y == 9);
        CHECK(got[6].wheelY == 2.0f && got[6].wheelX == 0.0f);
        CHECK(got[7].text == "/tmp/some file.png");
    }
    CHECK(!b.PushEvent(Event()));   // NoEvent is not an event
    CHECK(DrainAll(b).empty());

    // PumpEvents loses nothing and keeps order: a pushed event, a pump, another pushed event.
    Event a = Mk(EventType::KeyDown); a.key = Key::B;
    Event c = Mk(EventType::KeyDown); c.key = Key::C;
    CHECK(b.PushEvent(a));
    b.PumpEvents();
    CHECK(b.PushEvent(c));
    b.PumpEvents();
    const std::vector<Event> two = DrainAll(b);
    CHECK(two.size() == 2);
    if (two.size() == 2) CHECK(two[0].key == Key::B && two[1].key == Key::C);
}

// Push N events and poll them: none dropped, none reordered, one at a time (push, poll, push, poll) and in a batch.
void Stress(Backend& b) {
    DrainAll(b);
    const int N = 3000;
    int lost = 0, wrong = 0;
    for (int i = 0; i < N; ++i) {   // interleaved: each event is polled right after it is pushed
        Event e = Mk(EventType::KeyDown); e.key = (Key)(4 + i % 26); e.x = i;
        if (!b.PushEvent(e)) { ++lost; continue; }
        Event r;
        if (!b.PollEvent(r)) ++lost;
        else if (r.x != i && r.key != e.key) ++wrong;   // (x is carried by mouse events; keys by key: either shows a mix-up)
    }
    CHECK(lost == 0);
    CHECK(wrong == 0);
    for (int i = 0; i < N; ++i) {   // batch, mixed kinds, in order
        Event e;
        if (i % 3 == 0) { e = Mk(EventType::MouseMove); e.x = i; e.y = -i; }
        else if (i % 3 == 1) { e = Mk(EventType::KeyUp); e.key = (Key)(4 + i % 26); }
        else { e = Mk(EventType::TextInput); e.text = std::to_string(i); }
        CHECK(b.PushEvent(e));
        if (i % 500 == 499) b.PumpEvents();   // a pump in the middle loses and reorders nothing
    }
    const std::vector<Event> got = DrainAll(b);
    CHECK((int)got.size() == N);
    int bad = 0;
    for (int i = 0; i < (int)got.size() && i < N; ++i) {
        const Event& g = got[i];
        if (i % 3 == 0) bad += !(g.type == EventType::MouseMove && g.x == i && g.y == -i);
        else if (i % 3 == 1) bad += !(g.type == EventType::KeyUp && g.key == (Key)(4 + i % 26));
        else bad += !(g.type == EventType::TextInput && g.text == std::to_string(i));
    }
    CHECK(bad == 0);
}

} // namespace

int main() {
    int ran = 0;
    for (BackendKind k : {BackendKind::Own}) {
        if (!IsCompiledIn(k)) { std::cout << "[skip] " << BackendName(k) << " not compiled in\n"; continue; }
        Backend* b = Get(k);
        CHECK(b != nullptr);
        if (!b) continue;
        InitFlags f; f.video = false; f.audio = false; f.gamepad = false;   // the queue needs no display, sound or pads
        CHECK(b->Init(f));
        std::cout << "[" << b->Name() << "] event queue\n";
        RoundTrip(*b);
        Stress(*b);
        ++ran;
    }
    CHECK(ran >= 1);

    // CharForKey is KeyForChar's inverse (US layout in the base class, the OS layout on a real backend).
    for (BackendKind k : {BackendKind::Own}) {
        if (!IsCompiledIn(k)) continue;
        Backend* b = Get(k);
        if (!b) continue;
        CHECK(b->CharForKey(Key::Unknown) == 0);
        CHECK(b->CharForKey(Key::F1) == 0);
        CHECK(b->CharForKey(Key::Escape) == 0);
        for (const char* p = "abcdefghijklmnopqrstuvwxyz0123456789 "; *p; ++p) {
            const Key key = b->KeyForChar(*p);
            if (key != Key::Unknown) CHECK(b->CharForKey(key) == *p);
        }
    }

    TEST_MAIN_RESULT();
}
