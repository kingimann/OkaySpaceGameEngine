// Scene graph, fourth pass: callbacks that move, destroy and spawn while the scene iterates, tag and layer
// lookups after edits and with 10000 entries, Lifetime with huge, negative and NaN times, a NaN frame time,
// and the small additions a game author asked for: GetComponentsInParent, DestroyChildren, Scene::Stats.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

struct Tick : public Behaviour {
    int updates = 0;
    int* external = nullptr;   // a counter that outlives the component (it is freed with a destroyed object)
    void Update(float) override { ++updates; if (external) ++*external; }
};
struct A1 : public Behaviour {};
struct B1 : public Behaviour {};

// Moves other objects around from Update.
struct Reparenter : public Behaviour {
    GameObject* mover = nullptr;
    GameObject* newParent = nullptr;
    bool* result = nullptr;
    void Update(float) override {
        if (!mover) return;
        const bool ok = mover->transform->SetParent(newParent ? newParent->transform : nullptr, true);
        if (result) *result = ok;
        mover = nullptr;
    }
};
// Destroys a target from Update (the target may sit later in the component list).
struct Killer : public Behaviour {
    GameObject* target = nullptr;
    void Update(float) override { if (target) { gameObject->scene()->Destroy(target); target = nullptr; } }
};
// Spawns a clone of `prefab` while it is being destroyed.
struct SpawnsOnDestroy : public Behaviour {
    const GameObject* prefab = nullptr;
    GameObject* spawned = nullptr;
    void OnDestroy() override {
        if (prefab && gameObject && gameObject->scene()) spawned = gameObject->scene()->Instantiate(*prefab, Vec3{1, 2, 3});
    }
};

} // namespace

int main() {
    RUN_SUITE("SetParent from another object's Update");
    {
        Scene s("T");
        GameObject* newParent = s.CreateGameObject("NewParent");
        newParent->transform->localPosition = {10.0f, 0.0f, 0.0f};
        GameObject* victim = s.CreateGameObject("Victim");
        victim->transform->localPosition = {1.0f, 2.0f, 3.0f};
        auto* t1 = victim->AddComponent<Tick>();
        auto* r = s.CreateGameObject("R")->AddComponent<Reparenter>();
        bool ok = false;
        r->mover = victim; r->newParent = newParent; r->result = &ok;
        auto* t2 = s.CreateGameObject("Later")->AddComponent<Tick>();
        s.Update(0.016f);
        CHECK(ok);
        CHECK(victim->transform->Parent() == newParent->transform);
        CHECK((victim->transform->Position() - Vec3{1.0f, 2.0f, 3.0f}).Magnitude() < 1e-4f);   // the world pose was kept
        CHECK(t1->updates == 1 && t2->updates == 1);                                            // nobody was skipped or run twice
        // a component moves ITS OWN object under a sibling that then goes away in the same frame
        GameObject* p = s.CreateGameObject("P");
        GameObject* me = s.CreateGameObject("Me");
        auto* tm = me->AddComponent<Tick>();
        auto* rr = me->AddComponent<Reparenter>();
        rr->mover = me; rr->newParent = p;
        auto* kk = s.CreateGameObject("K")->AddComponent<Killer>();
        kk->target = p;
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(p->IsDestroyed());
        CHECK(me->IsDestroyed());                         // it was under p, so it went with it
        CHECK(tm != nullptr);
        // a parent that is being destroyed refuses new children only once it is destroyed
        GameObject* dead = s.CreateGameObject("Dead");
        s.Destroy(dead);
        s.FlushDestroyed();
        GameObject* late = s.CreateGameObject("Late");
        CHECK(!late->transform->SetParent(dead->transform, true));
        CHECK(late->transform->Parent() == nullptr);
    }

    RUN_SUITE("Destroy from the Update of a sibling");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        auto* kill = a->AddComponent<Killer>();
        int bUpdates = 0;
        b->AddComponent<Tick>()->external = &bUpdates;
        kill->target = b;
        s.Update(0.016f);
        CHECK(bUpdates == 1);                             // still ran this frame, destroyed at its end
        CHECK(b->IsDestroyed());
        CHECK(s.Find("B") == nullptr);
        CHECK(s.Objects().size() == 1);
        // killing an object that is also its own sibling's parent
        GameObject* p = s.CreateGameObject("P");
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        auto* kc = s.CreateGameObject("K2")->AddComponent<Killer>();
        kc->target = p;
        int cUpdates = 0;
        c->AddComponent<Tick>()->external = &cUpdates;
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(cUpdates == 1);
        CHECK(c->IsDestroyed() && p->IsDestroyed());
    }

    RUN_SUITE("Instantiate from OnDestroy");
    {
        Scene s("T");
        GameObject* proto = s.CreateGameObject("Proto");
        proto->AddComponent<Mover>()->velocity = {1.0f, 0.0f, 0.0f};   // a component the serializer copies
        GameObject* dying = s.CreateGameObject("Dying");
        auto* sp = dying->AddComponent<SpawnsOnDestroy>();
        sp->prefab = proto;
        s.Update(0.016f);
        s.Destroy(dying);
        s.Update(0.016f);                                  // OnDestroy spawns; the clone is adopted by the scene
        CHECK(dying->IsDestroyed());
        std::vector<GameObject*> clones;
        for (const auto& o : s.Objects()) if (o->name == "Proto" && o.get() != proto) clones.push_back(o.get());
        CHECK(clones.size() == 1);                        // the prototype and one clone
        if (clones.size() == 1) {
            GameObject* clone = clones[0];
            CHECK_NEAR(clone->transform->localPosition.x, 1.0f, 1e-5f);   // not woken yet: no Update ran on it
            s.Update(1.0f);                                // the next frame wakes it and runs it
            CHECK(clone->GetComponent<Mover>() != nullptr);
            CHECK_NEAR(clone->transform->localPosition.x, 2.0f, 1e-4f);
        }
    }
    {   // the dying object's own subtree is cloned from a prototype that is itself going away
        Scene s("T");
        GameObject* proto = s.CreateGameObject("Proto");
        GameObject* dying = s.CreateGameObject("Dying");
        dying->AddComponent<SpawnsOnDestroy>()->prefab = proto;
        s.Update(0.016f);
        s.Destroy(dying);
        s.Destroy(proto);
        s.Update(0.016f);                                  // whichever is destroyed first, nothing breaks
        CHECK(dying->IsDestroyed() && proto->IsDestroyed());
    }
    {   // a destroyed object cannot be cloned
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        s.Destroy(g);
        s.FlushDestroyed();
        CHECK(s.Instantiate(*g) == nullptr);
        CHECK(s.Instantiate(*g, Vec3{1, 1, 1}) == nullptr);
        CHECK(s.Instantiate(*g, static_cast<Transform*>(nullptr)) == nullptr);
        CHECK(s.Duplicate(*g) == nullptr);
        CHECK(s.Objects().empty());
    }

    RUN_SUITE("a NaN or infinite frame time");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Body");
        g->transform->localPosition = {0.0f, 10.0f, 0.0f};
        g->AddComponent<Rigidbody3D>();
        g->AddComponent<BoxCollider3D>();
        auto* t = g->AddComponent<Tick>();
        s.Update(0.016f);
        s.Update(kNaN);
        s.Update(std::numeric_limits<float>::infinity());
        s.Update(0.016f);
        const Vec3 p = g->transform->Position();
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        CHECK(t->updates == 4);
    }

    RUN_SUITE("tags after a rename or a Replace");
    {
        Scene s("T");
        std::vector<GameObject*> objs;
        for (int i = 0; i < 6; ++i) {
            GameObject* g = s.CreateGameObject("o" + std::to_string(i));
            g->tag = (i % 2 == 0) ? "Red" : "Blue";
            objs.push_back(g);
        }
        CHECK(s.FindWithTag("Red") == objs[0]);
        CHECK(s.FindGameObjectsWithTag("Blue").size() == 3);
        objs[0]->tag = "Blue";                                   // the first Red object changes colour
        CHECK(s.FindWithTag("Red") == objs[2]);
        CHECK(s.FindWithTag("Blue") == objs[0]);
        CHECK(s.CountWithTag("Blue") == 4);
        CHECK(s.ReplaceTag("Blue", "Green") == 4);
        CHECK(s.FindWithTag("Blue") == nullptr);
        CHECK(s.CountWithTag("Green") == 4);
        CHECK(s.FindWithTag("Green") == objs[0]);
        CHECK(s.ReplaceTag("Green", "") == 4);                   // untag
        CHECK(s.FindWithTag("Green") == nullptr);
        CHECK(s.FindWithTag("") == nullptr);
        CHECK(s.ReplaceTag("", "X") == 0);
        CHECK(s.ReplaceTag("Red", "bad tag ") == 0);             // a name Tags::Accepts refuses changes nothing
        CHECK(s.CountWithTag("Red") == 2);
        // a tag edited through a std::string reference (not counted) is still found right
        std::string& raw = static_cast<std::string&>(objs[2]->tag);
        raw = "Purple";
        CHECK(s.FindWithTag("Red") != objs[2]);
        CHECK(s.FindWithTag("Purple") == objs[2] || s.FindWithTag("Purple") == nullptr);
        objs[2]->tag = "Purple";
        CHECK(s.FindWithTag("Purple") == objs[2]);
        // destroying the first match moves the lookup to the next one
        s.Destroy(objs[4]);
        s.FlushDestroyed();
        CHECK(s.FindWithTag("Red") != objs[4]);
    }

    RUN_SUITE("10000 objects: tags, layers, names");
    {
        constexpr int kN = 10000;
        Scene s("Many");
        std::vector<GameObject*> objs;
        objs.reserve(kN);
        for (int i = 0; i < kN; ++i) {
            GameObject* g = s.CreateGameObject("n" + std::to_string(i % 500));
            g->tag = "T" + std::to_string(i % 100);
            g->layer = i % 32;
            objs.push_back(g);
        }
        CHECK(s.CountWithTag("T7") == kN / 100);
        CHECK(s.FindGameObjectsWithTag("T99").size() == static_cast<std::size_t>(kN / 100));
        CHECK(s.FindWithTag("T0") == objs[0]);
        CHECK(s.CountOnLayer(5) == kN / 32 + (5 < kN % 32 ? 1 : 0));
        CHECK(s.FindGameObjectsOnLayer(31).size() == static_cast<std::size_t>(kN / 32));
        CHECK(s.FindGameObjectsWithName("n3").size() == static_cast<std::size_t>(kN / 500));
        // rename every T3 to a new tag and look up in between
        CHECK(s.ReplaceTag("T3", "Three") == kN / 100);
        CHECK(s.CountWithTag("T3") == 0);
        CHECK(s.CountWithTag("Three") == kN / 100);
        CHECK(s.UnlistedTags().size() == 100);                    // every tag here is a free string, none is in the project list
        // one lookup per object, each after a tag change
        int found = 0;
        for (int i = 0; i < 200; ++i) {
            objs[static_cast<std::size_t>(i)]->tag = "Fresh";
            if (s.FindWithTag("Fresh") == objs[0]) ++found;
        }
        CHECK(found == 200);
        {   // 10000 different tags: the unlisted-tag report is exact, sorted, and does not scan its own result per object
            Scene d("Distinct");
            for (int i = 0; i < kN; ++i) d.CreateGameObject("o")->tag = "tag" + std::to_string(i);
            const std::vector<std::string> unlisted = d.UnlistedTags();
            CHECK(static_cast<int>(unlisted.size()) == kN);
            CHECK(std::is_sorted(unlisted.begin(), unlisted.end()));
            CHECK(d.ReplaceTag("tag77", "seventy-seven") == 1);
            CHECK(d.FindWithTag("seventy-seven") != nullptr && d.FindWithTag("tag77") == nullptr);
        }
        // the project lists with 10000 lines: the cap holds and nothing loops
        ProjectLists::Reset();
        std::string text;
        for (int i = 0; i < kN; ++i) text += "tag=Tag" + std::to_string(i) + "\n";
        for (int i = 0; i < kN; ++i) text += "layer_name_" + std::to_string(i % 40) + "=Name" + std::to_string(i) + "\n";
        const int taken = ProjectLists::Apply(text);
        CHECK(static_cast<int>(Tags::Custom().size()) == Tags::kMaxCustom);
        CHECK(taken >= Tags::kMaxCustom);
        CHECK(Layers::IsValid(31));
        ProjectLists::Reset();
        CHECK(Tags::Custom().empty());
    }

    RUN_SUITE("Lifetime with huge, negative and NaN times");
    {
        Scene s("T");
        // NaN seconds never ends; negative ends on the first frame
        GameObject* nan = s.CreateGameObject("Nan");
        nan->AddComponent<Lifetime>()->seconds = kNaN;
        GameObject* neg = s.CreateGameObject("Neg");
        neg->AddComponent<Lifetime>()->seconds = -1e30f;
        GameObject* big = s.CreateGameObject("Big");
        auto* lt = big->AddComponent<Lifetime>();
        lt->seconds = 1.0e6f;
        lt->SetElapsed(999999.0f);                              // one second to go, past where a float stops counting small steps
        s.Update(0.0f);
        for (int i = 0; i < 59; ++i) s.Update(1.0f / 60.0f);
        CHECK(!big->IsDestroyed());
        CHECK(lt->Remaining() > 0.0f);
        for (int i = 0; i < 3; ++i) s.Update(1.0f / 60.0f);
        CHECK(big->IsDestroyed());                              // 62 steps of 1/60 s: the second ran out
        CHECK(neg->IsDestroyed());
        CHECK(!nan->IsDestroyed());
        for (int i = 0; i < 10; ++i) s.Update(0.5f);
        CHECK(!nan->IsDestroyed());
    }
    {   // the elapsed time reads back as a float and survives a big seed
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* lt = g->AddComponent<Lifetime>();
        lt->seconds = 3.0e7f;
        lt->SetElapsed(1.0e7f);
        s.Update(0.25f);
        CHECK(std::isfinite(lt->Elapsed()));
        CHECK(lt->Elapsed() >= 1.0e7f);
        lt->SetElapsed(std::numeric_limits<float>::infinity());   // ignored
        CHECK(lt->Elapsed() < 2.0e7f);
    }

    RUN_SUITE("FindByGuid with many objects");
    {
        Scene s("T");
        std::vector<GameObject*> objs;
        for (int i = 0; i < 5000; ++i) objs.push_back(s.CreateGameObject("o" + std::to_string(i)));
        int wrong = 0;
        for (GameObject* g : objs) if (s.FindByGuid(g->Guid()) != g) ++wrong;     // one lookup per object
        CHECK(wrong == 0);
        CHECK(s.FindByGuid("") == nullptr);
        CHECK(s.FindByGuid("0123456789abcdef0123456789abcdef") == nullptr);
        // an id set after the index was built is found; the old id is gone
        const std::string old = objs[10]->Guid();
        CHECK(objs[10]->SetGuid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        CHECK(s.FindByGuid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == objs[10]);
        CHECK(s.FindByGuid(old) == nullptr);
        // destroyed objects are not found; creating and reparenting keep working
        const std::string gone = objs[20]->Guid();
        s.Destroy(objs[20]);
        s.FlushDestroyed();
        CHECK(s.FindByGuid(gone) == nullptr);
        GameObject* fresh = s.CreateGameObject("fresh");
        CHECK(s.FindByGuid(fresh->Guid()) == fresh);
        fresh->transform->SetParent(objs[0]->transform, false);
        CHECK(s.FindByGuid(fresh->Guid()) == fresh);
        const std::string freshGuid = fresh->Guid();
        s.Clear();
        CHECK(s.FindByGuid(freshGuid) == nullptr);                  // the index is not left pointing at freed objects
    }

    RUN_SUITE("delayed destroy with many objects");
    {
        Scene s("T");
        std::vector<GameObject*> objs;
        for (int i = 0; i < 4000; ++i) objs.push_back(s.CreateGameObject("o" + std::to_string(i)));
        for (int i = 0; i < 4000; ++i) s.Destroy(objs[static_cast<std::size_t>(i)], 1.0f + static_cast<float>(i % 4));   // 1, 2, 3, 4 seconds
        s.Destroy(objs[1], 0.5f);                    // asking again with a sooner time keeps the sooner one
        s.Destroy(objs[0], 9.0f);                    // a later time changes nothing
        s.Update(0.6f);
        CHECK(objs[1]->IsDestroyed());
        CHECK(!objs[0]->IsDestroyed());
        s.Update(0.5f);                              // 1.1 s in: the 1-second group is gone (objects 0, 4, 8 ...)
        CHECK(objs[0]->IsDestroyed() && objs[4]->IsDestroyed());
        CHECK(!objs[2]->IsDestroyed());
        s.Update(1.0f);                              // 2.1 s: the 2-second group is gone too
        CHECK(objs[5]->IsDestroyed() && !objs[2]->IsDestroyed());
        s.Update(1.0f);                              // 3.1 s
        CHECK(objs[2]->IsDestroyed() && !objs[3]->IsDestroyed());
        s.Update(1.0f);                              // 4.1 s
        CHECK(s.Objects().empty());
    }

    RUN_SUITE("GetComponentsInParent");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        GameObject* mid = s.CreateGameObject("Mid");
        GameObject* leaf = s.CreateGameObject("Leaf");
        mid->transform->SetParent(root->transform, false);
        leaf->transform->SetParent(mid->transform, false);
        auto* r = root->AddComponent<A1>();
        auto* m1 = mid->AddComponent<A1>();
        auto* m2 = mid->AddComponent<A1>();
        auto* l = leaf->AddComponent<A1>();
        leaf->AddComponent<B1>();
        // nearest first: the object itself, then each parent; an object's own components in stored order
        const auto all = leaf->GetComponentsInParent<A1>();
        CHECK(all.size() == 4);
        if (all.size() == 4) CHECK(all[0] == l && all[1] == m1 && all[2] == m2 && all[3] == r);
        CHECK(leaf->GetComponentsInParent<B1>().size() == 1);
        CHECK(root->GetComponentsInParent<A1>().size() == 1);
        CHECK(root->GetComponentsInParent<B1>().empty());
        // an inactive object is skipped unless asked for
        mid->active = false;
        const auto skipped = leaf->GetComponentsInParent<A1>();
        CHECK(skipped.size() == 2);
        if (skipped.size() == 2) CHECK(skipped[0] == l && skipped[1] == r);
        CHECK(leaf->GetComponentsInParent<A1>(true).size() == 4);
        CHECK(leaf->GetComponentInParent<A1>(true) == l);
    }

    RUN_SUITE("Scene::DestroyChildren");
    {
        Scene s("T");
        GameObject* list = s.CreateGameObject("List");
        std::vector<GameObject*> rows;
        for (int i = 0; i < 5; ++i) {
            GameObject* row = s.CreateGameObject("Row" + std::to_string(i));
            row->transform->SetParent(list->transform, false);
            GameObject* sub = s.CreateGameObject("Sub");
            sub->transform->SetParent(row->transform, false);
            rows.push_back(row);
        }
        GameObject* other = s.CreateGameObject("Other");
        CHECK(s.Objects().size() == 12);
        CHECK(s.DestroyChildren(list) == 5);                    // direct children queued
        CHECK(list->transform->ChildCount() == 5);              // still there until the end of the frame
        s.FlushDestroyed();
        CHECK(list->transform->ChildCount() == 0);
        CHECK(s.Objects().size() == 2);                         // List and Other
        CHECK(!list->IsDestroyed() && !other->IsDestroyed());
        CHECK(s.DestroyChildren(list) == 0);
        CHECK(s.DestroyChildren(nullptr) == 0);
        s.Destroy(other);
        s.FlushDestroyed();
        CHECK(s.DestroyChildren(other) == 0);                   // a destroyed object has none
    }

    RUN_SUITE("Scene::Stats");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        GameObject* d = s.CreateGameObject("D");
        b->transform->SetParent(a->transform, false);
        c->transform->SetParent(b->transform, false);
        a->AddComponent<A1>(); a->AddComponent<A1>(); b->AddComponent<A1>();
        c->AddComponent<B1>();
        d->active = false;
        b->active = false;                                      // takes c with it
        s.Update(0.016f);
        s.Destroy(d);
        const SceneGraphStats st = s.Stats();
        CHECK(st.objects == 4);
        CHECK(st.roots == 2);                                   // A and D
        CHECK(st.maxDepth == 2);                                // A -> B -> C
        CHECK(st.activeInHierarchy == 1);                       // only A
        CHECK(st.inactive == 3);
        CHECK(st.components == 4 + 4);                          // four Transforms plus four components
        CHECK(st.pendingDestroy == 1);
        int a1 = 0, b1 = 0, tr = 0;
        for (const auto& t : st.byType) {
            if (t.type == "A1") a1 = t.count;
            if (t.type == "B1") b1 = t.count;
            if (t.type == "Transform") tr = t.count;
        }
        CHECK(a1 == 3 && b1 == 1 && tr == 4);
        // biggest count first
        CHECK(!st.byType.empty() && st.byType.front().type == "Transform");
        const std::vector<std::string> lines = st.Lines(2);
        CHECK(!lines.empty());
        CHECK(lines.front().find("Objects: 4") != std::string::npos);
        CHECK(st.ToString().find("A1") != std::string::npos);
        // an empty scene
        Scene empty("E");
        const SceneGraphStats es = empty.Stats();
        CHECK(es.objects == 0 && es.roots == 0 && es.maxDepth == 0 && es.components == 0 && es.byType.empty());
        // a deep chain does not recurse
        Scene deep("Deep");
        GameObject* prev = nullptr;
        for (int i = 0; i < 50000; ++i) {
            GameObject* g = deep.CreateGameObject("n");
            if (prev) g->transform->SetParent(prev->transform, false);
            prev = g;
        }
        const SceneGraphStats ds = deep.Stats();
        CHECK(ds.objects == 50000 && ds.maxDepth == 49999 && ds.roots == 1 && ds.activeInHierarchy == 50000);
    }

    TEST_MAIN_RESULT();
}
