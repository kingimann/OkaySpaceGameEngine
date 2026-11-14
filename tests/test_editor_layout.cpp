#include "test_framework.hpp"
#include "EditorLayoutMath.hpp"
#include <string>
#include <vector>

// The editor's layout maths: overlay anchoring, label placement and fading, Inspector density,
// toolbar planning, maximize bookkeeping and the view preferences file.
using namespace okay::edlayout;

int main() {
    RUN_SUITE("editor_layout");

    const Rc canvas = MakeRc(0, 0, 800, 600);

    {   // Overlays: each corner, margin respected.
        const Pt sz{180, 100};
        auto tl = PlaceOverlay(canvas, sz, Corner::TopLeft, 8, {});
        CHECK(tl.visible && !tl.moved);
        CHECK_NEAR(tl.rect.x0, 8, 1e-4); CHECK_NEAR(tl.rect.y0, 8, 1e-4);
        auto tr = PlaceOverlay(canvas, sz, Corner::TopRight, 8, {});
        CHECK_NEAR(tr.rect.x1, 792, 1e-4); CHECK_NEAR(tr.rect.y0, 8, 1e-4);
        auto bl = PlaceOverlay(canvas, sz, Corner::BottomLeft, 8, {});
        CHECK_NEAR(bl.rect.x0, 8, 1e-4); CHECK_NEAR(bl.rect.y1, 592, 1e-4);
        auto br = PlaceOverlay(canvas, sz, Corner::BottomRight, 8, {});
        CHECK_NEAR(br.rect.x1, 792, 1e-4); CHECK_NEAR(br.rect.y1, 592, 1e-4);
    }
    {   // The stats line at the top left: the camera readout steps below it (the reported collision).
        const Rc statsLine = MakeRc(8, 6, 300, 18);
        auto p = PlaceOverlay(canvas, Pt{188, 100}, Corner::TopLeft, 8, {statsLine});
        CHECK(p.visible && p.moved);
        CHECK(!Overlaps(p.rect, statsLine));
        CHECK(p.rect.y0 >= statsLine.y1);
        // Bottom anchored overlays step UP past a rectangle instead.
        const Rc preview = MakeRc(500, 450, 290, 140);
        auto b = PlaceOverlay(canvas, Pt{188, 100}, Corner::BottomRight, 8, {preview});
        CHECK(b.moved && !Overlaps(b.rect, preview) && b.rect.y1 <= preview.y0);
        // Two rectangles stacked: still clear of both.
        const Rc second = MakeRc(8, 26, 400, 20);
        auto c = PlaceOverlay(canvas, Pt{188, 100}, Corner::TopLeft, 8, {statsLine, second});
        CHECK(c.visible && !Overlaps(c.rect, statsLine) && !Overlaps(c.rect, second));
        // No room left: hidden, never drawn over the thing to avoid.
        auto d = PlaceOverlay(MakeRc(0, 0, 200, 120), Pt{188, 100}, Corner::TopLeft, 8, {statsLine});
        CHECK(!d.visible);
        auto e = PlaceOverlay(MakeRc(0, 0, 100, 100), Pt{188, 100}, Corner::TopLeft, 8, {});
        CHECK(!e.visible);
        // A collapsed overlay (one line) fits where the full one did not.
        auto f = PlaceOverlay(MakeRc(0, 0, 200, 120), Pt{188, 20}, Corner::TopLeft, 8, {statsLine});
        CHECK(f.visible);
    }

    {   // Labels: one alone takes the nearest spot (right of the anchor) and clears the keep-out disc.
        LabelReq q; q.anchor = {400, 300}; q.size = {80, 14}; q.keepOut = 40; q.priority = 1;
        auto r = PlaceLabels({q}, canvas, {});
        CHECK(r.size() == 1 && r[0].visible && r[0].alpha == 1.0f && r[0].scale == 1.0f);
        CHECK(r[0].rect.x0 >= 400 + 40);
        CHECK(Inside(canvas, r[0].rect));
    }
    {   // Name label vs gizmo hint at the same anchor: they must not overlap each other, and stay out of the gizmo.
        LabelReq name; name.anchor = {400, 300}; name.size = {70, 14}; name.keepOut = 50; name.priority = 10;
        LabelReq hint = name; hint.size = {160, 14}; hint.priority = 5;
        auto r = PlaceLabels({hint, name}, canvas, {});    // deliberately out of priority order
        CHECK(r[0].visible && r[1].visible);
        CHECK(!Overlaps(r[0].rect, r[1].rect));
        const Rc disc = MakeRc(350, 250, 100, 100);
        CHECK(!Overlaps(r[0].rect, disc) && !Overlaps(r[1].rect, disc));
        // The higher priority one took the nearest (right-hand) spot.
        CHECK(r[1].rect.x0 >= 450 && r[1].rect.x0 < r[0].rect.x0 + 1e-3 + 400);
    }
    {   // An obstacle (the HUD box) where the label would go: it moves, never overlaps it.
        LabelReq q; q.anchor = {100, 20}; q.size = {90, 14}; q.keepOut = 12; q.priority = 1;
        const Rc hud = MakeRc(8, 6, 300, 18);
        auto r = PlaceLabels({q}, canvas, {hud});
        CHECK(r[0].visible && !Overlaps(r[0].rect, hud));
    }
    {   // Near the canvas edge: clamped inside.
        LabelReq q; q.anchor = {795, 595}; q.size = {90, 14}; q.keepOut = 10; q.priority = 1;
        auto r = PlaceLabels({q}, canvas, {});
        CHECK(r[0].visible && Inside(canvas, r[0].rect));
    }
    {   // Crowded: many labels on one spot. Some hide or fade, none that stay visible at full strength overlap.
        std::vector<LabelReq> qs;
        for (int i = 0; i < 12; ++i) { LabelReq q; q.anchor = {400.0f + i, 300.0f}; q.size = {90, 14}; q.keepOut = 6; q.priority = 12 - i; qs.push_back(q); }
        auto r = PlaceLabels(qs, canvas, {});
        int shown = 0, dimmed = 0, hidden = 0;
        for (auto& p : r) { if (!p.visible) ++hidden; else { ++shown; if (p.alpha < 1.0f || p.scale < 1.0f) ++dimmed; } }
        CHECK(shown >= 4);
        CHECK(hidden + dimmed > 0);
        for (std::size_t i = 0; i < r.size(); ++i)
            for (std::size_t j = i + 1; j < r.size(); ++j)
                if (r[i].visible && r[j].visible && r[i].alpha == 1.0f && r[j].alpha == 1.0f)
                    CHECK(!Overlaps(r[i].rect, r[j].rect));
        // The first (highest priority) got the nearest spot, at full strength.
        CHECK(r[0].visible && r[0].alpha == 1.0f);
    }
    {   // Determinism: the same input twice gives the same boxes.
        LabelReq q; q.anchor = {300, 200}; q.size = {60, 14}; q.keepOut = 20; q.priority = 3;
        auto a = PlaceLabels({q, q}, canvas, {}); auto b = PlaceLabels({q, q}, canvas, {});
        CHECK(a[1].rect.x0 == b[1].rect.x0 && a[1].rect.y0 == b[1].rect.y0);
    }
    {   // Crowd fade: 1 when roomy, 0.35 floor when packed, falling in between.
        CHECK(CrowdFade(1, 100) == 1.0f);
        CHECK(CrowdFade(5, 480000) == 1.0f);
        CHECK_NEAR(CrowdFade(500, 480000), 0.35f, 1e-4);
        const float mid = CrowdFade(100, 480000);
        CHECK(mid > 0.35f && mid < 1.0f);
        CHECK(CrowdFade(60, 480000) >= CrowdFade(100, 480000));
    }

    {   // Density: Comfortable is roomier than Compact in every metric; row height follows the padding.
        const auto c = ComputeInspectorMetrics(Density::Compact, 1.0f, 16.0f, 560.0f);
        const auto k = ComputeInspectorMetrics(Density::Comfortable, 1.0f, 16.0f, 560.0f);
        CHECK(k.itemSpacing.y > c.itemSpacing.y && k.framePadding.y > c.framePadding.y);
        CHECK(k.sectionGap > c.sectionGap && k.rowHeight > c.rowHeight);
        CHECK_NEAR(k.rowHeight, 16.0f + k.framePadding.y * 2, 1e-4);
        // Scaling follows the UI scale.
        const auto k2 = ComputeInspectorMetrics(Density::Comfortable, 2.0f, 32.0f, 1120.0f);
        CHECK_NEAR(k2.itemSpacing.y, k.itemSpacing.y * 2, 1e-3);
        // Wide panel: both columns, label at least its minimum, value + label + gap = the width.
        CHECK(!k.stacked && k.labelWidth >= 96.0f);
        CHECK_NEAR(k.valueWidth + k.labelWidth + k.innerSpacing.x, 560.0f, 1e-2);
        // 440 px panel (content ~ 400): still two readable columns.
        const auto n = ComputeInspectorMetrics(Density::Comfortable, 1.0f, 16.0f, 400.0f);
        CHECK(!n.stacked && n.labelWidth >= 96.0f && n.valueWidth >= 120.0f);
        // Narrower than both minimums: the value gets everything.
        const auto t = ComputeInspectorMetrics(Density::Comfortable, 1.0f, 16.0f, 200.0f);
        CHECK(t.stacked && t.valueWidth == 200.0f);
        // Every width keeps value <= width and labels never negative.
        for (float w = 50; w < 1500; w += 37) {
            auto m = ComputeInspectorMetrics(Density::Compact, 1.0f, 16.0f, w);
            CHECK(m.valueWidth <= w + 1e-3f && m.labelWidth >= 0.0f);
        }
        CHECK(std::string(DensityName(0)) == "Compact" && std::string(DensityName(1)) == "Comfortable");
        CHECK(ClampDensity(-4) == 0 && ClampDensity(9) == 1);
    }

    {   // Toolbar: three groups of icons. Wide: every label. Tighter: labels on the leading tools only. Tighter: none.
        // Narrowest: the last tools move into the overflow menu.
        std::vector<ToolbarItem> it = {
            {28, 90, 0}, {28, 100, 0}, {28, 80, 0},   // move rotate scale
            {28, 70, 1}, {28, 70, 1},                // space snap
            {28, 60, 2}};                            // frame
        auto wide = PlanToolbar(it, 900, true, 2, 10, 40);
        CHECK(wide.labels() && wide.labelCount == 6 && wide.overflowed == 0);
        CHECK(wide.dividerBefore[3] && wide.dividerBefore[5] && !wide.dividerBefore[1]);
        // All labels = 90+100+80+70+70+60 + 6 gaps/dividers (2+2+10+2+10) = 506; icons = 168 + 26 = 194.
        auto part = PlanToolbar(it, 400, true, 2, 10, 40);   // room 360: three labels fit (90+100+80+28*3+... ), not six
        CHECK(part.labels() && part.labelCount > 0 && part.labelCount < 6 && part.overflowed == 0);
        auto none = PlanToolbar(it, 240, true, 2, 10, 40);   // room 200: only bare icons (194) fit
        CHECK(!none.labels() && none.overflowed == 0);
        auto off = PlanToolbar(it, 150, false, 2, 10, 40);   // even the icons do not
        CHECK(off.overflowed > 0 && off.shown[0] && !off.shown.back());
        CHECK(off.used <= 150 - 40 + 1e-3);
        // Labels switched off: never label.
        CHECK(!PlanToolbar(it, 900, false, 2, 10, 40).labels());
        // More room never means fewer labels.
        int prev = 0;
        for (float a = 100; a < 1000; a += 10) {
            auto pl = PlanToolbar(it, a, true, 2, 10, 40);
            CHECK(pl.labelCount >= prev);
            prev = pl.labelCount;
            CHECK(pl.used <= (a - 40) + 1e-3 || pl.overflowed == (int)it.size());
        }
        // Shown items always form a prefix (nothing disappears from the middle).
        bool seenHidden = false; for (bool s : off.shown) { if (!s) seenHidden = true; else CHECK(!seenHidden); }
        CHECK(PlanToolbar({}, 100, true, 2, 10, 40).overflowed == 0);
    }

    {   // Maximize: enter stores, leave returns it exactly, double enter / leave are refused.
        MaximizeState s;
        std::string ini; std::vector<std::string> open; std::string name;
        CHECK(!LeaveMaximize(s, ini, open, name));
        CHECK(EnterMaximize(s, "[Window][Scene]\nPos=0,0\n", {"Scene", "Inspector"}, "Wide"));
        CHECK(s.on && !EnterMaximize(s, "other", {}, "x"));
        CHECK(s.layoutIni.find("Pos=0,0") != std::string::npos);
        CHECK(LeaveMaximize(s, ini, open, name));
        CHECK(ini == "[Window][Scene]\nPos=0,0\n" && open.size() == 2 && open[1] == "Inspector" && name == "Wide");
        CHECK(!s.on && s.layoutIni.empty());
    }

    {   // Preferences: defaults, round-trip, clamps, unknown keys left alone.
        ViewPrefs d;
        CHECK(!d.camHud && d.statsLine && d.showNames && !d.toolbarLabels && d.density == (int)Density::Comfortable);
        ViewPrefs p;
        p.camHud = true; p.camHudCollapsed = true; p.camHudCorner = (int)Corner::BottomLeft; p.camHudOpacityPct = 35;
        p.statsLine = false; p.showNames = false; p.toolbarLabels = true; p.density = (int)Density::Compact;
        ViewPrefs q = ViewPrefs::Parse("settingsver 3\nvsync 1\n" + p.Write() + "theme 1\n");
        CHECK(q.camHud && q.camHudCollapsed && q.camHudCorner == (int)Corner::BottomLeft && q.camHudOpacityPct == 35);
        CHECK(!q.statsLine && !q.showNames && q.toolbarLabels && q.density == (int)Density::Compact);
        ViewPrefs c = ViewPrefs::Parse("camhudcorner 77\ncamhudopacity 5\ndensity 9\n");
        CHECK(c.camHudCorner == kCornerCount - 1 && c.camHudOpacityPct == 20 && c.density == 1);
        c = ViewPrefs::Parse("camhudcorner -3\ncamhudopacity 900\ndensity -1\n");
        CHECK(c.camHudCorner == 0 && c.camHudOpacityPct == 100 && c.density == 0);
        // The built-in layout that was on screen comes back; -1 (a custom arrangement) is kept; junk is clamped.
        CHECK(d.layoutPreset == 0);
        CHECK(ViewPrefs::Parse("layoutpreset 3\n").layoutPreset == 3 && ViewPrefs::Parse("layoutpreset -1\n").layoutPreset == -1);
        CHECK(ViewPrefs::Parse("layoutpreset -9\n").layoutPreset == -1 && ViewPrefs::Parse("layoutpreset 4000\n").layoutPreset == 99);
        { ViewPrefs w; w.layoutPreset = 4; CHECK(ViewPrefs::Parse(w.Write()).layoutPreset == 4); }
        ViewPrefs u; CHECK(!u.Apply("vsync", 1) && u.Apply("shownames", 0) && !u.showNames);
        CHECK(std::string(CornerName(1)) == "Top right" && CornerName(9)[0] == '\0');
    }

    TEST_MAIN_RESULT();
}
