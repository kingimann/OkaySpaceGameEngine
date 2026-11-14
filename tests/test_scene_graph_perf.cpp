// Scene graph cost on deep and wide hierarchies (1.3, wave 4).
//  - ActiveInHierarchy answers from a cache that every change of an `active` flag or of a parent link
//    invalidates, so a frame over a long chain is linear. The work is COUNTED (GameObject::ActiveWalkSteps),
//    not timed, so the checks are deterministic.
//  - The answer is the same as walking the parents, after any sequence of edits (a seeded random check).
//  - Destroying one parent with a very large number of children, or a deep chain built leaf first, takes
//    linear time. These sizes took minutes with the old per-object list edits; they have a generous ctest
//    TIMEOUT instead of a clock inside the test.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace okay;

namespace {

struct Counter : public Behaviour {
    int* updates = nullptr;
    void Update(float) override { if (updates) ++*updates; }
};

// The plain parent walk: the reference the cache must agree with.
bool NaiveActive(const GameObject* g) {
    for (; g; ) {
        if (!g->active) return false;
        const Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = p ? p->gameObject : nullptr;
    }
    return true;
}

// A small deterministic random source (no <random> state to depend on).
struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<std::uint32_t>(s >> 33); }
    int Below(int n) { return static_cast<int>(Next() % static_cast<std::uint32_t>(n)); }
};

std::vector<GameObject*> BuildChain(Scene& s, int depth, std::vector<int>* counts = nullptr) {
    std::vector<GameObject*> chain;
    chain.reserve(static_cast<std::size_t>(depth));
    if (counts) counts->assign(static_cast<std::size_t>(depth), 0);
    GameObject* prev = nullptr;
    for (int i = 0; i < depth; ++i) {
        GameObject* g = s.CreateGameObject("n" + std::to_string(i));
        if (prev) g->transform->SetParent(prev->transform, /*worldPositionStays=*/false);
        if (counts) g->AddComponent<Counter>()->updates = &(*counts)[static_cast<std::size_t>(i)];
        chain.push_back(g);
        prev = g;
    }
    return chain;
}

} // namespace

int main() {
    RUN_SUITE("a frame over a deep chain does linear work");
    {
        constexpr int kDepth = 20000;
        Scene s("Deep");
        std::vector<int> counts;
        auto chain = BuildChain(s, kDepth, &counts);
        s.Update(0.016f);   // Awake / Start, the first full answer for every object
        for (int c : counts) CHECK(c == 1);
        // Nothing changed: the next frames ask a lot of questions (Update, LateUpdate, collider fit, render
        // gate ...) and every one is answered from the cache.
        const auto before = GameObject::ActiveWalkSteps();
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(GameObject::ActiveWalkSteps() == before);
        // One object in the middle is switched off: every object below it reads inactive, and the whole
        // chain is re-answered in work proportional to its length (the old walk was length squared / 2).
        chain[kDepth / 2]->active = false;
        const auto b2 = GameObject::ActiveWalkSteps();
        s.Update(0.016f);
        const auto steps = GameObject::ActiveWalkSteps() - b2;
        CHECK(steps <= static_cast<std::uint64_t>(4 * kDepth));
        CHECK(chain[kDepth / 2 - 1]->ActiveInHierarchy());
        CHECK(!chain[kDepth / 2]->ActiveInHierarchy());
        CHECK(!chain.back()->ActiveInHierarchy());
        int ran = 0;
        for (int i = 0; i < kDepth; ++i) if (counts[static_cast<std::size_t>(i)] == 4) ++ran;
        CHECK(ran == kDepth / 2);                        // the half above the switch-off kept updating, the half below stopped
        chain[kDepth / 2]->active = true;
        CHECK(chain.back()->ActiveInHierarchy());
    }
    {   // a 100000-deep chain with a component on every object: one frame, linear
        constexpr int kDepth = 100000;
        Scene s("Deep");
        std::vector<int> counts;
        auto chain = BuildChain(s, kDepth, &counts);
        const auto before = GameObject::ActiveWalkSteps();
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(GameObject::ActiveWalkSteps() - before <= static_cast<std::uint64_t>(8 * kDepth));
        for (int c : counts) if (c != 2) { CHECK(c == 2); break; }
    }

    RUN_SUITE("GameObject::active reads and assigns like a bool");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        static_assert(std::is_convertible<ActiveFlag, bool>::value, "reads as a bool");
        bool b = g->active;
        CHECK(b);
        g->active = false;
        CHECK(!g->active);
        CHECK(g->active == false);
        const bool flipped = !g->active;
        CHECK(flipped);
        g->active = !g->active;
        CHECK(g->active);
        g->active = 0;
        CHECK(!g->active);
        const bool mixed = g->active ? false : true;      // no ambiguity with a plain bool
        CHECK(mixed);
        GameObject* h = s.CreateGameObject("H");
        h->active = g->active;                              // flag to flag
        CHECK(!h->active);
        std::swap(g->active, h->active);                    // a whole-flag swap stays usable
        CHECK(!g->active && !h->active);
        CHECK(!g->ActiveInHierarchy() && !h->ActiveInHierarchy());
        CHECK(static_cast<bool>(s.CreateGameObject("I")->active));
    }

    RUN_SUITE("the cached answer follows every kind of change");
    {
        Scene s("T");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        b->transform->SetParent(a->transform, false);
        c->transform->SetParent(b->transform, false);
        CHECK(c->ActiveInHierarchy());                        // cached true
        a->active = false;
        CHECK(!c->ActiveInHierarchy());                       // a flag changed
        a->active = true;
        CHECK(c->ActiveInHierarchy());
        b->transform->SetParent(nullptr, true);               // c leaves a
        a->active = false;
        CHECK(c->ActiveInHierarchy());                        // no longer below the inactive a
        b->transform->SetParent(a->transform, true);          // back under it
        CHECK(!c->ActiveInHierarchy());
        a->transform->DetachChildren();
        CHECK(c->ActiveInHierarchy());
        a->active = true;
        GameObject* d = s.CreateGameObject("D");
        d->active = false;
        c->transform->SetParent(d->transform, false);
        CHECK(!c->ActiveInHierarchy());
        s.Destroy(d);
        s.FlushDestroyed();                                   // d and its child go: frozen, inactive shells
        CHECK(c->IsDestroyed() && !c->ActiveInHierarchy());
        CHECK(c->transform->Parent() == nullptr);
        // a flag written as the same value changes nothing
        GameObject* e = s.CreateGameObject("E");
        CHECK(e->ActiveInHierarchy());
        const auto n = GameObject::ActiveWalkSteps();
        e->active = true;
        CHECK(e->ActiveInHierarchy());
        CHECK(GameObject::ActiveWalkSteps() == n);
    }
    {   // inside one frame: an earlier component switches a parent off, a later one is skipped at once
        struct Switch : public Behaviour {
            GameObject* target = nullptr;
            void Update(float) override { if (target) target->active = false; }
        };
        Scene s("T");
        GameObject* parent = s.CreateGameObject("Parent");
        GameObject* child = s.CreateGameObject("Child");
        child->transform->SetParent(parent->transform, false);
        int childUpdates = 0;
        auto* sw = s.CreateGameObject("Switcher")->AddComponent<Switch>();
        sw->target = parent;
        child->AddComponent<Counter>()->updates = &childUpdates;
        s.Update(0.016f);
        CHECK(childUpdates == 0);                             // the switch ran first in this frame
        sw->target = nullptr;
        parent->active = true;
        s.Update(0.016f);
        CHECK(childUpdates == 1);
    }
    {   // random edits: the cache and the plain walk never disagree
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
            Lcg rng(seed);
            Scene s("Random");
            std::vector<GameObject*> objs;
            for (int i = 0; i < 120; ++i) objs.push_back(s.CreateGameObject("o" + std::to_string(i)));
            int mismatches = 0;
            for (int step = 0; step < 600; ++step) {
                GameObject* g = objs[static_cast<std::size_t>(rng.Below(static_cast<int>(objs.size())))];
                if (g->IsDestroyed()) continue;
                switch (rng.Below(6)) {
                    case 0: case 1: g->active = !g->active; break;
                    case 2: {
                        GameObject* p = objs[static_cast<std::size_t>(rng.Below(static_cast<int>(objs.size())))];
                        if (!p->IsDestroyed()) g->transform->SetParent(p->transform, rng.Below(2) == 0);   // a cycle is refused
                        break;
                    }
                    case 3: g->transform->SetParent(nullptr, true); break;
                    case 4: g->transform->DetachChildren(); break;
                    default:
                        if (rng.Below(8) == 0) { s.Destroy(g); s.FlushDestroyed(); }
                        break;
                }
                for (GameObject* o : objs) {
                    if (o->IsDestroyed()) continue;
                    if (o->ActiveInHierarchy() != NaiveActive(o)) ++mismatches;
                }
            }
            CHECK(mismatches == 0);
        }
    }

    RUN_SUITE("ActiveInHierarchy read from several threads");
    {
        // While nothing changes, any number of threads may ask (the cache is relaxed atomics: a race can only
        // make two threads compute the same answer). The expected values are fixed before the threads start.
        Scene s("Threads");
        std::vector<GameObject*> chains[4];
        for (int c = 0; c < 4; ++c) chains[c] = BuildChain(s, 3000);
        chains[1][1500]->active = false;                       // chain 1 is switched off from the middle down
        chains[3][0]->active = false;                          // chain 3 entirely
        std::atomic<int> wrong{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < 4; ++t)
            pool.emplace_back([&, t] {
                for (int round = 0; round < 6; ++round)
                    for (int k = 0; k < 4; ++k) {
                        const int c = (k + t) % 4;
                        for (int i = 0; i < 3000; ++i) {
                            bool expect = true;
                            if (c == 1) expect = i < 1500;
                            if (c == 3) expect = false;
                            if (chains[c][static_cast<std::size_t>(i)]->ActiveInHierarchy() != expect) ++wrong;
                        }
                    }
            });
        for (auto& th : pool) th.join();
        CHECK(wrong.load() == 0);
    }

    RUN_SUITE("a destroyed parent with a very large number of children");
    {
        constexpr int kKids = 150000;
        Scene s("Wide");
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localPosition = {1.0f, 2.0f, 3.0f};
        parent->transform->localScale = {2.0f, 2.0f, 2.0f};
        GameObject* last = nullptr;
        for (int i = 0; i < kKids; ++i) {
            GameObject* k = s.CreateGameObject("k");
            k->transform->SetParent(parent->transform, /*worldPositionStays=*/false);
            k->transform->localPosition = {static_cast<float>(i), 0.0f, 0.0f};
            last = k;
        }
        const Vec3 where = last->transform->Position();
        CHECK(static_cast<int>(parent->transform->ChildCount()) == kKids);
        s.Destroy(parent);
        s.FlushDestroyed();
        CHECK(s.Objects().empty());
        // (the oldest 10000 shells are freed at the end of a flush that leaves more than 20000: only the newest is looked at)
        CHECK(last->IsDestroyed());
        CHECK(last->transform->Parent() == nullptr);          // a frozen root shell
        const Vec3 now = last->transform->Position();         // at the pose it had (the newest shell is still kept)
        CHECK_NEAR(now.x, where.x, 1e-2f);
        CHECK_NEAR(now.y, where.y, 1e-4f);
        CHECK_NEAR(now.z, where.z, 1e-4f);
    }
    {   // the children go but their parent lives: the parent's list is emptied in one pass
        constexpr int kKids = 150000;
        Scene s("Wide");
        GameObject* parent = s.CreateGameObject("Pool");
        std::vector<GameObject*> kids;
        kids.reserve(kKids);
        for (int i = 0; i < kKids; ++i) {
            GameObject* k = s.CreateGameObject("bullet");
            k->transform->SetParent(parent->transform, false);
            kids.push_back(k);
        }
        // keep every 1000th alive, in order
        std::vector<GameObject*> keep;
        for (int i = 0; i < kKids; ++i) {
            if (i % 1000 == 0) keep.push_back(kids[static_cast<std::size_t>(i)]);
            else s.Destroy(kids[static_cast<std::size_t>(i)]);
        }
        s.FlushDestroyed();
        CHECK(parent->transform->ChildCount() == static_cast<int>(keep.size()));
        bool sameOrder = true;
        for (std::size_t i = 0; i < keep.size(); ++i) if (parent->transform->GetChild(static_cast<int>(i)) != keep[i]->transform) sameOrder = false;
        CHECK(sameOrder);
        CHECK(s.Objects().size() == keep.size() + 1);
    }

    RUN_SUITE("a deep chain destroyed from the leaf end");
    {
        // Objects are made leaf first, so the scene lists the leaf at the front: the old per-object detach
        // read the world pose through the whole chain for each of them.
        constexpr int kDepth = 100000;
        Scene s("Deep");
        std::vector<GameObject*> objs;
        objs.reserve(kDepth);
        for (int i = 0; i < kDepth; ++i) objs.push_back(s.CreateGameObject("c"));
        for (int i = 0; i + 1 < kDepth; ++i) {
            objs[static_cast<std::size_t>(i)]->transform->SetParent(objs[static_cast<std::size_t>(i + 1)]->transform, false);
            objs[static_cast<std::size_t>(i)]->transform->localPosition = {1.0f, 0.0f, 0.0f};
        }
        GameObject* top = objs.back();
        CHECK(objs.front()->transform->Depth() == kDepth - 1);
        s.Destroy(top);
        s.FlushDestroyed();
        CHECK(s.Objects().empty());
    }
    {   // the frozen pose of a destroyed subtree is the pose it had, parents turned and scaled included
        Scene s("Frozen");
        GameObject* a = s.CreateGameObject("A");
        a->transform->localPosition = {1.0f, 2.0f, 3.0f};
        a->transform->localRotation = Quat::Euler(0.0f, 0.0f, 90.0f);
        a->transform->localScale = {2.0f, 3.0f, 1.0f};
        GameObject* b = s.CreateGameObject("B");
        b->transform->SetParent(a->transform, false);
        b->transform->localPosition = {1.0f, 0.0f, 0.0f};
        b->transform->localRotation = Quat::Euler(0.0f, 30.0f, 0.0f);
        GameObject* c = s.CreateGameObject("C");
        c->transform->SetParent(b->transform, false);
        c->transform->localPosition = {0.0f, 1.0f, 0.0f};
        c->transform->localScale = {1.0f, 2.0f, 1.0f};
        GameObject* out = s.CreateGameObject("Outside");
        a->transform->SetParent(out->transform, false);       // the dying top has a surviving parent
        out->transform->localPosition = {0.0f, 0.0f, 10.0f};
        const Vec3 pa = a->transform->Position(), pb = b->transform->Position(), pc = c->transform->Position();
        const Quat rc = c->transform->Rotation();
        const Vec3 sc = c->transform->LossyScale();
        s.Destroy(a);
        s.FlushDestroyed();
        CHECK(out->transform->ChildCount() == 0);             // the survivor's list lost it
        CHECK(a->transform->Parent() == nullptr && b->transform->Parent() == nullptr && c->transform->Parent() == nullptr);
        CHECK((a->transform->Position() - pa).Magnitude() < 1e-4f);
        CHECK((b->transform->Position() - pb).Magnitude() < 1e-4f);
        CHECK((c->transform->Position() - pc).Magnitude() < 1e-4f);
        CHECK(1.0f - std::fabs(Quat::Dot(c->transform->Rotation(), rc)) < 1e-5f);
        CHECK((c->transform->LossyScale() - sc).Magnitude() < 1e-4f);
        CHECK(!out->IsDestroyed());
    }

    TEST_MAIN_RESULT();
}
