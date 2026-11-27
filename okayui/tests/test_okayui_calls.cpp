// Step 6.10 calls, headless: SetNextWindowCollapsed, SetNextWindowBgAlpha, SetWindowPos, BringWindowToDisplayFront, FindWindowByID,
// GetTopMostPopupModal, SetNextFrameWantCaptureKeyboard, GetTreeNodeToLabelSpacing, GetCursorStartPos, viewport calls, PushItemFlag,
// PushMultiItemsWidths, FindRenderedTextEnd, the context shims, StyleColorsDark / Light / Classic, Save / LoadIniSettings, and the
// draw-list AddNgon, AddNgonFilled, AddImageQuad, AddImageRounded.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

static void Golden(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("okayui step 6.10 calls");

    {   // SetNextWindowCollapsed: Always folds every time; FirstUseEver only the first time; NoCollapse windows never fold
        Reset(); Sim s(300, 200);
        int bodyRuns = 0;
        auto win = [&](bool collapse, UI::Cond c, int flags = 0) {
            UI::SetNextWindowPos({10, 10}); UI::SetNextWindowSize({200, 120});
            UI::SetNextWindowCollapsed(collapse, c);
            if (UI::Begin("coll", nullptr, flags)) ++bodyRuns;
            UI::End();
        };
        s.Frame([&] { win(true, UI::Cond_FirstUseEver); });
        CHECK(bodyRuns == 0);                                          // folded on first use
        s.Frame([&] { win(false, UI::Cond_FirstUseEver); });
        CHECK(bodyRuns == 0);                                          // the second FirstUseEver call is ignored
        s.Frame([&] { win(false, UI::Cond_Always); });
        CHECK(bodyRuns == 1);                                          // Always opens it
        s.Frame([&] { win(true, UI::Cond_Always); });
        CHECK(bodyRuns == 1);                                          // and folds it
        s.Frame([&] { win(true, UI::Cond_Always, UI::Window_NoCollapse); });
        CHECK(bodyRuns == 2);                                          // NoCollapse: never folded
        s.Frame([&] { UI::SetNextWindowCollapsed(true); UI::Begin(nullptr); UI::SetNextWindowBgAlpha(0.5f); });
        s.Frame([&] { win(false, UI::Cond_Always); }); CHECK(bodyRuns == 3);   // a null-title Begin drops the pending request
    }
    {   // SetNextWindowBgAlpha: the body is more transparent, the title bar is not
        Reset(); Sim s(300, 200);
        auto draw = [&](float a) {
            s.Frame([&] { UI::SetNextWindowPos({20, 20}); UI::SetNextWindowSize({200, 120}); if (a >= 0) UI::SetNextWindowBgAlpha(a); UI::Begin("bg"); UI::End(); });
            return s.Draw(255, 0, 0);
        };
        const Bitmap opaque = draw(-1.0f);
        const unsigned bodyOpaque = PixelAt(opaque, 120, 110), titleOpaque = PixelAt(opaque, 120, 28);
        const Bitmap half = draw(0.5f);
        const unsigned bodyHalf = PixelAt(half, 120, 110), titleHalf = PixelAt(half, 120, 28);
        const Bitmap clear = draw(0.0f);
        const unsigned bodyClear = PixelAt(clear, 120, 110);
        CHECK(bodyHalf != bodyOpaque && bodyHalf != Rgba(255, 0, 0));          // blended with the red behind it
        CHECK(((bodyHalf >> 24) & 255) > ((bodyOpaque >> 24) & 255));         // so it is redder
        CHECK(titleHalf == titleOpaque);                                       // the title bar is untouched
        CHECK(bodyClear == Rgba(255, 0, 0));                                   // alpha 0: the host colour shows through
        const Bitmap again = draw(-1.0f);                                      // one request, used up
        CHECK(PixelAt(again, 120, 110) == bodyOpaque);
        const Bitmap bad = draw(std::nanf(""));                                // non-finite: ignored
        CHECK(PixelAt(bad, 120, 110) == bodyOpaque);
    }
    {   // SetWindowPos by title and for the current window; FindWindowByID; BringWindowToDisplayFront
        Reset(); Sim s(400, 300);
        UI::CurrentWindow cw;
        auto ui = [&] {
            UI::SetNextWindowPos({10, 10}, UI::Cond_FirstUseEver); UI::SetNextWindowSize({100, 80}, UI::Cond_FirstUseEver);
            if (UI::Begin("A")) cw = UI::GetCurrentWindow();
            UI::End();
            UI::SetNextWindowPos({50, 50}, UI::Cond_FirstUseEver); UI::SetNextWindowSize({100, 80}, UI::Cond_FirstUseEver);
            UI::Begin("B"); UI::End();
        };
        s.Frames(2, ui);
        CHECK(UI::SetWindowPos("A", {200, 150}));
        s.Frames(2, ui);
        CHECK(UI::FindWindowByName("A").pos.x == 200 && UI::FindWindowByName("A").pos.y == 150);
        CHECK(!UI::SetWindowPos("nope", {1, 1}) && !UI::SetWindowPos(nullptr, {1, 1}) && !UI::SetWindowPos("", {1, 1}));
        CHECK(!UI::SetWindowPos("A", {std::nanf(""), 0}));
        CHECK(UI::SetWindowPos("A", {5, 5}, UI::Cond_Once));                  // found, but the condition says no (already positioned)
        s.Frames(2, ui);
        CHECK(UI::FindWindowByName("A").pos.x == 200);
        const UI::WindowInfo byId = UI::FindWindowByID(cw.id);
        CHECK(byId.found && byId.pos.x == 200 && !byId.docked);
        CHECK(!UI::FindWindowByID(0).found && !UI::FindWindowByID(12345u).found);
        // stacking: B was drawn last so it is in front; raising A puts A in front
        UI::CurrentWindow before; before.valid = true; std::strcpy(before.title, "A");
        CHECK(UI::BringWindowToDisplayFront(before));
        CHECK(UI::BringWindowToDisplayFront("A") && !UI::BringWindowToDisplayFront("zzz") && !UI::BringWindowToDisplayFront(nullptr));
        UI::CurrentWindow none;                                                // invalid snapshot
        CHECK(!UI::BringWindowToDisplayFront(none));
        // SetWindowPos on the current window moves it one frame later
        s.Frames(1, [&] { UI::SetNextWindowPos({10, 10}, UI::Cond_FirstUseEver); UI::SetNextWindowSize({100, 80}, UI::Cond_FirstUseEver); if (UI::Begin("A")) UI::SetWindowPos({77, 88}); UI::End(); });
        s.Frames(1, [&] { UI::Begin("A"); UI::End(); });
        CHECK(UI::FindWindowByName("A").pos.x == 77 && UI::FindWindowByName("A").pos.y == 88);
        UI::SetWindowPos({1, 1});                                              // outside a window: nothing happens
        CHECK(UI::FindWindowByName("A").pos.x == 77);
    }
    {   // GetTopMostPopupModal
        Reset(); Sim s(400, 300);
        bool open = false, modal = false;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 400, 300)) {
                if (open) { UI::OpenPopup("m"); open = false; }
                if (UI::BeginPopupModal("m", nullptr)) { UI::Text("hello"); UI::EndPopup(); }
                modal = UI::GetTopMostPopupModal();
            }
            UI::End();
        };
        s.Frames(2, ui); CHECK(!modal);
        open = true; s.Frames(3, ui); CHECK(modal);
        s.Tap(Key_Escape, ui); s.Frames(2, ui); CHECK(!modal);                 // Escape closes it
    }
    {   // SetNextFrameWantCaptureKeyboard
        Reset(); Sim s(100, 100);
        s.Frame([] {}); CHECK(!UI::GetIO().wantCaptureKeyboard);
        s.Frame([] { UI::SetNextFrameWantCaptureKeyboard(true); });
        CHECK(!UI::GetIO().wantCaptureKeyboard);                               // not this frame
        s.Frame([] {}); CHECK(UI::GetIO().wantCaptureKeyboard);                // the next one
        s.Frame([] {}); CHECK(!UI::GetIO().wantCaptureKeyboard);               // and only that one
        s.Frame([] { UI::SetNextFrameWantCaptureKeyboard(false); }); s.Frame([] {}); CHECK(!UI::GetIO().wantCaptureKeyboard);
    }
    {   // tree spacing, cursor start, viewport calls
        Reset(); Sim s(300, 200);
        float sp = -1; UI::Vec2 st{-1, -1}, ref{-2, -2}, vp{0, 0};
        const UI::Vec2 outside = UI::GetCursorStartPos();
        CHECK(outside.x == 0 && outside.y == 0);
        s.Frame([&] {
            UI::SetNextWindowPos({20, 30}); UI::SetNextWindowSize({260, 150});
            if (UI::Begin("w")) {
                st = UI::GetCursorStartPos(); ref = UI::GetWindowContentRegionMin(); sp = UI::GetTreeNodeToLabelSpacing();
                vp = UI::GetWindowViewport().size; UI::SetNextWindowViewport(7); UI::SetNextWindowViewport(0);
            }
            UI::End();
        });
        CHECK(st.x == ref.x && st.y == ref.y && st.x > 0 && st.y > 0);
        CHECK(sp >= 16.0f && sp <= 64.0f);
        CHECK(vp.x == 300 && vp.y == 200);
        Reset(); UI::UseLauncherLook(true);
        float spLook = 0; s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 100, 100)) spLook = UI::GetTreeNodeToLabelSpacing(); UI::End(); });
        CHECK(spLook == 24.0f);
        UI::UseLauncherLook(false);
    }
    {   // PushItemFlag: Disabled blocks clicks between push and pop; the other flags are accepted and change nothing
        Reset(); Sim s(300, 100);
        int clicks = 0; UI::Rect r; int mode = 0;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 100)) {
                if (mode == 1) UI::PushItemFlag(UI::ItemFlag_Disabled, true);
                if (mode == 2) UI::PushItemFlag(UI::ItemFlag_NoTabStop | UI::ItemFlag_MixedValue, true);
                if (mode == 3) UI::PushItemFlag(UI::ItemFlag_Disabled, false);
                if (UI::Button("b")) ++clicks;
                r = UI::GetItemRect();
                if (mode) UI::PopItemFlag();
            }
            UI::End();
        };
        s.Frame(ui);
        for (mode = 0; mode <= 3; ++mode) {
            const int before = clicks;
            s.Click(CenterX(r), CenterY(r), ui);
            CHECK(clicks == before + (mode == 1 ? 0 : 1));
        }
        UI::PopItemFlag(); UI::PopItemFlag();                                  // unbalanced pops are safe
        for (int i = 0; i < 40; ++i) UI::PushItemFlag(UI::ItemFlag_Disabled, true);   // more than the stack holds
        for (int i = 0; i < 40; ++i) UI::PopItemFlag();
        s.Frame(ui);
    }
    {   // PushMultiItemsWidths: three items share the width, the first item's width is on top
        Reset(); Sim s(500, 100);
        float w[3] = {0, 0, 0}, total = 0, gap = 0;
        s.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 500, 100)) {
                total = 300.0f; gap = UI::GetStyle().ItemInnerSpacing.x;
                UI::PushMultiItemsWidths(3, total);
                for (int i = 0; i < 3; ++i) { w[i] = UI::CalcItemWidth(); UI::PopItemWidth(); }
                UI::PushMultiItemsWidths(0, -5.0f); UI::PopItemWidth();         // bad counts and widths are clamped
                UI::PushMultiItemsWidths(100, std::nanf("")); for (int i = 0; i < 8; ++i) UI::PopItemWidth();
                UI::PushMultiItemsWidths(2, 1e9f); UI::PopItemWidth(); UI::PopItemWidth();
            }
            UI::End();
        });
        CHECK(w[0] > 50 && w[1] == w[0]);
        CHECK(std::fabs(w[0] + w[1] + w[2] + 2 * gap - total) <= 1.0f);
        CHECK(w[2] >= w[0] && w[2] - w[0] < 3.0f);
    }
    {   // FindRenderedTextEnd
        const char* a = "Name##id"; CHECK(UI::FindRenderedTextEnd(a) == a + 4);
        const char* b = "Plain"; CHECK(UI::FindRenderedTextEnd(b) == b + 5);
        const char* c = "##only"; CHECK(UI::FindRenderedTextEnd(c) == c);
        const char* d = "a#b#c"; CHECK(UI::FindRenderedTextEnd(d) == d + 5);
        CHECK(UI::FindRenderedTextEnd("") != nullptr && *UI::FindRenderedTextEnd("") == 0);
        CHECK(UI::FindRenderedTextEnd(nullptr) == nullptr);
        const char* e = "Name##id"; CHECK(UI::FindRenderedTextEnd(e, e + 3) == e + 3);          // search stops at textEnd
        CHECK(UI::FindRenderedTextEnd(e, e + 6) == e + 4);                                      // a "##" that fits is found
        const char* f = "ab#"; CHECK(UI::FindRenderedTextEnd(f, f + 3) == f + 3);             // a lone # at the end is not "##"
    }
    {   // context shims
        Reset();
        UI::Context* c = UI::CreateContext();
        CHECK(c != nullptr && UI::GetCurrentContext() == c);
        UI::Context* c2 = UI::CreateContext();                    // a second context (test_okayui_contexts tests them); c stays current
        CHECK(c2 != nullptr && c2 != c && UI::GetCurrentContext() == c);
        UI::DestroyContext(c2); CHECK(UI::GetCurrentContext() == c);
        UI::SetCurrentContext(nullptr); CHECK(UI::GetCurrentContext() == nullptr);
        UI::SetCurrentContext(reinterpret_cast<UI::Context*>(0x1234)); CHECK(UI::GetCurrentContext() == nullptr);   // a foreign handle is ignored
        UI::SetCurrentContext(c); CHECK(UI::GetCurrentContext() == c);
        UI::Style().rounding = 9.0f;
        UI::DestroyContext(reinterpret_cast<UI::Context*>(0x1234)); CHECK(UI::GetCurrentContext() == c && UI::Style().rounding == 9.0f);
        UI::DestroyContext(c);
        CHECK(UI::GetCurrentContext() == nullptr && UI::Style().rounding != 9.0f);               // state was reset
        CHECK(UI::CreateContext() == c);
        UI::DestroyContext(); CHECK(UI::GetCurrentContext() == nullptr); UI::CreateContext();
    }
    {   // style presets: colours change, sizes stay, set colours are cleared, the three differ
        Reset();
        UI::Style().rounding = 7.0f; UI::Style().textScale = 3.0f; UI::SetStyleColor(UI::Col_Text, 1, 2, 3, 255);
        const unsigned char dark0 = UI::Theme().bg[0];
        UI::StyleColorsLight();
        UI::Theme l = UI::Style();
        CHECK(l.bg[0] > 150 && l.text[0] < 80 && l.rounding == 7.0f && l.textScale == 3.0f && l.colorSet == 0);
        UI::StyleColorsClassic();
        UI::Theme c = UI::Style();
        CHECK(std::memcmp(c.bg, l.bg, 4) != 0 && c.bg[2] > c.bg[0]);
        UI::StyleColorsDark();
        UI::Theme d = UI::Style();
        CHECK(d.bg[0] == dark0 && std::memcmp(d.bg, c.bg, 4) != 0 && d.rounding == 7.0f);
        const UI::Theme def;
        CHECK(std::memcmp(d.text, def.text, 4) == 0 && std::memcmp(d.accent, def.accent, 4) == 0 && std::memcmp(d.titleBg, def.titleBg, 4) == 0);
    }
    {   // style presets on screen: a window with widgets in each preset (golden images)
        const char* names[3] = {"calls_style_dark", "calls_style_light", "calls_style_classic"};
        for (int k = 0; k < 3; ++k) {
            Reset(); Sim s(260, 190);
            if (k == 0) UI::StyleColorsDark(); else if (k == 1) UI::StyleColorsLight(); else UI::StyleColorsClassic();
            bool chk = true; float v = 0.4f; char txt[16] = "hello";
            auto ui = [&] {
                UI::SetNextWindowPos({10, 10}); UI::SetNextWindowSize({240, 170});
                if (UI::Begin("Style")) { UI::Text("Preset"); UI::Button("Button"); UI::Checkbox("Check", &chk); UI::SliderFloat("##s", &v, 0, 1); UI::InputText("##t", txt, sizeof txt); }
                UI::End();
            };
            s.Frames(2, ui);
            Golden(names[k], s.Draw(70, 70, 70));
        }
    }
    {   // SaveIniSettingsToMemory / LoadIniSettingsFromMemory: the dock layout as text
        Reset();
        std::size_t n = 99;
        const char* p = UI::SaveIniSettingsToMemory(&n);
        CHECK(p != nullptr && n == std::strlen(p));
        // The pointer is only valid until the next call, so keep a copy before calling again (the second call frees the old
        // text: Linux happened to leave the bytes in place, the Windows heap did not).
        const std::string a = p ? p : "";
        CHECK(UI::SaveIniSettingsToMemory() != nullptr);
        const std::string before = UI::DockSaveLayout();
        CHECK(before == a);
        if (before != a) std::fprintf(stderr, "  layout text: [%s]\n  memory text: [%s]\n", before.c_str(), a.c_str());
        UI::LoadIniSettingsFromMemory(nullptr, 5);
        UI::LoadIniSettingsFromMemory("garbage that is not a layout", 0);
        CHECK(UI::DockSaveLayout() == before);
        const std::string mine = before; UI::LoadIniSettingsFromMemory(mine.c_str(), mine.size());
        CHECK(UI::DockSaveLayout() == before);
    }
    {   // AddNgon / AddNgonFilled
        Reset(); Sim s(100, 100);
        s.Frame([&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 100, 100)) {
                const UI::DrawList& dl = UI::GetWindowDrawList();
                dl.AddNgonFilled({30, 30}, 20, 0xff0000ffu, 6);
                dl.AddNgon({70, 70}, 20, 0xff00ff00u, 5, 2.0f);
                dl.AddNgonFilled({70, 30}, 20, 0xffffffffu, 2);             // too few corners: nothing
                dl.AddNgon({30, 70}, -4, 0xffffffffu, 6);                   // bad radius: nothing
                dl.AddNgonFilled({std::nanf(""), 0}, 5, 0xffffffffu, 6);
                dl.AddNgonFilled({50, 50}, 3, 0xffff0000u, 100000);         // clamped to 128 corners, a tiny disc
            }
            UI::End();
        });
        const Bitmap b = s.Draw(0, 0, 0);
        CHECK(PixelAt(b, 30, 30) == Rgba(255, 0, 0));                       // inside the hexagon (colours are 0xAABBGGRR)
        CHECK(PixelAt(b, 49, 30) == Rgba(255, 0, 0));                       // first corner points right: it reaches x = 50
        CHECK(PixelAt(b, 30, 7) == Rgba(0, 0, 0));                          // above the flat top edge (hexagon half height 17.3)
        CHECK(PixelAt(b, 70, 70) == Rgba(0, 0, 0));                         // outline only: the centre is empty
        CHECK(CountNot(b, 50, 50, 100, 100, Rgba(0, 0, 0)) > 20);           // the outline is there
        CHECK(PixelAt(b, 50, 50) != Rgba(0, 0, 0) );                        // the tiny disc
        CHECK(PixelAt(b, 70, 30) == Rgba(0, 0, 0) && PixelAt(b, 30, 70) == Rgba(0, 0, 0));
    }
    {   // AddImageQuad / AddImageRounded: geometry and texture coordinates, read from the draw data
        Reset(); Sim s(200, 200);
        static char dummy; okay::ui2d::Tex tex = reinterpret_cast<okay::ui2d::Tex>(&dummy);
        auto frame = [&](const std::function<void(const UI::DrawList&)>& f) {
            s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 200, 200)) f(UI::GetWindowDrawList()); UI::End(); });
            return UI::GetDrawData();
        };
        auto texVerts = [&](const UI::DrawData& d, std::vector<UI::DrawVertex>& out) {
            out.clear();
            for (int c = 0; c < d.cmdCount; ++c) if (d.cmds[c].texture == (const void*)tex)
                for (int i = 0; i < d.cmds[c].idxCount; ++i) out.push_back(((const UI::DrawVertex*)d.vertices)[d.indices[d.cmds[c].idxOffset + i]]);
        };
        std::vector<UI::DrawVertex> v;
        // quad: two triangles, positions and uvs as given
        UI::DrawData d = frame([&](const UI::DrawList& dl) { dl.AddImageQuad(tex, {10, 10}, {60, 20}, {50, 70}, {5, 60}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, 0x80ff00ffu); });
        texVerts(d, v);
        CHECK(v.size() == 6);
        bool ok = v.size() == 6;
        for (auto& q : v) ok = ok && q.r == 0xff && q.g == 0x00 && q.b == 0xff && q.a == 0x80 && ((q.u == 0 || q.u == 1) && (q.v == 0 || q.v == 1));
        CHECK(ok);
        // a null texture, a non-finite corner: nothing
        d = frame([&](const UI::DrawList& dl) { dl.AddImageQuad(nullptr, {0, 0}, {1, 0}, {1, 1}, {0, 1}); dl.AddImageQuad(tex, {0, 0}, {1, 0}, {std::nanf(""), 1}, {0, 1}); });
        texVerts(d, v); CHECK(v.empty());
        // rounded, all corners: no vertex at the rectangle's corners, every vertex inside, uv is linear in position
        d = frame([&](const UI::DrawList& dl) { dl.AddImageRounded(tex, {20, 20}, {120, 100}, {0.25f, 0.5f}, {0.75f, 1.0f}, 0xffffffffu, 16.0f); });
        texVerts(d, v);
        CHECK(v.size() >= 3 * 12);
        bool inside = true, corner = false, lin = true;
        for (auto& q : v) {
            inside = inside && q.x >= 19.99f && q.x <= 120.01f && q.y >= 19.99f && q.y <= 100.01f;
            if ((std::fabs(q.x - 20) < 0.01f || std::fabs(q.x - 120) < 0.01f) && (std::fabs(q.y - 20) < 0.01f || std::fabs(q.y - 100) < 0.01f)) corner = true;
            lin = lin && std::fabs(q.u - (0.25f + 0.5f * (q.x - 20) / 100)) < 1e-3f && std::fabs(q.v - (0.5f + 0.5f * (q.y - 20) / 80)) < 1e-3f;
        }
        CHECK(inside && !corner && lin);
        // only the top corners rounded: the bottom corners are exact points, the top ones are cut
        d = frame([&](const UI::DrawList& dl) { dl.AddImageRounded(tex, {20, 20}, {120, 100}, {0, 0}, {1, 1}, 0xffffffffu, 16.0f, UI::DrawFlags_RoundCornersTop); });
        texVerts(d, v);
        bool bl = false, br = false, tl = false;
        for (auto& q : v) { if (q.x == 20 && q.y == 100) bl = true; if (q.x == 120 && q.y == 100) br = true; if (q.x == 20 && q.y == 20) tl = true; }
        CHECK(bl && br && !tl);
        // zero rounding, DrawFlags_None and an over-large rounding: a plain quad / a pill that stays inside
        d = frame([&](const UI::DrawList& dl) { dl.AddImageRounded(tex, {20, 20}, {120, 100}, {0, 0}, {1, 1}, 0xffffffffu, 0.0f); });
        texVerts(d, v); CHECK(v.size() == 6);
        d = frame([&](const UI::DrawList& dl) { dl.AddImageRounded(tex, {20, 20}, {120, 100}, {0, 0}, {1, 1}, 0xffffffffu, 30.0f, UI::DrawFlags_None); });
        texVerts(d, v); CHECK(v.size() == 6);
        d = frame([&](const UI::DrawList& dl) { dl.AddImageRounded(tex, {20, 20}, {120, 100}, {0, 0}, {1, 1}, 0xffffffffu, 1e9f); });
        texVerts(d, v);
        bool in2 = !v.empty(); for (auto& q : v) in2 = in2 && q.x >= 19.99f && q.x <= 120.01f && q.y >= 19.99f && q.y <= 100.01f;
        CHECK(in2);
        // bad input: nothing and no crash
        d = frame([&](const UI::DrawList& dl) {
            dl.AddImageRounded(nullptr, {0, 0}, {10, 10}, {0, 0}, {1, 1}, 0xffffffffu, 4);
            dl.AddImageRounded(tex, {10, 10}, {0, 0}, {0, 0}, {1, 1}, 0xffffffffu, 4);
            dl.AddImageRounded(tex, {0, 0}, {10, 10}, {std::nanf(""), 0}, {1, 1}, 0xffffffffu, 4);
            dl.AddImageRounded(tex, {0, 0}, {10, 10}, {0, 0}, {1, 1}, 0xffffffffu, std::nanf(""));   // NaN rounding means none: a plain quad
        });
        texVerts(d, v); CHECK(v.size() == 6);
    }
    {   // MouseButton names: Middle is never down or clicked; Left and Right behave as before
        Reset(); Sim s(200, 100);
        bool mid = false, midClick = false, left = false, leftClick = false, itemMid = false, itemLeft = false, right = false;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 200, 100)) {
                UI::Dummy(100, 50);
                mid = UI::IsMouseDown(UI::MouseButton_Middle); midClick = UI::IsMouseClicked(UI::MouseButton_Middle) || UI::IsMouseReleased(UI::MouseButton_Middle);
                left = UI::IsMouseDown(UI::MouseButton_Left); leftClick = UI::IsMouseClicked(UI::MouseButton_Left);
                right = UI::IsMouseDown(UI::MouseButton_Right);
                itemMid = UI::IsItemClicked(UI::MouseButton_Middle); itemLeft = UI::IsItemClicked(UI::MouseButton_Left);
            }
            UI::End();
        };
        s.Move(20, 20); s.Frame(ui); s.Down(); s.Frame(ui);
        CHECK(left && leftClick && itemLeft && !mid && !midClick && !itemMid && !right);
        s.Up(); s.Frame(ui);
        CHECK(!mid && !midClick);                                              // the left release is not a middle release
    }
    {   // Dir names drive ArrowButton; the ImGui key spellings are the same keys
        Reset(); Sim s(100, 60);
        int clicks = 0; UI::Rect r;
        auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 100, 60)) { if (UI::ArrowButton("a", UI::Dir_Down)) ++clicks; r = UI::GetItemRect(); } UI::End(); };
        s.Frame(ui); s.Click(CenterX(r), CenterY(r), ui); CHECK(clicks == 1);
        static_assert(UI::KeyId_UpArrow == UI::KeyId_Up && UI::KeyId_DownArrow == UI::KeyId_Down && UI::KeyId_LeftArrow == UI::KeyId_Left &&
                      UI::KeyId_RightArrow == UI::KeyId_Right && UI::KeyId_KeypadEnter == UI::KeyId_Enter && UI::KeyId_COUNT > UI::KeyId_Z, "key aliases");
        int fired = 0; auto k = [&] { if (UI::Shortcut(UI::Chord(UI::KeyId_UpArrow))) ++fired; };
        s.Hold(UI::KeyId_Up, true); s.Frame(k); CHECK(fired == 1);
    }
    {   // hover and focus flags: accepted, same answers as the plain calls, Hovered_AnyWindow sees other windows
        Reset(); Sim s(300, 200);
        bool h0 = false, h1 = false, hAnyInside = false, hAnyOther = false, hPlainOther = true, fAny = false, fPlain = false;
        auto ui = [&] {
            UI::SetNextWindowPos({10, 10}); UI::SetNextWindowSize({120, 100});
            if (UI::Begin("one")) { UI::Dummy(50, 20); h0 = UI::IsItemHovered(); h1 = UI::IsItemHovered(UI::Hovered_AllowWhenDisabled | UI::Hovered_DelayNormal); fPlain = UI::IsWindowFocused(); fAny = UI::IsWindowFocused(UI::Focused_AnyWindow | UI::Focused_RootAndChildWindows); }
            UI::End();
            UI::SetNextWindowPos({150, 10}); UI::SetNextWindowSize({120, 100});
            if (UI::Begin("two")) { hAnyOther = UI::IsWindowHovered(UI::Hovered_AnyWindow); hPlainOther = UI::IsWindowHovered(UI::Hovered_RootAndChildWindows); }
            UI::End();
        };
        s.Move(30, 50); s.Frames(2, ui);
        CHECK(h0 == h1 && fAny >= fPlain);
        s.Move(30, 40); s.Frames(2, ui);                                      // over window "one", asking from window "two"
        CHECK(hAnyOther && !hPlainOther);
        (void)hAnyInside;
        s.Move(200, 190); s.Frames(2, ui); CHECK(!hAnyOther);                 // over no window
    }
    {   // BeginChild with flags: Borders draws the panel, AutoResizeY follows the content one frame behind
        Reset(); Sim s(300, 400);
        int rows = 3; float startY = 0, endY = 0; UI::Rect child; int flags = UI::Child_AutoResizeY;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 400)) {
                startY = UI::GetCursorPos().y;
                if (UI::BeginChild("c", UI::Vec2{0, 0}, flags)) { for (int i = 0; i < rows; ++i) UI::Text("row"); }
                UI::EndChild();
                endY = UI::GetCursorPos().y;
            }
            UI::End();
        };
        s.Frame(ui); const float first = endY - startY;
        s.Frames(2, ui); const float three = endY - startY;
        CHECK(three > first && std::fabs(three - ((UI::GetStyle().ItemSpacing.y + 16) * 3 + 12 + UI::GetStyle().ItemSpacing.y)) < 40.0f);
        s.Frame(ui); CHECK(std::fabs((endY - startY) - three) < 0.01f);       // stable
        rows = 8; s.Frames(2, ui); const float eight = endY - startY; CHECK(eight > three + 30);
        rows = 1; s.Frames(2, ui); CHECK(endY - startY < three);              // shrinks again
        rows = 3; flags = UI::Child_None; s.Frames(2, ui); const float fixed = endY - startY;
        CHECK(fixed > 0 && std::fabs(fixed - three) > 1.0f);                  // a plain child keeps its default height
        UI::Vec2 bad{std::nanf(""), std::nanf("")};
        s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 400)) { if (UI::BeginChild("n", bad, UI::Child_AutoResizeY)) {} UI::EndChild(); UI::BeginChild(nullptr, UI::Vec2{0, 0}); } UI::End(); });
        CHECK(!UI::BeginChild("outside", UI::Vec2{10, 10}, UI::Child_Borders));
        // borders: the panel colour shows only with Child_Borders
        Reset(); Sim s2(300, 200);
        auto draw = [&](int f) {
            s2.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 200)) { if (UI::BeginChild("b", UI::Vec2{200, 100}, f)) UI::Text("x"); UI::EndChild(); } UI::End(); });
            return s2.Draw(1, 2, 3);
        };
        const Bitmap plain = draw(UI::Child_None);
        const Bitmap bordered = draw(UI::Child_Borders);
        CHECK(PixelAt(plain, 150, 90) == Rgba(1, 2, 3) && PixelAt(bordered, 150, 90) != Rgba(1, 2, 3));
        const Bitmap legacy = (s2.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 300, 200)) { if (UI::BeginChild("b", 200, 100, true)) UI::Text("x"); UI::EndChild(); } UI::End(); }), s2.Draw(1, 2, 3));
        CHECK(PixelAt(legacy, 150, 90) == PixelAt(bordered, 150, 90));        // same as the (w, h, border) form
    }
    {   // "##id" suffixes are not drawn and take no room: the picture of "Name##id" equals the picture of "Name", for the labelled widgets
        for (int kind = 0; kind < 9; ++kind) {
            Bitmap pic[2];
            for (int v = 0; v < 2; ++v) {
                Reset(); Sim s(320, 60);
                bool b = true; float f = 0.3f; int n = 2; char t[8] = "ab"; float f3[3] = {1, 2, 3}; int sel = 0; const char* items[2] = {"x", "y"};
                const char* lab = v ? "Name##secret" : "Name";
                s.Frames(2, [&] {
                    UI::BareNextWindow();
                    if (UI::Begin("w", 0, 0, 320, 60)) {
                        switch (kind) {
                            case 0: UI::Button(lab); break;
                            case 1: UI::Checkbox(lab, &b); break;
                            case 2: UI::SliderFloat(lab, &f, 0, 1); break;
                            case 3: UI::InputText(lab, t, sizeof t); break;
                            case 4: UI::DragFloat(lab, &f); break;
                            case 5: UI::SliderInt(lab, &n, 0, 5); break;
                            case 6: UI::Combo(lab, items, 2, &sel); break;
                            case 7: UI::DragFloat3(lab, f3); break;
                            case 8: UI::Selectable(lab, false); break;
                        }
                    }
                    UI::End();
                });
                pic[v] = s.Draw(5, 5, 5);
            }
            bool same = pic[0].rgba == pic[1].rgba;
            if (!same) std::cerr << "  label kind " << kind << " differs\n";
            CHECK(same);
        }
        Reset(); Sim s3(200, 40);
        float w0 = 0, w1 = 0;
        s3.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 200, 40)) { UI::Button("##only"); w0 = UI::GetItemRect().w; UI::Button(""); w1 = UI::GetItemRect().w; } UI::End(); });
        CHECK(std::fabs(w0 - w1) < 0.01f);
    }
    {   // type names, flag aliases and the new flags
        UI::ImVec4 a(0.1f, 0.2f, 0.3f, 0.4f); UI::Vec4 v = a; UI::ImVec4 back = v;
        CHECK(v.x == 0.1f && v.w == 0.4f && back.z == 0.3f && UI::ImVec4().w == 0.0f);
        UI::ImRect r(10, 20, 50, 100);
        CHECK(r.GetWidth() == 40 && r.GetHeight() == 80 && r.GetCenter().x == 30 && r.Contains({10, 20}) && !r.Contains({50, 20}) && r.Overlaps(UI::ImRect(40, 90, 60, 110)) && !r.Overlaps(UI::ImRect(50, 20, 60, 30)));
        r.Expand(5); CHECK(r.Min.x == 5 && r.Max.y == 105);
        UI::ImGuiID id = UI::GetID("x"); CHECK(id != 0);
        UI::ImTextureID tex = nullptr; CHECK(tex == nullptr);
        static_assert((int)UI::TabItem_SetSelected == (int)UI::Tab_SetSelected && (int)UI::TableColumn_WidthFixed == (int)UI::Col_WidthFixed && UI::TableColumn_WidthStretch == 0 &&
                      UI::Col_TabSelected == UI::Col_TabActive && UI::StyleVar_FrameBorderSize == UI::StyleVar_BorderSize, "aliases");
    }
    {   // InputText_Password shows one star per byte and never copies; InputText_CharsNoBlank drops typed blanks
        Reset(); Sim s(240, 40);
        char pw[32] = "secret", plain[32] = "secret", nb[32] = ""; UI::Rect rpw, rnb;
        int which = 0;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 240, 40)) {
                if (which == 0) { UI::InputText("##a", pw, sizeof pw, UI::InputText_Password); rpw = UI::GetItemRect(); }
                if (which == 1) { UI::InputText("##a", plain, sizeof plain); }
                if (which == 2) { UI::InputText("##a", nb, sizeof nb, UI::InputText_CharsNoBlank); rnb = UI::GetItemRect(); }
            }
            UI::End();
        };
        which = 0; s.Frames(2, ui); const Bitmap shown = s.Draw(5, 5, 5);
        which = 1; s.Frames(2, ui); const Bitmap raw = s.Draw(5, 5, 5);
        CHECK(shown.rgba != raw.rgba);                                       // the stars differ from the letters
        // the star picture does not depend on what the characters are
        std::strcpy(pw, "abcdef"); which = 0; s.Frames(2, ui); const Bitmap shown2 = s.Draw(5, 5, 5);
        CHECK(shown.rgba == shown2.rgba);
        // selecting everything and copying leaves the clipboard alone
        UI::SetClipboardText("keep");
        which = 0; s.Click(CenterX(rpw), CenterY(rpw), ui); s.Press(Key_SelectAll); s.Frame(ui); s.Press(Key_Copy); s.Frame(ui);
        CHECK(std::string(UI::GetClipboardText()) == "keep");
        // typing into the no-blank field
        which = 2; s.Click(CenterX(rnb), CenterY(rnb), ui); s.Type("a b\tc"); s.Frame(ui);
        CHECK(std::string(nb) == "abc");
    }
    {   // AddPolyline with DrawFlags_Closed closes the shape; flags 0 leaves it open
        Reset(); Sim s(100, 100);
        auto count = [&](int flags) {
            s.Frame([&] { UI::BareNextWindow(); if (UI::Begin("w", 0, 0, 100, 100)) { const UI::Vec2 p[3] = {{10, 10}, {90, 10}, {90, 90}}; UI::GetWindowDrawList().AddPolyline(p, 3, 0xffffffffu, flags, 2.0f); } UI::End(); });
            const Bitmap b = s.Draw(0, 0, 0); return PixelAt(b, 50, 50) != Rgba(0, 0, 0);   // the closing edge passes through the middle
        };
        CHECK(!count(0) && count(UI::DrawFlags_Closed));
    }
    {   // printf-style text equals the plain call with the formatted string; one argument stays literal
        Bitmap a, b;
        for (int v = 0; v < 2; ++v) {
            Reset(); Sim s(260, 140);
            s.Frames(2, [&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 0, 0, 260, 140)) {
                    if (v == 0) { UI::Text("%d items, %s", 3, "ok"); UI::TextDisabled("%.1f%%", 12.5); UI::TextWrapped("n=%d", 7); UI::BulletText("b%d", 1); UI::TextColored(UI::Vec4{1, 0, 0, 1}, "red %d", 5); UI::Text("100%"); }
                    else { UI::Text("3 items, ok"); UI::TextDisabled("12.5%"); UI::TextWrapped("n=7"); UI::BulletText("b1"); UI::TextColored(255, 0, 0, "red 5"); UI::Text("100%"); }
                }
                UI::End();
            });
            (v ? b : a) = s.Draw(5, 5, 5);
        }
        CHECK(a.rgba == b.rgba);
    }
    {   // Button with a size, SameLine with an offset, BeginPopupContextItem without an id
        Reset(); Sim s(300, 160);
        UI::Rect r1, r2, r3, r4, r5; float startX = 0; int clicks = 0;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 160)) {
                startX = UI::GetCursorScreenPos().x;
                if (UI::Button("Wide", UI::Vec2{200, 40})) ++clicks; r1 = UI::GetItemRect();
                UI::Button("Fit", UI::Vec2{0, 0}); r2 = UI::GetItemRect();
                UI::Button("Fill", UI::Vec2{-1, 0}); r3 = UI::GetItemRect();
                UI::Button("a"); r4 = UI::GetItemRect(); UI::SameLine(150.0f, -1.0f); UI::Button("b"); r5 = UI::GetItemRect();
                UI::Button("Nan", UI::Vec2{std::nanf(""), std::nanf("")});
                UI::SameLine(0, 30); UI::Button("c");
                if (UI::BeginPopupContextItem()) { UI::Text("ctx"); UI::EndPopup(); }
            }
            UI::End();
        };
        s.Frames(2, ui);
        CHECK(r1.w == 200 && r1.h == 40 && r2.w < 100 && r2.h == r4.h && r3.w > 200);
        CHECK(std::fabs(r5.x - (startX + 150.0f)) < 1.0f && r5.y == r4.y);
        s.Click(CenterX(r1), CenterY(r1), ui); CHECK(clicks == 1);
        s.rdown = true; s.Move(CenterX(r4), CenterY(r4)); s.Frame(ui); s.rdown = false; s.Frames(2, ui);   // right click on an item opens its own popup
    }
    {   // Dear ImGui code styles through GetStyle(): the writes are taken in (colours, rounding, padding, border, font scale), the rest stays
        Reset(); Sim s(300, 200);
        s.Frame([] {});
        const UI::ImVec4 dim0 = UI::GetStyleColorVec4(UI::Col_TextDisabled);
        const float pad0 = UI::Style().windowPadding;
        UI::StyleInfo& st = UI::GetStyle();
        st.Colors[UI::Col_Text] = UI::ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        st.Colors[UI::Col_FrameBg] = UI::ImVec4(0.0f, 0.5f, 0.0f, 1.0f);
        st.FrameRounding = 7.0f; st.WindowRounding = 0.0f; st.FrameBorderSize = 0.0f;
        s.Frame([] {});                                                   // taken in at the frame start
        UI::ImVec4 t = UI::GetStyleColorVec4(UI::Col_Text), fb = UI::GetStyleColorVec4(UI::Col_FrameBg);
        CHECK(t.x == 1.0f && t.y == 0.0f && t.z == 0.0f && fb.y > 0.49f && fb.y < 0.51f && fb.x == 0.0f);
        CHECK(UI::Style().rounding == 7.0f && UI::Style().windowRounding == 0.0f && UI::Style().borderPx == 0.0f);
        const UI::ImVec4 dim1 = UI::GetStyleColorVec4(UI::Col_TextDisabled);
        CHECK(dim1.x == dim0.x && dim1.w == dim0.w && UI::Style().windowPadding == pad0);   // fields nobody wrote are left alone
        UI::GetStyle().WindowPadding = UI::Vec2{14, 12};                    // taken in by the next GetStyle() call too
        CHECK(UI::GetStyle().WindowPadding.x == 14.0f && UI::Style().windowPadding == 14.0f);
        const UI::StyleInfo copy = UI::GetStyle();                        // the chat window's way: copy the style, assign it back
        UI::StyleColorsLight();
        UI::GetStyle() = copy; s.Frame([] {});
        t = UI::GetStyleColorVec4(UI::Col_Text); CHECK(t.x == 1.0f && t.y == 0.0f);
        UI::GetStyle().FrameRounding = std::nanf(""); s.Frame([] {}); CHECK(UI::Style().rounding == 7.0f);   // nonsense is ignored
        // IO::FontGlobalScale: the text grows with it, and back
        const float w1 = UI::CalcTextSize("Scaled text").x;
        UI::GetIO().FontGlobalScale = 1.5f; s.Frame([] {});
        const float w15 = UI::CalcTextSize("Scaled text").x;
        CHECK(w15 > w1 * 1.45f && w15 < w1 * 1.55f);
        UI::GetIO().FontGlobalScale = 1.5f; s.Frame([] {}); CHECK(UI::CalcTextSize("Scaled text").x == w15);   // not applied twice
        UI::GetIO().FontGlobalScale = 1.0f; s.Frame([] {}); CHECK(UI::CalcTextSize("Scaled text").x == w1);
        UI::GetIO().FontGlobalScale = -3.0f; s.Frame([] {}); CHECK(UI::CalcTextSize("Scaled text").x == w1);   // out of range: ignored
        UI::GetIO().FontGlobalScale = 1.0f;
    }
    {   // UseImGuiCoordinates: cursor positions from the window's / child's corner (padding included), window size of the innermost child
        Reset(); Sim s(400, 300);
        UI::Vec2 p0{}, p1{}, wsz{}, cpos{}, csz{}, after{}, own0{};
        float winH = 0.0f;
        auto ui = [&] {
            UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({400, 300});
            UI::Begin("root", nullptr, UI::Window_NoTitleBar);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // (Dear ImGui's child padding rules, as the launcher uses)
            p0 = UI::GetCursorPos(); wsz = UI::GetWindowSize();
            UI::PushStyleVar(UI::StyleVar_WindowPadding, UI::Vec2{28, 20});
            UI::BeginChild("content", UI::Vec2{0, 0}, UI::Child_AlwaysUseWindowPadding);
            UI::PopStyleVar();
            p1 = UI::GetCursorPos(); cpos = UI::GetWindowPos(); csz = UI::GetWindowSize(); winH = UI::GetWindowHeight();
            UI::SetCursorPosY(winH - 40.0f); after = UI::GetCursorScreenPos();   // a footer pinned 40 px above the child's bottom
            UI::EndChild();
            UI::PopStyleVar();
            UI::End();
        };
        UI::UseImGuiCoordinates(false); s.Frames(2, ui); own0 = p0;
        CHECK(own0.x == 0.0f && own0.y == 0.0f);                          // OkayUI's own rule: from the content corner
        UI::UseImGuiCoordinates(true); s.Frames(2, ui);
        CHECK(p0.x > 0.0f && p0.x == p0.y);                              // Dear ImGui's: the window padding
        CHECK(wsz.x == 400.0f && wsz.y == 300.0f);
        CHECK(p1.x == 28.0f && p1.y == 20.0f);                            // inside the child: its own padding
        CHECK(csz.y == winH && csz.y < 300.0f && csz.x < 400.0f && cpos.x > 0.0f);   // the child's rectangle, not the window's
        CHECK(after.y == cpos.y + winH - 40.0f);
        UI::UseImGuiCoordinates(false);
    }
    {   // CalcTextSize with a wrap width (Dear ImGui's measure): words wrap, a word wider than the line breaks inside it, '\n' breaks
        Reset(); Sim s(400, 300);
        s.Frame([] {});
        const float lh = UI::GetTextLineHeight();
        const UI::Vec2 one = UI::CalcTextSize("short", nullptr, false, 300.0f);
        CHECK(one.y == lh && one.x > 0.0f && one.x <= 300.0f);
        std::string words; while (words.size() < 200) words += "wrapped words ";
        const UI::Vec2 w = UI::CalcTextSize(words.c_str(), nullptr, false, 150.0f);
        CHECK(w.y >= 3.0f * lh && w.x <= 150.0f);
        const std::string xs(200, 'x');
        const UI::Vec2 x = UI::CalcTextSize(xs.c_str(), nullptr, false, 150.0f);   // one long word: broken inside, never wider than the line
        CHECK(x.y >= 3.0f * lh && x.x <= 150.0f && x.x > 100.0f);
        CHECK(UI::CalcTextSize("a\nb\nc").y == 3.0f * lh);
        CHECK(UI::CalcTextSize(xs.c_str()).y == lh);                        // no wrap width: one line
    }
    {   // InputTextMultiline with EnterReturnsTrue + CtrlEnterForNewLine (the launcher's chat composer): typing is no "send", Enter is and
        // breaks no line, Ctrl+Enter breaks the line
        Reset(); Sim s(400, 200);
        char buf[128] = "";
        int sends = 0;
        UI::Rect box{};
        auto ui = [&] {
            UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({400, 200}); UI::Begin("c", nullptr, UI::Window_NoTitleBar);
            if (UI::InputTextMultiline("##msg", buf, 128, UI::Vec2{300, 60}, UI::InputText_EnterReturnsTrue | UI::InputText_CtrlEnterForNewLine)) ++sends;
            box = UI::GetItemRect();
            UI::End();
        };
        s.Frames(2, ui);
        s.Click(box.x + 20, box.y + 20, ui);                                // the box takes the keyboard
        s.Frames(2, ui);
        s.text = "hello"; s.Frame(ui); s.Frame(ui);
        CHECK(std::strcmp(buf, "hello") == 0 && sends == 0);
        s.ctrl = true; s.edge[Key_Enter] = true; s.Frame(ui); s.ctrl = false; s.Frame(ui);
        CHECK(std::strchr(buf, '\n') != nullptr && sends == 0);          // Ctrl+Enter: a new line
        s.edge[Key_Enter] = true; s.Frame(ui); s.Frame(ui);
        CHECK(sends == 1 && std::strcmp(buf, "hello\n") == 0);           // Enter: sent, no extra line
        bool focusNext = true;                                              // SetKeyboardFocusHere before the box gives it the keyboard
        auto ui2 = [&] {
            UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({400, 200}); UI::Begin("c", nullptr, UI::Window_NoTitleBar);
            UI::InputText("##other", buf, 8);
            if (focusNext) { UI::SetKeyboardFocusHere(); focusNext = false; }
            UI::InputTextMultiline("##msg2", buf, 128, UI::Vec2{300, 60});
            UI::End();
        };
        buf[0] = 0; s.Frames(3, ui2);
        s.text = "x"; s.Frame(ui2); s.Frame(ui2);
        CHECK(std::strcmp(buf, "x") == 0);
    }
    {   // a framed text field starts its text (and its hint) at a pushed FramePadding.x, as Dear ImGui's does (the launcher's search pill
        // draws its magnifier in that room)
        Reset(); Sim s(400, 120);
        char buf[32] = "";
        auto hintX = [&](float padX) {
            s.Frame([&] {
                UI::SetNextWindowPos({0, 0}); UI::SetNextWindowSize({400, 120}); UI::Begin("f", nullptr, UI::Window_NoTitleBar);
                UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
                if (padX >= 0.0f) UI::PushStyleVar(UI::StyleVar_FramePadding, UI::Vec2{padX, 6.0f});
                UI::SetNextItemWidth(300.0f);
                UI::InputTextWithHint("##s", "Search", buf, 32);
                if (padX >= 0.0f) UI::PopStyleVar();
                UI::PopStyleVar();
                UI::End();
            });
            const Bitmap b = s.Draw(0, 0, 0);
            int first = -1;   // the leftmost column with hint-coloured text pixels in the field's row band
            for (int x = 0; x < 400 && first < 0; ++x)
                for (int y = 10; y < 60 && first < 0; ++y) { const unsigned p = PixelAt(b, x, y); if (((p >> 24) & 255) > 90 && ((p >> 16) & 255) > 90) first = x; }
            return first;
        };
        UI::UseImGuiCoordinates(true);   // (Dear ImGui's rules: the launcher's mode)
        const int plain = hintX(-1.0f), padded = hintX(30.0f);
        UI::UseImGuiCoordinates(false);
        CHECK(plain > 0 && padded > plain + 10);
    }
    TEST_MAIN_RESULT();
}
