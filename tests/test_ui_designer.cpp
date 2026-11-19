// The UI designer's logic (editor/UIDesignerModel.hpp): zoom and pan, rulers, snapping to guides / grid / siblings,
// resize handles, nudge, align and distribute, z-order, layers (hide / lock / pick), anchor picker and the
// responsive preview. No ImGui, no scene: plain rectangles.
#include "test_framework.hpp"
#include "../editor/UIDesignerModel.hpp"

using namespace oked::uid;

int main() {
    {
        RUN_SUITE("view: zoom about the cursor keeps the point under it, fit centres the canvas");
        View v; CHECK(v.zoom == 1.0f);
        const float cx = 400, cy = 300;
        float bx, by; v.ToCanvas(cx, cy, bx, by);
        v.ZoomAt(cx, cy, 2.0f);
        float ax, ay; v.ToCanvas(cx, cy, ax, ay);
        CHECK_NEAR(ax, bx, 0.01); CHECK_NEAR(ay, by, 0.01);
        CHECK_NEAR(v.zoom, 2.0, 1e-5);
        for (int i = 0; i < 40; ++i) v.ZoomAt(cx, cy, 2.0f);
        CHECK(v.zoom <= kMaxZoom);
        for (int i = 0; i < 80; ++i) v.ZoomAt(cx, cy, 0.5f);
        CHECK(v.zoom >= kMinZoom);
        v.Fit(1000, 500, 1280, 720, 20);
        float sx, sy; v.ToScreen(640, 360, sx, sy);
        CHECK_NEAR(sx, 500, 0.01); CHECK_NEAR(sy, 250, 0.01);
        CHECK_NEAR(v.zoom, 460.0 / 720.0, 1e-4);
        v.Pan(10, -5); float px, py; v.ToScreen(640, 360, px, py);
        CHECK_NEAR(px, 510, 0.01); CHECK_NEAR(py, 245, 0.01);
    }
    {
        RUN_SUITE("rulers: nice steps, never closer than the minimum gap");
        for (float z : {0.1f, 0.5f, 1.0f, 3.0f, 8.0f}) {
            const float s = RulerStep(z);
            CHECK(s * z >= 40.0f - 0.001f);
            const float m = s / 1.0f;
            const float p = std::pow(10.0f, std::floor(std::log10(m)));
            const float f = m / p;
            CHECK(std::fabs(f - 1) < 1e-4 || std::fabs(f - 2) < 1e-4 || std::fabs(f - 5) < 1e-4);
        }
        auto t = RulerTicks(-30, 250, 1.0f);
        CHECK(!t.empty());
        for (auto& k : t) CHECK(k.pos >= -30 && k.pos <= 250);
        bool zero = false; for (auto& k : t) if (k.pos == 0 && k.major) zero = true;
        CHECK(zero);
    }
    {
        RUN_SUITE("snap: guides, siblings, canvas edges and grid; nothing when far away");
        SnapOptions o; o.threshold = 6; o.gridStep = 0;
        std::vector<Rect> others = {{200, 50, 100, 40}};
        Rect mv{203, 300, 50, 50};
        SnapResult r = SnapMove(mv, others, {}, {0, 0, 1280, 720}, o);
        CHECK_NEAR(r.dx, -3, 1e-4); CHECK(r.dy == 0);
        CHECK(!r.lines.empty() && r.lines[0].vertical && r.lines[0].pos == 200);
        // centre to sibling centre
        Rect mv2{225, 300, 50, 50};   // centre 250 = sibling centre 250
        r = SnapMove(mv2, others, {}, {0, 0, 1280, 720}, o); CHECK(r.dx == 0);
        CHECK(!r.lines.empty());
        // guide
        std::vector<Guide> g = {{false, 400.0f}};
        r = SnapMove({10, 396, 20, 20}, {}, g, {0, 0, 1280, 720}, o);
        CHECK_NEAR(r.dy, 4, 1e-4);
        // far
        r = SnapMove({500, 500, 20, 20}, {}, {}, {0, 0, 1280, 720}, o);
        CHECK(r.dx == 0 && r.dy == 0 && r.lines.empty());
        // grid
        o.gridStep = 16;
        r = SnapMove({18, 33, 20, 20}, {}, {}, {0, 0, 1280, 720}, o);
        CHECK_NEAR(r.dx, -2, 1e-4); CHECK_NEAR(r.dy, -1, 1e-4);
        // snapping off
        o.enabled = false; r = SnapMove({18, 33, 20, 20}, {}, {}, {0, 0, 1280, 720}, o);
        CHECK(r.dx == 0 && r.dy == 0);
    }
    {
        RUN_SUITE("resize: each handle moves its own edges; shift keeps the shape; alt resizes from the middle");
        Rect b{100, 100, 200, 100};
        Rect r = Resize(b, Handle::E, 50, 99, false, false);
        CHECK(r.x == 100 && r.w == 250 && r.y == 100 && r.h == 100);
        r = Resize(b, Handle::NW, 20, 10, false, false);
        CHECK(r.x == 120 && r.y == 110 && r.w == 180 && r.h == 90);
        r = Resize(b, Handle::SE, 100, 0, true, false);
        CHECK_NEAR(r.w / r.h, 2.0, 1e-3);
        r = Resize(b, Handle::E, 40, 0, false, true);
        CHECK(r.w == 280 && r.x == 60);
        r = Resize(b, Handle::W, 500, 0, false, false);   // dragged past the other side: stops at the minimum
        CHECK(r.w == kMinSize && r.x + r.w == 300);
        r = Resize(b, Handle::N, 0, -30, false, false);
        CHECK(r.y == 70 && r.h == 130);
    }
    {
        RUN_SUITE("nudge: 1 pixel, shift = grid step, none when nothing is pressed");
        float dx, dy;
        CHECK(Nudge(-1, 0, false, 8, dx, dy) && dx == -1 && dy == 0);
        CHECK(Nudge(0, 1, true, 8, dx, dy) && dx == 0 && dy == 8);
        CHECK(Nudge(1, 0, true, 0, dx, dy) && dx == 10);   // no grid: 10
        CHECK(!Nudge(0, 0, false, 8, dx, dy));
    }
    {
        RUN_SUITE("align and distribute");
        std::vector<Rect> v = {{10, 10, 50, 20}, {100, 40, 30, 30}, {300, 5, 60, 10}};
        auto a = Align(v, AlignMode::Left);   CHECK(a[0].x == 10 && a[1].x == 10 && a[2].x == 10);
        a = Align(v, AlignMode::Right);       CHECK(a[0].x + a[0].w == 360 && a[1].x + a[1].w == 360);
        a = Align(v, AlignMode::CenterH);     CHECK_NEAR(a[0].x + a[0].w / 2, a[2].x + a[2].w / 2, 1e-4);
        a = Align(v, AlignMode::Top);         CHECK(a[1].y == 5);
        a = Align(v, AlignMode::Bottom);      CHECK(a[2].y + a[2].h == 70);
        a = Align(v, AlignMode::CenterV);     CHECK_NEAR(a[0].y + a[0].h / 2, a[1].y + a[1].h / 2, 1e-4);
        // align to canvas when only one is selected
        a = AlignTo({{10, 10, 50, 20}}, AlignMode::CenterH, {0, 0, 1280, 720}); CHECK_NEAR(a[0].x, 615, 1e-4);
        auto d = Distribute(v, true);
        const float g1 = d[1].x - (d[0].x + d[0].w), g2 = d[2].x - (d[1].x + d[1].w);
        CHECK_NEAR(g1, g2, 1e-4);
        CHECK(d[0].x == 10 && d[2].x == 300);
        CHECK(Distribute({v[0], v[1]}, true)[1].x == 100);   // two items: nothing to do
        // unsorted input keeps its order in the result
        std::vector<Rect> u = {v[2], v[0], v[1]};
        auto du = Distribute(u, true); CHECK(du[0].x == 300 && du[1].x == 10);
    }
    {
        RUN_SUITE("z-order");
        std::vector<int> o = {0, 1, 2, 3, 4};   // back to front
        CHECK((Reorder(o, {1}, ZOp::ToFront) == std::vector<int>{0, 2, 3, 4, 1}));
        CHECK((Reorder(o, {3}, ZOp::ToBack) == std::vector<int>{3, 0, 1, 2, 4}));
        CHECK((Reorder(o, {1}, ZOp::Forward) == std::vector<int>{0, 2, 1, 3, 4}));
        CHECK((Reorder(o, {3}, ZOp::Backward) == std::vector<int>{0, 1, 3, 2, 4}));
        CHECK((Reorder(o, {1, 2}, ZOp::Forward) == std::vector<int>{0, 3, 1, 2, 4}));
        CHECK((Reorder(o, {4}, ZOp::Forward) == o));
        CHECK((Reorder(o, {0}, ZOp::Backward) == o));
        CHECK((Reorder(o, {0, 1}, ZOp::ToFront) == std::vector<int>{2, 3, 4, 0, 1}));
    }
    {
        RUN_SUITE("layers: hide and lock decide what can be picked; topmost wins");
        std::vector<Layer> L = {{1, {0, 0, 100, 100}, false, false}, {2, {50, 50, 100, 100}, false, false}, {3, {60, 60, 10, 10}, false, false}};
        CHECK(PickTopmost(L, 65, 65) == 3);
        L[2].locked = true; CHECK(PickTopmost(L, 65, 65) == 2);
        L[1].hidden = true; CHECK(PickTopmost(L, 65, 65) == 1);
        CHECK(PickTopmost(L, 500, 500) == -1);
        CHECK(!IsDrawn(L[1]) && IsDrawn(L[2]));
        auto in = PickInRect(L, {0, 0, 200, 200});
        CHECK((in == std::vector<int>{1}));   // hidden and locked ones are not box-selected
        CHECK(UniqueName({"Button", "Button 2"}, "Button") == "Button 3");
        CHECK(UniqueName({}, "Panel") == "Panel");
    }
    {
        RUN_SUITE("anchor picker: nine points and three stretches map to the legacy presets");
        CHECK(AnchorFromCell(0, 0) == okay::UIAnchor::TopLeft);
        CHECK(AnchorFromCell(1, 1) == okay::UIAnchor::Center);
        CHECK(AnchorFromCell(2, 2) == okay::UIAnchor::BottomRight);
        CHECK(AnchorFromCell(2, 0) == okay::UIAnchor::TopRight);
        CHECK(AnchorFromStretch(true, false) == okay::UIAnchor::StretchHorizontal);
        CHECK(AnchorFromStretch(false, true) == okay::UIAnchor::StretchVertical);
        CHECK(AnchorFromStretch(true, true) == okay::UIAnchor::StretchFull);
        int c, r; CHECK(CellOfAnchor(okay::UIAnchor::BottomCenter, c, r) && c == 1 && r == 2);
        CHECK(!CellOfAnchor(okay::UIAnchor::StretchFull, c, r));
        CHECK(std::string(AnchorLabel(okay::UIAnchor::MiddleLeft)) == "Middle left");
        // re-anchoring keeps the widget where it is on screen
        okay::Vec2 pos{20, 30}, size{100, 40}, np;
        okay::UIAnchor to = okay::UIAnchor::BottomRight;
        np = ReanchorKeepPlace(okay::UIAnchor::TopLeft, to, pos, size, 1280, 720);
        okay::Vec2 o1, s1, o2, s2;
        okay::ResolveAnchorRect(okay::UIAnchor::TopLeft, pos, size, 1280, 720, o1, s1);
        okay::ResolveAnchorRect(to, np, size, 1280, 720, o2, s2);
        CHECK_NEAR(o1.x, o2.x, 1e-3); CHECK_NEAR(o1.y, o2.y, 1e-3);
    }
    {
        RUN_SUITE("responsive preview: sizes, and where an anchored widget lands");
        CHECK(PreviewSizes().size() >= 6);
        for (auto& p : PreviewSizes()) CHECK(p.w > 0 && p.h > 0 && p.name && p.name[0]);
        okay::Vec2 pos{-20, 20}, size{100, 40};
        Rect a = PlaceAt(okay::UIAnchor::TopRight, pos, size, 1280, 720);
        Rect b = PlaceAt(okay::UIAnchor::TopRight, pos, size, 800, 600);
        CHECK_NEAR((1280 - (a.x + a.w)), (800 - (b.x + b.w)), 1e-3);   // keeps its distance from the right edge
        Rect s = PlaceAt(okay::UIAnchor::StretchFull, {10, 10}, {10, 10}, 800, 600);
        CHECK(s.w == 780 && s.h == 580);
        CHECK(OutsideCanvas({-50, 0, 40, 10}, 800, 600) && !OutsideCanvas({10, 10, 40, 10}, 800, 600));
    }
    TEST_MAIN_RESULT();
}
