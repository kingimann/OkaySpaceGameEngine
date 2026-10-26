// UI layout containers: flex layout (justify, align, grow, shrink, wrap), UIMask
// clipping (drawing and clicks), horizontal and nested scrolling, the virtualized
// UIListView (pooled rows, rebinding, rows never saved) and their scene round trip.
#include "test_framework.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::ui2d;
using URect = okay::ui2d::Rect;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// Records each fill with the clip in effect when it was drawn.
struct ClipRecorder : Painter {
    struct Fill { URect r; bool clipped; URect clip; std::uint8_t red; };
    std::vector<Fill> fills;
    bool clip = false; URect clipRect{0, 0, 0, 0};
    std::uint8_t red = 0;
    void SetDrawColor(std::uint8_t r, std::uint8_t, std::uint8_t, std::uint8_t) override { red = r; }
    void FillRect(const URect* r) override { fills.push_back({*r, clip, clipRect, red}); }
    void DrawRect(const URect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const URect* r) override { clip = r != nullptr; if (r) clipRect = *r; }
    void GetClipRect(URect* r) override { *r = clip ? clipRect : URect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return clip; }
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

GameObject* Child(Scene& s, GameObject* parent, const char* name) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

UIPanel* Panel(Scene& s, GameObject* parent, const char* name, Vec2 pos, Vec2 size, int red = 200) {
    auto* p = Child(s, parent, name)->AddComponent<UIPanel>();
    p->position = pos; p->size = size;
    p->color = Color::FromBytes((unsigned char)red, 10, 10, 255);
    p->borderWidth = 0.0f;
    return p;
}

Vec2 ScreenPos(GameObject* g) { Vec2 o, sz; GetUIScreenRect(g, 800, 600, o, sz); return o; }

} // namespace

int main() {
    RUN_SUITE("ui_layout_containers");

    // ---- Flex row: justify + align ----
    {
        Scene s("flex");
        GameObject* box = s.CreateGameObject("box");
        auto* lg = box->AddComponent<UILayoutGroup>();
        lg->direction = UILayoutGroup::Direction::FlexRow;
        lg->origin = {0, 0}; lg->boxSize = {300, 100}; lg->spacing = 0; lg->padding = 0;
        lg->justify = UILayoutGroup::Justify::SpaceBetween;
        lg->align = UILayoutGroup::Align::Center;
        UIPanel* a = Panel(s, box, "a", {0, 0}, {50, 20});
        UIPanel* b = Panel(s, box, "b", {0, 0}, {50, 20});
        UIPanel* c = Panel(s, box, "c", {0, 0}, {50, 20});
        lg->Arrange();
        CHECK(Near(a->position.x, 0) && Near(b->position.x, 125) && Near(c->position.x, 250));
        CHECK(Near(a->position.y, 40));                       // centered across the 100 px line
        // The children land inside the box on screen.
        lg->origin = {100, 50};
        CHECK(Near(ScreenPos(b->gameObject).x, 225) && Near(ScreenPos(b->gameObject).y, 90));

        // Grow: b takes all the spare room.
        b->gameObject->AddComponent<UIFlexItem>()->grow = 1.0f;
        lg->justify = UILayoutGroup::Justify::Start;
        lg->Arrange();
        CHECK(Near(b->size.x, 200) && Near(b->position.x, 50) && Near(c->position.x, 250));
        lg->Arrange();                                        // stable: basis, not the grown size
        CHECK(Near(b->size.x, 200));

        // Stretch fills the line for flex items only.
        lg->align = UILayoutGroup::Align::Stretch;
        lg->Arrange();
        CHECK(Near(b->size.y, 100) && Near(a->size.y, 20));
    }

    // ---- Flex: shrink, wrap, column ----
    {
        Scene s("flex2");
        GameObject* box = s.CreateGameObject("box");
        auto* lg = box->AddComponent<UILayoutGroup>();
        lg->direction = UILayoutGroup::Direction::FlexRow;
        lg->origin = {0, 0}; lg->boxSize = {90, 100}; lg->spacing = 0; lg->padding = 0;
        std::vector<UIPanel*> ps;
        for (int i = 0; i < 3; ++i) {
            ps.push_back(Panel(s, box, "p", {0, 0}, {60, 20}));
            ps.back()->gameObject->AddComponent<UIFlexItem>();   // shrink 1
        }
        lg->Arrange();
        CHECK(Near(ps[0]->size.x, 30) && Near(ps[2]->position.x, 60));   // 180 px squeezed into 90

        for (UIPanel* p : ps) p->gameObject->GetComponent<UIFlexItem>()->shrink = 0.0f;
        lg->wrap = true; lg->spacingY = 5; lg->boxSize = {130, 100};
        lg->Arrange();
        CHECK(Near(ps[0]->position.y, 0) && Near(ps[1]->position.x, 60));   // two per line
        CHECK(Near(ps[2]->position.x, 0) && Near(ps[2]->position.y, 25));   // third wraps
        CHECK(Near(lg->ContentSize(), 45));

        lg->direction = UILayoutGroup::Direction::FlexColumn;
        lg->wrap = false; lg->spacing = 10;
        lg->Arrange();
        CHECK(Near(ps[1]->position.y, 30) && Near(ps[2]->position.y, 60) && Near(ps[2]->position.x, 0));
    }

    // ---- UIMask: drawing and clicks are clipped ----
    {
        Scene s("mask");
        GameObject* cv = s.CreateGameObject("canvas");
        cv->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
        UIPanel* m = Panel(s, cv, "mask", {10, 10}, {100, 50}, 50);
        m->gameObject->AddComponent<UIMask>();
        UIPanel* inner = Panel(s, m->gameObject, "inner", {0, 0}, {300, 300}, 150);
        ClipRecorder rec;
        RenderSceneUI(s, rec, 800, 600);
        bool sawInner = false, sawMask = false;
        for (const auto& f : rec.fills) {
            if (f.red == 150) { sawInner = true; CHECK(f.clipped && f.clip.x == 10 && f.clip.y == 10 && f.clip.w == 100 && f.clip.h == 50); }
            if (f.red == 50) { sawMask = true; CHECK(!f.clipped); }
        }
        CHECK(sawInner && sawMask);
        CHECK(UIClipContains(inner->gameObject, {50, 30}, 800, 600));
        CHECK(!UIClipContains(inner->gameObject, {200, 200}, 800, 600));   // visible rect only
        // Hidden graphic: the mask still clips but doesn't draw itself.
        m->gameObject->GetComponent<UIMask>()->showGraphic = false;
        ClipRecorder rec2;
        RenderSceneUI(s, rec2, 800, 600);
        bool maskDrawn = false;
        for (const auto& f : rec2.fills) if (f.red == 50) maskDrawn = true;
        CHECK(!maskDrawn);
    }

    // ---- Horizontal scrolling and nested offsets ----
    {
        Scene s("scroll");
        GameObject* svGo = s.CreateGameObject("sv");
        auto* sv = svGo->AddComponent<UIScrollView>();
        sv->position = {0, 0}; sv->size = {100, 100};
        sv->axis = UIScrollView::Axis::Horizontal;
        UIPanel* wide = Panel(s, svGo, "wide", {50, 0}, {250, 40});
        UpdateUILayouts(s);
        CHECK(Near(sv->contentWidth, 300) && Near(sv->ScrollMaxX(), 200));
        sv->SetScrollX(30);
        CHECK(Near(ScreenPos(wide->gameObject).x, 20));
        CHECK(Near(ScreenPos(wide->gameObject).y, 0));        // vertical scroll ignored on this axis
        sv->scroll = 15;
        CHECK(Near(ScreenPos(wide->gameObject).y, 0));

        // A widget inside a panel inside a scroll view moves by the scroll once.
        sv->axis = UIScrollView::Axis::Vertical;
        sv->scrollX = 0; sv->contentHeight = 1000; sv->SetScroll(10);
        UIPanel* holder = Panel(s, svGo, "holder", {0, 100}, {80, 80});
        UIPanel* leaf = Panel(s, holder->gameObject, "leaf", {5, 5}, {10, 10});
        CHECK(Near(ScreenPos(holder->gameObject).y, 90));
        CHECK(Near(ScreenPos(leaf->gameObject).y, 95));
        CHECK(Near(UIScrollTotal(leaf->gameObject).y, 10));
    }

    // ---- UIListView: virtualized rows ----
    {
        Scene s("list");
        GameObject* lgo = s.CreateGameObject("list");
        auto* lv = lgo->AddComponent<UIListView>();
        lv->position = {0, 0}; lv->size = {200, 100}; lv->itemHeight = 20; lv->spacing = 0;
        for (int i = 0; i < 1000; ++i) lv->items.push_back("item " + std::to_string(i));
        GameObject* tpl = Child(s, lgo, "Row");
        tpl->AddComponent<UIButton>()->size = {200, 20};
        std::vector<int> bound;
        lv->onBind = [&](GameObject*, int i) { bound.push_back(i); };
        UpdateUILayouts(s);
        CHECK(lv->Rows().size() == 6);                       // 100 / 20 + 1, not 1000
        CHECK(!tpl->active);
        CHECK(bound.size() == 6);
        bool has0 = false, has5 = false;
        for (GameObject* r : lv->Rows()) {
            CHECK(r->transient);
            const std::string& lbl = r->GetComponent<UIButton>()->label;
            if (lbl == "item 0") has0 = true;
            if (lbl == "item 5") has5 = true;
        }
        CHECK(has0 && has5);
        // Steady frames rebind nothing.
        bound.clear();
        UpdateUILayouts(s);
        CHECK(bound.empty());
        // Scrolling by two rows rebinds only the two rows that scrolled off.
        lv->SetScroll(40);
        UpdateUILayouts(s);
        CHECK(bound.size() == 2);
        bool has7 = false;
        for (GameObject* r : lv->Rows()) {
            if (r->GetComponent<UIButton>()->label == "item 7") {
                has7 = true;
                Vec2 o = ScreenPos(r);
                CHECK(Near(o.y, 7 * 20 - 40));
            }
        }
        CHECK(has7);
        lv->ScrollTo(999);
        CHECK(Near(lv->scroll, lv->ScrollMax()));

        // Saved without its pool rows; loads back with its template and items.
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("uilist ") != std::string::npos);
        CHECK(text.find("Row row") == std::string::npos);
        Scene b("list2");
        CHECK(SceneSerializer::Deserialize(b, text));
        UIListView* lv2 = nullptr;
        for (const auto& up : b.Objects()) if (auto* l = up->GetComponent<UIListView>()) lv2 = l;
        CHECK(lv2 != nullptr);
        if (lv2) {
            CHECK(lv2->items.size() == 1000 && lv2->items[999] == "item 999");
            CHECK(Near(lv2->itemHeight, 20) && lv2->Template() != nullptr);
            UpdateUILayouts(b);
            CHECK(lv2->Rows().size() == 6);
        }
    }

    // ---- Round trip of the flex, mask and scroll-axis fields ----
    {
        Scene s("rt");
        GameObject* g = s.CreateGameObject("box");
        auto* lg = g->AddComponent<UILayoutGroup>();
        lg->direction = UILayoutGroup::Direction::FlexColumn; lg->boxSize = {321, 123};
        lg->justify = UILayoutGroup::Justify::SpaceEvenly; lg->align = UILayoutGroup::Align::End; lg->wrap = true;
        GameObject* c = Child(s, g, "item");
        c->AddComponent<UIPanel>();
        auto* fi = c->AddComponent<UIFlexItem>();
        fi->grow = 2.5f; fi->shrink = 0.0f; fi->baseSize = {77, 33}; fi->alignSelf = UIFlexItem::Align::Center;
        c->AddComponent<UIMask>()->showGraphic = false;
        auto* sv = s.CreateGameObject("sv")->AddComponent<UIScrollView>();
        sv->axis = UIScrollView::Axis::Both; sv->contentWidth = 555; sv->autoContent = false;
        Scene b("rt2");
        CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(s)));
        UILayoutGroup* lg2 = nullptr; UIFlexItem* fi2 = nullptr; UIMask* mk2 = nullptr; UIScrollView* sv2 = nullptr;
        for (const auto& up : b.Objects()) {
            if (auto* x = up->GetComponent<UILayoutGroup>()) lg2 = x;
            if (auto* x = up->GetComponent<UIFlexItem>()) fi2 = x;
            if (auto* x = up->GetComponent<UIMask>()) mk2 = x;
            if (auto* x = up->GetComponent<UIScrollView>()) sv2 = x;
        }
        CHECK(lg2 && fi2 && mk2 && sv2);
        if (lg2) CHECK(lg2->direction == UILayoutGroup::Direction::FlexColumn && Near(lg2->boxSize.x, 321) &&
                       lg2->justify == UILayoutGroup::Justify::SpaceEvenly && lg2->align == UILayoutGroup::Align::End && lg2->wrap);
        if (fi2) CHECK(Near(fi2->grow, 2.5f) && Near(fi2->shrink, 0) && Near(fi2->baseSize.x, 77) && Near(fi2->baseSize.y, 33) && fi2->alignSelf == UIFlexItem::Align::Center);
        if (mk2) CHECK(!mk2->showGraphic);
        if (sv2) CHECK(sv2->axis == UIScrollView::Axis::Both && Near(sv2->contentWidth, 555) && !sv2->autoContent);
        // An old-format scroll view line (no axis fields) still loads as vertical.
        Scene c2("old");
        CHECK(SceneSerializer::Deserialize(c2, "okayscene 2\nname \"old\"\ngameobject 0 \"sv\"\n  active 1\n  parent -1\n"
                                          "  uiscroll 1 2 300 200 0 400 0.1 0.1 0.1 1\nend\n"));
        UIScrollView* old = nullptr;
        for (const auto& up : c2.Objects()) if (auto* x = up->GetComponent<UIScrollView>()) old = x;
        CHECK(old && old->axis == UIScrollView::Axis::Vertical && old->autoContent && Near(old->size.x, 300));
    }

    TEST_MAIN_RESULT();
}
