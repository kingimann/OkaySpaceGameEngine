// The window-mode frame loop (shared/OkayUIFrameLoop.hpp), headless: a fake okay::platform::Backend that hands out scripted events and a clock, a software
// bitmap Painter, and the real OkayUI in between. Checks one frame's order of work (events, time, UI, text input, draw, present), typing into a field,
// the text-input switch of the backend following the keyboard focus, the IME rectangle, resize counting, quit, an event flood, and that nothing is drawn
// after the quit. Written from what the loop is for.
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "FakePlatformBackend.hpp"

#include <deque>
#include <string>
#include <vector>

namespace UI = OkayUI;
namespace plat = okay::platform;
using okayuitest::Rgba;

using fakeplat::FakeBackend; using fakeplat::Ev; using fakeplat::Move; using fakeplat::Btn; using fakeplat::Text;

int main() {
    RUN_SUITE("okayui window-mode frame loop");
    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(320, 200);
    FakeBackend be;
    okayuihost::platform::FrameLoop loop(be, *painter);

    int clicks = 0, uiCalls = 0; char buf[32] = ""; UI::Rect btn, field; double seenTime = -1.0;
    auto ui = [&] {
        ++uiCalls; seenTime = UI::GetTime();
        UI::BareNextWindow();
        if (UI::Begin("w", 0, 0, 320, 200)) {
            if (UI::Button("Go")) ++clicks; btn = UI::GetItemRect();
            UI::InputText("##t", buf, sizeof buf); field = UI::GetItemRect();
        }
        UI::End();
    };
    be.now = 1500;
    CHECK(loop.Frame(ui) && loop.Frame(ui));
    CHECK(uiCalls == 2 && loop.GetHost().Frames() == 2 && loop.GetHost().DisplayWidth() == 320 && loop.GetHost().DisplayHeight() == 200);
    CHECK(seenTime > 1.4 && seenTime < 1.6);                   // the backend clock is OkayUI's time
    be.now = 2500; loop.Frame(ui);
    CHECK(seenTime > 2.4 && seenTime < 2.6);
    // the picture really went out through the painter
    std::vector<std::uint8_t> px; int w = 0, h = 0;
    CHECK(painter->ReadPixels(px, w, h) && w == 320);
    int lit = 0; for (size_t i = 0; i + 3 < px.size(); i += 4) if (px[i] != 24 || px[i + 1] != 26 || px[i + 2] != 32) ++lit;
    CHECK(lit > 1500);

    // a click through the backend's events
    const int bx = (int)(btn.x + btn.w * 0.5f), by = (int)(btn.y + btn.h * 0.5f);
    be.queue.push_back(Move(bx, by)); loop.Frame(ui);
    be.queue.push_back(Btn(true, bx, by)); loop.Frame(ui);
    be.queue.push_back(Btn(false, bx, by)); loop.Frame(ui);
    CHECK(clicks == 1);

    // typing: the field takes the keyboard, the backend is told to start text input once, with the caret rectangle
    const int fx = (int)(field.x + 20), fy = (int)(field.y + field.h * 0.5f);
    be.queue.push_back(Move(fx, fy)); be.queue.push_back(Btn(true, fx, fy)); loop.Frame(ui);
    be.queue.push_back(Btn(false, fx, fy)); loop.Frame(ui);
    CHECK(loop.TextInputOn() && be.textStarts == 1 && be.textStops == 0);
    be.queue.push_back(Text("hello")); loop.Frame(ui); loop.Frame(ui);
    CHECK(std::string(buf) == "hello" && be.textStarts == 1 && be.rectCalls >= 1 && be.rw >= 1 && be.rh >= 1 && be.rx >= (int)field.x && be.ry >= (int)field.y - 2);
    // clicking away drops the focus: text input stops once
    const int ox = 300, oy = 190;
    be.queue.push_back(Move(ox, oy)); be.queue.push_back(Btn(true, ox, oy)); loop.Frame(ui);
    be.queue.push_back(Btn(false, ox, oy)); loop.Frame(ui); loop.Frame(ui);
    CHECK(!loop.TextInputOn() && be.textStops == 1 && be.textStarts == 1);
    be.queue.push_back(Text("zzz")); loop.Frame(ui);
    CHECK(std::string(buf) == "hello");

    // a resize event is counted
    const int r0 = loop.Resizes();
    be.queue.push_back(Ev(plat::EventType::WindowResized)); loop.Frame(ui);
    CHECK(loop.Resizes() == r0 + 1);

    // a flood: 10000 events do not stall (4096 per frame) and none is lost
    for (int i = 0; i < 10000; ++i) be.queue.push_back(Move(i % 300, i % 190));
    int frames = 0; while (!be.queue.empty() && frames < 10) { loop.Frame(ui); ++frames; }
    CHECK(be.queue.empty() && frames == 3);
    // hostile events: unknown types, absurd coordinates and a huge text do not break a frame
    plat::Event bad = Move(-2000000000, 2000000000); be.queue.push_back(bad);
    be.queue.push_back(Text(std::string(100000, 'x').c_str())); be.queue.push_back(Ev(plat::EventType::NoEvent));
    CHECK(loop.Frame(ui));

    // quit: no frame is drawn after it, and the loop keeps saying no
    const int framesBefore = loop.GetHost().Frames(), callsBefore = uiCalls;
    be.queue.push_back(Ev(plat::EventType::WindowClose));
    CHECK(!loop.Frame(ui) && !loop.Frame(ui));
    CHECK(loop.GetHost().Frames() == framesBefore && uiCalls == callsBefore);
    TEST_MAIN_RESULT();
}
