// BeginTooltip / EndTooltip (a tooltip that takes widgets), GetStyle (style fields for ported code), the mapping layer
// (OkayUIMap.hpp) and ListClipper (visible rows only). Headless, through the same frame driver as the other OkayUI tests.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIMap.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

static bool SameBitmap(const Bitmap& a, const Bitmap& b) {
    return a.w == b.w && a.h == b.h && a.rgba == b.rgba;
}
static int DiffPixels(const Bitmap& a, const Bitmap& b) {
    if (a.w != b.w || a.h != b.h) return -1;
    int n = 0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) ++n;
    return n;
}

struct Im2 { float x, y; };
struct Im4 { float x, y, z, w; };

int main() {
    RUN_SUITE("okayui scopes");

    // ---------------- tooltip scope ----------------
    {   // the scope with one Text looks exactly like SetTooltip with the same text
        Reset();
        Sim a(320, 240), b(320, 240);
        a.Move(100, 100); b.Move(100, 100);
        auto withScope = [&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 320, 240)) { UI::Text("hello"); if (UI::BeginTooltip()) { UI::Text("tip text"); UI::EndTooltip(); } } UI::End(); };
        a.Frame(withScope);
        const Bitmap ba = a.Draw(10, 10, 10);
        Reset();
        auto withSet = [&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 320, 240)) { UI::Text("hello"); UI::SetTooltip("tip text"); } UI::End(); };
        b.Frame(withSet);
        const Bitmap bb = b.Draw(10, 10, 10);
        const int d = DiffPixels(ba, bb);
        std::printf("  scope vs SetTooltip: %d differing pixels\n", d);
        CHECK(d == 0);                                              // the same box and the same text, pixel for pixel
        // and a box really is there: the panel colour sits just inside its top-left corner
    }
    {   // several widgets: the box grows with the content, sits on top of what is below, and leaves the layout alone
        Reset();
        Sim s(400, 300);
        s.Move(50, 50);
        float afterY = -1, plainY = -1;
        auto ui = [&](bool tip) { return [&, tip] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 400, 300)) {
                UI::Button("under"); UI::Text("second row");
                const UI::Vec2 before = UI::GetCursorPos();
                if (tip && UI::BeginTooltip()) { UI::Text("line one"); UI::Separator(); UI::Text("line two is longer than one"); UI::Text("line three"); UI::EndTooltip(); }
                const UI::Vec2 after = UI::GetCursorPos();
                (tip ? afterY : plainY) = after.y - before.y;
                UI::Text("after");
            }
            UI::End(); }; };
        s.Frame(ui(true)); const Bitmap with = s.Draw(10, 10, 10);
        Reset(); Sim s2(400, 300); s2.Move(50, 50); s2.Frame(ui(false)); const Bitmap without = s2.Draw(10, 10, 10);
        CHECK(afterY == 0.0f && plainY == 0.0f);                    // the cursor did not move
        CHECK(DiffPixels(with, without) > 500);                     // something big was drawn
        // the box is on top of the button and text below it: the area inside it is the same flat colour (panel over the dark clear colour)
        const unsigned flat = PixelAt(with, 66, 70);
        int same = 0; for (int y = 66; y < 74; ++y) for (int x = 66; x < 74; ++x) if (PixelAt(with, x, y) == flat) ++same;
        CHECK(same == 64 && flat != PixelAt(without, 66, 70));
    }
    {   // input is off inside the tooltip: a button in it does not click and does not take the press
        Reset();
        Sim s(400, 300);
        int clicks = 0; UI::Rect r;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 400, 300)) {
                if (UI::BeginTooltip()) { if (UI::Button("tipbtn")) ++clicks; r = UI::GetItemRect(); UI::EndTooltip(); }
            }
            UI::End();
        };
        s.Move(100, 100); s.Frame(ui);
        s.Move(CenterX(r), CenterY(r)); s.Frame(ui);                 // the tooltip follows the cursor, so aim at where it is now
        s.Down(); s.Frame(ui); s.Up(); s.Frame(ui);
        CHECK(clicks == 0);
    }
    {   // flips left and above at the display edge, and stays inside the display
        Reset();
        Sim s(200, 120);
        s.Move(190, 110);
        s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 200, 120)) { if (UI::BeginTooltip()) { UI::Text("wide tip"); UI::EndTooltip(); } } UI::End(); });
        const Bitmap b = s.Draw(10, 10, 10);
        int left = -1, right = -1, top = -1;
        for (int y = 0; y < 120; ++y) for (int x = 0; x < 200; ++x) if (PixelAt(b, x, y) != Rgba(10, 10, 10)) { if (left < 0 || x < left) left = x; if (x > right) right = x; if (top < 0) top = y; }
        CHECK(left >= 0 && left < 190 && right < 190 && top < 110);  // wholly left of and above the cursor
        CHECK(left >= 2 && top >= 0);                                // and inside the display
    }
    {   // blocked pointer: nothing is shown but the widgets ran; unbalanced and nested use is safe
        Reset();
        Sim s(300, 200); s.Move(100, 100); s.blocked = true;
        int ran = 0;
        s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 200)) { if (UI::BeginTooltip()) { ++ran; UI::Text("hidden"); UI::EndTooltip(); } } UI::End(); });
        const Bitmap b = s.Draw(10, 10, 10);
        CHECK(ran == 1);
        Reset(); Sim s2(300, 200); s2.Move(100, 100); s2.blocked = true;
        s2.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 200)) {} UI::End(); });
        const Bitmap empty = s2.Draw(10, 10, 10);
        CHECK(SameBitmap(b, empty));                                // blocked: identical to no tooltip at all
        Reset(); Sim s3(300, 200); s3.Move(100, 100);
        UI::EndTooltip();                                           // outside a frame / without Begin: nothing
        s3.Frame([&] {
            UI::EndTooltip();
            CHECK(!UI::BeginTooltip());                             // outside a window: refused
            UI::EndTooltip();
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 200)) {
                CHECK(UI::BeginTooltip());
                CHECK(UI::BeginTooltip()); UI::Text("inner"); UI::EndTooltip();   // nested: draws into the outer one
                UI::Text("outer"); UI::EndTooltip();
                UI::EndTooltip();                                   // one too many: ignored
            }
            UI::End();
        });
        CHECK(UI::DroppedGeometry() == 0);
        UI::Text("after the frame");                                // layout is inactive again: no crash
    }
    {   // fuzz: random nesting of tooltip, groups and widgets never leaves a layout or group stack broken
        Reset();
        Sim s(320, 240);
        unsigned seed = 12345;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 16) & 0x7fff; };
        for (int f = 0; f < 200; ++f) {
            s.Move((float)(rnd() % 340), (float)(rnd() % 260));
            s.Frame([&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 0, 0, 320, 240)) {
                    int open = 0, groups = 0;
                    for (int k = 0; k < 14; ++k) switch (rnd() % 7) {
                        case 0: if (UI::BeginTooltip()) ++open; break;
                        case 1: if (open > 0) { UI::EndTooltip(); --open; } break;
                        case 2: UI::Text("some text"); break;
                        case 3: UI::BeginGroup(); ++groups; break;
                        case 4: if (groups > 0) { UI::EndGroup(); --groups; } break;
                        case 5: UI::Button("b"); break;
                        default: UI::SameLine(); UI::Text("x"); break;
                    }
                    while (open-- > 0) UI::EndTooltip();
                    while (groups-- > 0) UI::EndGroup();
                    UI::Text("tail");
                }
                UI::End();
            });
        }
        CHECK(UI::DroppedGeometry() == 0);
        s.Draw();
    }
    {   // golden-free regression: a tooltip over text in the launcher look draws and the TrueType text is in the box
        Reset(); UI::UseLauncherLook(true);
        Sim s(320, 200); s.Move(60, 60);
        s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 320, 200)) { UI::Text("row"); if (UI::BeginTooltip()) { UI::Text("Tip in slate"); UI::Text("second line"); UI::EndTooltip(); } } UI::End(); });
        const Bitmap b = s.Draw(10, 10, 10);
        CHECK(CountNot(b, 74, 74, 200, 130, Rgba(10, 10, 10)) > 300);
        UI::UseLauncherLook(false);
    }

    // ---------------- GetStyle ----------------
    {
        Reset();
        Sim s(300, 200);
        UI::Vec2 spacing, spacing2; UI::Vec4 txt, pushed; float wp = 0;
        s.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 200)) {
                const UI::StyleInfo& st = UI::GetStyle();
                spacing = st.ItemSpacing; wp = st.WindowPadding.x; txt = st.Colors[UI::Col_Text];
                UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{3.0f, 9.0f});
                spacing2 = UI::GetStyle().ItemSpacing;
                UI::PopStyleVar();
                UI::PushStyleColor(UI::Col_Text, UI::RGBA(255, 0, 0));
                pushed = UI::GetStyle().Colors[UI::Col_Text];
                UI::PopStyleColor();
            }
            UI::End();
        });
        CHECK(spacing.x > 0 && spacing.y > 0);
        CHECK(spacing2.x == 3.0f && spacing2.y == 9.0f);             // follows PushStyleVar
        CHECK(wp == UI::Style().windowPadding);
        CHECK_NEAR(txt.x, UI::Style().text[0] / 255.0f, 1e-6);
        CHECK(pushed.x == 1.0f && pushed.y == 0.0f);                  // follows PushStyleColor
        const UI::StyleInfo& st = UI::GetStyle();
        CHECK(st.Alpha == 1.0f && st.ScrollbarSize > 0 && st.FramePadding.x > 0 && st.FramePadding.y > 0 && st.CellPadding.x > 0);
        CHECK_NEAR(st.FrameRounding, UI::Style().rounding, 1e-6);
        for (int i = 0; i < UI::Col_COUNT; ++i) { const UI::Vec4 c = st.Colors[i], g = UI::GetStyleColorVec4((UI::Col)i); CHECK(c.x == g.x && c.w == g.w); }
        // a frame padding is half of what a framed row adds to the text height, so ported "row = text + 2 * padding.y" maths lands on the row
        CHECK_NEAR(UI::GetTextLineHeight() + 2.0f * st.FramePadding.y, UI::GetFrameHeight(), 0.6f);
    }

    // ---------------- mapping layer ----------------
    {
        const UI::Vec2 v2 = UI::FromVec2(Im2{1.5f, -2.0f});
        CHECK(v2.x == 1.5f && v2.y == -2.0f);
        const UI::Vec4 v4 = UI::FromVec4(Im4{0.1f, 0.2f, 0.3f, 0.4f});
        CHECK(v4.x == 0.1f && v4.w == 0.4f);
        const Im4 back = UI::ToVec4<Im4>(v4); CHECK(back.z == 0.3f);
        const Im2 b2 = UI::ToVec2<Im2>(v2); CHECK(b2.y == -2.0f);
        CHECK(UI::FromColor(Im4{1, 0, 0, 1}) == UI::RGBA(255, 0, 0, 255));
        const float nan = std::nanf("");
        CHECK(UI::FromColor(Im4{nan, 2.0f, -1.0f, 1.0f}) == UI::RGBA(0, 255, 0, 255));
        const Im4 c = UI::ToColor<Im4>(UI::RGBA(255, 128, 0, 255)); CHECK(c.x == 1.0f && std::fabs(c.y - 128 / 255.0f) < 1e-5f && c.w == 1.0f);
        const UI::FlagMap t[] = { {1, 100}, {2, 200}, {6, 400}, {0, 999}, {16, 800} };
        int un = -1;
        CHECK(UI::MapFlags(1 | 2, t, 5, &un) == (100 | 200) && un == 0);
        CHECK(UI::MapFlags(6, t, 5, &un) == (200 | 400) && un == 0);   // 6 = 2|4: both pairs match; the bits are consumed
        CHECK(UI::MapFlags(1 | 32, t, 5, &un) == 100 && un == 32);   // unknown bit reported
        CHECK(UI::MapFlags(0, t, 5, &un) == 0 && un == 0);
        CHECK(UI::MapFlags(7, nullptr, 0, &un) == 0 && un == 7);
        CHECK(UI::MapFlags(7, t, 0) == 0);
        CHECK(UI::MapEnum(2, t, 5) == 200 && UI::MapEnum(3, t, 5) == -1 && UI::MapEnum(3, t, 5, 7) == 7);
    }

    // ---------------- ListClipper ----------------
    {
        const int N = 1000;
        auto run = [&](Sim& s, int mode, std::vector<int>* drawn, float* scrollMax, float* step) {
            s.Frame([&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 0, 0, 300, 240)) {
                    UI::BeginChild("list", 0, 200, true);
                    char buf[32];
                    if (mode == 0) {                              // no clipper: every row
                        for (int i = 0; i < N; ++i) { std::snprintf(buf, sizeof buf, "row %d", i); UI::Text(buf); if (drawn) drawn->push_back(i); }
                    } else {
                        UI::ListClipper clip;
                        clip.Begin(N, mode == 2 ? *step : -1.0f);
                        while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) { std::snprintf(buf, sizeof buf, "row %d", i); UI::Text(buf); if (drawn) drawn->push_back(i); }
                    }
                    if (scrollMax) *scrollMax = UI::GetScrollMaxY();
                    UI::EndChild();
                }
                UI::End();
            });
        };
        // the row step, measured from an unclipped list
        float step = 0, smax0 = 0;
        {
            Reset(); Sim s(300, 240); float y0 = 0, y1 = 0;
            s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 240)) { UI::BeginChild("list", 0, 200, true); y0 = UI::GetCursorPos().y; UI::Text("a"); y1 = UI::GetCursorPos().y; UI::EndChild(); } UI::End(); });
            step = y1 - y0; CHECK(step > 8.0f);
        }
        for (int mode = 1; mode <= 2; ++mode) {
            std::printf("  clipper mode %d\n", mode);
            Reset(); Sim ref(300, 240); run(ref, 0, nullptr, &smax0, nullptr); run(ref, 0, nullptr, &smax0, nullptr);
            Reset(); Sim c(300, 240); std::vector<int> drawn; float smax = 0;
            run(c, mode, &drawn, &smax, &step); drawn.clear(); run(c, mode, &drawn, &smax, &step);
            CHECK((int)drawn.size() < 40 && !drawn.empty());                 // only the visible rows were laid out
            CHECK(drawn.front() == 0);
            CHECK_NEAR(smax, smax0, 1.0f);                                    // the scroll range is the full list's
            // pixel-identical to the unclipped list at the top, after wheel scrolls, and at the very end
            const Bitmap top = c.Draw(10, 10, 10);
            Reset(); Sim r2(300, 240); run(r2, 0, nullptr, nullptr, nullptr); run(r2, 0, nullptr, nullptr, nullptr);
            CHECK(SameBitmap(top, r2.Draw(10, 10, 10)));
            for (int round = 0; round < 3; ++round) {
                Reset(); Sim a(300, 240), b(300, 240);
                run(a, 0, nullptr, nullptr, nullptr); run(b, mode, nullptr, nullptr, &step);
                for (int k = 0; k < 4; ++k) { a.Move(100, 100); b.Move(100, 100); a.wheel = b.wheel = (round == 0 ? -3.0f : round == 1 ? -40.0f : -400.0f); run(a, 0, nullptr, nullptr, nullptr); run(b, mode, nullptr, nullptr, &step); }
                std::vector<int> d; run(b, mode, &d, nullptr, &step);
                const int diff = DiffPixels(a.Draw(10, 10, 10), b.Draw(10, 10, 10));
                CHECK(diff == 0);
                CHECK((int)d.size() < 40);
                if (round == 2) CHECK(!d.empty() && d.back() == N - 1);        // scrolled to the end: the last row is laid out
            }
        }
        {   // edge counts
            for (int n : {0, 1, 2, 3}) {
                Reset(); Sim s(300, 240); int rows = 0;
                for (int f = 0; f < 2; ++f) { rows = 0; s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 240)) { UI::ListClipper c; c.Begin(n); while (c.Step()) for (int i = c.DisplayStart; i < c.DisplayEnd; ++i) { ++rows; UI::Text("r"); } UI::Text("tail"); } UI::End(); }); }
                CHECK(rows == n);                                            // a short list is drawn whole, exactly once per row
            }
            // forced range keeps a far row in the loop; no window: no rows, no crash
            Reset(); Sim s(300, 240); bool saw = false;
            for (int f = 0; f < 3; ++f) { saw = false; s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 240)) { UI::BeginChild("l", 0, 100, true); UI::ListClipper c; c.Begin(500); c.ForceDisplayRangeByIndices(300, 300); while (c.Step()) for (int i = c.DisplayStart; i < c.DisplayEnd; ++i) { if (i == 300) saw = true; UI::Text("r"); } UI::EndChild(); } UI::End(); }); }
            CHECK(saw);
            UI::ListClipper out; out.Begin(10); CHECK(!out.Step());
            UI::ListClipper out2; out2.Begin(-5); CHECK(!out2.Step());
            UI::ListClipper out3; out3.Begin(10, std::nanf("")); CHECK(!out3.Step());
        }
    }
    // ---------------- window and mouse queries: FindWindowByName, SetNextWindowFocus, GetMouseDragDelta, IsAnyItemHovered ----------------
    {   // FindWindowByName reports the position, size and focus of a window by title, and "not found" for an unknown one
        Reset(); Sim s(400, 300);
        auto ui = [&] {
            UI::SetNextWindowPos(UI::Vec2{20, 30}); UI::SetNextWindowSize(UI::Vec2{200, 120});
            if (UI::Begin("Alpha", nullptr, 0)) UI::Text("a");
            UI::End();
            UI::SetNextWindowPos(UI::Vec2{150, 90}); UI::SetNextWindowSize(UI::Vec2{200, 120});
            if (UI::Begin("Beta", nullptr, 0)) UI::Text("b");
            UI::End();
        };
        UI::WindowInfo none = UI::FindWindowByName("Alpha"); CHECK(!none.found);   // before any frame
        s.Frame(ui); s.Frame(ui);
        const UI::WindowInfo a = UI::FindWindowByName("Alpha"), b = UI::FindWindowByName("Beta");
        CHECK(a.found && a.pos.x == 20 && a.pos.y == 30 && a.size.x == 200 && a.size.y == 120);
        CHECK(b.found && b.pos.x == 150 && b.size.y == 120);
        CHECK(!UI::FindWindowByName("Nope").found && !UI::FindWindowByName(nullptr).found && !UI::FindWindowByName("").found);
        CHECK(!a.docked);
    }
    {   // SetNextWindowFocus gives the named next window the focus and raises it; without it the focus stays
        Reset(); Sim s(400, 300);
        bool focusBeta = false;
        auto ui = [&] {
            UI::SetNextWindowPos(UI::Vec2{20, 30}); UI::SetNextWindowSize(UI::Vec2{200, 120});
            if (UI::Begin("Alpha", nullptr, 0)) UI::Text("a");
            UI::End();
            UI::SetNextWindowPos(UI::Vec2{150, 90}); UI::SetNextWindowSize(UI::Vec2{200, 120});
            if (focusBeta) UI::SetNextWindowFocus();
            if (UI::Begin("Beta", nullptr, 0)) UI::Text("b");
            UI::End();
        };
        s.Frame(ui); s.Frame(ui);
        focusBeta = true; s.Frame(ui); focusBeta = false; s.Frame(ui);
        CHECK(UI::FindWindowByName("Beta").focused && !UI::FindWindowByName("Alpha").focused);
        s.Frame([&] { UI::SetNextWindowFocus(); UI::Begin(nullptr, nullptr, 0); });   // a null title drops the request: no crash, nothing stuck
        s.Frame(ui);
        CHECK(UI::FindWindowByName("Beta").focused);
    }
    {   // GetMouseDragDelta: zero until the threshold, then the distance from the press; zero again after release
        Reset(); Sim s(300, 200);
        UI::Vec2 d{-1, -1};
        auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 200)) { UI::Button("b"); d = UI::GetMouseDragDelta(); } UI::End(); };
        s.Move(50, 50); s.Frame(ui); CHECK(d.x == 0 && d.y == 0);
        s.Down(); s.Frame(ui); CHECK(d.x == 0 && d.y == 0);
        s.Move(52, 51); s.Frame(ui); CHECK(d.x == 0 && d.y == 0);        // under the 4 px threshold
        s.Move(90, 70); s.Frame(ui); CHECK(d.x == 40 && d.y == 20);
        UI::Vec2 d0 = UI::GetMouseDragDelta(0, 100.0f); (void)d0;
        s.Up(); s.Frame(ui); CHECK(d.x == 0 && d.y == 0);
        d = UI::GetMouseDragDelta(1); CHECK(d.x == 0 && d.y == 0);       // right button: not tracked, zero
    }
    {   // IsAnyItemHovered: true while the pointer is on any item, false over empty space and outside a frame
        Reset(); Sim s(300, 200);
        bool any = false;
        auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 200)) { UI::Button("hit me"); } any = UI::IsAnyItemHovered(); UI::End(); };
        s.Move(150, 190); s.Frame(ui); s.Frame(ui); CHECK(!any);
        s.Move(30, 20); s.Frame(ui); s.Frame(ui); CHECK(any);
        s.Move(150, 190); s.Frame(ui); s.Frame(ui); CHECK(!any);
    }
    TEST_MAIN_RESULT();
}
