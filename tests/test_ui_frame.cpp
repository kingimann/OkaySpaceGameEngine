// UIFrame: converting a widget from its anchor preset to a UI Frame lands on exactly
// the same pixels (every preset, root and nested, at several canvas sizes including
// a fractional Canvas scale) and undoes as one step; a write to the widget's own
// position / size / anchor fields (a script, a tween) moves the frame instead of
// being overwritten; free anchors and pivots resolve as documented; the frame saves
// and loads.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/UI/UIFrameTransform.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/EventSystem.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::ui2d;
using URect = okay::ui2d::Rect;

namespace {

// Records every rect the UI pass fills or outlines: the drawn pixels, as rects.
struct RectRecorder : Painter {
    std::vector<int> v;
    void SetDrawColor(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void FillRect(const URect* r) override { v.insert(v.end(), {r->x, r->y, r->w, r->h}); }
    void DrawRect(const URect* r) override { v.insert(v.end(), {-1, r->x, r->y, r->w, r->h}); }
    void DrawLine(int x0, int y0, int x1, int y1) override { v.insert(v.end(), {-2, x0, y0, x1, y1}); }
    void DrawPoint(int, int) override {}
    void SetClipRect(const URect*) override {}
    void GetClipRect(URect* r) override { *r = URect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return false; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override {}
    void CopyF(Tex, const URect*, const FRect*) override {}
    void CopyEx(Tex, const URect*, const URect*, double, const Point*, int) override {}
    void CopyExF(Tex, const URect*, const FRect*, double, const FPoint*, int) override {}
    void Geometry(Tex, const Vertex*, int, const int*, int) override {}
};

struct Size { int w, h; Canvas::ScaleMode mode; Vec2 ref; };

// A canvas holding, for every preset, a root-level panel and a button nested in a
// centred parent panel, with fractional offsets so rounding is exercised.
void Build(Scene& s, Canvas::ScaleMode mode, Vec2 ref) {
    GameObject* cv = s.CreateGameObject("canvas");
    auto* c = cv->AddComponent<Canvas>();
    c->scaleMode = mode; c->referenceResolution = ref;
    GameObject* parent = s.CreateGameObject("parent");
    parent->transform->SetParent(cv->transform, false);
    auto* pp = parent->AddComponent<UIPanel>();
    pp->anchor = UIAnchor::Center; pp->size = {401.5f, 303.25f}; pp->position = {11.5f, -7.25f};
    for (int a = 0; a < 12; ++a) {
        const bool stretch = a >= 9;
        const Vec2 pos = stretch ? Vec2{21.5f, 13.25f} : Vec2{13.3f, 7.7f};
        const Vec2 size = stretch ? Vec2{25.75f, 17.5f} : Vec2{120.5f, 44.25f};
        GameObject* g = s.CreateGameObject("root" + std::to_string(a));
        g->transform->SetParent(cv->transform, false);
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = (UIAnchor)a; p->position = pos; p->size = size;
        GameObject* k = s.CreateGameObject("kid" + std::to_string(a));
        k->transform->SetParent(parent->transform, false);
        auto* b = k->AddComponent<UIButton>();
        b->anchor = (UIAnchor)a; b->position = pos * 0.5f; b->size = size * 0.5f; b->label = "";
    }
}

struct Snapshot { std::vector<float> rects; std::vector<int> drawn; };

Snapshot Take(Scene& s, const Size& sz) {
    Snapshot out;
    for (const auto& up : s.Objects()) {
        Vec2 o, r;
        if (GetUIScreenRect(up.get(), (float)sz.w, (float)sz.h, o, r))
            out.rects.insert(out.rects.end(), {o.x, o.y, r.x, r.y});
    }
    RectRecorder rec;
    RenderSceneUI(s, rec, sz.w, sz.h);
    out.drawn = rec.v;
    return out;
}

int Frames(Scene& s) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up->GetComponent<UIFrame>()) ++n;
    return n;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

// +90° clockwise on screen (Y grows downward): (x, y) -> (-y, x).
Vec2 Cw90(Vec2 v) { return {-v.y, v.x}; }

Vec2 ScaledAbout(Vec2 pivot, Vec2 scale, Vec2 p) {
    return pivot + Vec2{(p.x - pivot.x) * scale.x, (p.y - pivot.y) * scale.y};
}

// A painter that keeps the pixels, so a turned frame can be checked and saved.
struct PixPainter : Painter {
    Image img;
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    explicit PixPainter(int w, int h) : img(w, h) { img.Fill(Color::FromBytes(16, 18, 24, 255)); }
    void SetDrawColor(std::uint8_t rr, std::uint8_t gg, std::uint8_t bb, std::uint8_t aa) override { r = rr; g = gg; b = bb; a = aa; }
    void FillRect(const URect* rc) override {
        if (!rc || a < 16) return;
        const Color c = Color::FromBytes(r, g, b, a);
        for (int y = rc->y; y < rc->y + rc->h; ++y)
            for (int x = rc->x; x < rc->x + rc->w; ++x) img.SetPixel(x, y, c);
    }
    void DrawRect(const URect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const URect*) override {}
    void GetClipRect(URect* rc) override { *rc = URect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return false; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override {}
    void CopyF(Tex, const URect*, const FRect*) override {}
    void CopyEx(Tex, const URect*, const URect*, double, const Point*, int) override {}
    void CopyExF(Tex, const URect*, const FRect*, double, const FPoint*, int) override {}
    void Geometry(Tex, const Vertex* v, int nv, const int* idx, int ni) override {
        if (!v || nv < 3 || a < 16) return;
        const Color c = Color::FromBytes(r, g, b, a);
        const int n = idx ? ni : nv;
        auto at = [&](int i) { return Vec2{v[i].position.x, v[i].position.y}; };
        auto edge = [](Vec2 p, Vec2 q, Vec2 r) { return (r.x - p.x) * (q.y - p.y) - (r.y - p.y) * (q.x - p.x); };
        for (int i = 0; i + 2 < n; i += 3) {
            const int ia = idx ? idx[i] : i, ib = idx ? idx[i + 1] : i + 1, ic = idx ? idx[i + 2] : i + 2;
            if (ia < 0 || ib < 0 || ic < 0 || ia >= nv || ib >= nv || ic >= nv) continue;
            Vec2 a = at(ia), b = at(ib), c2 = at(ic);
            if (edge(a, b, c2) < 0.0f) std::swap(b, c2);
            const int x0 = (int)std::floor(std::min(a.x, std::min(b.x, c2.x)));
            const int y0 = (int)std::floor(std::min(a.y, std::min(b.y, c2.y)));
            const int x1 = (int)std::ceil(std::max(a.x, std::max(b.x, c2.x)));
            const int y1 = (int)std::ceil(std::max(a.y, std::max(b.y, c2.y)));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const Vec2 p{x + 0.5f, y + 0.5f};
                    if (edge(a, b, p) >= 0.0f && edge(b, c2, p) >= 0.0f && edge(c2, a, p) >= 0.0f)
                        img.SetPixel(x, y, c);
                }
        }
    }
};

std::string StripFrameTail(const std::string& text) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        if (any) out << '\n';
        any = true;
        const auto pos = line.find("uiframe");
        if (pos == std::string::npos) { out << line; continue; }
        std::istringstream ls(line.substr(pos + 7));
        std::vector<std::string> tok;
        std::string t;
        while (ls >> t) tok.push_back(t);
        if (tok.size() >= 13) tok.resize(10);   // drop rotation and the two scales
        out << line.substr(0, pos) << "uiframe";
        for (const auto& x : tok) out << ' ' << x;
    }
    if (!text.empty() && text.back() == '\n') out << '\n';
    return out.str();
}

// A native script component: moves its sibling panel in Start, as game code does.
struct MovePanelScript : Behaviour {
    void Start() override {
        if (auto* p = gameObject->GetComponent<UIPanel>()) { p->position = {77.0f, 55.0f}; p->size = {66.0f, 22.0f}; }
    }
};

} // namespace

int main() {
    RUN_SUITE("ui_frame");

    // ---- Converting is pixel-identical for every preset and canvas size ----
    const Size sizes[] = {
        {800, 600, Canvas::ScaleMode::ConstantPixelSize, {800, 600}},
        {1366, 768, Canvas::ScaleMode::ScaleWithScreenSize, {1920, 1080}},   // scale 0.711...
        {1001, 703, Canvas::ScaleMode::ScaleWithScreenSize, {1280, 720}},    // odd sizes, fractional scale
    };
    for (const Size& sz : sizes) {
        Scene s("convert");
        Build(s, sz.mode, sz.ref);
        const Snapshot before = Take(s, sz);
        CHECK(!before.drawn.empty() && before.rects.size() == 25 * 4);
        CHECK(ConvertSceneToUIFrames(s) == 25);                     // 1 parent + 12 roots + 12 kids
        const Snapshot after = Take(s, sz);
        CHECK(after.rects == before.rects);                         // exactly, not approximately
        CHECK(after.drawn == before.drawn);
        int firstBad = -1;
        for (std::size_t i = 0; i < before.rects.size() && i < after.rects.size(); ++i)
            if (before.rects[i] != after.rects[i]) { firstBad = (int)i; break; }
        if (firstBad >= 0)
            std::printf("  rect value %d differs at %dx%d: %.6f vs %.6f\n", firstBad, sz.w, sz.h,
                        before.rects[firstBad], after.rects[firstBad]);
    }

    // ---- Convert-all is one undo step ----
    {
        okay::editor::EditorState ed;
        Build(ed.scene(), Canvas::ScaleMode::ConstantPixelSize, {800, 600});
        const Size sz{800, 600, Canvas::ScaleMode::ConstantPixelSize, {800, 600}};
        const Snapshot before = Take(ed.scene(), sz);
        ed.PushUndo();
        CHECK(ConvertSceneToUIFrames(ed.scene()) == 25);
        CHECK(Frames(ed.scene()) == 25);
        CHECK(ed.Undo());
        CHECK(Frames(ed.scene()) == 0);
        CHECK(Take(ed.scene(), sz).rects == before.rects);
    }

    // ---- A write to the widget's own fields moves the frame (never reverted) ----
    {
        Scene s("mirror");
        GameObject* g = s.CreateGameObject("panel");
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = UIAnchor::TopLeft; p->position = {10, 10}; p->size = {50, 50};
        UIFrame* f = ConvertToUIFrame(g);
        CHECK(f != nullptr);
        g->AddComponent<MovePanelScript>();
        s.Start();
        UpdateUILayouts(s);        // the frame settles before the update ...
        s.Update(0.016f);          // ... the script's Start writes the widget's fields ...
        Vec2 o, sz;
        CHECK(GetUIScreenRect(g, 800, 600, o, sz));
        CHECK(Near(o.x, 77) && Near(o.y, 55) && Near(sz.x, 66) && Near(sz.y, 22));   // ... and the rect moved
        CHECK(Near(f->position.x, 77) && Near(f->size.y, 22));
        UpdateUILayouts(s);        // the next frame keeps it
        s.Update(0.016f);
        CHECK(GetUIScreenRect(g, 800, 600, o, sz) && Near(o.x, 77));

        // A tween or tool writing through GetUIRect moves the frame and the mirror follows.
        UIRect r = GetUIRect(g);
        CHECK(r.frame == f && r.position == &f->position);
        *r.position = Vec2{5, 6};
        GetUIRect(g);
        CHECK(Near(p->position.x, 5) && Near(p->position.y, 6));

        // Writing the widget's anchor applies that preset to the frame.
        p->anchor = UIAnchor::BottomRight;
        CHECK(GetUIScreenRect(g, 800, 600, o, sz));
        CHECK(Near(o.x, 800 - 66 + 5) && Near(o.y, 600 - 22 + 6));
        CHECK(f->Preset() == UIAnchor::BottomRight);
    }

    // ---- Free anchors and pivots ----
    {
        Scene s("free");
        GameObject* g = s.CreateGameObject("panel");
        auto* p = g->AddComponent<UIPanel>();
        p->size = {100, 40};
        UIFrame* f = ConvertToUIFrame(g);
        f->anchorMin = f->anchorMax = {0.5f, 0.5f};
        f->pivot = {0.0f, 1.0f};                      // bottom-left corner on the centre
        f->position = {0, 0};
        Vec2 o, sz;
        CHECK(GetUIScreenRect(g, 800, 600, o, sz));
        CHECK(Near(o.x, 400) && Near(o.y, 260) && Near(sz.x, 100));
        // Stretch between 25% and 75% of the width, 10 px margins.
        f->anchorMin = {0.25f, 0.0f}; f->anchorMax = {0.75f, 0.0f};
        f->position = {10, 0}; f->size = {10, 40}; f->pivot = {0, 0};
        CHECK(GetUIScreenRect(g, 800, 600, o, sz));
        CHECK(Near(o.x, 210) && Near(sz.x, 380) && Near(sz.y, 40));
        CHECK(GetUIRect(g).Stretches());
        // Custom anchors: code that only knows presets still sees one.
        CHECK(p->anchor == f->Preset());

        // Saved and loaded with its custom anchors; the widget mirrors it after loading.
        Scene b("free2");
        CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(s)));
        UIFrame* f2 = nullptr; GameObject* g2 = nullptr;
        for (const auto& up : b.Objects()) if (auto* x = up->GetComponent<UIFrame>()) { f2 = x; g2 = up.get(); }
        CHECK(f2 && Near(f2->anchorMin.x, 0.25f) && Near(f2->anchorMax.x, 0.75f) && Near(f2->position.x, 10));
        if (g2) {
            CHECK(GetUIScreenRect(g2, 800, 600, o, sz));
            CHECK(Near(o.x, 210) && Near(sz.x, 380));
        }
    }

    // ---- New widgets without an editable size (layout groups) are left alone ----
    {
        Scene s("lg");
        GameObject* g = s.CreateGameObject("lg");
        g->AddComponent<UILayoutGroup>();
        CHECK(ConvertToUIFrame(g) == nullptr);
    }

    // ---- Rotation and scale: children inherit them, hit tests follow the shape ----
    {
        Scene s("turn");
        s.physicsEnabled = false;
        GameObject* parent = s.CreateGameObject("Panel");
        auto* pp = parent->AddComponent<UIPanel>();
        pp->anchor = UIAnchor::TopLeft;
        pp->position = {100.0f, 80.0f};
        pp->size = {200.0f, 100.0f};
        pp->shape = UIShape::Rectangle;
        pp->color = Color::FromBytes(220, 40, 50, 255);
        UIFrame* pf = ConvertToUIFrame(parent);
        CHECK(pf != nullptr);
        pf->pivot = {0.5f, 0.5f};

        GameObject* child = s.CreateGameObject("Child");
        child->transform->SetParent(parent->transform, false);
        auto* cp = child->AddComponent<UIPanel>();
        cp->anchor = UIAnchor::TopLeft;
        cp->position = {20.0f, 10.0f};
        cp->size = {40.0f, 20.0f};
        cp->shape = UIShape::Rectangle;
        cp->color = Color::FromBytes(40, 90, 220, 255);
        UIFrame* cf = ConvertToUIFrame(child);
        CHECK(cf != nullptr);

        Vec2 po, psz, co, csz;
        CHECK(GetUIScreenRect(parent, 800, 600, po, psz));
        CHECK(GetUIScreenRect(child, 800, 600, co, csz));
        const Vec2 pivot = po + psz * 0.5f;                 // position places the pivot
        CHECK(Near(pivot.x, pp->position.x) && Near(pivot.y, pp->position.y));
        const Vec2 childCenter = co + csz * 0.5f;

        // No turn and a scale of 1 leaves the layout rect alone.
        Vec2 corners[4];
        CHECK(UIFrameCorners(parent, 800, 600, corners));
        CHECK(Near(corners[0].x, po.x) && Near(corners[0].y, po.y));
        CHECK(Near(corners[1].x, po.x + psz.x) && Near(corners[2].y, po.y + psz.y));
        CHECK(!UIFrameTransformed(parent));

        // Non-uniform scale around the pivot, before any turn. The child's
        // centre moves twice as far in X and half as far in Y.
        pf->scale = {2.0f, 0.5f};
        CHECK(UIFrameTransformed(child));                   // inherited, even with the child's own scale at 1
        CHECK(UIFrameCorners(child, 800, 600, corners));
        const Vec2 scaledCenter = ScaledAbout(pivot, pf->scale, childCenter);
        const Vec2 gotCenter = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
        CHECK(Near(gotCenter.x, scaledCenter.x) && Near(gotCenter.y, scaledCenter.y));
        // The child's own box is scaled too: 40x20 becomes 80x10.
        CHECK(Near((corners[1] - corners[0]).x, 80.0f) && Near((corners[3] - corners[0]).y, 10.0f));

        // A quarter turn clockwise around the same pivot. Distance from the
        // pivot is kept; the offset swings from right/down to down/left.
        pf->scale = {1.0f, 1.0f};
        pf->rotation = 90.0f;
        CHECK(UIFrameCorners(child, 800, 600, corners));
        const Vec2 turnedCenter = pivot + Cw90(childCenter - pivot);
        const Vec2 gotTurned = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
        CHECK(Near(gotTurned.x, turnedCenter.x) && Near(gotTurned.y, turnedCenter.y));
        // The child's right edge now points down.
        const Vec2 right = corners[1] - corners[0];
        CHECK(Near(right.x, 0.0f) && Near(right.y, 40.0f));

        // Hit testing: the turned child contains its new centre and not the
        // place it used to sit. A point in the old box but outside the turned
        // one misses; a point outside the old box but inside the turned one hits.
        UICanvas::Set(800, 600);
        CHECK(UIScreenContains(child, turnedCenter, 800, 600));
        CHECK(!UIScreenContains(child, childCenter, 800, 600));
        auto* events = s.CreateGameObject("Events")->AddComponent<EventSystem>();
        events->Pump(s, turnedCenter, true, true);
        CHECK(events->Hovered() == child);
        events->Pump(s, childCenter, true, true);
        CHECK(events->Hovered() != child);

        // A button's own click test follows the same shape.
        GameObject* button = s.CreateGameObject("Go");
        button->transform->SetParent(parent->transform, false);
        auto* bt = button->AddComponent<UIButton>();
        bt->anchor = UIAnchor::TopLeft;
        bt->position = {150.0f, 30.0f};
        bt->size = {40.0f, 20.0f};
        bt->label = "";
        UIFrame* bf = ConvertToUIFrame(button);
        CHECK(bf != nullptr);
        Vec2 bo, bsz;
        CHECK(GetUIScreenRect(button, 800, 600, bo, bsz));
        const Vec2 buttonWas = bo + bsz * 0.5f;
        const Vec2 buttonNow = pivot + Cw90(buttonWas - pivot);
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 800, 600, o, sz); };
        UIWorld().clipContains = nullptr;
        CHECK(bt->Contains(buttonNow));
        CHECK(!bt->Contains(buttonWas));
        UIWorld().rectOf = nullptr;
        UICanvas::Set(1280, 720);

        // Anchors still place the child: a centred child stays on the pivot
        // when the parent turns about its centre.
        GameObject* mid = s.CreateGameObject("Mid");
        mid->transform->SetParent(parent->transform, false);
        auto* mp = mid->AddComponent<UIPanel>();
        mp->anchor = UIAnchor::Center;
        mp->position = {0.0f, 0.0f};
        mp->size = {20.0f, 20.0f};
        ConvertToUIFrame(mid);
        CHECK(UIFrameCorners(mid, 800, 600, corners));
        const Vec2 midCenter = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
        CHECK(Near(midCenter.x, pivot.x) && Near(midCenter.y, pivot.y));

        // A vertical stack's spacing turns with the frame (layout writes local
        // positions; the frame then turns them).
        pf->rotation = 0.0f;
        auto* stack = parent->AddComponent<UILayoutGroup>();
        stack->direction = UILayoutGroup::Direction::Vertical;
        stack->spacing = 4.0f;
        stack->origin = {8.0f, 6.0f};
        stack->anchor = UIAnchor::TopLeft;
        GameObject* rowA = s.CreateGameObject("RowA");
        rowA->transform->SetParent(parent->transform, false);
        auto* ra = rowA->AddComponent<UIButton>();
        ra->size = {30.0f, 16.0f}; ra->label = "";
        GameObject* rowB = s.CreateGameObject("RowB");
        rowB->transform->SetParent(parent->transform, false);
        auto* rb = rowB->AddComponent<UIButton>();
        rb->size = {30.0f, 16.0f}; rb->label = "";
        stack->Arrange();
        Vec2 ao, asz, bo2, bsz2;
        CHECK(GetUIScreenRect(rowA, 800, 600, ao, asz));
        CHECK(GetUIScreenRect(rowB, 800, 600, bo2, bsz2));
        const Vec2 aCenter = ao + asz * 0.5f, bCenter = bo2 + bsz2 * 0.5f;
        const float gap = (bCenter - aCenter).Magnitude();
        pf->rotation = 90.0f;
        CHECK(UIFrameCorners(rowA, 800, 600, corners));
        const Vec2 aTurn = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
        CHECK(UIFrameCorners(rowB, 800, 600, corners));
        const Vec2 bTurn = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
        CHECK(Near(aTurn.x, (pivot + Cw90(aCenter - pivot)).x));
        CHECK(Near(bTurn.y, (pivot + Cw90(bCenter - pivot)).y));
        CHECK(Near((bTurn - aTurn).Magnitude(), gap));
    }

    // ---- Old scenes load with no turn and a scale of 1; new values round-trip ----
    {
        Scene s("save");
        GameObject* g = s.CreateGameObject("Panel");
        auto* p = g->AddComponent<UIPanel>();
        p->position = {12.0f, 18.0f}; p->size = {80.0f, 30.0f};
        UIFrame* f = ConvertToUIFrame(g);
        const std::string fresh = SceneSerializer::Serialize(s);
        const std::string old = StripFrameTail(fresh);
        CHECK(old.find("uiframe") != std::string::npos);
        CHECK(old.find("uiframe") != std::string::npos && old.find(" 0 1 1\n") == std::string::npos);
        Scene legacy("legacy");
        CHECK(SceneSerializer::Deserialize(legacy, old));
        UIFrame* loaded = nullptr;
        for (const auto& up : legacy.Objects()) if (auto* x = up->GetComponent<UIFrame>()) loaded = x;
        CHECK(loaded != nullptr);
        CHECK(Near(loaded->rotation, 0.0f) && Near(loaded->scale.x, 1.0f) && Near(loaded->scale.y, 1.0f));
        Vec2 o, sz;
        CHECK(GetUIScreenRect(legacy.Find("Panel"), 800, 600, o, sz));
        CHECK(Near(o.x, 12.0f) && Near(o.y, 18.0f) && Near(sz.x, 80.0f) && Near(sz.y, 30.0f));

        f->rotation = 33.5f;
        f->scale = {1.25f, 0.5f};
        f->pivot = {0.25f, 0.75f};
        Scene again("again");
        CHECK(SceneSerializer::Deserialize(again, SceneSerializer::Serialize(s)));
        UIFrame* f2 = again.Find("Panel")->GetComponent<UIFrame>();
        CHECK(f2 != nullptr);
        CHECK(Near(f2->rotation, 33.5f) && Near(f2->scale.x, 1.25f) && Near(f2->scale.y, 0.5f));
        CHECK(Near(f2->pivot.x, 0.25f) && Near(f2->pivot.y, 0.75f));
        Vec2 before[4], after[4];
        CHECK(UIFrameCorners(g, 800, 600, before));
        CHECK(UIFrameCorners(again.Find("Panel"), 800, 600, after));
        for (int i = 0; i < 4; ++i) CHECK(Near(before[i].x, after[i].x) && Near(before[i].y, after[i].y));
    }

    // ---- Scripts and visual-script nodes get and set the same fields ----
    {
        Scene s("script");
        s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Panel");
        g->AddComponent<UIPanel>()->size = {40.0f, 40.0f};
        ConvertToUIFrame(g);
        auto* ctrl = s.CreateGameObject("Ctrl")->AddComponent<ScriptComponent>("okayscript");
        ctrl->LoadSource(
            "function start() {\n"
            "  ui_set_rotation(\"Panel\", 90);\n"
            "  ui_set_scale(\"Panel\", 2, 0.5);\n"
            "  rot = ui_get_rotation(\"Panel\");\n"
            "  sc = ui_get_scale(\"Panel\");\n"
            "  ui_set_scale(\"Wide\", 3);\n"
            "}\n");
        GameObject* wide = s.CreateGameObject("Wide");
        wide->AddComponent<UIPanel>()->size = {10.0f, 10.0f};
        s.Start();
        UIFrame* f = g->GetComponent<UIFrame>();
        UIFrame* w = wide->GetComponent<UIFrame>();
        CHECK(f && Near(f->rotation, 90.0f) && Near(f->scale.x, 2.0f) && Near(f->scale.y, 0.5f));
        CHECK(w && Near(w->scale.x, 3.0f) && Near(w->scale.y, 3.0f));   // one number sets both
        CHECK(ctrl->VM());
        CHECK_NEAR(ctrl->VM()->GetGlobal("rot").AsFloat(), 90.0f, 1e-3f);
        CHECK_NEAR(ctrl->VM()->GetGlobal("sc").AsVec3().x, 2.0f, 1e-3f);
        CHECK_NEAR(ctrl->VM()->GetGlobal("sc").AsVec3().y, 0.5f, 1e-3f);

        auto* host = s.CreateGameObject("Host")->AddComponent<VisualScriptComponent>();
        std::string err;
        const char* graph =
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 Fn.ui_set_rotation\ndefault 2 0 \"Panel\"\ndefault 2 1 45\n"
            "node 3 Fn.ui_set_scale\ndefault 3 0 \"Panel\"\ndefault 3 1 1.5\ndefault 3 2 0.25\n"
            "node 4 Fn.ui_get_rotation\ndefault 4 0 \"Panel\"\n"
            "node 5 SetVar deg\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\ndata 5 0 4 0\n";
        CHECK(host->LoadFromText(graph, &err));
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(Near(f->rotation, 45.0f) && Near(f->scale.x, 1.5f) && Near(f->scale.y, 0.25f));
        CHECK_NEAR(host->GetVariable("deg").AsFloat(), 45.0f, 1e-3f);
    }

    // ---- A turned, stretched frame actually draws that way ----
    {
        Scene s("draw");
        s.physicsEnabled = false;
        GameObject* parent = s.CreateGameObject("Panel");
        auto* pp = parent->AddComponent<UIPanel>();
        pp->anchor = UIAnchor::TopLeft;
        pp->position = {180.0f, 160.0f};
        pp->size = {180.0f, 48.0f};
        pp->shape = UIShape::Rectangle;
        pp->cornerRadius = 0.0f;
        pp->color = Color::FromBytes(220, 40, 50, 255);
        UIFrame* pf = ConvertToUIFrame(parent);
        pf->pivot = {0.5f, 0.5f};
        pf->rotation = 28.0f;
        pf->scale = {1.45f, 0.65f};
        GameObject* child = s.CreateGameObject("Child");
        child->transform->SetParent(parent->transform, false);
        auto* cp = child->AddComponent<UIPanel>();
        cp->anchor = UIAnchor::TopLeft;
        cp->position = {12.0f, 8.0f};
        cp->size = {36.0f, 20.0f};
        cp->shape = UIShape::Rectangle;
        cp->cornerRadius = 0.0f;
        cp->color = Color::FromBytes(40, 110, 230, 255);
        ConvertToUIFrame(child);

        PixPainter pix(640, 400);
        RenderSceneUI(s, pix, 640, 400);
        Vec2 pc[4], cc[4];
        CHECK(UIFrameCorners(parent, 640, 400, pc));
        CHECK(UIFrameCorners(child, 640, 400, cc));
        const Vec2 parentInside = (pc[0] + pc[1] + pc[2] + pc[3]) * 0.25f;
        const Vec2 childInside = (cc[0] + cc[1] + cc[2] + cc[3]) * 0.25f;
        Vec2 layoutO, layoutS;
        CHECK(GetUIScreenRect(parent, 640, 400, layoutO, layoutS));
        // Where the unturned top-left used to be is off the turned panel.
        CHECK(!UIScreenContains(parent, layoutO, 640, 400));
        CHECK(UIScreenContains(parent, parentInside, 640, 400));
        CHECK(UIScreenContains(child, childInside, 640, 400));
        auto red = pix.img.GetPixel((int)std::lround(parentInside.x), (int)std::lround(parentInside.y));
        auto blue = pix.img.GetPixel((int)std::lround(childInside.x), (int)std::lround(childInside.y));
        auto empty = pix.img.GetPixel((int)std::lround(layoutO.x), (int)std::lround(layoutO.y));
        CHECK(red.r > 0.7f && red.b < 0.3f);
        CHECK(blue.b > 0.6f && blue.r < 0.35f);
        CHECK(empty.r < 0.2f && empty.b < 0.2f);
        CHECK(pix.img.SavePNG("/tmp/okay-ui-frame-rotated.png"));
    }

    TEST_MAIN_RESULT();
}
