// The ImGui-style helper calls in OkayUICompat.hpp: viewport, colour conversion, theme colours, content extents, scroll-to-cursor,
// window size constraints and the accepted-and-ignored calls. Headless, through the same frame driver as the other OkayUI tests.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace okayuitest;
namespace UI = OkayUI;

int main() {
    RUN_SUITE("okayui compat");
    {   // viewport: the display size the host gave, centred
        Reset();
        Sim sim(640, 480);
        UI::Viewport vp;
        sim.Frame([&] { vp = UI::GetMainViewport(); });
        CHECK(vp.pos.x == 0 && vp.pos.y == 0 && vp.size.x == 640 && vp.size.y == 480);
        CHECK(vp.GetCenter().x == 320 && vp.GetCenter().y == 240);
    }
    {   // colour conversion round trips and clamps
        const UI::Color32 c = UI::RGBA(10, 128, 255, 64);
        const UI::Vec4 v = UI::ColorConvertU32ToFloat4(c);
        CHECK_NEAR(v.x, 10 / 255.0f, 1e-6); CHECK_NEAR(v.y, 128 / 255.0f, 1e-6); CHECK_NEAR(v.z, 1.0f, 1e-6); CHECK_NEAR(v.w, 64 / 255.0f, 1e-6);
        CHECK(UI::ColorConvertFloat4ToU32(v) == c);
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        CHECK(UI::ColorConvertFloat4ToU32(UI::Vec4{-1.0f, 2.0f, nan, inf}) == UI::RGBA(0, 255, 0, 0));   // clamped, NaN and infinity are 0
        for (unsigned r = 0; r < 256; r += 51) for (unsigned g = 0; g < 256; g += 51) for (unsigned b = 0; b < 256; b += 51) {
            float h, s, val, rr, gg, bb;
            UI::ColorConvertRGBtoHSV(r / 255.0f, g / 255.0f, b / 255.0f, h, s, val);
            UI::ColorConvertHSVtoRGB(h, s, val, rr, gg, bb);
            CHECK_NEAR(rr, r / 255.0f, 1e-4); CHECK_NEAR(gg, g / 255.0f, 1e-4); CHECK_NEAR(bb, b / 255.0f, 1e-4);
        }
        float r, g, b;
        UI::ColorConvertHSVtoRGB(1.25f, 1.0f, 1.0f, r, g, b);          // a hue past 1 wraps: 0.25 is yellow-green
        float r2, g2, b2; UI::ColorConvertHSVtoRGB(0.25f, 1.0f, 1.0f, r2, g2, b2);
        CHECK(r == r2 && g == g2 && b == b2);
        UI::ColorConvertHSVtoRGB(nan, nan, nan, r, g, b);
        CHECK(r == 0 && g == 0 && b == 0);
    }
    {   // theme colours as floats
        Reset();
        const UI::Theme& t = UI::Style();
        const UI::Vec4 a = UI::GetStyleColorVec4(UI::Col_Accent), tx = UI::GetStyleColorVec4(UI::Col_Text);
        CHECK_NEAR(a.x, t.accent[0] / 255.0f, 1e-6); CHECK_NEAR(a.z, t.accent[2] / 255.0f, 1e-6);
        CHECK_NEAR(tx.y, t.text[1] / 255.0f, 1e-6);
        CHECK(UI::GetStyleColorVec4((UI::Col)99).w > 0.0f);   // an unknown id answers with the text colour, never garbage
    }
    {   // ids
        CHECK(UI::GetID("a") != UI::GetID("b") && UI::GetID("a") == UI::GetID("a"));
        CHECK(UI::GetID(nullptr) != 0 && UI::GetID("") != 0);
    }
    {   // content extents, scroll
        Reset();
        Sim sim(300, 300);
        UI::Vec2 mx0, mx1;
        sim.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 280, 280)) {
                mx0 = UI::GetContentRegionMax();
                UI::Dummy(1, 100);
                mx1 = UI::GetWindowContentRegionMax();
            }
            UI::End();
        });
        CHECK(mx0.x > 200 && mx0.y > 200);
        CHECK(mx1.x == mx0.x);                     // the right edge does not move
        CHECK(mx1.y >= 0);                         // and the bottom stays inside the window (relative to its content)
    }
    {   // SetScrollHereY: a log that follows its newest line (ratio 1) lands at the end; ratio 0 puts the cursor row at the top
        Reset();
        Sim sim(300, 300);
        float scrollEnd = -1, scrollMax = -1, scrollTop = -1, viewH = 0;
        int mode = 1;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 280, 280)) {
                UI::BeginChild("c", 0, 120, true);
                for (int i = 0; i < 40; ++i) {
                    UI::Text("line");
                    if (i == 39 && mode == 1) UI::SetScrollHereY(1.0f);
                    if (i == 20 && mode == 0) UI::SetScrollHereY(0.0f);
                }
                scrollMax = UI::GetScrollMaxY(); viewH = UI::GetChildViewHeight();
                UI::EndChild();
                if (mode == 1) scrollEnd = UI::GetScrollY();
            }
            UI::End();
        };
        sim.Frames(4, ui);
        UI::BeginFrame(UI::Input{});   // (a fresh frame so GetScrollY reads outside any child: 0)
        UI::EndFrameData();
        CHECK(scrollMax > 300);
        // read the child's scroll from inside it on the next frames
        float inside = -1;
        auto ui2 = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 280, 280)) {
                UI::BeginChild("c", 0, 120, true);
                for (int i = 0; i < 40; ++i) UI::Text("line");
                inside = UI::GetScrollY();
                UI::EndChild();
            }
            UI::End();
        };
        sim.Frames(2, ui2);
        CHECK(inside >= scrollMax - 2.0f);                 // ratio 1 on the last line = scrolled to the end
        Reset();
        Sim sim2(300, 300);
        mode = 0;
        sim2.Frames(4, ui);
        float mid = -1;
        auto ui3 = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 280, 280)) {
                UI::BeginChild("c", 0, 120, true);
                for (int i = 0; i < 40; ++i) UI::Text("line");
                mid = UI::GetScrollY();
                UI::EndChild();
            }
            UI::End();
        };
        sim2.Frames(2, ui3);
                CHECK_NEAR(mid, 21.0f * (scrollMax + viewH) / 40.0f, 1.5f);   // the cursor after row 20 sits at the top: 21 rows scrolled
        // outside a scrolling child: nothing happens, nothing crashes
        UI::SetScrollHereY(0.5f);
        CHECK(UI::GetScrollX() == 0.0f);
        UI::SetScrollX(50.0f);
        CHECK(UI::GetScrollX() == 0.0f);
    }
    {   // size constraints make the next window resizable; the ignored calls are harmless between frames
        Reset();
        Sim sim(400, 300);
        UI::Vec2 sz;
        auto ui = [&] {
            UI::SetNextWindowSizeConstraints(UI::Vec2{150, 100}, UI::Vec2{1e9f, 1e9f});
            if (UI::Begin("Resizable", 20, 20, 200, 150)) {
                UI::SetItemDefaultFocus(); UI::SetNextItemAllowOverlap(); UI::TableSetupScrollFreeze(1, 1);
                UI::Text("x");
                sz = UI::GetWindowSize();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(sz.x == 200 && sz.y == 150);
        // drag the bottom-right grip far left and up: the window shrinks but not below the minimum
        sim.Move(20 + 200 - 4, 20 + 150 - 4); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(30, 30); sim.Frames(3, ui); sim.Up(); sim.Frames(2, ui);
        CHECK(sz.x >= 150 && sz.y >= 100 && sz.x < 200);
        // hostile numbers
        const float nan = std::numeric_limits<float>::quiet_NaN();
        sim.Frame([&] { UI::SetNextWindowSizeConstraints(UI::Vec2{nan, nan}, UI::Vec2{nan, nan}); UI::Begin("n", 0, 0, 100, 100); UI::End(); });
    }
    TEST_MAIN_RESULT();
}
