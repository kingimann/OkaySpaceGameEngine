// OkayUI colours: the full Col_* set (defaults, push / pop / set / reset, GetStyleColorVec4, and that the widgets that use a colour
// really draw with it), the colour conversions, ColorEdit3/4 (alpha, flags, hex, picker popup), ColorButton and ColorPicker3/4
// (saturation / value square, hue bar, alpha bar, hex field, original preview). Headless, pixel checks through the software backend.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

const unsigned kClear = Rgba(12, 14, 20);
UI::Vec2 V(float x, float y) { return UI::Vec2{x, y}; }
unsigned P(const Bitmap& b, float x, float y) { return PixelAt(b, (int)x, (int)y); }
int CountEq(const Bitmap& b, unsigned c) { int n = 0; for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == c) ++n; return n; }
bool Near4(UI::Vec4 a, float r, float g, float b, float al, float eps = 1.0f / 255.0f + 1e-5f) {
    return std::fabs(a.x - r) <= eps && std::fabs(a.y - g) <= eps && std::fabs(a.z - b) <= eps && std::fabs(a.w - al) <= eps;
}
// distinct colours to push, so a pixel of this colour can only come from the push
const unsigned char kC[4] = {201, 31, 77, 255};
const unsigned kCpx = Rgba(201, 31, 77);
void Push(UI::Col c) { UI::PushStyleColor(c, kC[0], kC[1], kC[2], kC[3]); }

// ---- the style colours -----------------------------------------------------------------------------------------------

void TestStyleColors() {
    RUN_SUITE("colours: the full set, defaults, push / pop / set / reset");
    Reset();
    CHECK(UI::Col_COUNT >= 58);
    std::set<std::string> names;
    for (int i = 0; i < UI::Col_COUNT; ++i) {
        const char* n = UI::GetStyleColorName((UI::Col)i);
        CHECK(n && n[0] == 'C' && std::strncmp(n, "Col_", 4) == 0);
        names.insert(n);
    }
    CHECK((int)names.size() == UI::Col_COUNT);
    CHECK(std::string(UI::GetStyleColorName(UI::Col_Text)) == "Col_Text" && std::string(UI::GetStyleColorName(UI::Col_ModalWindowDimBg)) == "Col_ModalWindowDimBg");
    CHECK(std::string(UI::GetStyleColorName((UI::Col)-1)).empty() && std::string(UI::GetStyleColorName((UI::Col)UI::Col_COUNT)).empty());
    // the original eight read the base theme; the button names are those fields
    const UI::Theme& t = UI::Style();
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Text), t.text[0] / 255.0f, t.text[1] / 255.0f, t.text[2] / 255.0f, 1.0f));
    CHECK(UI::Col_Button == UI::Col_Bg && UI::Col_ButtonHovered == UI::Col_BgHover && UI::Col_ButtonActive == UI::Col_BgDown);
    // a colour never set follows the base theme field it comes from
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_TextDisabled), t.textDim[0] / 255.0f, t.textDim[1] / 255.0f, t.textDim[2] / 255.0f, 1.0f));
    UI::Style().textDim[0] = 17;
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_TextDisabled), 17 / 255.0f, t.textDim[1] / 255.0f, t.textDim[2] / 255.0f, 1.0f));
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_WindowBg), t.panel[0] / 255.0f, t.panel[1] / 255.0f, t.panel[2] / 255.0f, t.panel[3] / 255.0f));
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_FrameBg), t.field[0] / 255.0f, t.field[1] / 255.0f, t.field[2] / 255.0f, 1.0f));
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_TitleBgActive), t.titleBg[0] / 255.0f, t.titleBg[1] / 255.0f, t.titleBg[2] / 255.0f, 1.0f));
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_BorderShadow), 0, 0, 0, 0));
    // an id outside the set answers transparent black
    CHECK(Near4(UI::GetStyleColorVec4((UI::Col)-5), 0, 0, 0, 0) && Near4(UI::GetStyleColorVec4((UI::Col)9999), 0, 0, 0, 0));
    // every id: push, read back, pop, back to the default
    for (int i = 0; i < UI::Col_COUNT; ++i) {
        const UI::Vec4 before = UI::GetStyleColorVec4((UI::Col)i);
        UI::PushStyleColor((UI::Col)i, kC[0], kC[1], kC[2], 128);
        CHECK(Near4(UI::GetStyleColorVec4((UI::Col)i), 201 / 255.0f, 31 / 255.0f, 77 / 255.0f, 128 / 255.0f));
        UI::PopStyleColor();
        CHECK(Near4(UI::GetStyleColorVec4((UI::Col)i), before.x, before.y, before.z, before.w));
    }
    // nesting restores in order, including a colour pushed twice
    UI::PushStyleColor(UI::Col_Header, 1, 2, 3, 4);
    UI::PushStyleColor(UI::Col_Header, 5, 6, 7, 8);
    UI::PushStyleColor(UI::Col_Tab, 9, 9, 9, 9);
    UI::PopStyleColor(2);
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Header), 1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f));
    UI::PopStyleColor();
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Header), t.track[0] / 255.0f, t.track[1] / 255.0f, t.track[2] / 255.0f, t.track[3] / 255.0f));
    UI::PopStyleColor(5);                                                  // popping an empty stack is harmless
    // packed and float overloads
    UI::PushStyleColor(UI::Col_Separator, UI::RGBA(10, 20, 30, 40));
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Separator), 10 / 255.0f, 20 / 255.0f, 30 / 255.0f, 40 / 255.0f));
    UI::PopStyleColor();
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    UI::PushStyleColor(UI::Col_Separator, UI::Vec4{0.5f, 2.0f, -1.0f, nan});
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Separator), 0.5f, 1.0f, 0.0f, 0.0f));
    UI::PopStyleColor();
    UI::PushStyleColor(UI::Col_Separator, UI::Vec4{inf, -inf, 0.25f, 1.0f});
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Separator), 1.0f, 0.0f, 0.25f, 1.0f));
    UI::PopStyleColor();
    UI::PushStyleColor((UI::Col)9999, 1, 2, 3, 4);                          // an unknown id does not crash and pops cleanly
    UI::PopStyleColor();
    // set / reset
    UI::SetStyleColor(UI::Col_Tab, 11, 22, 33, 44);
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Tab), 11 / 255.0f, 22 / 255.0f, 33 / 255.0f, 44 / 255.0f));
    UI::PushStyleColor(UI::Col_Tab, 1, 1, 1, 1); UI::PopStyleColor();      // a pop gives back the set value, not the default
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Tab), 11 / 255.0f, 22 / 255.0f, 33 / 255.0f, 44 / 255.0f));
    UI::ResetStyleColor(UI::Col_Tab);
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Tab), t.track[0] / 255.0f, t.track[1] / 255.0f, t.track[2] / 255.0f, t.track[3] / 255.0f));
    UI::SetStyleColor((UI::Col)-3, 1, 2, 3, 4); UI::ResetStyleColor((UI::Col)-3); UI::ResetStyleColor((UI::Col)9999);
    // GetColorU32 follows
    UI::SetStyleColor(UI::Col_PlotLines, 90, 80, 70, 200);
    CHECK(UI::GetColorU32(UI::Col_PlotLines) == UI::RGBA(90, 80, 70, 200));
    CHECK((UI::GetColorU32(UI::Col_PlotLines, 0.5f) >> 24) == 100);
    // an unbalanced push is undone at the start of the next frame
    Sim sim(100, 100);
    UI::PushStyleColor(UI::Col_Header, 1, 2, 3, 4);
    sim.Frame([] {});
    CHECK(Near4(UI::GetStyleColorVec4(UI::Col_Header), t.track[0] / 255.0f, t.track[1] / 255.0f, t.track[2] / 255.0f, t.track[3] / 255.0f));
    // the launcher look swaps the base theme and clears what was set
    UI::SetStyleColor(UI::Col_Tab, 1, 2, 3, 4);
    UI::UseLauncherLook(true);
    CHECK(!Near4(UI::GetStyleColorVec4(UI::Col_Tab), 1 / 255.0f, 2 / 255.0f, 3 / 255.0f, 4 / 255.0f));
    UI::UseLauncherLook(false);
}

void TestConversions() {
    RUN_SUITE("colours: conversions");
    float h, s, v, r, g, b;
    UI::ColorConvertRGBtoHSV(1, 0, 0, &h, &s, &v); CHECK(h == 0 && s == 1 && v == 1);
    UI::ColorConvertRGBtoHSV(0, 1, 0, &h, &s, &v); CHECK_NEAR(h, 1.0 / 3.0, 1e-6);
    UI::ColorConvertRGBtoHSV(0, 0, 1, &h, &s, &v); CHECK_NEAR(h, 2.0 / 3.0, 1e-6);
    UI::ColorConvertRGBtoHSV(0.5f, 0.5f, 0.5f, &h, &s, &v); CHECK(s == 0 && v == 0.5f);
    UI::ColorConvertRGBtoHSV(0, 0, 0, &h, &s, &v); CHECK(s == 0 && v == 0);
    UI::ColorConvertRGBtoHSV(std::numeric_limits<float>::quiet_NaN(), 2.0f, -1.0f, &h, &s, &v); CHECK(std::isfinite(h) && std::isfinite(s) && std::isfinite(v));
    UI::ColorConvertRGBtoHSV(1, 0, 0, nullptr, nullptr, nullptr);
    // round trip over a grid
    int bad = 0;
    for (int i = 0; i <= 10; ++i) for (int j = 0; j <= 10; ++j) for (int k = 0; k <= 10; ++k) {
        const float R = i / 10.0f, G = j / 10.0f, B = k / 10.0f;
        UI::ColorConvertRGBtoHSV(R, G, B, &h, &s, &v);
        UI::ColorConvertHSVtoRGB(h, s, v, &r, &g, &b);
        if (std::fabs(r - R) > 1e-5f || std::fabs(g - G) > 1e-5f || std::fabs(b - B) > 1e-5f) ++bad;
    }
    CHECK(bad == 0);
    UI::ColorConvertHSVtoRGB(1.0f, 1, 1, &r, &g, &b); CHECK(r == 1 && g == 0 && b == 0);              // the hue wraps
    UI::ColorConvertHSVtoRGB(-0.25f, 1, 1, &r, &g, &b); CHECK_NEAR(r, 0.5, 1e-6); CHECK_NEAR(g, 0.0, 1e-6); CHECK_NEAR(b, 1.0, 1e-6); // -0.25 wraps to 0.75: violet
    UI::ColorConvertHSVtoRGB(std::numeric_limits<float>::infinity(), 1, 1, &r, &g, &b); CHECK(std::isfinite(r));
    UI::ColorConvertHSVtoRGB(0.5f, 5.0f, 5.0f, &r, &g, &b); CHECK(r >= 0 && r <= 1 && g <= 1 && b <= 1);
    UI::ColorConvertHSVtoRGB(0.5f, 1, 1, nullptr, &g, nullptr);
    const UI::Vec4 c4 = UI::ColorConvertU32ToFloat4(UI::RGBA(255, 128, 0, 64));
    CHECK(Near4(c4, 1.0f, 128 / 255.0f, 0.0f, 64 / 255.0f));
    CHECK(UI::ColorConvertFloat4ToU32(c4) == UI::RGBA(255, 128, 0, 64));
    CHECK(UI::ColorConvertFloat4ToU32(UI::Vec4{2, -1, std::numeric_limits<float>::quiet_NaN(), 1}) == UI::RGBA(255, 0, 0, 255));
    // hex
    char buf[16]; float c[4] = {1.0f, 0.5f, 0.0f, 0.25f};
    UI::ColorToHex(c, false, buf, 16); CHECK(std::string(buf) == "#FF8000");
    UI::ColorToHex(c, true, buf, 16); CHECK(std::string(buf) == "#FF800040");
    UI::ColorToHex(c, true, buf, 5); CHECK(std::string(buf) == "#FF8");   // cut to the capacity, still terminated
    UI::ColorToHex(c, true, buf, 0); UI::ColorToHex(nullptr, true, buf, 16); CHECK(buf[0] == 0);
    float d[4] = {0.1f, 0.1f, 0.1f, 0.7f};
    CHECK(UI::ColorFromHex("#00ff80", d) && d[0] == 0 && d[1] == 1.0f && std::fabs(d[2] - 128 / 255.0f) < 1e-6f && d[3] == 0.7f);
    CHECK(UI::ColorFromHex("0A141E28", d) && std::fabs(d[0] - 10 / 255.0f) < 1e-6f && std::fabs(d[3] - 40 / 255.0f) < 1e-6f);
    const float keep[4] = {d[0], d[1], d[2], d[3]};
    const char* badOnes[] = {"", "#", "#12345", "#1234567", "#123456789", "#GG0000", "# 00000", "#00000G", "##000000", "ff ff ff", nullptr};
    for (int i = 0; badOnes[i] || i == 10; ++i) { if (!badOnes[i]) break; CHECK(!UI::ColorFromHex(badOnes[i], d)); }
    CHECK(!UI::ColorFromHex(nullptr, d) && !UI::ColorFromHex("#000000", nullptr));
    CHECK(d[0] == keep[0] && d[1] == keep[1] && d[2] == keep[2] && d[3] == keep[3]);   // a refused text changes nothing
}

// ---- which widget draws with which colour ----------------------------------------------------------------------------------

struct Rig {
    Sim sim;
    explicit Rig(float w = 420, float h = 320) : sim(w, h) { Reset(); }
};

void TestWidgetsUseColours() {
    RUN_SUITE("colours: widgets draw with them");
    {   // TextDisabled
        Rig r; auto ui = [&] { if (UI::Begin("W", 10, 10, 400, 100)) { Push(UI::Col_TextDisabled); UI::TextDisabled("dim words"); UI::PopStyleColor(); } UI::End(); };
        r.sim.Frames(2, ui); CHECK(CountEq(r.sim.Draw(12, 14, 20), kCpx) > 20);
    }
    {   // window body, title bar (idle / focused / collapsed), border untouched
        Rig r; UI::Rect ra, rb; UI::Vec2 pa, pb;
        auto ui = [&] {
            UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(180, 120), UI::Cond_FirstUseEver);
            if (UI::Begin("ta", nullptr, 0)) UI::Text("a");
            pa = UI::GetWindowPos(); UI::End();
            UI::SetNextWindowPos(V(220, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(180, 120), UI::Cond_FirstUseEver);
            if (UI::Begin("tb", nullptr, 0)) UI::Text("b");
            pb = UI::GetWindowPos(); UI::End();
        };
        UI::SetStyleColor(UI::Col_TitleBg, 10, 200, 10); UI::SetStyleColor(UI::Col_TitleBgActive, 200, 10, 10); UI::SetStyleColor(UI::Col_TitleBgCollapsed, 10, 10, 200);
        UI::SetStyleColor(UI::Col_WindowBg, 120, 60, 180);
        r.sim.Frames(3, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(P(b, pa.x + 100, pa.y + 3) == Rgba(10, 200, 10));            // the first window lost focus when the second appeared
        CHECK(P(b, pb.x + 100, pb.y + 3) == Rgba(200, 10, 10));            // the focused one
        CHECK(P(b, pa.x + 100, pa.y + 100) == Rgba(120, 60, 180));         // the body
        r.sim.Click(pa.x + 5, pa.y + 5, ui);                                 // the caret: collapse "ta"
        r.sim.Move(400, 300); r.sim.Frames(2, ui);
        const Bitmap b2 = r.sim.Draw(12, 14, 20);
        CHECK(P(b2, pa.x + 100, pa.y + 3) == Rgba(10, 10, 200));
        (void)ra; (void)rb;
    }
    {   // frame background of a text field: idle, hovered, focused; and the selection
        Rig r; char txt[32] = "hello";
        auto ui = [&] { UI::TextField(7, 20, 20, 200, 30, txt, 32); };
        UI::SetStyleColor(UI::Col_FrameBg, 10, 200, 10); UI::SetStyleColor(UI::Col_FrameBgHovered, 200, 200, 10); UI::SetStyleColor(UI::Col_FrameBgActive, 10, 200, 200);
        UI::SetStyleColor(UI::Col_TextSelectedBg, 250, 10, 250);
        r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 200, 35) == Rgba(10, 200, 10));
        r.sim.Move(200, 35); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 210, 35) == Rgba(200, 200, 10));
        r.sim.Click(200, 35, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 210, 35) == Rgba(10, 200, 200));
        r.sim.Press(Key_SelectAll); r.sim.Frames(2, ui);
        CHECK(CountEq(r.sim.Draw(12, 14, 20), Rgba(250, 10, 250)) > 100);
    }
    {   // check mark and radio dot, slider grab (idle and held)
        Rig r; bool on = true; int radio = 0; float v = 0.5f;
        auto ui = [&] { UI::Checkbox(1, 20, 20, 24, "c", &on); UI::RadioButton(2, 20, 60, 24, "r", &radio, 0); UI::Slider(3, 20, 100, 200, 28, &v, 0, 1); };
        UI::SetStyleColor(UI::Col_CheckMark, 250, 10, 250); UI::SetStyleColor(UI::Col_SliderGrab, 10, 250, 10); UI::SetStyleColor(UI::Col_SliderGrabActive, 250, 250, 10);
        r.sim.Frames(2, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(P(b, 32, 32) == Rgba(250, 10, 250));
        CHECK(P(b, 32, 72) == Rgba(250, 10, 250));
        CHECK(P(b, 120, 114) == Rgba(10, 250, 10));
        r.sim.Move(120, 114); r.sim.Down(); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 120, 114) == Rgba(250, 250, 10));
    }
    {   // headers: idle, hovered, held; tree node and selectable hover; separator
        Rig r; UI::Rect rh, rs, rt;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 400, 300)) {
                UI::CollapsingHeader("head"); rh = UI::GetItemRect();
                UI::TreeNodeEx("tree", UI::TreeNode_Leaf); rt = UI::GetItemRect();
                UI::Selectable("sel", false); rs = UI::GetItemRect();
                UI::Separator();
            }
            UI::End();
        };
        UI::SetStyleColor(UI::Col_Header, 10, 200, 10); UI::SetStyleColor(UI::Col_HeaderHovered, 200, 200, 10); UI::SetStyleColor(UI::Col_HeaderActive, 10, 200, 200);
        UI::SetStyleColor(UI::Col_Separator, 250, 10, 250);
        r.sim.Frames(2, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(P(b, rh.x + rh.w - 6, rh.y + rh.h * 0.5f) == Rgba(10, 200, 10));
        CHECK(CountEq(b, Rgba(250, 10, 250)) > 100);
        r.sim.Move(rh.x + rh.w - 6, rh.y + rh.h * 0.5f); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), rh.x + rh.w - 6, rh.y + rh.h * 0.5f) == Rgba(200, 200, 10));
        r.sim.Down(); r.sim.Frame(ui);
        CHECK(P(r.sim.Draw(12, 14, 20), rh.x + rh.w - 6, rh.y + rh.h * 0.5f) == Rgba(10, 200, 200));
        r.sim.Up(); r.sim.Move(rt.x + rt.w - 6, rt.y + rt.h * 0.5f); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), rt.x + rt.w - 6, rt.y + rt.h * 0.5f) == Rgba(200, 200, 10));
        r.sim.Move(rs.x + rs.w - 6, rs.y + rs.h * 0.5f); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), rs.x + rs.w - 6, rs.y + rs.h * 0.5f) == Rgba(200, 200, 10));
    }
    {   // scroll bars of a child region: track, thumb, hovered thumb
        Rig r; UI::Rect rc;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 300, 300)) {
                UI::BeginChild("kid", 200, 100, true); for (int i = 0; i < 20; ++i) UI::Text("row"); UI::EndChild(); rc = UI::GetItemRect();
            }
            UI::End();
        };
        UI::SetStyleColor(UI::Col_ScrollbarBg, 10, 200, 10); UI::SetStyleColor(UI::Col_ScrollbarGrab, 200, 200, 10); UI::SetStyleColor(UI::Col_ScrollbarGrabHovered, 10, 200, 200);
        r.sim.Frames(3, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        const float sx = rc.x + rc.w - 5.0f;
        CHECK(CountEq(b, Rgba(10, 200, 10)) > 100);                                // the track
        CHECK(P(b, sx, rc.y + 8) == Rgba(200, 200, 10));                           // the thumb at the top
        r.sim.Move(sx, rc.y + 8); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), sx, rc.y + 8) == Rgba(10, 200, 200));
    }
    {   // child, popup and modal dim
        Rig r; bool open = true, openModal = false;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 380, 300)) {
                if (openModal) { UI::OpenPopup("modal"); openModal = false; }
                UI::BeginChild("kid", 150, 80, true); UI::Text("x"); UI::EndChild();
                if (UI::Button("open")) UI::OpenPopup("pop");
                if (UI::BeginPopup("pop")) { UI::Text("popup body"); UI::Dummy(10, 40); UI::EndPopup(); }
                if (UI::BeginPopupModal("modal", nullptr)) { UI::Text("modal body"); UI::EndPopup(); }
            }
            UI::End();
        };
        UI::SetStyleColor(UI::Col_ChildBg, 10, 200, 10); UI::SetStyleColor(UI::Col_PopupBg, 200, 200, 10); UI::SetStyleColor(UI::Col_ModalWindowDimBg, 250, 10, 250);
        r.sim.Frames(2, ui);
        CHECK(CountEq(r.sim.Draw(12, 14, 20), Rgba(10, 200, 10)) > 500);
        r.sim.Click(60, 140, ui); r.sim.Frames(2, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(CountEq(b, Rgba(200, 200, 10)) > 300);                                  // the popup body
        openModal = true; r.sim.Frames(3, ui);
        const Bitmap bm = r.sim.Draw(12, 14, 20);
        CHECK(P(bm, 5, 300) == Rgba(250, 10, 250));                                 // the dim covers the display
    }
    {   // tabs: idle, hovered, selected
        Rig r; int cur = 0;
        auto ui = [&] { UI::Tab(1, 20, 20, 90, 30, "", &cur, 0); UI::Tab(2, 120, 20, 90, 30, "", &cur, 1); };
        UI::SetStyleColor(UI::Col_Tab, 10, 200, 10); UI::SetStyleColor(UI::Col_TabHovered, 200, 200, 10); UI::SetStyleColor(UI::Col_TabActive, 10, 200, 200);
        r.sim.Frames(2, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(P(b, 60, 40) == Rgba(10, 200, 200));                                  // index 0 is selected
        CHECK(P(b, 160, 40) == Rgba(10, 200, 10));
        r.sim.Move(160, 40); r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 160, 40) == Rgba(200, 200, 10));
    }
    {   // menu bar, tables (header, banded row, row borders, column borders), plots, resize grip
        Rig r; UI::Rect rb;
        auto ui = [&] {
            UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 300), UI::Cond_FirstUseEver);
            if (UI::Begin("W", nullptr, 0)) {
                UI::BeginMenuBar(); UI::EndMenuBar(); rb = UI::GetItemRect();
                if (UI::BeginTable("t", 2, UI::Table_RowBg | UI::Table_BordersInnerH)) {
                    UI::TableSetupColumn("a"); UI::TableSetupColumn("b"); UI::TableHeadersRow();
                    for (int i = 0; i < 3; ++i) { UI::TableNextRow(); UI::TableNextColumn(); UI::Text("x"); UI::TableNextColumn(); UI::Text("y"); }
                    UI::EndTable();
                }
                const float vals[5] = {0, 1, 0.5f, 1, 0};
                UI::PlotLines("pl", vals, 5, 0, 1, 40); UI::PlotHistogram("ph", vals, 5, 0, 1, 40);
            }
            UI::End();
        };
        UI::SetStyleColor(UI::Col_MenuBarBg, 11, 201, 12); UI::SetStyleColor(UI::Col_TableHeaderBg, 12, 11, 201); UI::SetStyleColor(UI::Col_TableRowBgAlt, 201, 11, 12);
        UI::SetStyleColor(UI::Col_TableBorderLight, 13, 202, 203); UI::SetStyleColor(UI::Col_TableBorderStrong, 204, 13, 205);
        UI::SetStyleColor(UI::Col_PlotLines, 205, 206, 13); UI::SetStyleColor(UI::Col_PlotHistogram, 14, 207, 208);
        UI::SetStyleColor(UI::Col_ResizeGrip, 209, 14, 210);
        r.sim.Frames(3, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(CountEq(b, Rgba(11, 201, 12)) > 200);
        CHECK(CountEq(b, Rgba(12, 11, 201)) > 200);
        CHECK(CountEq(b, Rgba(201, 11, 12)) > 100);
        CHECK(CountEq(b, Rgba(13, 202, 203)) > 50);
        CHECK(CountEq(b, Rgba(204, 13, 205)) > 10);
        CHECK(CountEq(b, Rgba(205, 206, 13)) > 10);
        CHECK(CountEq(b, Rgba(14, 207, 208)) > 100);
        CHECK(CountEq(b, Rgba(209, 14, 210)) > 5);
    }
    {   // the drop target outline while a typed drag is over an item
        Rig r; UI::Rect rt, rs;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 380, 200)) {
                UI::Button("source"); rs = UI::GetItemRect();
                if (UI::BeginDragDropSource()) { UI::SetDragDropPayload("T", "x", 1); UI::EndDragDropSource(); }
                UI::Button("target"); rt = UI::GetItemRect();
                if (UI::BeginDragDropTarget()) UI::EndDragDropTarget();
            }
            UI::End();
        };
        UI::SetStyleColor(UI::Col_DragDropTarget, 201, 11, 12);
        r.sim.Frames(2, ui);
        r.sim.Move(CenterX(rs), CenterY(rs)); r.sim.Frame(ui); r.sim.Down(); r.sim.Frame(ui);
        r.sim.Move(CenterX(rs) + 20, CenterY(rs) + 10); r.sim.Frame(ui); r.sim.Move(CenterX(rt), CenterY(rt)); r.sim.Frames(2, ui);
        CHECK(CountEq(r.sim.Draw(12, 14, 20), Rgba(201, 11, 12)) > 100);
        r.sim.Up(); r.sim.Frames(2, ui);
    }
    {   // docking: the preview of a drop and the empty dock
        Rig r(640, 480); UI::Vec2 wp{0, 0};
        auto ui = [&] {
            UI::DockSpace(UI::Rect{0, 0, 640, 480});
            if (UI::DockBegin("D")) UI::Text("d");
            UI::DockEnd();
            if (UI::DockBegin("F")) UI::Text("f");
            wp = UI::GetWindowPos();
            UI::DockEnd();
        };
        UI::SetStyleColor(UI::Col_DockingEmptyBg, 12, 201, 13); UI::SetStyleColor(UI::Col_DockingPreview, 201, 12, 14);
        r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 600, 60) == Rgba(12, 201, 13));             // an empty dock root
        UI::GetDockLayout().Dock("D", UI::GetDockLayout().Root(), UI::DockSide_Center);
        r.sim.Frames(2, ui);
        CHECK(P(r.sim.Draw(12, 14, 20), 600, 60) != Rgba(12, 201, 13));             // a dock with a window is not "empty"
        r.sim.Move(wp.x + 60, wp.y + 8); r.sim.Frame(ui); r.sim.Down(); r.sim.Frame(ui);
        r.sim.Move(240, 300); r.sim.Frame(ui); r.sim.Move(250, 310); r.sim.Frame(ui);
        const UI::DockDropInfo d = UI::GetDockDrop();
        CHECK(d.active);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(CountEq(b, Rgba(201, 12, 14)) > 500);                                 // the translucent preview, drawn opaque here
        r.sim.Up(); r.sim.Frames(2, ui);
    }
}

// ---- ColorEdit, ColorButton, ColorPicker ---------------------------------------------------------------------------------

void TestColorEdit() {
    RUN_SUITE("colours: ColorEdit3 / ColorEdit4 / ColorButton");
    {   // the swatch shows a checkerboard through the alpha; the opaque colour has none
        Rig r; float half[4] = {1, 0, 0, 0.5f}, opaque[4] = {1, 0, 0, 1};
        UI::Rect rc;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 400, 300)) {
                UI::PushID(1); UI::SetNextItemWidth(380); UI::ColorEdit4("", half); rc = UI::GetItemRect(); UI::PopID();
                UI::PushID(2); UI::ColorButton("b", opaque); UI::PopID();
                UI::PushID(3); UI::ColorButton("c", half, UI::ColorEdit_AlphaPreviewHalf, 0.0f); UI::PopID();
            }
            UI::End();
        };
        r.sim.Frames(2, ui);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        std::set<unsigned> seen;
        for (int y = (int)rc.y + 3; y < (int)(rc.y + rc.h) - 3; ++y) for (int x = (int)rc.x + 3; x < (int)(rc.x + rc.h) - 3; ++x) seen.insert(PixelAt(b, x, y));
        CHECK(seen.size() >= 2);                                                     // light and dark checks, tinted red
        // the opaque ColorButton is one colour inside
    }
    {   // NoAlpha: three cells, alpha untouched; NoInputs: the swatch alone; NoLabel; the sliders edit
        Rig r; float c[4] = {0.2f, 0.4f, 0.6f, 0.3f}; UI::Rect r3, r0;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 500, 300)) {
                UI::PushID(1); UI::SetNextItemWidth(400); UI::ColorEdit4("", c, UI::ColorEdit_NoAlpha); r3 = UI::GetItemRect(); UI::PopID();
                UI::PushID(2); UI::SetNextItemWidth(400); UI::ColorEdit4("", c, UI::ColorEdit_NoInputs); r0 = UI::GetItemRect(); UI::PopID();
            }
            UI::End();
        };
        r.sim.Frames(2, ui);
        const float cw = r3.h, sw = r3.w - cw - 6.0f, each = (sw - 8.0f) / 3.0f;
        const float x2 = r3.x + cw + 6.0f + 2.0f * (each + 4.0f) + each * 0.5f;       // the third cell
        r.sim.Click(x2, CenterY(r3), ui);
        CHECK_NEAR(c[2], 0.5, 0.05); CHECK(c[0] == 0.2f && c[1] == 0.4f && c[3] == 0.3f);
        const float x3 = r3.x + r3.w - 2.0f;                                           // where an alpha cell would be: nothing there
        r.sim.Click(x3, CenterY(r3), ui);
        CHECK(c[3] == 0.3f);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        // with only the swatch there are no sliders: right of it is plain window body
        const unsigned pan = P(b, r0.x + r0.w - 3, r0.y + 3);
        CHECK(CountNot(b, (int)(r0.x + r0.h + 10), (int)r0.y, (int)(r0.x + r0.w - 2), (int)(r0.y + r0.h), pan) == 0);
    }
    {   // Hex: a text field in place of the sliders; typing a whole colour applies it
        Rig r; float c[4] = {1, 0, 0, 1}; UI::Rect rc;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 500, 300)) { UI::PushID(1); UI::SetNextItemWidth(400); UI::ColorEdit4("", c, UI::ColorEdit_Hex | UI::ColorEdit_NoPicker); rc = UI::GetItemRect(); UI::PopID(); }
            UI::End();
        };
        r.sim.Frames(2, ui);
        r.sim.Click(rc.x + rc.h + 60, CenterY(rc), ui);                               // focus the field
        r.sim.Press(Key_SelectAll); r.sim.Frame(ui);
        r.sim.Type("#0000FF"); r.sim.Frame(ui);
        CHECK(c[0] == 0 && c[1] == 0 && c[2] == 1.0f && c[3] == 1.0f);
        r.sim.Press(Key_SelectAll); r.sim.Frame(ui);
        r.sim.Type("12"); r.sim.Frame(ui);                                             // not yet a colour: no change
        CHECK(c[2] == 1.0f);
        r.sim.Press(Key_SelectAll); r.sim.Frame(ui);
        r.sim.Type("#10203040"); r.sim.Frame(ui);
        CHECK_NEAR(c[0], 16 / 255.0, 1e-4); CHECK_NEAR(c[3], 64 / 255.0, 1e-4);
        r.sim.Click(450, 250, ui); r.sim.Frames(2, ui);                                // click away: focus leaves, the field shows the colour again
        CHECK_NEAR(c[1], 32 / 255.0, 1e-4);
    }
    {   // the swatch opens a picker popup under it; dragging inside edits the colour; a click elsewhere closes it
        Rig r(600, 560); float c[4] = {1, 0, 0, 1}; UI::Rect rc; bool ch = false;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 560, 520)) { UI::PushID(1); ch = UI::ColorEdit4("col", c) || ch; rc = UI::GetItemRect(); UI::PopID(); UI::Text("below"); }
            UI::End();
        };
        r.sim.Frames(2, ui);
        const Bitmap before = r.sim.Draw(12, 14, 20);
        const unsigned ref = P(before, rc.x + 200, rc.y + rc.h + 150);                 // the plain window body
        r.sim.Click(rc.x + rc.h * 0.5f, CenterY(rc), ui);
        r.sim.Move(580, 540); r.sim.Frames(2, ui);
        const Bitmap open = r.sim.Draw(12, 14, 20);
        CHECK(CountNot(open, (int)rc.x, (int)(rc.y + rc.h + 4), (int)(rc.x + 240), (int)(rc.y + rc.h + 200), ref) > CountNot(before, (int)rc.x, (int)(rc.y + rc.h + 4), (int)(rc.x + 240), (int)(rc.y + rc.h + 200), ref) + 3000);
        // drag in the square: the popup starts at (swatch x, row bottom + 2), padding 6
        const float px = rc.x + 6.0f, py = rc.y + rc.h + 2.0f + 6.0f;
        ch = false;
        r.sim.Move(px + 40, py + 40); r.sim.Frame(ui); r.sim.Down(); r.sim.Frame(ui); r.sim.Move(px + 60, py + 20); r.sim.Frame(ui); r.sim.Up(); r.sim.Frame(ui);
        CHECK(ch);
        CHECK(c[0] > c[1] && c[0] > c[2] && c[1] < 0.99f);
        // a click far outside closes it
        r.sim.Click(560, 520, ui); r.sim.Move(580, 540); r.sim.Frames(2, ui);
        const Bitmap shut = r.sim.Draw(12, 14, 20);
        CHECK(CountNot(shut, (int)rc.x, (int)(rc.y + rc.h + 4), (int)(rc.x + 240), (int)(rc.y + rc.h + 200), ref) < CountNot(open, (int)rc.x, (int)(rc.y + rc.h + 4), (int)(rc.x + 240), (int)(rc.y + rc.h + 200), ref) - 3000);
    }
    {   // NoPicker: the swatch click opens nothing
        Rig r(600, 560); float c[4] = {1, 0, 0, 1}; UI::Rect rc;
        auto ui = [&] { if (UI::Begin("W", 10, 10, 560, 520)) { UI::ColorEdit3("col", c, UI::ColorEdit_NoPicker); rc = UI::GetItemRect(); } UI::End(); };
        r.sim.Frames(2, ui);
        const Bitmap before = r.sim.Draw(12, 14, 20);
        const unsigned ref = P(before, rc.x + 200, rc.y + rc.h + 150);
        r.sim.Click(rc.x + rc.h * 0.5f, CenterY(rc), ui); r.sim.Move(580, 540); r.sim.Frames(2, ui);
        CHECK(CountNot(r.sim.Draw(12, 14, 20), (int)rc.x, (int)(rc.y + rc.h + 4), (int)(rc.x + 240), (int)(rc.y + rc.h + 200), ref) == 0);
    }
    {   // hostile input: null, NaN, huge, in no window
        Rig r; float nan[4] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -5.0f, 1e30f};
        auto ui = [&] {
            UI::ColorEdit4("outside", nan);                                             // outside any window: nothing
            if (UI::Begin("W", 10, 10, 400, 300)) {
                UI::ColorEdit4("a", nullptr); UI::ColorEdit3("b", nullptr); UI::ColorEdit4("c", nan); UI::ColorEdit3("d", nan, 0x7fffffff);
                UI::ColorButton("e", nullptr); UI::ColorButton("f", nan, std::numeric_limits<float>::quiet_NaN());
                UI::ColorPicker4("g", nullptr); UI::ColorPicker3("h", nullptr); UI::ColorPicker4("i", nan); UI::ColorPicker3("j", nan, -1);
            }
            UI::End();
        };
        r.sim.Frames(3, ui);
        CHECK(true);
        const UI::DrawData d = UI::GetDrawData();
        CHECK(d.indexCount % 3 == 0 && d.dropped == 0);
    }
}

// where the picker's square is: the cursor where it starts, and the size it works out
struct PickerRig {
    Sim sim{500, 520};
    float c[4] = {1, 0, 0, 1};
    int flags = 0;
    bool three = false;
    float c3[3] = {1, 0, 0};
    UI::Vec2 origin{0, 0};
    float side = 0, barW = 18, gap = 8;
    bool changed = false;
    bool alphaBar = true;
    PickerRig() { Reset(); }
    void Ui() {
        if (UI::Begin("W", 10, 10, 480, 500)) {
            origin = UI::GetCursorScreenPos();
            const float avail = UI::GetContentRegionAvail().x;
            const bool hasAlpha = !three && !(flags & UI::ColorEdit_NoAlpha) && !(flags & UI::ColorEdit_NoAlphaBar);
            alphaBar = hasAlpha;
            side = std::max(48.0f, std::min(avail - (hasAlpha ? 2.0f : 1.0f) * (barW + gap), UI::GetFrameHeight() * 7.0f));
            changed = three ? UI::ColorPicker3("p", c3, flags) : UI::ColorPicker4("p", c, flags);
        }
        UI::End();
    }
    void F(int n = 1) { for (int i = 0; i < n; ++i) sim.Frame([&] { Ui(); }); }
    float SvX(float s) const { return origin.x + s * side; }
    float SvY(float v) const { return origin.y + (1.0f - v) * side; }
    float HueX() const { return origin.x + side + gap + barW * 0.5f; }
    float AlphaX() const { return origin.x + side + 2 * gap + barW + barW * 0.5f; }
    void Drag(float x0, float y0, float x1, float y1) { sim.Move(x0, y0); F(); sim.Down(); F(); sim.Move(x1, y1); F(); sim.Up(); F(); }
};

void TestPicker() {
    RUN_SUITE("colours: ColorPicker4 / ColorPicker3");
    {   // the square: saturation across, value up
        PickerRig p; p.F(2);
        CHECK(p.side > 100 && p.side <= 200);
        p.Drag(p.SvX(0.5f), p.SvY(0.5f), p.SvX(0.5f), p.SvY(0.5f));
        CHECK_NEAR(p.c[0], 0.5, 0.01); CHECK_NEAR(p.c[1], 0.25, 0.01); CHECK_NEAR(p.c[2], 0.25, 0.01); CHECK(p.c[3] == 1.0f);
        p.Drag(p.SvX(0.999f), p.SvY(0.999f), p.SvX(0.999f), p.SvY(0.999f));       // top right: the pure hue
        CHECK_NEAR(p.c[0], 1.0, 0.02); CHECK_NEAR(p.c[1], 0.0, 0.02);
        p.Drag(p.SvX(0.001f), p.SvY(0.999f), p.SvX(0.001f), p.SvY(0.999f));       // top left: white
        CHECK_NEAR(p.c[0], 1.0, 0.01); CHECK_NEAR(p.c[1], 1.0, 0.01); CHECK_NEAR(p.c[2], 1.0, 0.01);
        p.Drag(p.SvX(0.7f), p.SvY(0.001f), p.SvX(0.7f), p.SvY(0.001f));       // the bottom edge: black, whatever the saturation
        CHECK(p.c[0] < 0.01f && p.c[1] < 0.01f && p.c[2] < 0.01f);
        // a drag that leaves the square is clamped to its edge
        p.Drag(p.SvX(0.5f), p.SvY(0.5f), p.SvX(3.0f), p.SvY(3.0f));
        CHECK_NEAR(p.c[0], 1.0, 0.01); CHECK_NEAR(p.c[1], 0.0, 0.01);
    }
    {   // the hue bar keeps saturation and value; grey keeps its hue
        PickerRig p; p.c[0] = 0.5f; p.c[1] = 0.25f; p.c[2] = 0.25f; p.F(2);   // h 0, s 0.5, v 0.5
        p.Drag(p.HueX(), p.origin.y + p.side / 3.0f, p.HueX(), p.origin.y + p.side / 3.0f);
        float h, s, v; UI::ColorConvertRGBtoHSV(p.c[0], p.c[1], p.c[2], &h, &s, &v);
        CHECK_NEAR(h, 1.0 / 3.0, 0.01); CHECK_NEAR(s, 0.5, 0.01); CHECK_NEAR(v, 0.5, 0.01);
        // the caller makes it grey; the picker still remembers the hue
        p.c[0] = p.c[1] = p.c[2] = 0.5f; p.F(2);
        p.Drag(p.SvX(0.999f), p.SvY(0.999f), p.SvX(0.999f), p.SvY(0.999f));            // full saturation and value: the remembered hue (green)
        CHECK(p.c[1] > 0.95f && p.c[0] < 0.1f && p.c[2] < 0.1f);
        // and black keeps hue and saturation
        p.Drag(p.SvX(0.001f), p.SvY(0.001f), p.SvX(0.001f), p.SvY(0.001f)); CHECK(p.c[1] < 0.01f);
        p.c[0] = p.c[1] = p.c[2] = 0.0f; p.F(2);
        p.Drag(p.SvX(0.999f), p.SvY(0.999f), p.SvX(0.999f), p.SvY(0.999f));
        CHECK(p.c[1] > 0.95f);
    }
    {   // the alpha bar: opaque at the top, clear at the bottom; the colour does not move
        PickerRig p; p.c[0] = 0.8f; p.c[1] = 0.4f; p.c[2] = 0.2f; p.F(2);
        p.Drag(p.AlphaX(), p.origin.y + p.side * 0.25f, p.AlphaX(), p.origin.y + p.side * 0.25f);
        CHECK_NEAR(p.c[3], 0.75, 0.01);
        CHECK(p.c[0] == 0.8f && p.c[1] == 0.4f && p.c[2] == 0.2f);              // untouched, not round-tripped through HSV
        p.Drag(p.AlphaX(), p.origin.y + 1, p.AlphaX(), p.origin.y + 1);
        CHECK_NEAR(p.c[3], 1.0, 0.02);
        p.Drag(p.AlphaX(), p.origin.y + 2, p.AlphaX(), p.origin.y + p.side + 50);   // pressed on the bar, dragged far below it: clamped to clear
        CHECK(p.c[3] < 0.01f);
        p.c[3] = 0.6f; p.F(2);
        p.Drag(p.AlphaX(), p.origin.y + p.side + 50, p.AlphaX(), p.origin.y + 3);     // a press below the bar grabs nothing
        CHECK_NEAR(p.c[3], 0.6, 0.0001);
    }
    {   // the picture: square corners, hue bar, alpha bar gradient
        PickerRig p; p.F(3);
        const Bitmap b = p.sim.Draw(12, 14, 20);
        const unsigned tl = P(b, p.SvX(0) + 2, p.SvY(1) + 2), tr = P(b, p.SvX(1) - 3, p.SvY(1) + 2), bl = P(b, p.SvX(0) + 2, p.SvY(0) - 3);
        CHECK(((tl >> 24) & 255) > 240 && ((tl >> 16) & 255) > 230 && ((tl >> 8) & 255) > 230);   // white-ish
        CHECK(((tr >> 24) & 255) > 240 && ((tr >> 16) & 255) < 20);                               // the pure hue (red)
        CHECK(((bl >> 24) & 255) < 20 && ((bl >> 16) & 255) < 20);                                // black
        const unsigned hueTop = P(b, p.HueX(), p.origin.y + 2), hueMid = P(b, p.HueX(), p.origin.y + p.side * 0.5f);
        CHECK(((hueTop >> 24) & 255) > 240 && ((hueTop >> 16) & 255) < 30);                       // red at the top
        CHECK(((hueMid >> 24) & 255) < 60 && ((hueMid >> 16) & 255) > 200);                       // cyan in the middle
        const unsigned aTop = P(b, p.AlphaX(), p.origin.y + 3), aBot = P(b, p.AlphaX(), p.origin.y + p.side - 4);
        CHECK(((aTop >> 24) & 255) > 200 && ((aTop >> 16) & 255) < 60);                           // red at the top
        CHECK(aTop != aBot);
        // the marker ring is visible at the colour's place: its pixels differ from the plain gradient there
        PickerRig q; q.F(3);
        const Bitmap b2 = q.sim.Draw(12, 14, 20);
        CHECK(CountNot(b2, (int)(q.SvX(1) - 9), (int)(q.SvY(1) - 3), (int)(q.SvX(1) + 1), (int)(q.SvY(1) + 9), P(b2, q.SvX(1) - 3, q.SvY(1) + 14)) > 5);
    }
    {   // NoAlpha / ColorPicker3: no alpha bar, alpha not written; NoAlphaBar keeps typing alpha possible
        PickerRig p; p.three = true; p.c3[1] = 0.5f; p.F(3);
        CHECK(!p.alphaBar);
        p.Drag(p.SvX(0.999f), p.SvY(0.999f), p.SvX(0.999f), p.SvY(0.999f));
        CHECK_NEAR(p.c3[0], 1.0, 0.02); CHECK_NEAR(p.c3[1], 0.5, 0.02); CHECK_NEAR(p.c3[2], 0.0, 0.02);   // the pure hue of the orange it started as
        p.Drag(p.SvX(0.001f), p.SvY(0.999f), p.SvX(0.001f), p.SvY(0.999f));
        CHECK_NEAR(p.c3[2], 1.0, 0.02);
        PickerRig q; q.flags = UI::ColorEdit_NoAlpha; q.c[3] = 0.3f; q.F(3);
        q.Drag(q.SvX(0.5f), q.SvY(0.5f), q.SvX(0.5f), q.SvY(0.5f));
        CHECK(q.c[3] == 0.3f);                                                     // never touched
    }
    {   // the original preview: click its right half to go back
        PickerRig p; p.c[0] = 0.2f; p.c[1] = 0.4f; p.c[2] = 0.6f; p.F(3);
        p.Drag(p.SvX(0.999f), p.SvY(0.999f), p.SvX(0.999f), p.SvY(0.999f));
        CHECK(std::fabs(p.c[0] - 0.2f) > 0.1f);
        // the preview strip is the next row under the square (classic look: 6 px between rows, rows are one frame height)
        const float totalW = p.side + (p.alphaBar ? 2.0f : 1.0f) * (p.barW + p.gap) - p.gap;
        const float rowH = UI::GetFrameHeight();
        p.sim.Move(p.origin.x + totalW * 0.75f, p.origin.y + p.side + 6.0f + rowH * 0.5f); p.F(); p.sim.Down(); p.F(); p.sim.Up(); p.F();
        CHECK_NEAR(p.c[0], 0.2, 0.002); CHECK_NEAR(p.c[1], 0.4, 0.002); CHECK_NEAR(p.c[2], 0.6, 0.002);
    }
    {   // a picture of the picker (golden): a mid orange with alpha, the hue bar and the hex row
        PickerRig p; p.c[0] = 0.9f; p.c[1] = 0.5f; p.c[2] = 0.2f; p.c[3] = 0.7f; p.F(3);
        p.Drag(p.SvX(0.8f), p.SvY(0.7f), p.SvX(0.8f), p.SvY(0.7f));
        p.sim.Move(480, 500); p.F(2);
        std::string why;
        const bool ok = MatchesGolden("colorpicker", p.sim.Draw(12, 14, 20), &why);
        if (!ok) std::cerr << "  golden colorpicker: " << why << "\n";
        CHECK(ok);
    }
    {   // the hex field follows the colour and applies typed colours; the cells show 0..255
        PickerRig p; p.c[0] = 1; p.c[1] = 0.5f; p.c[2] = 0; p.F(3);
        const float totalW = p.side + 2.0f * (p.barW + p.gap) - p.gap;
        const float rowH = UI::GetFrameHeight();
        // rows below the square: preview, hex field, cells (6 px spacing)
        const float hexY = p.origin.y + p.side + 6.0f + rowH + 6.0f + rowH * 0.5f;
        p.sim.Click(p.origin.x + totalW * 0.5f, hexY, [&] { p.Ui(); });
        p.sim.Press(Key_SelectAll); p.F();
        p.sim.Type("#336699"); p.F();
        CHECK_NEAR(p.c[0], 0x33 / 255.0, 1e-4); CHECK_NEAR(p.c[1], 0x66 / 255.0, 1e-4); CHECK_NEAR(p.c[2], 0x99 / 255.0, 1e-4); CHECK(p.c[3] == 1.0f);
        p.sim.Press(Key_SelectAll); p.F();
        p.sim.Type("#11223344"); p.F();
        CHECK_NEAR(p.c[3], 0x44 / 255.0, 1e-4);
        p.sim.Press(Key_SelectAll); p.F();
        p.sim.Type("#zzz"); p.F();
        CHECK_NEAR(p.c[0], 0x11 / 255.0, 1e-4);                                       // garbage changes nothing
    }
}

// ---- fuzz: random flags, floats (NaN, inf, huge), mouse over the picker and a ColorEdit with its popup ------------------------

struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
    unsigned Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 16); }
    int Int(int n) { return (int)(Next() % (unsigned)n); }
    float F(float a, float b) { return a + (b - a) * (float)(Next() % 10001) / 10000.0f; }
    float Nasty() {
        switch (Int(8)) {
            case 0: return std::numeric_limits<float>::quiet_NaN();
            case 1: return std::numeric_limits<float>::infinity();
            case 2: return -std::numeric_limits<float>::infinity();
            case 3: return 1e30f;
            case 4: return -1e30f;
            default: return F(-0.5f, 1.5f);
        }
    }
};

void TestFuzz() {
    RUN_SUITE("colours: fuzz");
    long long frames = 0;
    for (int seed = 1; seed <= 60; ++seed) {
        Reset();
        if (seed & 1) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        Rng r((unsigned long long)seed);
        Sim sim(500, 520);
        float a[4] = {r.Nasty(), r.Nasty(), r.Nasty(), r.Nasty()}, b[4] = {0.3f, 0.6f, 0.9f, 0.5f}, c[3] = {0.1f, 0.2f, 0.3f};
        int flags = 0;
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 480, 500)) {
                UI::ColorPicker4("a", a, flags);
                UI::ColorEdit4("b", b, flags);
                UI::ColorEdit3("c", c, flags);
                UI::ColorButton("d", b, flags, 0);
            }
            UI::End();
        };
        for (int f = 0; f < 150; ++f) {
            flags = r.Int(256);
            const int what = r.Int(10);
            if (what < 4) sim.Move(r.F(-10, 510), r.F(-10, 530));
            else if (what < 6) { if (!sim.down) sim.Down(); }
            else if (what < 8) { if (sim.down) sim.Up(); }
            else if (what == 8) sim.Type(r.Int(2) ? "#aB09" : "ff");
            else if (r.Int(3) == 0) sim.Press(Key_SelectAll);
            if (r.Int(30) == 0) { a[r.Int(4)] = r.Nasty(); b[r.Int(4)] = r.Nasty(); }
            sim.Frame(ui);
            ++frames;
            for (int i = 0; i < 4; ++i) { /* a may stay NaN until the picker writes it; a written value is finite and in range */ }
            const UI::DrawData d = UI::GetDrawData();
            CHECK(d.indexCount % 3 == 0 && d.dropped == 0);
            if (f % 50 == 0) sim.Draw(12, 14, 20);
        }
        for (int i = 0; i < 4; ++i) if (std::isfinite(b[i])) CHECK(b[i] >= -1e30f);
        if (seed & 1) UI::UseLauncherLook(false);
    }
    CHECK(frames > 0);
}

} // namespace

int main() {
    TestStyleColors();
    TestConversions();
    TestWidgetsUseColours();
    TestColorEdit();
    TestPicker();
    TestFuzz();
    TEST_MAIN_RESULT();
}
