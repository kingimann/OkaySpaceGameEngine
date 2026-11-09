// Scene graph audit: component removal while callbacks run, destroying parents whose
// children or components do things in OnDestroy, tag index staleness, Clear then reuse,
// adding / removing components while Update iterates, loader range checks, runtime-only
// (transient) objects, instance ids and guids.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct Plain : public Behaviour {
    int updates = 0;
    void Update(float) override { ++updates; }
};
struct Other : public Behaviour {};
struct Extra : public Behaviour {};

// OnDestroy removes a sibling that sits earlier in the component list.
struct RemovesOther : public Behaviour {
    Component* victim = nullptr;
    void OnDestroy() override { if (victim && gameObject) gameObject->RemoveComponent(victim); }
};
// OnDestroy adds a component (the component vector may reallocate).
struct AddsExtra : public Behaviour {
    void OnDestroy() override { if (gameObject) { for (int i = 0; i < 16; ++i) gameObject->AddComponent<Extra>(); } }
};
// OnDisable or OnDestroy removes the component itself.
struct RemovesSelf : public Behaviour {
    int destroys = 0;
    void OnDestroy() override { ++destroys; if (gameObject) gameObject->RemoveComponent(this); }
};

// OnDestroy moves an object under the dying one.
struct AdoptsOnDestroy : public Behaviour {
    GameObject* adoptee = nullptr;
    void OnDestroy() override { if (adoptee) adoptee->transform->SetParent(transform, /*worldPositionStays=*/true); }
};
// OnDestroy destroys another object.
struct KillsOnDestroy : public Behaviour {
    Scene* scene = nullptr; GameObject* next = nullptr; int* order = nullptr; int id = 0;
    void OnDestroy() override {
        if (order) *order = *order * 10 + id;
        if (scene && next) scene->Destroy(next);
    }
};

struct AddsInUpdate : public Behaviour {
    int calls = 0; Extra* added = nullptr;
    void Update(float) override { if (++calls == 1) added = gameObject->AddComponent<Extra>(); }
};
struct RemovesInUpdate : public Behaviour {
    Component* victim = nullptr;
    void Update(float) override { if (victim) { gameObject->RemoveComponent(victim); victim = nullptr; } }
};
struct CountsUpdate : public Behaviour {
    int* counter = nullptr;   // outside the component: it is freed with its object
    void Update(float) override { if (counter) ++*counter; }
};
struct DestroysInUpdate : public Behaviour {
    std::vector<GameObject*> targets;
    void Update(float) override { for (GameObject* g : targets) gameObject->scene()->Destroy(g); targets.clear(); }
};

} // namespace

int main() {
    RUN_SUITE("RemoveComponent survives what its callbacks do");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* a = g->AddComponent<Other>();
        auto* b = g->AddComponent<RemovesOther>();
        auto* c = g->AddComponent<Extra>();
        b->victim = a;                                    // removes an EARLIER component from inside its own removal
        s.Update(0.016f);
        CHECK(g->RemoveComponent(b));
        CHECK(g->GetComponent<RemovesOther>() == nullptr);   // the right one went
        CHECK(g->GetComponent<Other>() == nullptr);
        CHECK(g->GetComponent<Extra>() == c);                // the bystander stayed
        CHECK(g->Components().size() == 2);                  // Transform + Extra
    }
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* b = g->AddComponent<AddsExtra>();
        g->AddComponent<Other>();
        s.Update(0.016f);
        CHECK(g->RemoveComponent(b));
        CHECK(g->GetComponent<AddsExtra>() == nullptr);
        CHECK(g->GetComponent<Other>() != nullptr);
        CHECK(g->GetComponents<Extra>().size() == 16);
    }
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* r = g->AddComponent<RemovesSelf>();
        g->AddComponent<Other>();
        s.Update(0.016f);
        CHECK(g->RemoveComponent(r));                        // its own OnDestroy asks for the same removal: ignored
        CHECK(g->GetComponent<RemovesSelf>() == nullptr);
        CHECK(g->GetComponent<Other>() != nullptr);
    }

    RUN_SUITE("destroying a parent whose OnDestroy adopts a live object");
    {
        Scene s("T");
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localPosition = {1.0f, 0.0f, 0.0f};
        GameObject* adoptee = s.CreateGameObject("Adoptee");
        adoptee->transform->localPosition = {5.0f, 0.0f, 0.0f};
        parent->AddComponent<AdoptsOnDestroy>()->adoptee = adoptee;
        s.Update(0.016f);
        s.Destroy(parent);
        s.Update(0.016f);
        CHECK(parent->IsDestroyed());
        CHECK(!adoptee->IsDestroyed());
        CHECK(adoptee->transform->Parent() == nullptr);      // not left hanging under a dead shell
        CHECK(adoptee->ActiveInHierarchy());
        CHECK_NEAR(adoptee->transform->Position().x, 5.0f, 1e-4f);
        CHECK(parent->transform->ChildCount() == 0);
    }

    RUN_SUITE("OnDestroy that destroys more objects");
    {
        Scene s("T");
        std::vector<GameObject*> chain;
        int order = 0;
        for (int i = 0; i < 5; ++i) chain.push_back(s.CreateGameObject("c" + std::to_string(i)));
        for (int i = 0; i < 4; ++i) {
            auto* k = chain[static_cast<std::size_t>(i)]->AddComponent<KillsOnDestroy>();
            k->scene = &s; k->next = chain[static_cast<std::size_t>(i + 1)]; k->order = &order; k->id = i + 1;
        }
        chain[4]->AddComponent<KillsOnDestroy>()->order = &order;
        chain[4]->GetComponent<KillsOnDestroy>()->id = 5;
        s.Update(0.016f);
        s.Destroy(chain[0]);
        s.Update(0.016f);
        CHECK(order == 12345);                              // each OnDestroy ran once, in the chain's order
        CHECK(s.Objects().size() == 0);
    }
    {   // a longer chain than one flush takes: the rest goes in the following frames, nothing leaks or crashes
        Scene s("T");
        std::vector<GameObject*> chain;
        for (int i = 0; i < 40; ++i) chain.push_back(s.CreateGameObject("c" + std::to_string(i)));
        for (int i = 0; i + 1 < 40; ++i) {
            auto* k = chain[static_cast<std::size_t>(i)]->AddComponent<KillsOnDestroy>();
            k->scene = &s; k->next = chain[static_cast<std::size_t>(i + 1)];
        }
        s.Update(0.016f);
        s.Destroy(chain[0]);
        for (int f = 0; f < 10; ++f) s.Update(0.016f);
        CHECK(s.Objects().empty());
    }

    RUN_SUITE("Destroy and DestroyImmediate");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        b->transform->SetParent(a->transform);
        auto* pl = b->AddComponent<Plain>();
        s.Update(0.016f);
        s.DestroyImmediate(a);                               // outside any update: gone right now, children with it
        CHECK(a->IsDestroyed() && b->IsDestroyed());
        CHECK(s.Find("A") == nullptr && s.Find("B") == nullptr);
        CHECK(pl != nullptr);
        s.DestroyImmediate(nullptr);                         // harmless
        s.DestroyImmediate(a);                               // again: harmless
        // called from a component's Update it is deferred to the end of the frame
        GameObject* d = s.CreateGameObject("D");
        GameObject* e = s.CreateGameObject("E");
        auto* e_plain = e->AddComponent<Plain>();
        auto* kill = d->AddComponent<DestroysInUpdate>();
        struct Imm : public Behaviour {
            GameObject* target = nullptr; bool aliveAfter = false;
            void Update(float) override {
                if (!target) return;
                gameObject->scene()->DestroyImmediate(target);
                aliveAfter = !target->IsDestroyed();   // still a live object mid-frame
                target = nullptr;
            }
        };
        auto* imm = d->AddComponent<Imm>();
        imm->target = e;
        (void)kill;
        s.Update(0.016f);
        CHECK(imm->aliveAfter);
        CHECK(e->IsDestroyed());                             // and gone once the frame ended
        CHECK(e_plain != nullptr);
    }

    RUN_SUITE("components added and removed while Update iterates");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        auto* adds = g->AddComponent<AddsInUpdate>();
        auto* removes = g->AddComponent<RemovesInUpdate>();
        int victimUpdates = 0;
        auto* victim = g->AddComponent<CountsUpdate>();
        victim->counter = &victimUpdates;
        removes->victim = victim;
        s.Update(0.016f);
        CHECK(adds->added != nullptr);                       // new component exists at once
        CHECK(g->GetComponent<CountsUpdate>() == nullptr);   // victim was removed before its turn
        CHECK(victimUpdates == 0);
        s.Update(0.016f);
        CHECK(adds->calls == 2);
    }

    RUN_SUITE("destroying objects from Update");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        int bUpdates = 0, cUpdates = 0;
        b->AddComponent<CountsUpdate>()->counter = &bUpdates;
        c->AddComponent<CountsUpdate>()->counter = &cUpdates;
        auto* d = a->AddComponent<DestroysInUpdate>();
        d->targets = {b, c, b, nullptr, a};                  // twice, null, itself
        s.Update(0.016f);
        CHECK(bUpdates == 1 && cUpdates == 1);               // they still ran this frame
        CHECK(s.Objects().empty());
        s.Update(0.016f);
    }

    RUN_SUITE("tag index follows every way a tag can change");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        a->tag = "Enemy"; b->tag = "Enemy";
        CHECK(s.FindWithTag("Enemy") == a && s.FindGameObjectsWithTag("Enemy").size() == 2);
        a->tag.clear();                                      // std::string mutators, not assignment
        CHECK(s.FindWithTag("Enemy") == b);
        CHECK(s.FindGameObjectsWithTag("Enemy").size() == 1);
        a->tag += "Boss";
        CHECK(s.FindWithTag("Boss") == a);
        a->tag.append("2");
        CHECK(s.FindWithTag("Boss") == nullptr && s.FindWithTag("Boss2") == a);
        a->tag.assign("Enemy");
        CHECK(s.FindGameObjectsWithTag("Enemy").size() == 2);
        a->tag.replace(0, 1, "e");
        CHECK(s.FindWithTag("enemy") == a);
        a->tag.pop_back();
        a->tag.push_back('y');
        CHECK(s.FindWithTag("enemy") == a);
        a->tag.erase(0, 1);
        a->tag.insert(0, "E");
        CHECK(s.FindGameObjectsWithTag("Enemy").size() == 2);
        a->tag.resize(2);
        CHECK(s.FindWithTag("En") == a);
        a->tag[0] = 'X';
        CHECK(s.FindWithTag("Xn") == a);
        std::string moved = "Moved";
        a->tag.swap(moved);
        CHECK(s.FindWithTag("Moved") == a);
        CHECK(a->SetTag("Player") && s.FindWithTag("Player") == a);
        CHECK(!a->SetTag("bad\ttag") && s.FindWithTag("Player") == a);
        a->name = "Renamed";                                 // the name has nothing to do with it
        CHECK(s.FindWithTag("Player") == a);
        // reparenting and destroying
        a->transform->SetParent(b->transform);
        CHECK(s.FindWithTag("Player") == a);
        s.Destroy(a);
        s.Update(0.016f);
        CHECK(s.FindWithTag("Player") == nullptr);
        // copying a tag value does not make a stale entry
        b->tag = b->tag;
        CHECK(s.FindWithTag("Enemy") == b);
    }

    RUN_SUITE("Clear then reuse");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        a->tag = "Hero";
        const std::uint64_t id1 = a->InstanceId();
        const std::uint64_t v1 = s.StructureVersion();
        s.Update(0.016f);
        s.Clear();
        CHECK(s.Objects().empty());
        CHECK(s.StructureVersion() != v1);
        CHECK(s.FindWithTag("Hero") == nullptr);
        CHECK(GameObject::FromInstanceId(id1) == nullptr);
        GameObject* b = s.CreateGameObject("B");
        b->tag = "Hero";
        CHECK(b->InstanceId() != id1);
        CHECK(s.FindWithTag("Hero") == b);
        CHECK(s.Find("A") == nullptr && s.Find("B") == b);
        CHECK(s.Roots().size() == 1);
        s.Update(0.016f);
        s.Clear();
        s.Clear();                                           // clearing an empty scene is fine
        CHECK(s.Objects().empty());
    }

    RUN_SUITE("instance ids and guids stay unique");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        for (int i = 0; i < 3; ++i) s.CreateGameObject("K" + std::to_string(i))->transform->SetParent(root->transform);
        GameObject* c1 = s.Instantiate(*root);
        GameObject* c2 = s.Instantiate(*root);
        std::set<std::uint64_t> ids; std::set<std::string> guids;
        for (const auto& o : s.Objects()) { ids.insert(o->InstanceId()); guids.insert(o->Guid()); }
        CHECK(ids.size() == s.Objects().size() && guids.size() == s.Objects().size());
        CHECK(c1 && c2 && c1->Guid() != root->Guid() && c1->Guid() != c2->Guid());
        // a saved scene with two objects sharing a guid loads with unique ones
        const std::string text =
            "okayscene 3\n"
            "gameobject 0 \"A\"\n  guid \"aaaa\"\n  parent -1\nend\n"
            "gameobject 1 \"B\"\n  guid \"aaaa\"\n  parent -1\nend\n";
        Scene t("L");
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(t, text, nullptr, &warnings));
        CHECK(t.Find("A")->Guid() != t.Find("B")->Guid());
        CHECK(!warnings.empty());
        // a load keeps the saved ids, a second load of the same text in another scene has the same ones
        Scene u("U");
        CHECK(SceneSerializer::Deserialize(u, SceneSerializer::Serialize(t)));
        CHECK(u.Find("A")->Guid() == t.Find("A")->Guid());
        CHECK(u.Find("A")->InstanceId() != t.Find("A")->InstanceId());
        CHECK(t.FindByGuid(t.Find("B")->Guid()) == t.Find("B"));
        CHECK(t.FindByGuid("") == nullptr && t.FindByGuid("ffff") == nullptr);
    }

    RUN_SUITE("AddComponent<Transform> returns the one it has");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        Transform* t = g->AddComponent<Transform>();
        CHECK(t == g->transform);
        CHECK(g->GetComponents<Transform>().size() == 1);
        CHECK(!g->RemoveComponent(g->transform));
    }

    RUN_SUITE("layer from a file is a layer");
    {
        const std::string text =
            "okayscene 3\n"
            "gameobject 0 \"High\"\n  layer 99\n  parent -1\nend\n"
            "gameobject 1 \"Neg\"\n  layer -3\n  parent -1\nend\n"
            "gameobject 2 \"Ok\"\n  layer 7\n  parent -1\nend\n";
        Scene s("L");
        CHECK(SceneSerializer::Deserialize(s, text));
        CHECK(s.Find("High")->layer == (99 & 31));
        CHECK(s.Find("Neg")->layer == (-3 & 31));
        CHECK(s.Find("Ok")->layer == 7);
        for (const auto& o : s.Objects()) CHECK(o->layer >= 0 && o->layer < 32);
    }

    RUN_SUITE("runtime-only objects and what hangs under them");
    {
        Scene s("T");
        GameObject* real = s.CreateGameObject("Real");
        GameObject* gen = s.CreateGameObject("Generated");
        gen->transient = true;
        gen->transform->localPosition = {100.0f, 0.0f, 0.0f};
        GameObject* under = s.CreateGameObject("UnderGenerated");
        under->transform->SetParent(gen->transform, false);
        under->transform->localPosition = {1.0f, 0.0f, 0.0f};
        GameObject* genChild = s.CreateGameObject("GeneratedChild");
        genChild->transient = true;
        genChild->transform->SetParent(real->transform, false);
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(t, text, &err));
        CHECK(t.Find("Real") != nullptr);
        CHECK(t.Find("Generated") == nullptr);
        CHECK(t.Find("GeneratedChild") == nullptr);
        // an object under a runtime-only one is runtime-only with it: it must not come back
        // as a loose root with a pose that only made sense under its parent
        CHECK(t.Find("UnderGenerated") == nullptr);
        CHECK(s.Validate().issues.empty());                  // and the saved form has no parent link into thin air
        // the same rule for a clone
        GameObject* clone = s.Instantiate(*gen);
        CHECK(clone == nullptr);
        GameObject* realClone = s.Instantiate(*real);
        CHECK(realClone != nullptr && realClone->transform->ChildCount() == 0);
    }

    RUN_SUITE("a NaN in a saved scene does not brick it");
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        g->transform->localPosition = {nan, 1.0f, 2.0f};
        g->transform->localScale = {inf, 1.0f, 1.0f};
        GameObject* h = s.CreateGameObject("H");
        h->transform->localPosition = {3.0f, 4.0f, 5.0f};
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("L");
        std::string err;
        std::vector<std::string> warnings;
        CHECK(SceneSerializer::Deserialize(t, text, &err, &warnings));
        CHECK(err.empty());
        GameObject* a = t.Find("G");
        CHECK(a != nullptr);
        if (a) {
            CHECK(a->transform->localPosition.x == 0.0f && a->transform->localPosition.y == 0.0f);   // reset, not half kept
            CHECK(a->transform->localScale.x == 1.0f);
        }
        CHECK(t.Find("H") && t.Find("H")->transform->localPosition.y == 4.0f);                    // the rest of the file is untouched
        bool said = false;
        for (const auto& w : warnings) if (w.find("non-finite transform") != std::string::npos) said = true;
        CHECK(said);
        // an all-zero rotation is no rotation either
        Scene z("Z");
        CHECK(SceneSerializer::Deserialize(z, "okayscene 3\ngameobject 0 \"Z\"\n  parent -1\n  transform 1 2 3 0 0 0 0 1 1 1\nend\n"));
        CHECK(z.Find("Z") && z.Find("Z")->transform->localRotation.w == 1.0f);
        // numbers with a sign or an exponent read as before, a doubled sign is not a number
        Scene n("N");
        CHECK(SceneSerializer::Deserialize(n, "okayscene 3\ngameobject 0 \"N\"\n  parent -1\n  transform -1.5 +2 3e2 0 0 0 1 -1 1e-1 1\nend\n"));
        GameObject* nn = n.Find("N");
        CHECK(nn && nn->transform->localPosition.x == -1.5f && nn->transform->localPosition.y == 2.0f && nn->transform->localPosition.z == 300.0f);
        CHECK(nn && nn->transform->localScale.x == -1.0f && nn->transform->localScale.y == 0.1f);
        Scene d("D");
        CHECK(!SceneSerializer::Deserialize(d, "okayscene 3\ngameobject 0 \"D\"\n  parent -1\n  transform --5 0 0 0 0 0 1 1 1 1\nend\n"));
        CHECK(d.Objects().empty());
    }

    RUN_SUITE("delayed destroy");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        s.Destroy(a, 1.0f);
        s.Destroy(b, 0.0f);                                  // at once
        s.Destroy(c, std::numeric_limits<float>::quiet_NaN());   // a bad time is not "never": at once
        s.Update(0.5f);
        CHECK(b->IsDestroyed() && c->IsDestroyed() && !a->IsDestroyed());
        s.Update(0.4f);
        CHECK(!a->IsDestroyed());
        s.Destroy(a, 5.0f);                                  // asking again keeps the sooner time
        s.Update(0.2f);
        CHECK(a->IsDestroyed());
    }

    TEST_MAIN_RESULT();
}
