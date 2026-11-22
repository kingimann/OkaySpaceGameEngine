// okay::platform event queue, no display needed: PushEvent / PollEvent round trips on every compiled-in backend, PumpEvents
// keeping order, the sdl2 backend's `native` pointer, unmapped events, render resets and CharForKey. SDL's own queue works
// without a video driver, so this runs anywhere. (Real window events are in test_platform_window.cpp.)
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include <string>
#include <vector>
#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#define OKAY_TEST_HAVE_SDL 1
#endif
#endif

using namespace okay::platform;

namespace {

std::vector<Event> DrainAll(Backend& b) {
    std::vector<Event> v;
    Event e;
    while (b.PollEvent(e)) v.push_back(e);
    return v;
}

// One poll. (SDL 2.30.0's own SDL_PollEvent misses an event pushed just after the queue emptied; the sdl2 backend avoids that,
// and the stress test below proves it: no retries anywhere in this file.)
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
    for (BackendKind k : {BackendKind::Sdl2, BackendKind::Own}) {
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
    for (BackendKind k : {BackendKind::Sdl2, BackendKind::Own}) {
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

#ifdef OKAY_TEST_HAVE_SDL
    if (IsCompiledIn(BackendKind::Sdl2)) {
        Backend* b = Get(BackendKind::Sdl2);
        std::cout << "[sdl2] native pointer, unmapped events, render resets, flipped wheel\n";
        DrainAll(*b);
        SDL_Event e;
        // A mapped event carries the SDL record it came from.
        SDL_zero(e); e.type = SDL_KEYDOWN; e.key.keysym.scancode = SDL_SCANCODE_Q; e.key.keysym.sym = SDLK_q; e.key.windowID = 3;
        CHECK(SDL_PushEvent(&e) == 1);
        Event r;
        CHECK(PollRetry(*b, r));
        CHECK(r.type == EventType::KeyDown && r.key == Key::Q);
        const SDL_Event* n = (const SDL_Event*)r.native;
        CHECK(n != nullptr);
        if (n) CHECK(n->type == SDL_KEYDOWN && n->key.keysym.sym == SDLK_q && n->key.windowID == 3);

        // An event the seam has no type for is dropped by default ...
        SDL_zero(e); e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_ENTER; e.window.windowID = 3;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(!b->PollEvent(r));
        // ... and returned as Other, with `native`, once asked for. (Dear ImGui's backend needs ENTER and LEAVE.)
        b->ReportUnmappedEvents(true);
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(PollRetry(*b, r));
        CHECK(r.type == EventType::Other);
        n = (const SDL_Event*)r.native;
        CHECK(n && n->type == SDL_WINDOWEVENT && n->window.event == SDL_WINDOWEVENT_ENTER && n->window.windowID == 3);
        b->ReportUnmappedEvents(false);

        // Render device resets (the D3D11 device lost) have their own types.
        SDL_zero(e); e.type = SDL_RENDER_TARGETS_RESET;
        CHECK(SDL_PushEvent(&e) == 1);
        SDL_zero(e); e.type = SDL_RENDER_DEVICE_RESET;
        CHECK(SDL_PushEvent(&e) == 1);
        std::vector<Event> rs;
        rs = DrainAll(*b);
        CHECK(rs.size() == 2);
        if (rs.size() == 2) CHECK(rs[0].type == EventType::RenderTargetsReset && rs[1].type == EventType::RenderDeviceReset);

        // A flipped (natural scrolling) wheel keeps its meaning through PushEvent and back; the raw SDL value is recoverable.
        SDL_zero(e); e.type = SDL_MOUSEWHEEL; e.wheel.y = 3; e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        CHECK(SDL_PushEvent(&e) == 1);
        CHECK(PollRetry(*b, r));
        CHECK(r.type == EventType::MouseWheel && r.wheelFlipped && r.wheelY == -3.0f);
        Event again = r;
        CHECK(b->PushEvent(again));
        CHECK(PollRetry(*b, r));
        CHECK(r.wheelFlipped && r.wheelY == -3.0f);
        n = (const SDL_Event*)r.native;
        CHECK(n && n->wheel.y == 3 && n->wheel.direction == SDL_MOUSEWHEEL_FLIPPED);

        // A dropped file's path survives, and the SDL string is freed (run under ASan / valgrind to see a leak).
        Event drop; drop.type = EventType::DropFile; drop.text = "/a/b c.okayscene";
        CHECK(b->PushEvent(drop));
        CHECK(PollRetry(*b, r));
        CHECK(r.type == EventType::DropFile && r.text == "/a/b c.okayscene");
        // Text longer than SDL's 32-byte text event is cut, not overrun.
        Event longText; longText.type = EventType::TextInput; longText.text = std::string(80, 'x');
        CHECK(b->PushEvent(longText));
        CHECK(PollRetry(*b, r));
        CHECK(r.type == EventType::TextInput && r.text.size() == 31);
    }
#endif
    TEST_MAIN_RESULT();
}
