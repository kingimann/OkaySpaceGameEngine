// UIRenderOptions and the painter hooks of the engine UI renderer, checked with a
// recording painter (no SDL): the missing-texture hook, edit-mode resting state for
// animated widgets, and that edit mode ignores input.
#include "test_framework.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <Okay.hpp>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::ui2d;
using URect = okay::ui2d::Rect;

namespace {

struct RecordingPainter : Painter {
    std::vector<RGBA> colors;   // every SetDrawColor
    std::vector<std::string> missing;
    std::vector<URect> missingRects;
    int fills = 0;
    bool clip = false; URect clipRect{0, 0, 0, 0};

    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { colors.push_back({r, g, b, a}); }
    void FillRect(const URect*) override { ++fills; }
    void DrawRect(const URect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const URect* r) override { clip = r != nullptr; if (r) clipRect = *r; }
    void GetClipRect(URect* r) override { *r = clip ? clipRect : URect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return clip; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }   // nothing loads
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override {}
    void CopyF(Tex, const URect*, const FRect*) override {}
    void CopyEx(Tex, const URect*, const URect*, double, const Point*, int) override {}
    void CopyExF(Tex, const URect*, const FRect*, double, const FPoint*, int) override {}
    void Geometry(Tex, const Vertex*, int, const int*, int) override {}
    void MissingTexture(const std::string& path, const URect& where) override {
        missing.push_back(path); missingRects.push_back(where);
    }

    bool UsedColor(const Color& c) const {
        const std::uint8_t r = (std::uint8_t)(c.r * 255), g = (std::uint8_t)(c.g * 255), b = (std::uint8_t)(c.b * 255);
        for (const RGBA& k : colors) if (k.r == r && k.g == g && k.b == b && k.a > 0) return true;
        return false;
    }
};

bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

} // namespace

int main() {
    RUN_SUITE("ui_renderer_options");

    // --- Missing textures are reported with the widget's rect ---------------------
    {
        Scene s("missing"); s.physicsEnabled = false;
        auto* im = s.CreateGameObject("img")->AddComponent<UIImage>();
        im->position = Vec2{10, 20}; im->size = Vec2{64, 32}; im->texture = "no_such_image.png";
        auto* raw = s.CreateGameObject("raw")->AddComponent<UIRawImage>();
        raw->position = Vec2{100, 20}; raw->size = Vec2{40, 40}; raw->texture = "no_such_raw.png";
        auto* bt = s.CreateGameObject("btn")->AddComponent<UIButton>();
        bt->icon = "no_such_icon.png"; bt->iconSize = 16.0f;
        auto* flat = s.CreateGameObject("flat")->AddComponent<UIImage>();   // no texture at all
        flat->position = Vec2{200, 20};
        RecordingPainter p;
        RenderSceneUI(s, p, 640, 360);
        CHECK(p.missing.size() == 3);
        CHECK(Has(p.missing, "no_such_image.png"));
        CHECK(Has(p.missing, "no_such_raw.png"));
        CHECK(Has(p.missing, "no_such_icon.png"));
        for (std::size_t i = 0; i < p.missing.size(); ++i)
            if (p.missing[i] == "no_such_image.png") {
                CHECK(p.missingRects[i].x == 10 && p.missingRects[i].y == 20);
                CHECK(p.missingRects[i].w == 64 && p.missingRects[i].h == 32);
            }
        CHECK(!p.clip);   // the pass leaves the painter unclipped
    }

    // --- A toggle that is on: live mode animates in, edit mode shows it on --------
    {
        Scene s("toggle"); s.physicsEnabled = false;
        auto* tg = s.CreateGameObject("tg")->AddComponent<UIToggle>();
        tg->on = true;
        tg->checkColor = Color::FromBytes(10, 200, 30, 255);
        RecordingPainter live;
        RenderSceneUI(s, live, 640, 360);   // no Update yet: the slide-in hasn't started
        CHECK(!live.UsedColor(tg->checkColor));
        UIRenderOptions edit; edit.interactive = false;
        RecordingPainter rest;
        RenderSceneUI(s, rest, 640, 360, edit);
        CHECK(rest.UsedColor(tg->checkColor));
        tg->on = false;
        RecordingPainter off;
        RenderSceneUI(s, off, 640, 360, edit);
        CHECK(!off.UsedColor(tg->checkColor));
    }

    // --- Edit mode never acts on input (the fullscreen map's click waypoint) ------
    {
        Scene s("map"); s.physicsEnabled = false;
        auto* mm = s.CreateGameObject("map")->AddComponent<Minimap>();
        mm->fullscreen = true; mm->mapClickWaypoint = true;
        UIRenderOptions edit; edit.interactive = false;
        RecordingPainter p;
        RenderSceneUI(s, p, 640, 360, edit);
        CHECK(!mm->hasUserWaypoint);
        CHECK(p.fills > 0);   // still drawn
    }

    // --- Clip rects are never negative: scrolled-out Rating / Tabs stay clipped -----
    // (SDL_IntersectRect can leave a negative size for disjoint rects, and a negative
    // clip rect turns clipping off, so such widgets used to draw outside the view.)
    {
        URect a{0, 0, 10, 10}, b{20, 20, 5, 5}, out{1, 1, 1, 1};
        CHECK(!IntersectRect(&a, &b, &out));
        CHECK(out.w == 0 && out.h == 0);
        URect c{5, 5, 10, 10};
        CHECK(IntersectRect(&a, &c, &out));
        CHECK(out.x == 5 && out.y == 5 && out.w == 5 && out.h == 5);

        Scene s("scrolled"); s.physicsEnabled = false;
        GameObject* svGo = s.CreateGameObject("scroll");
        auto* sv = svGo->AddComponent<UIScrollView>();
        sv->position = Vec2{10, 10}; sv->size = Vec2{200, 100};
        sv->contentHeight = 1000.0f; sv->autoContent = false; sv->scroll = 600.0f;   // children far above
        GameObject* rtGo = s.CreateGameObject("rating");
        rtGo->transform->SetParent(svGo->transform, false);
        auto* rt = rtGo->AddComponent<UIRating>(); rt->position = Vec2{8, 8}; rt->value = 3.5f; rt->allowHalf = true;
        GameObject* tbGo = s.CreateGameObject("tabs");
        tbGo->transform->SetParent(svGo->transform, false);
        tbGo->AddComponent<UITabs>()->position = Vec2{8, 50};

        struct ClipLog : RecordingPainter {
            std::vector<URect> clips; bool negative = false;
            void SetClipRect(const URect* r) override {
                RecordingPainter::SetClipRect(r);
                if (!r) return;
                clips.push_back(*r);
                if (r->w < 0 || r->h < 0) negative = true;
            }
        } p;
        RenderSceneUI(s, p, 640, 360);
        CHECK(!p.negative);
        bool empty = false;
        for (const URect& r : p.clips) if (r.w == 0 || r.h == 0) empty = true;
        CHECK(empty);   // the star / tab-strip fill was clipped away, not drawn unclipped
    }

    // --- WorldUI through the caller's projector ----------------------------------
    {
        Scene s("world"); s.physicsEnabled = false;
        auto* go = s.CreateGameObject("label");
        auto* wu = go->AddComponent<WorldUI>();
        wu->text = "Hi";
        wu->background = Color::FromBytes(0, 0, 0, 200);   // a box we can count
        UIRenderOptions opt; opt.worldLabelsViaUIWorld = true;
        RecordingPainter none;
        UIWorld().active = false;
        RenderSceneUI(s, none, 640, 360, opt);   // no projector: skipped, no camera needed
        CHECK(none.fills == 0);
        UIWorld().active = true;
        UIWorld().project = [](const Vec3&, Vec2& out, float& depth) { out = Vec2{320, 180}; depth = 8.0f; return true; };
        RecordingPainter via;
        RenderSceneUI(s, via, 640, 360, opt);
        CHECK(via.fills > 0);                     // drawn where the projector put it
        UIWorld().active = false;
        UIWorld().project = nullptr;
    }

    TEST_MAIN_RESULT();
}
