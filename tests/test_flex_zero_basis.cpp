// A UIFlexItem whose widget is 0 wide (or 0 tall) has a flex basis of 0: the grow
// share is handed out from nothing, so items of widths 0, 0 and 100 that all grow
// by 1 in a 400 px row end up 100, 100 and 200. The layout writes the grown size
// back into the widget; this checks that later Arrange() calls (and a save and load
// in between) do not take that written size as the basis (issue #888 finding 43).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

GameObject* Child(Scene& s, GameObject* parent, const char* name) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

UIPanel* FlexPanel(Scene& s, GameObject* box, const char* name, Vec2 size, float grow) {
    auto* p = Child(s, box, name)->AddComponent<UIPanel>();
    p->position = {0, 0}; p->size = size; p->borderWidth = 0.0f;
    p->gameObject->AddComponent<UIFlexItem>()->grow = grow;
    return p;
}

// Builds the harness from the report: a Flex Row (or Column) box 400 long and 100
// across, spacing 0, three panels 0, 0 and 100 long that all grow by 1.
UILayoutGroup* Harness(Scene& s, bool row, std::vector<UIPanel*>& ps) {
    GameObject* box = s.CreateGameObject("box");
    auto* lg = box->AddComponent<UILayoutGroup>();
    lg->direction = row ? UILayoutGroup::Direction::FlexRow : UILayoutGroup::Direction::FlexColumn;
    lg->origin = {0, 0}; lg->boxSize = row ? Vec2{400, 100} : Vec2{100, 400};
    lg->spacing = 0; lg->padding = 0;
    const float lens[3] = {0, 0, 100};
    const char* names[3] = {"a", "b", "c"};
    for (int i = 0; i < 3; ++i)
        ps.push_back(FlexPanel(s, box, names[i], row ? Vec2{lens[i], 20} : Vec2{20, lens[i]}, 1.0f));
    return lg;
}

float Main(const UIPanel* p, bool row) { return row ? p->size.x : p->size.y; }

bool Is100_100_200(const std::vector<UIPanel*>& ps, bool row, const char* what, int pass) {
    std::printf("  %s arrange %d: %.1f %.1f %.1f\n", what, pass, Main(ps[0], row), Main(ps[1], row), Main(ps[2], row));
    return Near(Main(ps[0], row), 100) && Near(Main(ps[1], row), 100) && Near(Main(ps[2], row), 200);
}

template <class T> T* Find(Scene& s, const char* name) {
    for (const auto& up : s.Objects())
        if (up->name == name)
            if (auto* c = up->GetComponent<T>()) return c;
    return nullptr;
}

} // namespace

int main() {
    RUN_SUITE("flex_zero_basis");

    // ---- The report's harness: Flex Row, four Arrange() calls ----
    {
        Scene s("row");
        std::vector<UIPanel*> ps;
        UILayoutGroup* lg = Harness(s, true, ps);
        for (int pass = 1; pass <= 4; ++pass) {
            lg->Arrange();
            CHECK(Is100_100_200(ps, true, "row", pass));
            CHECK(Near(ps[1]->position.x, 100) && Near(ps[2]->position.x, 200));
        }
    }

    // ---- The same in a Flex Column ----
    {
        Scene s("col");
        std::vector<UIPanel*> ps;
        UILayoutGroup* lg = Harness(s, false, ps);
        for (int pass = 1; pass <= 4; ++pass) {
            lg->Arrange();
            CHECK(Is100_100_200(ps, false, "column", pass));
        }
    }

    // ---- Through UpdateUILayouts, as the editor and player do every frame ----
    {
        Scene s("frames");
        std::vector<UIPanel*> ps;
        Harness(s, true, ps);
        for (int pass = 1; pass <= 4; ++pass) {
            UpdateUILayouts(s);
            CHECK(Is100_100_200(ps, true, "frame", pass));
        }
    }

    // ---- Cross axis: Stretch writes the line's thickness into a 0-tall widget ----
    {
        Scene s("cross");
        GameObject* box = s.CreateGameObject("box");
        auto* lg = box->AddComponent<UILayoutGroup>();
        lg->direction = UILayoutGroup::Direction::FlexRow;
        lg->origin = {0, 0}; lg->boxSize = {400, 100}; lg->spacing = 0; lg->padding = 0;
        lg->align = UILayoutGroup::Align::Stretch;
        UIPanel* a = FlexPanel(s, box, "a", {100, 0}, 0.0f);
        UIPanel* b = FlexPanel(s, box, "b", {100, 30}, 0.0f);
        for (int pass = 1; pass <= 3; ++pass) {
            lg->Arrange();
            CHECK(Near(a->size.y, 100) && Near(b->size.y, 100));   // stretched across the box
        }
        // Back to Start: each keeps its own (base) height, 0 and 30, not the stretched 100.
        lg->align = UILayoutGroup::Align::Start;
        lg->Arrange();
        std::printf("  cross after Stretch -> Start: heights %.1f %.1f\n", a->size.y, b->size.y);
        CHECK(Near(a->size.y, 0) && Near(b->size.y, 30));
        // Wrapped, the line is as thick as its thickest base (30), not the old 100.
        lg->align = UILayoutGroup::Align::Stretch; lg->wrap = true;
        lg->Arrange();
        lg->Arrange();
        std::printf("  cross wrapped: heights %.1f %.1f, content %.1f\n", a->size.y, b->size.y, lg->ContentSize());
        CHECK(Near(a->size.y, 30) && Near(b->size.y, 30) && Near(lg->ContentSize(), 30));
    }

    // ---- A non-zero basis behaves as before ----
    {
        Scene s("nonzero");
        GameObject* box = s.CreateGameObject("box");
        auto* lg = box->AddComponent<UILayoutGroup>();
        lg->direction = UILayoutGroup::Direction::FlexRow;
        lg->origin = {0, 0}; lg->boxSize = {400, 100}; lg->spacing = 0; lg->padding = 0;
        UIPanel* a = FlexPanel(s, box, "a", {50, 20}, 1.0f);    // basis taken from the widget: 50
        UIPanel* b = FlexPanel(s, box, "b", {999, 20}, 1.0f);   // authored basis wins over the widget
        b->gameObject->GetComponent<UIFlexItem>()->baseSize = {150, 20};
        for (int pass = 1; pass <= 3; ++pass) {
            lg->Arrange();
            // 400 - 50 - 150 = 200 spare, 100 each.
            CHECK(Near(a->size.x, 150) && Near(b->size.x, 250) && Near(b->position.x, 150));
        }
        CHECK(Near(a->gameObject->GetComponent<UIFlexItem>()->baseSize.x, 50));
        CHECK(Near(b->gameObject->GetComponent<UIFlexItem>()->baseSize.x, 150));
        // Setting the basis back to 0 still means "take the widget's size", once.
        a->gameObject->GetComponent<UIFlexItem>()->baseSize = {0, 0};
        a->size = {10, 20};
        lg->Arrange();
        lg->Arrange();
        CHECK(Near(a->gameObject->GetComponent<UIFlexItem>()->baseSize.x, 10));
        CHECK(Near(a->size.x, 130) && Near(b->size.x, 270));
    }

    // ---- Save and load between arranges (the editor saves a laid-out scene) ----
    {
        Scene s("rt");
        std::vector<UIPanel*> ps;
        UILayoutGroup* lg = Harness(s, true, ps);
        lg->Arrange();
        CHECK(Is100_100_200(ps, true, "saved", 1));
        const std::string text = SceneSerializer::Serialize(s);
        Scene b("rt2");
        CHECK(SceneSerializer::Deserialize(b, text));
        std::vector<UIPanel*> qs = {Find<UIPanel>(b, "a"), Find<UIPanel>(b, "b"), Find<UIPanel>(b, "c")};
        UILayoutGroup* lg2 = Find<UILayoutGroup>(b, "box");
        CHECK(lg2 && qs[0] && qs[1] && qs[2]);
        if (lg2 && qs[0] && qs[1] && qs[2]) {
            for (int pass = 1; pass <= 3; ++pass) {
                lg2->Arrange();
                CHECK(Is100_100_200(qs, true, "loaded", pass));
            }
            // A second round trip keeps it.
            Scene c("rt3");
            CHECK(SceneSerializer::Deserialize(c, SceneSerializer::Serialize(b)));
            std::vector<UIPanel*> rs = {Find<UIPanel>(c, "a"), Find<UIPanel>(c, "b"), Find<UIPanel>(c, "c")};
            UILayoutGroup* lg3 = Find<UILayoutGroup>(c, "box");
            CHECK(lg3 && rs[0] && rs[1] && rs[2]);
            if (lg3 && rs[0] && rs[1] && rs[2]) {
                lg3->Arrange();
                lg3->Arrange();
                CHECK(Is100_100_200(rs, true, "reloaded", 2));
            }
        }
        // The record still has the same five fields.
        const auto at = text.find("uiflexitem ");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            const std::string line = text.substr(at, text.find('\n', at) - at);
            int fields = 0;
            for (std::size_t i = 0; i < line.size(); ++i)
                if (line[i] != ' ' && (i == 0 || line[i - 1] == ' ')) ++fields;
            std::printf("  saved record: %s\n", line.c_str());
            CHECK(fields == 6);   // the tag and grow, shrink, base x, base y, align self
        }
        // A scene saved before any layout ran (basis 0, widget 0) loads and lays out the same.
        Scene fresh("fresh");
        std::vector<UIPanel*> fs;
        Harness(fresh, true, fs);
        Scene d("rt4");
        CHECK(SceneSerializer::Deserialize(d, SceneSerializer::Serialize(fresh)));
        std::vector<UIPanel*> ds = {Find<UIPanel>(d, "a"), Find<UIPanel>(d, "b"), Find<UIPanel>(d, "c")};
        UILayoutGroup* lg4 = Find<UILayoutGroup>(d, "box");
        if (lg4 && ds[0] && ds[1] && ds[2]) {
            lg4->Arrange();
            lg4->Arrange();
            CHECK(Is100_100_200(ds, true, "fresh", 2));
        } else {
            CHECK(false);
        }
    }

    TEST_MAIN_RESULT();
}
