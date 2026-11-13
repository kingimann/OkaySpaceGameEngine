// The OkayUI host layer (shared/OkayUIHost.hpp): SDL events in, OkayUI::Input out, and the hosted region that decides who owns
// the mouse. No window: the events are plain SDL_Event structs.
#include "test_framework.hpp"
#include "OkayUIHost.hpp"
#include "OkayUITestKit.hpp"

#include <cstring>

using namespace okayuihost;
namespace UI = OkayUI;

static SDL_Event Text(const char* s) { SDL_Event e; std::memset(&e, 0, sizeof(e)); e.type = SDL_TEXTINPUT; std::strncpy(e.text.text, s, sizeof(e.text.text) - 1); return e; }
static SDL_Event Key(SDL_Keycode k, Uint16 mod = 0, bool down = true, bool repeat = false) {
    SDL_Event e; std::memset(&e, 0, sizeof(e));
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP; e.key.keysym.sym = k; e.key.keysym.mod = mod; e.key.repeat = repeat ? 1 : 0;
    return e;
}
static SDL_Event Wheel(int y, Uint32 dir = SDL_MOUSEWHEEL_NORMAL) { SDL_Event e; std::memset(&e, 0, sizeof(e)); e.type = SDL_MOUSEWHEEL; e.wheel.y = y; e.wheel.direction = dir; return e; }

int main() {
    RUN_SUITE("okayui host");
    {   // text and every key edge reach the input when keys are delivered; the state is one-shot
        InputCollector c;
        c.Feed(Text("ab")); c.Feed(Text("\xC3\xA9"));
        for (SDL_Keycode k : {SDLK_BACKSPACE, SDLK_DELETE, SDLK_LEFT, SDLK_RIGHT, SDLK_HOME, SDLK_END, SDLK_RETURN, SDLK_ESCAPE}) c.Feed(Key(k));
        c.Feed(Key(SDLK_a, KMOD_CTRL));
        c.Feed(Key(SDLK_LEFT, KMOD_LSHIFT));
        UI::Input in = c.Build(10, 20, true, false, false, true);
        CHECK(in.mouseX == 10 && in.mouseY == 20 && in.mouseDown && !in.rightDown && !in.blocked);
        CHECK(in.text && !std::strcmp(in.text, "ab\xC3\xA9"));
        CHECK(in.backspace && in.del && in.left && in.right && in.home && in.end && in.enter && in.escape && in.selectAll && in.shift);
        const UI::Input again = c.Build(10, 20, false, false, false, true);
        CHECK(again.text == nullptr && !again.backspace && !again.left && !again.selectAll && !again.mouseDown);
    }
    {   // a plain 'a' is not select-all; the Command key counts like Ctrl; shift follows the latest key event
        InputCollector c;
        c.Feed(Key(SDLK_a));
        CHECK(!c.Build(0, 0, false, false, false, true).selectAll);
        c.Feed(Key(SDLK_a, KMOD_GUI));
        CHECK(c.Build(0, 0, false, false, false, true).selectAll);
        c.Feed(Key(SDLK_LSHIFT, KMOD_LSHIFT));
        CHECK(c.Build(0, 0, false, false, false, true).shift);
        c.Feed(Key(SDLK_LSHIFT, 0, false));
        CHECK(!c.Build(0, 0, false, false, false, true).shift);
    }
    {   // key repeat: each repeated KEYDOWN is another edge (OkayUI wants one per press or repeat)
        InputCollector c;
        c.Feed(Key(SDLK_BACKSPACE, 0, true, true));
        CHECK(c.Build(0, 0, false, false, false, true).backspace);
    }
    {   // a click that goes down and up between two frames is not lost: down for one frame, up for the next
        InputCollector c;
        SDL_Event d; std::memset(&d, 0, sizeof(d)); d.type = SDL_MOUSEBUTTONDOWN; d.button.button = SDL_BUTTON_LEFT;
        SDL_Event u = d; u.type = SDL_MOUSEBUTTONUP;
        c.Feed(d); c.Feed(u);
        CHECK(c.Build(5, 5, false, false, false, false).mouseDown);          // the state sampled at the frame says "up", the event says it was pressed
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
        d.button.button = SDL_BUTTON_RIGHT; c.Feed(d);
        const UI::Input r = c.Build(5, 5, false, false, false, false);
        CHECK(r.rightDown && !r.mouseDown);
        SDL_Event m = d; m.button.button = SDL_BUTTON_MIDDLE; c.Feed(m);      // other buttons are not OkayUI's
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
    }
    {   // keys are dropped, not kept, when OkayUI may not have them: nothing arrives a frame late
        InputCollector c;
        c.Feed(Text("x")); c.Feed(Key(SDLK_BACKSPACE));
        const UI::Input dropped = c.Build(0, 0, false, false, false, false);
        CHECK(dropped.text == nullptr && !dropped.backspace);
        const UI::Input next = c.Build(0, 0, false, false, false, true);
        CHECK(next.text == nullptr && !next.backspace);
    }
    {   // the wheel accumulates in a frame and is delivered even when keys are not; flipped direction inverts it
        InputCollector c;
        c.Feed(Wheel(2)); c.Feed(Wheel(1));
        CHECK(c.Build(0, 0, false, false, false, false).wheel == 3.0f);
        c.Feed(Wheel(2, SDL_MOUSEWHEEL_FLIPPED));
        CHECK(c.Build(0, 0, false, false, false, false).wheel == -2.0f);
        CHECK(c.Build(0, 0, false, false, false, false).wheel == 0.0f);
    }
    {   // a flood of text cannot overflow the buffer: what does not fit is dropped
        InputCollector c;
        for (int i = 0; i < 100; ++i) c.Feed(Text("abcdefgh"));
        const UI::Input in = c.Build(0, 0, false, false, false, true);
        CHECK(in.text && std::strlen(in.text) < 64 && std::strlen(in.text) > 0);
    }
    {   // the hosted region: owns the mouse only inside the rectangle, only while ImGui said it is hovered, and only for one more frame
        Region r;
        CHECK(!r.Owns(5, 5) && !r.Valid());
        r.Offer(100, 100, 200, 50, true);
        CHECK(r.Owns(100, 100) && r.Owns(299, 149) && !r.Owns(300, 100) && !r.Owns(99, 100) && !r.Owns(150, 150));
        r.Offer(100, 100, 200, 50, false);                     // another window covers it
        CHECK(!r.Owns(150, 120));
        r.Offer(100, 100, 200, 50, true);
        r.NextFrame();                                         // still valid the next frame...
        CHECK(r.Owns(150, 120));
        r.NextFrame();                                         // ...but a panel that was not drawn again is dropped
        CHECK(!r.Owns(150, 120) && !r.Valid());
    }
    {   // end to end through OkayUI: typed text from SDL events edits a focused field; a blocked frame does not click
        okayuitest::Reset();
        okayuitest::Sim sim(300, 100);
        char buf[32] = "";
        UI::Rect field;
        InputCollector c;
        auto frame = [&](const UI::Input& in) {
            UI::BeginFrame(in);
            UI::BareNextWindow();
            if (UI::Begin("host", 0, 0, 300, 100)) { UI::InputText("##f", buf, (int)sizeof(buf)); field = UI::GetItemRect(); }
            UI::End();
            UI::EndFrameData();
        };
        frame(c.Build(0, 0, false, false, false, false));
        const float cx = field.x + 20, cy = field.y + field.h * 0.5f;
        frame(c.Build(cx, cy, false, false, true, false));      // blocked press: no focus
        frame(c.Build(cx, cy, true, false, true, false));
        frame(c.Build(cx, cy, false, false, true, false));
        CHECK(!UI::WantsTextInput());
        frame(c.Build(cx, cy, false, false, false, false));     // a real click focuses it
        frame(c.Build(cx, cy, true, false, false, false));
        frame(c.Build(cx, cy, false, false, false, false));
        CHECK(UI::WantsTextInput());
        c.Feed(Text("hi")); c.Feed(Text("!"));
        frame(c.Build(cx, cy, false, false, false, true));
        CHECK(!std::strcmp(buf, "hi!"));
        c.Feed(Key(SDLK_BACKSPACE));
        frame(c.Build(cx, cy, false, false, false, true));
        CHECK(!std::strcmp(buf, "hi"));
        c.Feed(Key(SDLK_ESCAPE));
        frame(c.Build(cx, cy, false, false, false, true));
        CHECK(!UI::WantsTextInput());
    }
    TEST_MAIN_RESULT();
}
