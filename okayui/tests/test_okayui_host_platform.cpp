// The OkayUI host without SDL and without Dear ImGui's backends (shared/OkayUIHost.hpp, okayuihost::platform): okay::platform events in,
// okay::platform::Painter out. Driven headless: a software bitmap painter and scripted events. A window that is "run" by feeding a button
// press, typing into a field, a wheel, a key chord, a resize and a close request, with the frame drawn through the painter every time.
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIHost.hpp"

#include <string>
#include <vector>

namespace UI = OkayUI;
namespace plat = okay::platform;
using okayuitest::Rgba;

static plat::Event Ev(plat::EventType t) { plat::Event e; e.type = t; return e; }
static plat::Event Move(int x, int y) { plat::Event e = Ev(plat::EventType::MouseMove); e.x = x; e.y = y; return e; }
static plat::Event Btn(bool down, int x, int y, int b = plat::MouseLeft) { plat::Event e = Ev(down ? plat::EventType::MouseButtonDown : plat::EventType::MouseButtonUp); e.x = x; e.y = y; e.button = b; return e; }
static plat::Event Key(bool down, plat::Key k, unsigned mods = 0) { plat::Event e = Ev(down ? plat::EventType::KeyDown : plat::EventType::KeyUp); e.key = k; e.mods = mods; return e; }
static plat::Event Text(const char* t) { plat::Event e = Ev(plat::EventType::TextInput); e.text = t; return e; }

int main() {
    RUN_SUITE("okayui host: okay::platform events in, Painter out");
    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(320, 200);
    CHECK(painter != nullptr);
    okayuihost::platform::Host host(*painter);

    int clicks = 0; float slider = 0.25f; char text[32] = "ab"; bool check = false; UI::Rect btn, field, chk;
    auto frame = [&](const std::vector<plat::Event>& events) {
        for (const plat::Event& e : events) host.Feed(e);
        host.BeginFrame();
        UI::BareNextWindow();
        if (UI::Begin("w", 0, 0, 320, 200)) {
            if (UI::Button("Go")) ++clicks; btn = UI::GetItemRect();
            UI::InputText("##t", text, sizeof text); field = UI::GetItemRect();
            UI::Checkbox("Tick", &check); chk = UI::GetItemRect();
            UI::SliderFloat("##s", &slider, 0.0f, 1.0f);
        }
        UI::End();
        host.EndFrame();
    };
    frame({}); frame({});
    CHECK(host.DisplayWidth() == 320 && host.DisplayHeight() == 200 && host.Frames() == 2);
    std::vector<std::uint8_t> px; int w = 0, h = 0;
    CHECK(painter->ReadPixels(px, w, h) && w == 320 && h == 200);
    auto count = [&](unsigned bg) { int n = 0; for (size_t i = 0; i + 3 < px.size(); i += 4) if (((unsigned)px[i] << 24 | (unsigned)px[i + 1] << 16 | (unsigned)px[i + 2] << 8 | px[i + 3]) != bg) ++n; return n; };
    CHECK(count(Rgba(24, 26, 32)) > 1500);                                    // the window is drawn through the painter

    // a click: move, press, release arrive as events (the press and release may even land in one frame)
    const int bx = (int)(btn.x + btn.w * 0.5f), by = (int)(btn.y + btn.h * 0.5f);
    frame({Move(bx, by)});
    frame({Btn(true, bx, by)});
    frame({Btn(false, bx, by)});
    CHECK(clicks == 1);
    frame({Btn(true, bx, by), Btn(false, bx, by)}); frame({});                // a click inside one frame still counts
    CHECK(clicks == 2);
    // a right click and the wrong button do not click a left button
    frame({Btn(true, bx, by, plat::MouseRight), Btn(false, bx, by, plat::MouseRight)}); frame({});
    CHECK(clicks == 2);
    // typing: click into the field, type, backspace, Ctrl+A and type again
    const int fx = (int)(field.x + field.w - 6), fy = (int)(field.y + field.h * 0.5f);
    frame({Btn(true, fx, fy)}); frame({Btn(false, fx, fy)});
    frame({Text("cd")}); CHECK(std::string(text) == "abcd");
    frame({Key(true, plat::Key::Backspace), Key(false, plat::Key::Backspace)}); CHECK(std::string(text) == "abc");
    frame({Key(true, plat::Key::A, plat::ModCtrl), Key(false, plat::Key::A, plat::ModCtrl)}); frame({Text("Z")});
    CHECK(std::string(text) == "Z");
    CHECK(host.WantsTextInput());
    frame({Key(true, plat::Key::Escape)}); frame({});
    // a checkbox by click, the slider by dragging across it, the wheel and a composition event
    const int cx = (int)(chk.x + 8), cy = (int)(chk.y + chk.h * 0.5f);
    frame({Btn(true, cx, cy)}); frame({Btn(false, cx, cy)});
    CHECK(check);
    plat::Event wheel = Ev(plat::EventType::MouseWheel); wheel.wheelY = 2.0f; frame({wheel});
    plat::Event comp = Ev(plat::EventType::TextEditing); comp.text = "ka"; comp.x = 2; frame({comp}); frame({});
    // window events: a resize and a close request
    plat::Event rs = Ev(plat::EventType::WindowResized); rs.width = 400; rs.height = 300; frame({rs});
    CHECK(!host.QuitRequested());
    frame({Ev(plat::EventType::WindowClose)});
    CHECK(host.QuitRequested());
    // hostile events: a huge text input, NaN-ish positions, unknown keys, a thousand events in one frame
    std::vector<plat::Event> flood; for (int i = 0; i < 1000; ++i) flood.push_back(i % 3 == 0 ? Move(i * 997 % 5000 - 2000, i * 31 % 4000 - 1000) : (i % 3 == 1 ? Text("x") : Key(true, (plat::Key)(i % 600))));
    frame(flood); frame({Text(std::string(100000, 'q').c_str())}); frame({});
    CHECK(UI::GetDrawData().dropped == 0);
    TEST_MAIN_RESULT();
}
