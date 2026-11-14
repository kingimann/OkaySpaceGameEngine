// OkayUI widgets, step 3: InputTextMultiline, InputTextWithHint, ArrowButton, Selectable with flags and a size, tree node flags (open on
// arrow, double click, bullet, span full width, no push, framed), PlotLines / PlotHistogram with options (offset, overlay, getter, hover),
// ProgressBar with a size and an indeterminate bar. Headless: synthetic input through the test kit Sim, pixels through the software backend.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

const unsigned kClear = Rgba(12, 14, 20);
UI::Vec2 V(float x, float y) { return UI::Vec2{x, y}; }
unsigned P(const Bitmap& b, float x, float y) { return PixelAt(b, (int)x, (int)y); }
int CountEq(const Bitmap& b, unsigned c) { int n = 0; for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == c) ++n; return n; }

struct Rig {
    Sim sim;
    std::function<void()> body;
    explicit Rig(float w = 520, float h = 420) : sim(w, h) { Reset(); }
    void Frame() { sim.Frame([&] { if (UI::Begin("W", 10, 10, 500, 400) && body) body(); UI::End(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Move(float x, float y) { sim.Move(x, y); Frame(); }
    // 20 idle frames first: two clicks within 0.3 s at one spot are a double click (a word is selected)
    void Click(float x, float y) { Frames(20); sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void Type(const char* t) { sim.Type(t); Frame(); }
    void Key(okayuitest::Key k) { sim.Press(k); Frame(); }
};

// ---- InputTextMultiline -------------------------------------------------------------------------------------------------

void TestMultiline() {
    RUN_SUITE("InputTextMultiline");
    {
        Rig r; char buf[128] = "first\nsecond"; UI::Rect rc; bool ch = false;
        r.body = [&] { UI::PushID(1); ch = UI::InputTextMultiline("", buf, sizeof buf, V(300, 100)) || ch; rc = UI::GetItemRect(); UI::PopID(); };
        r.Frames(2);
        CHECK_NEAR(rc.w, 300, 1.0); CHECK_NEAR(rc.h, 100, 1.0);
        CHECK(!ch && std::string(buf) == "first\nsecond");                         // drawing alone never touches the buffer
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(CountNot(b, (int)rc.x + 4, (int)rc.y + 4, (int)rc.x + 120, (int)rc.y + 60, P(b, rc.x + 280, rc.y + 90)) > 30);   // the two lines are drawn
        // no gutter: the leftmost pixels of the box are the field colour, and the text starts near the left edge
        CHECK(P(b, rc.x + 2, rc.y + 20) == P(b, rc.x + 200, rc.y + 90));
        r.Click(rc.x + 250, rc.y + 8);                                             // click past the end of line 1: the caret goes to its end
        r.Type("!");
        CHECK(ch && std::string(buf) == "first!\nsecond");
        r.Key(Key_Enter);
        CHECK(std::string(buf) == "first!\n\nsecond");                             // Enter breaks the line
        r.Type("x");
        CHECK(std::string(buf) == "first!\nx\nsecond");
        r.Key(Key_Backspace); r.Key(Key_Backspace);
        CHECK(std::string(buf) == "first!\nsecond");
        // a change made by the caller shows up in the box
        std::strcpy(buf, "replaced"); r.Frames(2);
        r.Click(rc.x + 250, rc.y + 8); r.Type("?");
        CHECK(std::string(buf) == "replaced?");
        // select all, copy, paste elsewhere
        r.Key(Key_SelectAll); r.Key(Key_Copy);
        CHECK(std::string(UI::GetClipboardText()) == "replaced?");
        r.Key(Key_Paste);
        CHECK(std::string(buf) == "replaced?");                                    // pasting replaced the selection with the same text
        // undo
        r.Type("z"); CHECK(std::string(buf) == "replaced?z");
        r.sim.Press(Key_Left); r.Frame();
    }
    {   // a picture (golden): two boxes, one with text and a label, one empty and read only
        Rig r(360, 200); static char t1[96] = "Hello,\nmultiline world\nthird line"; char t2[8] = "";
        r.body = [&] {
            UI::InputTextMultiline("notes", t1, sizeof t1, V(220, 80));
            UI::InputTextMultiline("empty", t2, sizeof t2, V(220, 50), UI::InputText_ReadOnly);
        };
        r.Frames(3);
        std::string why;
        const bool ok = MatchesGolden("multiline", r.sim.Draw(12, 14, 20), &why);
        if (!ok) std::cerr << "  golden multiline: " << why << "\n";
        CHECK(ok);
    }
    {   // the capacity: an edit that does not fit is cut, the buffer stays terminated and valid
        Rig r; char buf[8] = "abc"; UI::Rect rc;
        r.body = [&] { UI::InputTextMultiline("c", buf, sizeof buf, V(200, 60)); rc = UI::GetItemRect(); };
        r.Frames(2);
        r.Click(rc.x + 150, rc.y + 8);
        r.Type("defghijkl");
        CHECK(std::strlen(buf) == 7 && std::string(buf) == "abcdefg");
        r.Type("m"); CHECK(std::string(buf) == "abcdefg");
        // a multi-byte character that would straddle the end is dropped whole
        char b2[5] = "ab";
        Rig r2; UI::Rect rc2;
        r2.body = [&] { UI::InputTextMultiline("d", b2, sizeof b2, V(200, 60)); rc2 = UI::GetItemRect(); };
        r2.Frames(2);
        r2.Click(rc2.x + 150, rc2.y + 8);
        r2.Type("c\xC3\xA9");                                                      // "cé" is 3 bytes after "ab": 5 > 4 usable
        CHECK(std::strlen(b2) <= 4 && (std::string(b2) == "abc" || std::string(b2) == "ab"));
        CHECK(std::string(b2).find('\xC3') == std::string::npos || std::string(b2).find("\xC3\xA9") != std::string::npos);
        // an unterminated buffer is terminated, not read past
        char raw[4] = {'a', 'b', 'c', 'd'};
        Rig r3; r3.body = [&] { UI::InputTextMultiline("e", raw, sizeof raw); };
        r3.Frames(2);
        CHECK(raw[3] == 0);
    }
    {   // read only: no edits; two boxes keep their own state; scrolling a long text
        Rig r; char a[64] = "alpha", b[64] = "beta"; UI::Rect ra, rb;
        r.body = [&] {
            UI::InputTextMultiline("a", a, sizeof a, V(200, 50), UI::InputText_ReadOnly); ra = UI::GetItemRect();
            UI::InputTextMultiline("b", b, sizeof b, V(200, 50)); rb = UI::GetItemRect();
        };
        r.Frames(2);
        r.Click(ra.x + 150, ra.y + 8); r.Type("X"); r.Key(Key_Backspace); r.Key(Key_Delete);
        CHECK(std::string(a) == "alpha");
        r.Click(rb.x + 150, rb.y + 8); r.Type("Y");
        CHECK(std::string(a) == "alpha" && std::string(b) == "betaY");
        r.Click(ra.x + 150, ra.y + 8); r.Type("Z");
        CHECK(std::string(b) == "betaY");                                           // typing went to the focused box only
    }
    {   // a long text: more lines than fit; the wheel scrolls; the box is clipped
        Rig r; static char big[4096]; big[0] = 0;
        for (int i = 0; i < 40; ++i) { char l[24]; std::snprintf(l, sizeof l, "line %d\n", i); std::strcat(big, l); }
        UI::Rect rc;
        r.body = [&] { UI::InputTextMultiline("big", big, sizeof big, V(240, 90)); rc = UI::GetItemRect(); UI::Text("below"); };
        r.Frames(2);
        const Bitmap before = r.sim.Draw(12, 14, 20);
        r.Move(rc.x + 100, rc.y + 40);
        for (int i = 0; i < 5; ++i) { r.sim.wheel = -2.0f; r.Frame(); }
        r.Frames(2);
        const Bitmap after = r.sim.Draw(12, 14, 20);
        int diff = 0;
        for (int y = (int)rc.y + 4; y < (int)(rc.y + rc.h) - 4; ++y) for (int x = (int)rc.x + 4; x < (int)(rc.x + 200); ++x) if (PixelAt(before, x, y) != PixelAt(after, x, y)) ++diff;
        CHECK(diff > 200);                                                          // the content moved
        // nothing is drawn past the box: the strip just under it has no text pixels (scrolled lines are clipped)
        CHECK(CountEq(r.sim.Draw(12, 14, 20), ThemeRgba(UI::Style().text)) > 0);
        int below = 0;
        for (int y = (int)(rc.y + rc.h) + 1; y < (int)(rc.y + rc.h) + 4; ++y) for (int x = (int)rc.x; x < (int)(rc.x + rc.w); ++x) if (PixelAt(after, x, y) == ThemeRgba(UI::Style().text)) ++below;
        CHECK(below == 0);
    }
    {   // hostile: null buffer, tiny cap, NaN size, no window
        Rig r; char one[1] = {0}; char nul[2] = {0, 0};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        UI::InputTextMultiline("outside", nul, 2);
        r.body = [&] {
            UI::InputTextMultiline("n", nullptr, 10); UI::InputTextMultiline("z", nul, 0); UI::InputTextMultiline("o", one, 1);
            UI::InputTextMultiline(nullptr, nul, 2, V(nan, nan)); UI::InputTextMultiline("big", nul, 2, V(1e9f, 1e9f)); UI::InputTextMultiline("neg", nul, 2, V(-5, -5), 0xffff);
        };
        r.Frames(3);
        r.sim.Click(100, 100, [&] { r.Frame(); });
        CHECK(one[0] == 0);
    }
}

// ---- hint, arrow, progress ----------------------------------------------------------------------------------------------

void TestHintArrowProgress() {
    RUN_SUITE("InputTextWithHint, ArrowButton, ProgressBar");
    {   // the hint shows only while empty
        Rig r; char buf[32] = ""; UI::Rect rc;
        r.body = [&] { UI::PushID(1); UI::SetNextItemWidth(300); UI::InputTextWithHint("", "Search...", buf, sizeof buf); rc = UI::GetItemRect(); UI::PopID(); };
        r.Frames(2);
        const Bitmap empty = r.sim.Draw(12, 14, 20);
        const unsigned fieldCol = P(empty, rc.x + rc.w - 8, rc.y + rc.h * 0.5f);
        CHECK(CountNot(empty, (int)rc.x + 6, (int)rc.y + 5, (int)rc.x + 160, (int)rc.y + (int)rc.h - 5, fieldCol) > 20);
        r.Click(rc.x + 100, rc.y + rc.h * 0.5f); r.Type("x");
        const Bitmap typed = r.sim.Draw(12, 14, 20);
        // with text, the hint's wide stretch is gone: far right of the text is plain field again
        CHECK(CountNot(typed, (int)rc.x + 60, (int)rc.y + 5, (int)rc.x + 160, (int)rc.y + (int)rc.h - 5, P(typed, rc.x + rc.w - 8, rc.y + rc.h * 0.5f)) == 0);
        CHECK(std::string(buf) == "x");
        r.Key(Key_Backspace);
        CHECK(buf[0] == 0);
        CHECK(CountNot(r.sim.Draw(12, 14, 20), (int)rc.x + 60, (int)rc.y + 5, (int)rc.x + 160, (int)rc.y + (int)rc.h - 5, fieldCol) > 5);   // the hint is back
    }
    {   // arrows point four ways and are square buttons that click
        Rig r; int clicks[4] = {0, 0, 0, 0}; UI::Rect rr[4];
        r.body = [&] {
            const char* ids[4] = {"l", "r", "u", "d"};
            for (int i = 0; i < 4; ++i) { if (UI::ArrowButton(ids[i], i)) ++clicks[i]; rr[i] = UI::GetItemRect(); if (i < 3) UI::SameLine(); }
        };
        r.Frames(2);
        for (int i = 0; i < 4; ++i) { CHECK(rr[i].w == rr[i].h && rr[i].w > 10); }
        const Bitmap b = r.sim.Draw(12, 14, 20);
        // the arrow glyphs differ: left vs right are mirrored, up vs down are mirrored
        auto cen = [&](const UI::Rect& q, bool xAxis) {
            const unsigned bg = P(b, q.x + 3, q.y + 3); double sum = 0; int n = 0;
            for (int y = (int)q.y + 5; y < (int)(q.y + q.h) - 5; ++y) for (int x = (int)q.x + 5; x < (int)(q.x + q.w) - 5; ++x)
                if (PixelAt(b, x, y) != bg) { sum += xAxis ? x - (q.x + q.w * 0.5) : y - (q.y + q.h * 0.5); ++n; }
            return n ? sum / n : 0.0;
        };
        auto sumX = [&](const UI::Rect& q) { return cen(q, true); };
        auto sumY = [&](const UI::Rect& q) { return cen(q, false); };
        CHECK(sumX(rr[0]) > 0.5 && sumX(rr[1]) < -0.5);   // a triangle's mass is on its base side: left-pointing is right of centre
        CHECK(sumY(rr[2]) > 0.5 && sumY(rr[3]) < -0.5);
        for (int i = 0; i < 4; ++i) r.Click(CenterX(rr[i]), CenterY(rr[i]));
        CHECK(clicks[0] == 1 && clicks[1] == 1 && clicks[2] == 1 && clicks[3] == 1);
    }
    {   // ProgressBar: a size, an overlay, an indeterminate bar that moves with time
        Rig r; UI::Rect rb, rc; double t = 0.0;
        r.body = [&] {
            UI::ProgressBar(0.5f, V(200, 24), "half"); rb = UI::GetItemRect();
            UI::ProgressBar(-1.0f, V(200, 24)); rc = UI::GetItemRect();
            UI::ProgressBar(1.5f, V(0, 0)); UI::ProgressBar(std::numeric_limits<float>::quiet_NaN(), V(-4, 1e9f)); UI::ProgressBar(0.2f);
        };
        auto frame = [&](double now) { UI::Input in; in.displayW = 520; in.displayH = 420; in.time = now; UI::BeginFrame(in); if (UI::Begin("W", 10, 10, 500, 400)) r.body(); UI::End(); UI::EndFrameData(); };
        frame(1.0); frame(1.0 + 1.0 / 60.0);
        CHECK_NEAR(rb.w, 200, 0.5); CHECK_NEAR(rb.h, 24, 0.5);
        const Bitmap b1 = [&] { Bitmap bm(520, 420); bm.Clear(12, 14, 20); UI::soft::Render(UI::GetDrawData(), bm); return bm; }();
        const unsigned acc = ThemeRgba(UI::Style().accent);
        // the half bar: accent to about the middle
        CHECK(P(b1, rb.x + 40, rb.y + rb.h * 0.5f) == acc && P(b1, rb.x + 160, rb.y + rb.h * 0.5f) != acc);
        auto accentCentre = [&](const Bitmap& bm) { double s = 0; int n = 0; for (int x = (int)rc.x + 2; x < (int)(rc.x + rc.w) - 2; ++x) if (PixelAt(bm, x, (int)(rc.y + rc.h * 0.5f)) == acc) { s += x; ++n; } return n ? s / n : -1.0; };
        const double c1 = accentCentre(b1);
        frame(1.3);
        const Bitmap b2 = [&] { Bitmap bm(520, 420); bm.Clear(12, 14, 20); UI::soft::Render(UI::GetDrawData(), bm); return bm; }();
        const double c2 = accentCentre(b2);
        CHECK(c1 > 0 && c2 > 0 && std::fabs(c1 - c2) > 5.0);                          // the block moved
        CHECK(c1 >= rc.x && c1 <= rc.x + rc.w && c2 >= rc.x && c2 <= rc.x + rc.w);   // and stays inside the bar
        (void)t;
    }
}

// ---- Selectable ---------------------------------------------------------------------------------------------------------

void TestSelectable() {
    RUN_SUITE("Selectable: size, flags, pointer");
    Rig r; bool sel = false; int clicks = 0, dbl = 0, dis = 0; UI::Rect rs, rsz, rd, rdb;
    bool popup = false, popupOpen = false; UI::Rect ro, rpick;
    r.body = [&] {
        if (UI::Selectable("plain", &sel)) ++clicks; rs = UI::GetItemRect();
        UI::Selectable("sized", false, UI::Selectable_None, V(120, 40)); rsz = UI::GetItemRect();
        if (UI::Selectable("off", false, UI::Selectable_Disabled)) ++dis; rd = UI::GetItemRect();
        if (UI::Selectable("double", false, UI::Selectable_DoubleClickOnly)) ++dbl; rdb = UI::GetItemRect();
        if (UI::Button("open")) UI::OpenPopup("pp");
        ro = UI::GetItemRect();
        if (UI::BeginPopup("pp")) { if (UI::Selectable("pick", false, UI::Selectable_ClosePopup)) popup = true; rpick = UI::GetItemRect(); UI::EndPopup(); }
        popupOpen = UI::IsPopupOpen("pp");
    };
    r.Frames(2);
    CHECK(rs.w > 300 && rs.h > 10);                       // default: the width left, one row
    CHECK_NEAR(rsz.w, 120, 0.5); CHECK_NEAR(rsz.h, 40, 0.5);
    r.Click(CenterX(rs), CenterY(rs));
    CHECK(clicks == 1 && sel);                            // the pointer overload toggled it
    r.Click(CenterX(rs), CenterY(rs));
    CHECK(clicks == 2 && !sel);
    // a click outside the sized row's 120 px, on the same line, does not hit it
    int before = clicks;
    r.Click(rsz.x + 200, CenterY(rsz));
    CHECK(clicks == before);
    r.Click(CenterX(rd), CenterY(rd));
    CHECK(dis == 0);                                       // disabled: no click
    r.Move(CenterX(rd), CenterY(rd));
    const Bitmap hov = r.sim.Draw(12, 14, 20);
    CHECK(P(hov, rd.x + rd.w - 6, CenterY(rd)) != ThemeRgba(UI::Style().bgHover));   // and no hover highlight
    // double click only: one click does nothing, two quick clicks fire once
    r.Click(CenterX(rdb), CenterY(rdb));
    CHECK(dbl == 0);
    r.sim.Move(CenterX(rdb), CenterY(rdb)); r.Frame();
    r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(dbl == 1);
    // ClosePopup closes the popup it is in
    r.Click(CenterX(ro), CenterY(ro));
    r.Frames(2);
    CHECK(popupOpen && rpick.w > 0);
    r.Click(CenterX(rpick), CenterY(rpick));
    r.Frames(2);
    CHECK(popup && !popupOpen);
    // without the flag the popup stays open
    popup = false;
    r.Click(CenterX(ro), CenterY(ro)); r.Frames(2);
    CHECK(popupOpen);
}

// ---- tree node flags ----------------------------------------------------------------------------------------------------

void TestTreeFlags() {
    RUN_SUITE("TreeNodeEx flags");
    {   // OpenOnArrow: a click on the label does not open, a click on the arrow does
        Rig r; bool open = false; UI::Rect rn; bool clicked = false;
        r.body = [&] {
            open = UI::TreeNodeEx("node", UI::TreeNode_OpenOnArrow); rn = UI::GetItemRect();
            if (UI::IsItemClicked()) clicked = true;
            if (open) { UI::Text("child"); UI::TreePop(); }
        };
        r.Frames(2);
        CHECK(!open);
        r.Click(rn.x + 80, CenterY(rn));                    // on the label
        CHECK(!open && clicked);                             // the click is seen but the node stays shut
        r.Click(rn.x + 8, CenterY(rn));                      // on the arrow
        CHECK(open);
        r.Click(rn.x + 8, CenterY(rn));
        CHECK(!open);
    }
    {   // OpenOnDoubleClick: a single click does not; a double click does. Both flags: the arrow opens on one click too
        Rig r; bool open = false, open2 = false; UI::Rect rn, rn2;
        r.body = [&] {
            open = UI::TreeNodeEx("dbl", UI::TreeNode_OpenOnDoubleClick); rn = UI::GetItemRect(); if (open) { UI::Text("c1"); UI::TreePop(); }
            open2 = UI::TreeNodeEx("both", UI::TreeNode_OpenOnDoubleClick | UI::TreeNode_OpenOnArrow); rn2 = UI::GetItemRect(); if (open2) { UI::Text("c2"); UI::TreePop(); }
        };
        r.Frames(2);
        r.Click(rn.x + 60, CenterY(rn));
        CHECK(!open);
        r.sim.Move(rn.x + 60, CenterY(rn)); r.Frame();
        r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(open);
        r.Frames(2);
        r.Click(rn2.x + 8, CenterY(rn2));                    // the arrow, one click
        CHECK(open2);
    }
    {   // Bullet draws a dot instead of the arrow; the node still opens
        Rig r; bool open = false, open2 = false; UI::Rect rb, ra;
        r.body = [&] {
            open = UI::TreeNodeEx("bul", UI::TreeNode_Bullet); rb = UI::GetItemRect(); if (open) { UI::Text("x"); UI::TreePop(); }
            open2 = UI::TreeNodeEx("arr", 0); ra = UI::GetItemRect(); if (open2) { UI::Text("y"); UI::TreePop(); }
        };
        r.Frames(2);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        // the arrow glyph is a ">" (narrow, left-aligned in 4..20 px); the bullet is a round dot centred a little right of it: compare the two
        auto ink = [&](const UI::Rect& q) { int n = 0; for (int y = (int)q.y + 2; y < (int)(q.y + q.h) - 2; ++y) for (int x = (int)q.x; x < (int)q.x + 22; ++x) if (PixelAt(b, x, y) == ThemeRgba(UI::Style().text)) ++n; return n; };
        CHECK(ink(rb) > 5 && ink(ra) > 5);
        bool diff = false;
        for (int y = 0; y < (int)rb.h && !diff; ++y) for (int x = 0; x < 22; ++x) if (PixelAt(b, (int)rb.x + x, (int)rb.y + y) != PixelAt(b, (int)ra.x + x, (int)ra.y + y)) { diff = true; break; }
        CHECK(diff);
        r.Click(rb.x + 60, CenterY(rb));
        CHECK(open);
    }
    {   // SpanFullWidth: inside an indent the row's rectangle starts at the content edge
        Rig r; UI::Rect rn, rs; bool o1 = false, o2 = false;
        r.body = [&] {
            o1 = UI::TreeNodeEx("outer", UI::TreeNode_DefaultOpen);
            if (o1) {
                o2 = UI::TreeNodeEx("narrow", UI::TreeNode_Leaf); rn = UI::GetItemRect(); if (o2) UI::TreePop();
                o2 = UI::TreeNodeEx("wide", UI::TreeNode_Leaf | UI::TreeNode_SpanFullWidth); rs = UI::GetItemRect(); if (o2) UI::TreePop();
                UI::TreePop();
            }
        };
        r.Frames(2);
        CHECK(rs.x < rn.x - 10.0f && rs.x + rs.w >= rn.x + rn.w - 0.5f);      // starts left of the indented row, ends at the same right edge
        // the highlight on hover spans from the content edge
        r.Move(rs.x + 2, CenterY(rs));
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(P(b, rs.x + 2, CenterY(rs)) == ThemeRgba(UI::Style().bgHover));
        r.Move(rn.x + rn.w * 0.5f, CenterY(rn));
        const Bitmap b2 = r.sim.Draw(12, 14, 20);
        CHECK(P(b2, rs.x + 2, CenterY(rn)) != ThemeRgba(UI::Style().bgHover));  // the narrow row's highlight starts at its own (indented) left edge
    }
    {   // NoTreePushOnOpen: no indent, no TreePop; Framed draws a header background
        Rig r; UI::Rect rn, rc; bool o = false;
        r.body = [&] {
            o = UI::TreeNodeEx("np", UI::TreeNode_DefaultOpen | UI::TreeNode_NoTreePushOnOpen | UI::TreeNode_Framed); rn = UI::GetItemRect();
            UI::Text("after"); rc = UI::GetItemRect();
        };
        r.Frames(2);
        CHECK(o && std::fabs(rc.x - rn.x) < 1.0f);                 // not indented
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(P(b, rn.x + rn.w - 6, CenterY(rn)) == ThemeRgba(UI::Style().track));   // the framed row's background
    }
}

// ---- plots --------------------------------------------------------------------------------------------------------------

float Saw(void* data, int i) { return (float)(i % 4) + *(float*)data; }

void TestPlots() {
    RUN_SUITE("PlotLines / PlotHistogram options");
    const unsigned lineCol = Rgba(201, 31, 77), hovCol = Rgba(31, 201, 77);
    {   // a plot of a ring buffer: the offset rotates the picture; overlay text; size; the hover index and tooltip
        Rig r; float vals[8] = {0, 1, 2, 3, 4, 5, 6, 7}; UI::Rect rp, rp2; int hovered = -2; int hovered2 = -2;
        UI::PlotOptions o; o.size = V(240, 60); o.overlay = "ovr"; o.scaleMin = 0; o.scaleMax = 7;
        UI::PlotOptions o2 = o; o2.valuesOffset = 4; o2.overlay = nullptr;
        UI::SetStyleColor(UI::Col_PlotLines, 201, 31, 77, 255); UI::SetStyleColor(UI::Col_PlotLinesHovered, 31, 201, 77, 255);
        r.body = [&] {
            UI::PushID(1); { const UI::Vec2 c0 = UI::GetCursorScreenPos(); rp = UI::Rect{c0.x, c0.y, 240, 60}; } hovered = UI::PlotLines("a", vals, 8, o); UI::PopID();
            UI::PushID(2); { const UI::Vec2 c0 = UI::GetCursorScreenPos(); rp2 = UI::Rect{c0.x, c0.y, 240, 60}; } hovered2 = UI::PlotLines("b", vals, 8, o2); UI::PopID();
        };
        r.Frames(2);
        CHECK(hovered == -1 && hovered2 == -1);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(CountEq(b, lineCol) > 100);
        // hover over the middle of the first plot: an index in range, the dot in the hover colour, and a tooltip
        r.Move(rp.x + 2.0f + (rp.w - 4.0f) * 3.0f / 7.0f, rp.y + 30);
        CHECK(hovered == 3);
        const Bitmap bh = r.sim.Draw(12, 14, 20);
        CHECK(CountEq(bh, hovCol) > 5);
        r.Move(500, 400);
        CHECK(hovered == -1);
        // the ring: the first plot starts at the lowest value, the second (offset 4) at a high one
        r.Move(500, 400);
        const Bitmap rb = r.sim.Draw(12, 14, 20);
        auto leftY = [&](const UI::Rect& q) { for (int x = (int)q.x + 2; x < (int)q.x + 12; ++x) { double s = 0; int n = 0; for (int y = (int)q.y; y < (int)(q.y + q.h); ++y) if (PixelAt(rb, x, y) == lineCol) { s += y; ++n; } if (n) return s / n - q.y; } return -1.0; };
        const double y1 = leftY(rp), y2 = leftY(rp2);
        CHECK(y1 > rp.h * 0.7 && y2 >= 0 && y2 < rp2.h * 0.6);
    }
    {   // the ring offset's hover index is the data index; the getter and the histogram
        Rig r; float add = 0; UI::Rect rp; int hovered = -2;
        UI::PlotOptions o; o.size = V(200, 60); o.valuesOffset = 2; o.scaleMin = 0; o.scaleMax = 4;
        UI::SetStyleColor(UI::Col_PlotHistogram, 201, 31, 77, 255); UI::SetStyleColor(UI::Col_PlotHistogramHovered, 31, 201, 77, 255);
        r.body = [&] { const UI::Vec2 c0 = UI::GetCursorScreenPos(); rp = UI::Rect{c0.x, c0.y, 200, 60}; hovered = UI::PlotHistogram("h", Saw, &add, 8, o); };
        r.Frames(2);
        const Bitmap b = r.sim.Draw(12, 14, 20);
        CHECK(CountEq(b, lineCol) > 100);
        // bar 0 of the picture is data index 2; hover its left end
        r.Move(rp.x + 2.0f + (rp.w - 4.0f) / 16.0f, rp.y + 50);
        CHECK(hovered == 2);
        CHECK(CountEq(r.sim.Draw(12, 14, 20), hovCol) > 20);
        // the first bar is data index 2 (value 2 of the scale 0..4): half the height
        r.Move(500, 400);
        const Bitmap hb = r.sim.Draw(12, 14, 20);
        const float bx = rp.x + 2.0f + (rp.w - 4.0f) / 16.0f;
        int top = -1, bottom = -1;
        for (int y = (int)rp.y; y < (int)(rp.y + rp.h); ++y) if (PixelAt(hb, (int)bx, y) == lineCol) { if (top < 0) top = y; bottom = y; }
        CHECK(top >= 0 && std::fabs((bottom - top + 1) - (rp.h - 4.0f) * 0.5f) < 3.0f);
    }
    {   // gaps and clamping: NaN, inf, values outside the scale never produce garbage or a crash
        Rig r; float v[6] = {0.5f, std::numeric_limits<float>::quiet_NaN(), 100.0f, -100.0f, std::numeric_limits<float>::infinity(), 0.5f};
        UI::PlotOptions o; o.size = V(200, 60); o.scaleMin = 0; o.scaleMax = 1;
        UI::PlotOptions autoScale; autoScale.size = V(200, 60);
        r.body = [&] { UI::PlotLines("l", v, 6, o); UI::PlotHistogram("h", v, 6, o); UI::PlotLines("auto", v, 6, autoScale); UI::PlotHistogram("auto2", v, 6, autoScale); };
        r.Frames(3);
        const UI::DrawData d = UI::GetDrawData();
        CHECK(d.indexCount % 3 == 0 && d.dropped == 0);
        // every vertex is finite and inside the display
        const UI::DrawVertex* vx = (const UI::DrawVertex*)d.vertices; bool ok = true;
        for (int i = 0; i < d.vertexCount; ++i) if (!std::isfinite(vx[i].x) || !std::isfinite(vx[i].y) || vx[i].x < -100 || vx[i].x > 2000 || vx[i].y < -100 || vx[i].y > 2000) ok = false;
        CHECK(ok);
        // all-NaN data, an empty array, a null array, a negative count, huge counts and offsets
        float nanv[3] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
        UI::PlotOptions weird; weird.valuesOffset = -7; weird.size = V(std::numeric_limits<float>::quiet_NaN(), -3);
        UI::PlotOptions huge; huge.valuesOffset = 2000000000; huge.size = V(1e9f, 1e9f);
        static float big[200000];
        for (int i = 0; i < 200000; ++i) big[i] = std::sin((float)i * 0.01f);
        r.body = [&] {
            UI::PlotLines("n", nanv, 3, o); UI::PlotLines("e", v, 0, o); UI::PlotLines("z", (const float*)nullptr, 5, o); UI::PlotLines("neg", v, -3, o);
            UI::PlotLines("w", v, 6, weird); UI::PlotHistogram("w2", v, 6, huge); UI::PlotLines("big", big, 200000, autoScale); UI::PlotHistogram("big2", big, 200000, autoScale);
            UI::PlotLines("getter", (float (*)(void*, int))nullptr, nullptr, 5, o);
        };
        r.Frames(3);
        CHECK(true);
    }
}

// ---- fuzz: random text, keys, flags, sizes against the new widgets ------------------------------------------------------

struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
    unsigned Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 16); }
    int Int(int n) { return (int)(Next() % (unsigned)n); }
    float F(float a, float b) { return a + (b - a) * (float)(Next() % 10001) / 10000.0f; }
    float Nasty() {
        switch (Int(7)) {
            case 0: return std::numeric_limits<float>::quiet_NaN();
            case 1: return std::numeric_limits<float>::infinity();
            case 2: return -1e30f;
            case 3: return 1e30f;
            default: return F(-50.0f, 400.0f);
        }
    }
};

void TestFuzz() {
    RUN_SUITE("step 3 widgets: fuzz");
    const char* pieces[] = {"a", "bc", "\n", "\n\n", "\t", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xFF", "   ", "word word word", "\r\n", "\x01"};
    for (int seed = 1; seed <= 40; ++seed) {
        Reset();
        if (seed & 1) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        Rng r((unsigned long long)seed);
        Sim sim(520, 520);
        const int cap1 = 1 + r.Int(200), cap2 = 1 + r.Int(8);
        std::string store1((size_t)cap1 + 8, 'x'), store2((size_t)cap2 + 8, 'x');   // slack after the declared capacity: a write past it would show
        char* b1 = &store1[0]; char* b2 = &store2[0];
        b1[0] = 0; b2[0] = 0;
        float vals[16]; for (float& v : vals) v = r.Nasty();
        bool open[3] = {false, false, false};
        auto ui = [&] {
            if (UI::Begin("W", 10, 10, 500, 500)) {
                UI::InputTextMultiline("m1", b1, cap1, UI::Vec2{r.Nasty(), r.Nasty()}, r.Int(2) ? UI::InputText_ReadOnly : 0);
                UI::InputTextMultiline("m2", b2, cap2, UI::Vec2{150, 40});
                UI::PlotOptions o; o.valuesOffset = r.Int(100) - 30; o.size = UI::Vec2{r.Nasty(), r.Nasty()}; o.scaleMin = r.Nasty(); o.scaleMax = r.Nasty();
                UI::PlotLines("pl", vals, 1 + r.Int(16), o); UI::PlotHistogram("ph", vals, 1 + r.Int(16), o);
                for (int i = 0; i < 3; ++i) {
                    char l[8]; std::snprintf(l, sizeof l, "n%d", i);
                    open[i] = UI::TreeNodeEx(l, (unsigned)r.Int(512));
                    if (open[i]) { UI::Selectable("child", r.Int(2) != 0, (unsigned)r.Int(16), UI::Vec2{r.Nasty(), r.Nasty()}); if (!(r.Int(8) == 0)) UI::TreePop(); }
                }
                UI::ProgressBar(r.Nasty(), UI::Vec2{r.Nasty(), r.Nasty()}, "x"); UI::ArrowButton("a", r.Int(8) - 2);
            }
            UI::End();
        };
        for (int f = 0; f < 120; ++f) {
            const int what = r.Int(12);
            if (what < 3) sim.Move(r.F(-10, 530), r.F(-10, 530));
            else if (what == 3) { if (!sim.down) sim.Down(); }
            else if (what == 4) { if (sim.down) sim.Up(); }
            else if (what == 5) sim.Type(pieces[r.Int(13)]);
            else if (what == 6) sim.Press((okayuitest::Key)r.Int(okayuitest::Key_COUNT));
            else if (what == 7) sim.wheel = r.F(-3, 3);
            else if (what == 8 && r.Int(3) == 0) { b1[0] = (char)('a' + r.Int(26)); b1[1] = 0; }
            sim.Frame(ui);
            // the buffers stay terminated inside their capacity and the slack is untouched
            CHECK(std::memchr(b1, 0, (size_t)cap1) != nullptr && std::memchr(b2, 0, (size_t)cap2) != nullptr);
            bool slackOk = true;
            for (size_t i = (size_t)cap1; i < store1.size(); ++i) if (store1[i] != 'x') slackOk = false;
            for (size_t i = (size_t)cap2; i < store2.size(); ++i) if (store2[i] != 'x') slackOk = false;
            CHECK(slackOk);
            const UI::DrawData d = UI::GetDrawData();
            CHECK(d.indexCount % 3 == 0 && d.dropped == 0);
            if (f % 40 == 0) sim.Draw(12, 14, 20);
        }
        if (seed & 1) UI::UseLauncherLook(false);
    }
}

} // namespace

int main() {
    TestMultiline();
    TestHintArrowProgress();
    TestSelectable();
    TestTreeFlags();
    TestPlots();
    TestFuzz();
    TEST_MAIN_RESULT();
}
