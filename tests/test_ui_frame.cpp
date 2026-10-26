// UIFrame: converting a widget from its anchor preset to a UI Frame lands on exactly
// the same pixels (every preset, root and nested, at several canvas sizes including
// a fractional Canvas scale) and undoes as one step; a write to the widget's own
// position / size / anchor fields (a script, a tween) moves the frame instead of
// being overwritten; free anchors and pivots resolve as documented; the frame saves
// and loads.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <Okay.hpp>
#include <cmath>
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

    TEST_MAIN_RESULT();
}
