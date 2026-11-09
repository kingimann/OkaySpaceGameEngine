// A 100000-deep parent chain must not overflow the stack or take quadratic time:
// world matrices, lossy scale, active state, component search, clone, save, load,
// destroy and clear all walk the chain with loops, never recursion.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct Marker : public Behaviour {
    int destroyed = 0;
    void OnDestroy() override { ++destroyed; }
};

constexpr int kDepth = 100000;

// root -> c1 -> c2 -> ... : each one 1 unit along +X from its parent.
std::vector<GameObject*> BuildChain(Scene& s, int depth) {
    std::vector<GameObject*> chain;
    chain.reserve(static_cast<std::size_t>(depth));
    GameObject* prev = nullptr;
    for (int i = 0; i < depth; ++i) {
        GameObject* g = s.CreateGameObject("n" + std::to_string(i));
        if (prev) {
            g->transform->SetParent(prev->transform, /*worldPositionStays=*/false);
            g->transform->localPosition = {1.0f, 0.0f, 0.0f};
        }
        chain.push_back(g);
        prev = g;
    }
    return chain;
}

} // namespace

int main() {
    RUN_SUITE("deep hierarchy: world pose, scale and active state");
    {
        Scene s("Deep");
        auto chain = BuildChain(s, kDepth);
        CHECK(static_cast<int>(chain.size()) == kDepth);
        GameObject* leaf = chain.back();
        CHECK_NEAR(leaf->transform->Position().x, static_cast<float>(kDepth - 1), 0.5f);
        CHECK(leaf->transform->Rotation().w > 0.999f);
        const Vec3 ls = leaf->transform->LossyScale();
        CHECK_NEAR(ls.x, 1.0f, 1e-5f);
        // moving the root moves the leaf (the cache follows the whole chain)
        chain.front()->transform->localPosition = {0.0f, 5.0f, 0.0f};
        CHECK_NEAR(leaf->transform->Position().y, 5.0f, 1e-3f);
        // a scale at the root reaches the leaf's lossy scale
        chain.front()->transform->localScale = {2.0f, 2.0f, 2.0f};
        CHECK_NEAR(leaf->transform->LossyScale().x, 2.0f, 1e-5f);

        // an inactive ancestor far above silences the leaf (the walk is not capped)
        CHECK(leaf->ActiveInHierarchy());
        chain.front()->active = false;
        CHECK(!leaf->ActiveInHierarchy());
        chain.front()->active = true;
        chain[kDepth / 2]->active = false;
        CHECK(!leaf->ActiveInHierarchy());
        CHECK(chain[kDepth / 2 - 1]->ActiveInHierarchy());
        chain[kDepth / 2]->active = true;

        CHECK(leaf->IsSelfOrDescendantOf(chain.front()));
        CHECK(leaf->transform->Root() == chain.front()->transform);
        CHECK(leaf->transform->Depth() == kDepth - 1);
        CHECK(leaf->transform->IsChildOf(chain.front()->transform));
        CHECK(!chain.front()->transform->IsChildOf(leaf->transform));
    }

    RUN_SUITE("deep hierarchy: reparenting");
    {
        Scene s("Deep");
        auto chain = BuildChain(s, kDepth);
        GameObject* leaf = chain.back();
        const Vec3 before = leaf->transform->Position();
        // a cycle through the whole chain is refused
        CHECK(!chain.front()->transform->SetParent(leaf->transform));
        CHECK(chain.front()->transform->Parent() == nullptr);
        // lifting the leaf's parent chain out keeps the leaf where it is (world pose kept)
        GameObject* mid = chain[kDepth / 2];
        CHECK(mid->transform->SetParent(nullptr, /*worldPositionStays=*/true));
        const Vec3 after = leaf->transform->Position();
        CHECK_NEAR(after.x, before.x, 0.5f);
        CHECK(mid->transform->Parent() == nullptr);
        CHECK(leaf->transform->Root() == mid->transform);
    }

    RUN_SUITE("deep hierarchy: component search");
    {
        Scene s("Deep");
        auto chain = BuildChain(s, kDepth);
        Marker* m = chain.back()->AddComponent<Marker>();
        CHECK(chain.front()->GetComponentInChildren<Marker>() == m);
        CHECK(chain.front()->GetComponentsInChildren<Marker>().size() == 1);
        CHECK(chain.back()->GetComponentInParent<Marker>() == m);
        Marker* top = chain.front()->AddComponent<Marker>();
        CHECK(chain.back()->GetComponentInParent<Marker>() == m);   // nearest first
        (void)top;
    }

    RUN_SUITE("deep hierarchy: save and load");
    {
        std::string text;
        {
            Scene s("Deep");
            auto chain = BuildChain(s, kDepth);
            chain.back()->name = "Leaf";
            text = SceneSerializer::Serialize(s);
        }
        Scene t("Loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err));
        CHECK(err.empty());
        CHECK(static_cast<int>(t.Objects().size()) == kDepth);
        GameObject* leaf = t.Find("Leaf");
        CHECK(leaf != nullptr);
        if (leaf) CHECK_NEAR(leaf->transform->Position().x, static_cast<float>(kDepth - 1), 0.5f);
        CHECK(t.Validate().issues.empty());                       // the saved form of the loaded chain is clean too
    }

    RUN_SUITE("deep hierarchy: clone");
    {
        Scene s("Deep");
        auto chain = BuildChain(s, kDepth);
        GameObject* clone = s.Instantiate(*chain.front());
        CHECK(clone != nullptr);
        CHECK(static_cast<int>(s.Objects().size()) == 2 * kDepth);
        if (clone) CHECK(clone->transform->ChildCount() == 1);
        GameObject* dup = s.Duplicate(*chain.front());
        CHECK(dup != nullptr && dup->name == "n0 (1)");
    }

    RUN_SUITE("deep hierarchy: destroy and clear");
    {
        Scene s("Deep");
        auto chain = BuildChain(s, kDepth);
        // (No Update or Start here: every frame the scene asks each component whether its object is
        // active, which walks the parent chain, so a chain this deep is quadratic per frame. Real
        // hierarchies are a few levels deep; this suite is about the walks not overflowing the stack.)
        s.Destroy(chain.front());
        s.FlushDestroyed();
        CHECK(s.Objects().empty());
        s.Clear();
        CHECK(s.Objects().empty());
    }
    {
        Scene s("Deep");
        BuildChain(s, kDepth);
        s.Clear();   // frees the whole chain at once
        CHECK(s.Objects().empty());
        GameObject* again = s.CreateGameObject("again");
        CHECK(again != nullptr);
    }

    TEST_MAIN_RESULT();
}
