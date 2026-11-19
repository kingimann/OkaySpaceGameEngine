// Draw-list channels (ChannelsSplit / ChannelsSetCurrent / ChannelsMerge) of the window list, headless: draw order across channels, clip
// rectangles kept per channel, a split left open is merged by End, bad calls change nothing, a random run keeps the picture identical to
// the same shapes drawn in channel order. Written from the behaviour of the calls (what the visual-script editor and the dock host
// need: background under nodes, tab icons over tabs).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cstdint>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
constexpr UI::Color32 kRed = 0xff0000ffu, kGreen = 0xff00ff00u, kBlue = 0xffff0000u;   // 0xAABBGGRR
struct Px { int r, g, b; };
Px At(const UI::soft::Bitmap& b, int x, int y) { const size_t i = ((size_t)y * (size_t)b.w + (size_t)x) * 4; return Px{b.rgba[i], b.rgba[i + 1], b.rgba[i + 2]}; }
bool Is(Px p, int r, int g, int b) { return p.r == r && p.g == g && p.b == b; }

void Win(Sim& s, const std::function<void(const UI::DrawList&)>& body) {
    s.Frame([&] {
        UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{300, 200}, UI::Cond_Always);
        UI::Begin("w", nullptr, UI::Window_NoTitleBar | UI::Window_NoResize | UI::Window_NoBringToFront);
        body(UI::GetWindowDrawList());
        UI::End();
    });
}
}

int main() {
    RUN_SUITE("okayui draw-list channels");
    {   // no channels: later shapes are on top
        Reset(); Sim s(320, 240);
        Win(s, [](const UI::DrawList& d) { d.AddRectFilled({20, 20}, {80, 80}, kRed); d.AddRectFilled({50, 50}, {110, 110}, kGreen); });
        const auto& b = s.Draw(0, 0, 0);
        CHECK(Is(At(b, 65, 65), 0, 255, 0) && Is(At(b, 30, 30), 255, 0, 0));
    }
    {   // red on channel 1 drawn FIRST, green on channel 0 after: red ends on top
        Reset(); Sim s(320, 240);
        Win(s, [](const UI::DrawList& d) {
            CHECK(d._Splitter._Count == 1);
            d.ChannelsSplit(2);
            CHECK(d._Splitter._Count == 2 && d._Splitter._Current == 0);
            d.ChannelsSetCurrent(1); CHECK(d._Splitter._Current == 1);
            d.AddRectFilled({20, 20}, {80, 80}, kRed);
            d.ChannelsSetCurrent(0);
            d.AddRectFilled({50, 50}, {110, 110}, kGreen);
            d.ChannelsMerge();
            CHECK(d._Splitter._Count == 1 && d._Splitter._Current == 0);
            d.AddRectFilled({100, 100}, {130, 130}, kBlue);   // after the merge: normal order again
        });
        const auto& b = s.Draw(0, 0, 0);
        CHECK(Is(At(b, 65, 65), 255, 0, 0) && Is(At(b, 100 + 5, 105), 0, 0, 255) && Is(At(b, 95, 95), 0, 255, 0));
    }
    {   // three channels, drawn in the order 2, 0, 1; merged as 0, 1, 2
        Reset(); Sim s(320, 240);
        Win(s, [](const UI::DrawList& d) {
            d.ChannelsSplit(3);
            d.ChannelsSetCurrent(2); d.AddRectFilled({10, 10}, {60, 60}, kBlue);
            d.ChannelsSetCurrent(0); d.AddRectFilled({10, 10}, {60, 60}, kRed);
            d.ChannelsSetCurrent(1); d.AddRectFilled({10, 10}, {60, 60}, kGreen);
            d.ChannelsSetCurrent(2); d.AddRectFilled({30, 30}, {90, 90}, kRed);   // more on channel 2 after leaving it
            d.ChannelsMerge();
        });
        const auto& b = s.Draw(0, 0, 0);
        CHECK(Is(At(b, 20, 20), 0, 0, 255) && Is(At(b, 70, 70), 255, 0, 0) && Is(At(b, 45, 45), 255, 0, 0));
    }
    {   // a clip rectangle pushed on one channel stays on that channel only
        Reset(); Sim s(320, 240);
        Win(s, [](const UI::DrawList& d) {
            d.ChannelsSplit(2);
            d.ChannelsSetCurrent(1);
            d.PushClipRect({0, 0}, {50, 200});
            d.AddRectFilled({20, 20}, {120, 60}, kRed);          // clipped at x = 50
            d.ChannelsSetCurrent(0);
            d.AddRectFilled({20, 100}, {120, 140}, kGreen);      // not clipped
            d.ChannelsSetCurrent(1);
            d.AddRectFilled({20, 150}, {120, 190}, kBlue);       // still clipped (the push belongs to channel 1)
            d.PopClipRect();
            d.ChannelsMerge();
        });
        const auto& b = s.Draw(0, 0, 0);
        const Px bg = At(b, 290, 190);   // the window's own background
        CHECK(Is(At(b, 30, 30), 255, 0, 0) && Is(At(b, 90, 30), bg.r, bg.g, bg.b) && Is(At(b, 90, 120), 0, 255, 0) && Is(At(b, 30, 170), 0, 0, 255) && Is(At(b, 90, 170), bg.r, bg.g, bg.b));
    }
    {   // a split left open is merged when the window ends; the order still holds
        Reset(); Sim s(320, 240);
        Win(s, [](const UI::DrawList& d) {
            d.ChannelsSplit(2);
            d.ChannelsSetCurrent(1); d.AddRectFilled({20, 20}, {80, 80}, kRed);
            d.ChannelsSetCurrent(0); d.AddRectFilled({50, 50}, {110, 110}, kGreen);
        });
        const auto& b = s.Draw(0, 0, 0);
        CHECK(Is(At(b, 65, 65), 255, 0, 0));
        Win(s, [](const UI::DrawList& d) { CHECK(d._Splitter._Count == 1); });   // and the next frame starts clean
    }
    {   // bad calls do nothing
        Reset(); Sim s(320, 240);
        Win(s, [](const UI::DrawList& d) {
            d.ChannelsSetCurrent(1); d.ChannelsMerge();                     // no split yet
            d.ChannelsSplit(1); d.ChannelsSplit(9); d.ChannelsSplit(-3);     // refused
            CHECK(d._Splitter._Count == 1);
            d.ChannelsSplit(2); d.ChannelsSplit(4);                          // the second is refused
            CHECK(d._Splitter._Count == 2);
            d.ChannelsSetCurrent(-1); d.ChannelsSetCurrent(2); d.ChannelsSetCurrent(1); d.ChannelsSetCurrent(1);
            d.AddRectFilled({20, 20}, {80, 80}, kRed);
            d.ChannelsMerge(); d.ChannelsMerge();
            UI::GetForegroundDrawList().ChannelsSplit(2);                   // the foreground list has no channels
            CHECK(UI::GetForegroundDrawList()._Splitter._Count == 1);
        });
        CHECK(Is(At(s.Draw(0, 0, 0), 30, 30), 255, 0, 0));
    }
    {   // random: the picture equals the same shapes drawn by channel, in channel order
        unsigned seed = 7u; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (int round = 0; round < 40; ++round) {
            struct Shape { int ch; float x, y; UI::Color32 c; };
            std::vector<Shape> shapes;
            const int nch = 2 + (int)(rnd() % 4u), n = 3 + (int)(rnd() % 10u);
            const UI::Color32 cols[] = {kRed, kGreen, kBlue, 0xff00ffffu, 0xffff00ffu, 0xffffff00u};
            for (int i = 0; i < n; ++i) shapes.push_back(Shape{(int)(rnd() % (unsigned)nch), (float)(rnd() % 150u), (float)(rnd() % 100u), cols[rnd() % 6u]});
            Reset(); Sim a(320, 240), c(320, 240);
            Win(a, [&](const UI::DrawList& d) {
                d.ChannelsSplit(nch);
                for (const Shape& sh : shapes) { d.ChannelsSetCurrent(sh.ch); d.AddRectFilled({sh.x, sh.y}, {sh.x + 60, sh.y + 60}, sh.c); }
                d.ChannelsMerge();
            });
            std::vector<std::uint8_t> pa = a.Draw(0, 0, 0).rgba;
            Reset();
            Win(c, [&](const UI::DrawList& d) { for (int ch = 0; ch < nch; ++ch) for (const Shape& sh : shapes) if (sh.ch == ch) d.AddRectFilled({sh.x, sh.y}, {sh.x + 60, sh.y + 60}, sh.c); });
            CHECK(pa == c.Draw(0, 0, 0).rgba);
        }
    }
    TEST_MAIN_RESULT();
}
