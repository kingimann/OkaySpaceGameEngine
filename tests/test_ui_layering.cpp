#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>
#include <vector>

using namespace okay;

// Build one draw item per UI object, keyed by its canonical default layer — the
// same contract the player and editor use to drive the single UI draw pass.
static std::vector<UIDrawItem> ItemsFor(Scene& s) {
    std::vector<UIDrawItem> items;
    const auto& objs = s.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i) {
        int layer = UIDefaultLayer(objs[i].get());
        if (layer >= 0) items.push_back(UIDrawItem{i, layer, layer});
    }
    return SortUIDrawItems(objs, std::move(items));
}

// Position of a named object within a sorted item list (-1 if absent).
static int RankOf(Scene& s, const std::vector<UIDrawItem>& v, const std::string& name) {
    for (int k = 0; k < (int)v.size(); ++k)
        if (s.Objects()[v[k].index]->name == name) return k;
    return -1;
}

int main() {
    RUN_SUITE("ui_layering");

    // --- UIDefaultLayer: the canonical type scale ----------------------
    {
        Scene s("dl"); s.physicsEnabled = false;
        GameObject* im = s.CreateGameObject("im"); im->AddComponent<UIImage>();
        GameObject* pn = s.CreateGameObject("pn"); pn->AddComponent<UIPanel>();
        GameObject* bt = s.CreateGameObject("bt"); bt->AddComponent<UIButton>();
        GameObject* tx = s.CreateGameObject("tx"); { auto* t = tx->AddComponent<TextRenderer>(); t->screenSpace = true; }
        GameObject* none = s.CreateGameObject("none");
        CHECK(UIDefaultLayer(im) == 1);
        CHECK(UIDefaultLayer(pn) == 2);
        CHECK(UIDefaultLayer(bt) == 12);
        CHECK(UIDefaultLayer(tx) == 13);
        CHECK(UIDefaultLayer(none) == -1);
        // Non-screen-space text is world text, not a UI layer.
        GameObject* wt = s.CreateGameObject("wt"); wt->AddComponent<TextRenderer>();  // screenSpace defaults false
        CHECK(UIDefaultLayer(wt) == -1);
    }

    // --- Default order reproduces the type-layer order ------------------
    {
        Scene s("ord"); s.physicsEnabled = false;
        GameObject* cv = s.CreateGameObject("cv"); cv->AddComponent<Canvas>();
        GameObject* pn = s.CreateGameObject("pn"); pn->AddComponent<UIPanel>(); pn->transform->SetParent(cv->transform, false);
        GameObject* im = s.CreateGameObject("im"); im->AddComponent<UIImage>(); im->transform->SetParent(cv->transform, false);
        GameObject* tx = s.CreateGameObject("tx"); { auto* t = tx->AddComponent<TextRenderer>(); t->screenSpace = true; } tx->transform->SetParent(cv->transform, false);
        auto v = ItemsFor(s);
        // image(1) < panel(2) < text(13) regardless of authoring order.
        CHECK(RankOf(s, v, "im") < RankOf(s, v, "pn"));
        CHECK(RankOf(s, v, "pn") < RankOf(s, v, "tx"));
    }

    // --- uiDrawOrder override layers across types ----------------------
    {
        Scene s("ovr"); s.physicsEnabled = false;
        GameObject* cv = s.CreateGameObject("cv"); cv->AddComponent<Canvas>();
        GameObject* im = s.CreateGameObject("im"); im->AddComponent<UIImage>(); im->transform->SetParent(cv->transform, false);
        GameObject* pn = s.CreateGameObject("pn"); pn->AddComponent<UIPanel>(); pn->transform->SetParent(cv->transform, false);
        // Default: image below panel.
        CHECK(RankOf(s, ItemsFor(s), "im") < RankOf(s, ItemsFor(s), "pn"));
        // Lift the image above the panel (override 3 > panel layer 2).
        im->uiDrawOrder = 3;
        CHECK(RankOf(s, ItemsFor(s), "im") > RankOf(s, ItemsFor(s), "pn"));
        // Send the panel behind everything with a negative override.
        im->uiDrawOrder = 0; pn->uiDrawOrder = -5;
        auto v = ItemsFor(s);
        CHECK(RankOf(s, v, "pn") < RankOf(s, v, "im"));
    }

    // --- Canvas sortOrder dominates the per-object override ------------
    {
        Scene s("cs"); s.physicsEnabled = false;
        GameObject* lo = s.CreateGameObject("lo"); lo->AddComponent<Canvas>()->sortOrder = 0;
        GameObject* hi = s.CreateGameObject("hi"); hi->AddComponent<Canvas>()->sortOrder = 10;
        GameObject* a = s.CreateGameObject("a"); a->AddComponent<UIImage>(); a->transform->SetParent(lo->transform, false);
        GameObject* b = s.CreateGameObject("b"); b->AddComponent<UIImage>(); b->transform->SetParent(hi->transform, false);
        // Even a huge override on the low-canvas widget can't beat a higher Canvas sortOrder.
        a->uiDrawOrder = 9999;
        auto v = ItemsFor(s);
        CHECK(RankOf(s, v, "a") < RankOf(s, v, "b"));
    }

    // --- Hierarchy: child draws above parent; siblings keep order -----
    {
        Scene s("hier"); s.physicsEnabled = false;
        GameObject* cv = s.CreateGameObject("cv"); cv->AddComponent<Canvas>();
        GameObject* p = s.CreateGameObject("p"); p->AddComponent<UIPanel>(); p->transform->SetParent(cv->transform, false);
        GameObject* c = s.CreateGameObject("c"); c->AddComponent<UIPanel>(); c->transform->SetParent(p->transform, false);
        GameObject* q = s.CreateGameObject("q"); q->AddComponent<UIPanel>(); q->transform->SetParent(cv->transform, false);
        auto v = ItemsFor(s);
        CHECK(RankOf(s, v, "p") < RankOf(s, v, "c"));   // child over parent
        CHECK(RankOf(s, v, "p") < RankOf(s, v, "q"));   // earlier sibling first
    }

    // --- Bring to Front / Send to Back reorder siblings ---------------
    {
        Scene s("front"); s.physicsEnabled = false;
        GameObject* cv = s.CreateGameObject("cv"); cv->AddComponent<Canvas>();
        GameObject* p1 = s.CreateGameObject("p1"); p1->AddComponent<UIPanel>(); p1->transform->SetParent(cv->transform, false);
        GameObject* c1 = s.CreateGameObject("c1"); c1->AddComponent<UIPanel>(); c1->transform->SetParent(p1->transform, false);
        GameObject* p2 = s.CreateGameObject("p2"); p2->AddComponent<UIPanel>(); p2->transform->SetParent(cv->transform, false);
        CHECK(RankOf(s, ItemsFor(s), "p1") < RankOf(s, ItemsFor(s), "p2"));   // initial
        s.MoveToFront(p1);                                                     // p1 -> last sibling
        {
            auto v = ItemsFor(s);
            CHECK(RankOf(s, v, "p1") > RankOf(s, v, "p2"));   // now on top of p2
            CHECK(RankOf(s, v, "c1") > RankOf(s, v, "p1"));   // child still follows its parent
        }
        s.MoveToBack(p1);                                                      // p1 -> first sibling
        CHECK(RankOf(s, ItemsFor(s), "p1") < RankOf(s, ItemsFor(s), "p2"));
    }

    // --- uiDrawOrder survives a scene save/load -----------------------
    {
        Scene s("ser"); s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("a"); a->AddComponent<UIPanel>(); a->uiDrawOrder = 7;
        GameObject* b = s.CreateGameObject("b"); b->AddComponent<UIPanel>();   // default 0
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("ser2"); std::string err;
        CHECK(SceneSerializer::Deserialize(s2, txt, &err));
        int ra = -1, rb = -1;
        for (const auto& g : s2.Objects()) {
            if (g->name == "a") ra = g->uiDrawOrder;
            if (g->name == "b") rb = g->uiDrawOrder;
        }
        CHECK(ra == 7);
        CHECK(rb == 0);
    }

    TEST_MAIN_RESULT();
}
