// Scene graph, fifth pass: the world-matrix cache checked against a plain walk after random edits (written through
// the setters and straight into the public fields), counted work for world reads, constant-cost removal from a very
// long child list with the order kept, the look-at variants, GetOrAddComponent, ForEachDescendant, FindByPath with
// duplicate and slash-carrying names, Scene::Stats on a huge scene, Mover / Spinner under a destroyed parent, and
// layer and tag names in other languages.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// A small deterministic generator (no <random> distributions: those differ between standard libraries).
struct TestRng {
    std::uint64_t s;
    explicit TestRng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<std::uint32_t>(s >> 16); }
    int Int(int n) { return static_cast<int>(Next() % static_cast<std::uint32_t>(n)); }
    float Float(float lo, float hi) { return lo + (hi - lo) * (static_cast<float>(Next() % 100001u) / 100000.0f); }
};

bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol * (1.0f + std::fabs(a)); }
bool NearV(const Vec3& a, const Vec3& b, float tol = 1e-4f) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }
bool NearQ(const Quat& a, const Quat& b, float tol = 1e-4f) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol) && Near(a.w, b.w, tol); }

// The world pose worked out from the local fields only, with no cache: root first, as a chain walk does it.
struct PlainWorld { Mat4 world; Vec3 position; Quat rotation; Vec3 scale; };
PlainWorld Plain(const Transform* t) {
    std::vector<const Transform*> chain;
    for (const Transform* p = t; p; p = p->Parent()) chain.push_back(p);
    PlainWorld w;
    Mat4 parentWorld;
    bool root = true;
    for (std::size_t i = chain.size(); i-- > 0;) {
        const Transform* c = chain[i];
        const Mat4 local = Mat4::TRS(c->localPosition, c->localRotation, c->localScale);
        if (root) {
            w.world = local; w.rotation = c->localRotation; w.scale = c->localScale; w.position = c->localPosition;
            root = false;
        } else {
            parentWorld = w.world;
            w.world = parentWorld * local;
            w.position = parentWorld.MultiplyPoint(c->localPosition);
            w.rotation = (w.rotation * c->localRotation).Normalized();
            w.scale = {w.scale.x * c->localScale.x, w.scale.y * c->localScale.y, w.scale.z * c->localScale.z};
        }
    }
    return w;
}

bool SameAsPlain(const Transform* t) {
    const PlainWorld p = Plain(t);
    const Mat4 m = t->LocalToWorldMatrix();
    for (int i = 0; i < 16; ++i) if (!Near(m.m[i], p.world.m[i], 1e-3f)) return false;
    return NearV(t->Position(), p.position, 1e-3f) && NearQ(t->Rotation(), p.rotation, 1e-3f) &&
           NearV(t->LossyScale(), p.scale, 1e-3f);
}

} // namespace

int main() {
    // =====================================================================================================
    RUN_SUITE("world reads after random edits agree with a plain walk of the chain");
    {
        for (int seed = 1; seed <= 3; ++seed) {
            Scene s("T");
            TestRng rng(static_cast<std::uint64_t>(seed));
            std::vector<GameObject*> g;
            const int n = 40;
            for (int i = 0; i < n; ++i) {
                GameObject* o = s.CreateGameObject("N" + std::to_string(i));
                if (i > 0) o->transform->SetParent(g[static_cast<std::size_t>(rng.Int(i))]->transform, false);   // an earlier one: no loops
                o->transform->localPosition = {rng.Float(-5, 5), rng.Float(-5, 5), rng.Float(-5, 5)};
                o->transform->localRotation = Quat::Euler(rng.Float(-90, 90), rng.Float(-90, 90), rng.Float(-90, 90));
                o->transform->localScale = {rng.Float(0.5f, 2), rng.Float(0.5f, 2), rng.Float(0.5f, 2)};
                g.push_back(o);
            }
            bool allSame = true;
            int checks = 0;
            for (int step = 0; step < 4000; ++step) {
                Transform* t = g[static_cast<std::size_t>(rng.Int(n))]->transform;
                switch (rng.Int(16)) {
                    case 0: t->localPosition.x = rng.Float(-5, 5); break;                                  // plain field writes: the
                    case 1: t->localRotation = Quat::Euler(rng.Float(-90, 90), rng.Float(-90, 90), 0); break;   // cache has to notice them
                    case 2: t->localScale.y = rng.Float(0.5f, 2); break;
                    case 3: t->localPosition = {rng.Float(-5, 5), rng.Float(-5, 5), rng.Float(-5, 5)}; break;
                    case 4: t->Translate({rng.Float(-1, 1), 0, 0}); break;
                    case 5: t->Rotate({rng.Float(-30, 30), rng.Float(-30, 30), 0}); break;
                    case 6: t->SetPosition({rng.Float(-9, 9), rng.Float(-9, 9), rng.Float(-9, 9)}); break;
                    case 7: t->SetRotation(Quat::Euler(rng.Float(-90, 90), rng.Float(-90, 90), rng.Float(-90, 90))); break;
                    case 8: t->SetLossyScale({rng.Float(0.5f, 2), rng.Float(0.5f, 2), rng.Float(0.5f, 2)}); break;
                    case 9: t->SetParent(g[static_cast<std::size_t>(rng.Int(n))]->transform, rng.Int(2) == 0); break;   // a loop is refused
                    case 10: t->SetParent(nullptr, rng.Int(2) == 0); break;
                    case 11: t->SetSiblingIndex(rng.Int(6)); break;
                    case 12: t->LookAt({rng.Float(-9, 9), rng.Float(-9, 9), rng.Float(-9, 9)}); break;
                    case 13: t->SetLocalEulerAngles({rng.Float(-60, 60), rng.Float(-60, 60), rng.Float(-60, 60)}); break;
                    case 14: t->DetachChildren(); break;
                    default: t->SetPositionAndRotation({rng.Float(-9, 9), rng.Float(-9, 9), rng.Float(-9, 9)}, Quat::Euler(rng.Float(-90, 90), 0, 0)); break;
                }
                // Scales must stay readable (a stretch or a parent chain can only make them bigger or smaller by a few times).
                for (int r = 0; r < 3; ++r) {
                    const Transform* probe = g[static_cast<std::size_t>(rng.Int(n))]->transform;
                    const Vec3 sc = probe->LossyScale();
                    if (!(std::fabs(sc.x) < 1e6f && std::fabs(sc.y) < 1e6f && std::fabs(sc.z) < 1e6f)) continue;   // far outside a sane range: skip, not a cache matter
                    ++checks;
                    if (!SameAsPlain(probe)) { allSame = false; std::cerr << "  differs at seed " << seed << " step " << step << "\n"; }
                }
                if (!allSame) break;
            }
            CHECK(allSame);
            CHECK(checks > 3000);
        }
    }

    // =====================================================================================================
    RUN_SUITE("world reads: work is counted, an unchanged chain is only checked, never rebuilt");
    {
        Scene s("T");
        const int depth = 200;
        std::vector<GameObject*> chain;
        for (int i = 0; i < depth; ++i) {
            GameObject* o = s.CreateGameObject("C" + std::to_string(i));
            if (i > 0) o->transform->SetParent(chain.back()->transform, false);
            o->transform->localPosition = {1.0f, 0.0f, 0.0f};
            o->transform->localRotation = Quat::Euler(0.0f, 1.0f, 0.0f);
            chain.push_back(o);
        }
        Transform* leaf = chain.back()->transform;

        std::uint64_t rebuilds = Transform::WorldRebuilds(), steps = Transform::WorldWalkSteps();
        const Vec3 first = leaf->Position();
        CHECK(Transform::WorldRebuilds() - rebuilds == static_cast<std::uint64_t>(depth - 1));   // the parents' chain: every level once (the leaf itself is not needed for a position)
        // The leaf's own matrix, rotation and scale are one level more, and then nothing again.
        rebuilds = Transform::WorldRebuilds();
        (void)leaf->LocalToWorldMatrix();
        CHECK(Transform::WorldRebuilds() - rebuilds == 1);

        // Nothing changed: reading it again, in any form, rebuilds nothing and looks at each level once.
        rebuilds = Transform::WorldRebuilds(); steps = Transform::WorldWalkSteps();
        for (int i = 0; i < 10; ++i) { (void)leaf->Rotation(); (void)leaf->LossyScale(); (void)leaf->Position(); }
        CHECK(Transform::WorldRebuilds() == rebuilds);
        CHECK(Transform::WorldWalkSteps() - steps <= 30u * static_cast<std::uint64_t>(depth));   // 30 reads, one pass of the chain each (the position one pass of its parents)
        CHECK(Transform::WorldWalkSteps() - steps >= 29u * static_cast<std::uint64_t>(depth - 1));
        CHECK(leaf->Position().x == first.x && leaf->Position().y == first.y && leaf->Position().z == first.z);

        // One edit in the middle: only the levels from there down are rebuilt, once, however many reads follow.
        rebuilds = Transform::WorldRebuilds();
        chain[100]->transform->localPosition.y = 5.0f;   // a plain field write
        (void)leaf->Position();
        (void)leaf->Rotation();
        (void)leaf->LossyScale();
        (void)chain[150]->transform->Position();
        CHECK(Transform::WorldRebuilds() - rebuilds == static_cast<std::uint64_t>(depth - 100));   // levels 100 .. 199
        CHECK(SameAsPlain(leaf));

        // An edit at the root rebuilds the whole chain once.
        rebuilds = Transform::WorldRebuilds();
        chain[0]->transform->localScale = {1.0f, 1.0f, 1.0f};   // the same value: no change at all
        (void)leaf->Position();
        CHECK(Transform::WorldRebuilds() == rebuilds);
        chain[0]->transform->localPosition = {9.0f, 0.0f, 0.0f};
        (void)leaf->Position();
        (void)leaf->Rotation();
        (void)chain[77]->transform->Rotation();
        CHECK(Transform::WorldRebuilds() - rebuilds == static_cast<std::uint64_t>(depth));
        CHECK(SameAsPlain(leaf));
    }
    {
        // Building a deep chain that keeps each object's world pose: one walk of the parent chain per SetParent, not several.
        Scene s("T");
        const int n = 1500;
        std::vector<GameObject*> chain;
        for (int i = 0; i < n; ++i) {
            GameObject* o = s.CreateGameObject("D");
            o->transform->localPosition = {1.0f, 0.0f, 0.0f};
            if (i > 0) o->transform->SetParent(chain.back()->transform, /*worldPositionStays=*/false);
            chain.push_back(o);
        }
        // Take the whole chain apart and put it back together keeping every world pose, leaf first.
        for (int i = n - 1; i > 0; --i) chain[static_cast<std::size_t>(i)]->transform->SetParent(nullptr, true);
        const std::uint64_t steps = Transform::WorldWalkSteps();
        for (int i = 1; i < n; ++i) chain[static_cast<std::size_t>(i)]->transform->SetParent(chain[static_cast<std::size_t>(i - 1)]->transform, true);
        const std::uint64_t used = Transform::WorldWalkSteps() - steps;
        const std::uint64_t oneWalkEach = static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(n) / 2;   // the sum of the depths
        CHECK(used <= oneWalkEach + 4u * static_cast<std::uint64_t>(n));   // it used to be three walks each
        CHECK(NearV(chain.back()->transform->Position(), Vec3{static_cast<float>(n), 0.0f, 0.0f}, 1e-3f));   // every object sits 1 further along: the world poses were kept
        CHECK(chain.back()->transform->Depth() == n - 1);
    }

    // =====================================================================================================
    RUN_SUITE("taking children out of one long list, one at a time, costs the same for each");
    {
        Scene s("T");
        const int n = 300000;
        GameObject* p = s.CreateGameObject("P");
        std::vector<Transform*> kids;
        kids.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            GameObject* c = s.CreateGameObject("c");
            c->transform->SetParent(p->transform, false);
            kids.push_back(c->transform);
        }
        CHECK(p->transform->ChildCount() == n);
        CHECK(p->transform->GetChild(0) == kids.front() && p->transform->GetChild(n - 1) == kids.back());

        const std::uint64_t work = Transform::ChildListWork();
        for (int i = 0; i < n / 2; ++i) kids[static_cast<std::size_t>(i)]->SetParent(nullptr, false);   // the front first: the worst order for a vector
        const std::uint64_t used = Transform::ChildListWork() - work;
        CHECK(used < 6u * static_cast<std::uint64_t>(n));   // a few slots per removal; erasing from the front moved n/2 * n/2 = 22 billion
        CHECK(p->transform->ChildCount() == n / 2);
        // The ones that stayed are in their old order and know their place.
        const auto& left = p->transform->Children();
        bool inOrder = left.size() == static_cast<std::size_t>(n / 2);
        for (std::size_t i = 0; inOrder && i < left.size(); i += 997) inOrder = left[i] == kids[static_cast<std::size_t>(n / 2) + i];
        CHECK(inOrder);
        CHECK(kids[static_cast<std::size_t>(n / 2)]->GetSiblingIndex() == 0);
        CHECK(kids.back()->GetSiblingIndex() == n / 2 - 1);
        CHECK(kids.front()->Parent() == nullptr);

        // The rest from the back: the cheapest order, and the list is empty at the end.
        const std::uint64_t work2 = Transform::ChildListWork();
        for (int i = n - 1; i >= n / 2; --i) kids[static_cast<std::size_t>(i)]->SetParent(nullptr, false);
        CHECK(Transform::ChildListWork() - work2 < 3u * static_cast<std::uint64_t>(n));
        CHECK(p->transform->ChildCount() == 0 && p->transform->Children().empty());

        // Put them back (in the old order) and take them out the middle way round; Destroy of the whole parent is still linear.
        for (int i = 0; i < n; ++i) kids[static_cast<std::size_t>(i)]->SetParent(p->transform, false);
        CHECK(p->transform->GetChild(0) == kids.front() && p->transform->GetChild(n - 1) == kids.back());
        s.Destroy(p);
        s.FlushDestroyed();
        CHECK(s.Objects().size() == 0);
    }
    {
        // The order of the others never changes: a random mix of removals, additions and reordering against a plain vector.
        Scene s("T");
        TestRng rng(7);
        GameObject* p = s.CreateGameObject("P");
        std::vector<Transform*> model;                  // what the child list must read as
        std::vector<Transform*> outside;                // objects that are not children
        for (int i = 0; i < 700; ++i) {
            GameObject* c = s.CreateGameObject("k" + std::to_string(i));
            c->transform->SetParent(p->transform, false);
            model.push_back(c->transform);
        }
        for (int i = 0; i < 100; ++i) outside.push_back(s.CreateGameObject("o" + std::to_string(i))->transform);
        bool ok = true;
        auto agree = [&](const char* what, int step) {
            const auto& kids = p->transform->Children();
            bool same = kids.size() == model.size() && p->transform->ChildCount() == static_cast<int>(model.size());
            for (std::size_t i = 0; same && i < kids.size(); ++i) same = kids[i] == model[i];
            if (!same) { ok = false; std::cerr << "  list differs after " << what << " at step " << step << "\n"; }
        };
        for (int step = 0; step < 3000 && ok; ++step) {
            const int op = rng.Int(8);
            if (op <= 2 && model.size() > 5) {   // remove a random child (parent null, or another object)
                const std::size_t at = static_cast<std::size_t>(rng.Int(static_cast<int>(model.size())));
                Transform* c = model[at];
                model.erase(model.begin() + static_cast<long>(at));
                c->SetParent(nullptr, false);
                outside.push_back(c);
            } else if (op == 3 && !outside.empty()) {   // add one at the end
                const std::size_t at = static_cast<std::size_t>(rng.Int(static_cast<int>(outside.size())));
                Transform* c = outside[at];
                outside.erase(outside.begin() + static_cast<long>(at));
                c->SetParent(p->transform, false);
                model.push_back(c);
            } else if (op == 4 && !model.empty()) {     // SetSiblingIndex
                const std::size_t at = static_cast<std::size_t>(rng.Int(static_cast<int>(model.size())));
                const int to = rng.Int(static_cast<int>(model.size()) + 4) - 2;
                Transform* c = model[at];
                const int clamped = std::clamp(to, 0, static_cast<int>(model.size()) - 1);
                model.erase(model.begin() + static_cast<long>(at));
                model.insert(model.begin() + clamped, c);
                c->SetSiblingIndex(to);
            } else if (op == 5 && model.size() > 2) {   // MoveChild one step
                const std::size_t at = static_cast<std::size_t>(rng.Int(static_cast<int>(model.size())));
                const int dir = rng.Int(2) == 0 ? -1 : 1;
                const std::size_t to = dir < 0 ? (at == 0 ? at : at - 1) : (at + 1 >= model.size() ? at : at + 1);
                std::swap(model[at], model[to]);
                p->transform->MoveChild(model[to], dir);
            } else if (op == 6 && model.size() > 2) {   // to an edge, or next to another child
                const std::size_t at = static_cast<std::size_t>(rng.Int(static_cast<int>(model.size())));
                Transform* c = model[at];
                if (rng.Int(2) == 0) {
                    const bool front = rng.Int(2) == 0;
                    model.erase(model.begin() + static_cast<long>(at));
                    if (front) model.insert(model.begin(), c); else model.push_back(c);
                    p->transform->MoveChildToEdge(c, front);
                } else {
                    const std::size_t other = static_cast<std::size_t>(rng.Int(static_cast<int>(model.size())));
                    Transform* anchor = model[other];
                    if (anchor != c) {
                        const bool after = rng.Int(2) == 0;
                        model.erase(model.begin() + static_cast<long>(at));
                        auto a = std::find(model.begin(), model.end(), anchor);
                        model.insert(after ? a + 1 : a, c);
                        p->transform->ReorderChild(c, anchor, after);
                    }
                }
            } else if (!model.empty()) {                 // reads that must not disturb anything
                const std::size_t at = static_cast<std::size_t>(rng.Int(static_cast<int>(model.size())));
                if (model[at]->GetSiblingIndex() != static_cast<int>(at) || p->transform->GetChild(static_cast<int>(at)) != model[at]) { ok = false; std::cerr << "  index differs at step " << step << "\n"; }
            }
            if (p->transform->ChildCount() != static_cast<int>(model.size())) { ok = false; std::cerr << "  count differs at step " << step << "\n"; }
            if (step % 7 == 0) agree("an edit", step);
        }
        agree("the last edit", 3000);
        CHECK(ok);
        for (Transform* o : outside) { o->SetParent(p->transform, false); model.push_back(o); }   // a long list again
        agree("adding them all back", 3001);
        CHECK(ok && model.size() > 100);
        // Destroying a part of a long list through the scene leaves the rest in order with right places.
        std::vector<Transform*> keep;
        for (std::size_t i = 0; i < model.size(); ++i) {
            if (i % 3 == 0) s.Destroy(model[i]->gameObject); else keep.push_back(model[i]);
        }
        s.FlushDestroyed();
        const auto& after = p->transform->Children();
        bool same = after.size() == keep.size();
        for (std::size_t i = 0; same && i < after.size(); ++i) same = after[i] == keep[i] && after[i]->GetSiblingIndex() == static_cast<int>(i);
        CHECK(same);
    }
    {
        // Readers on several threads right after removals (holes not yet swept out) all see a clean list.
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        std::vector<Transform*> kids;
        for (int i = 0; i < 1000; ++i) {
            GameObject* c = s.CreateGameObject("c");
            c->transform->SetParent(p->transform, false);
            kids.push_back(c->transform);
        }
        for (int i = 0; i < 1000; i += 3) kids[static_cast<std::size_t>(i)]->SetParent(nullptr, false);   // holes are left (fewer than half)
        const int expected = p->transform->ChildCount();
        CHECK(expected == 666);
        std::atomic<int> bad{0};
        std::vector<std::thread> readers;
        for (int t = 0; t < 4; ++t)
            readers.emplace_back([&] {
                for (int round = 0; round < 50; ++round) {
                    const auto& list = p->transform->Children();
                    if (static_cast<int>(list.size()) != expected) ++bad;
                    for (const Transform* c : list) if (!c || c->Parent() != p->transform) { ++bad; break; }
                }
            });
        for (auto& th : readers) th.join();
        CHECK(bad == 0);
    }

    // =====================================================================================================
    RUN_SUITE("look-at variants");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        Transform* t = g->transform;
        t->LookDirection({1.0f, 0.0f, 0.0f});
        CHECK(NearV(t->Forward(), {1.0f, 0.0f, 0.0f}) && NearV(t->Up(), {0.0f, 1.0f, 0.0f}));
        t->LookDirection({0.0f, 3.0f, 0.0f});                       // straight up: along the default up hint
        CHECK(NearV(t->Forward(), {0.0f, 1.0f, 0.0f}));
        CHECK(std::isfinite(t->Rotation().x) && std::isfinite(t->Rotation().w) && Near(t->Rotation().Magnitude(), 1.0f, 1e-3f));
        t->LookDirection({0.0f, -2.0f, 0.0f});                      // straight down
        CHECK(NearV(t->Forward(), {0.0f, -1.0f, 0.0f}));
        t->LookDirection({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 1.0f});   // the hint runs along the direction
        CHECK(NearV(t->Forward(), {0.0f, 0.0f, 1.0f}) && std::isfinite(t->Up().x));
        const Quat held = t->Rotation();
        t->LookDirection({0.0f, 0.0f, 0.0f});                       // nothing: no turn
        t->LookDirection({kNaN, 1.0f, 0.0f});
        t->LookDirection({std::numeric_limits<float>::infinity(), 0.0f, 0.0f});
        CHECK(NearQ(t->Rotation(), held, 1e-6f));
        t->LookDirection({1.0f, 0.0f, 0.0f}, {kNaN, 0.0f, 0.0f});   // a bad hint falls back to the world up
        CHECK(NearV(t->Forward(), {1.0f, 0.0f, 0.0f}) && NearV(t->Up(), {0.0f, 1.0f, 0.0f}));

        // another transform as the target; a null one does nothing
        GameObject* target = s.CreateGameObject("Target");
        target->transform->localPosition = {0.0f, 0.0f, 10.0f};
        t->LookDirection({1.0f, 0.0f, 0.0f});
        t->LookAt(static_cast<const Transform*>(nullptr));
        CHECK(NearV(t->Forward(), {1.0f, 0.0f, 0.0f}));
        t->LookAt(target->transform);
        CHECK(NearV(t->Forward(), {0.0f, 0.0f, 1.0f}));

        // under a turned parent the direction is a world one
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);
        GameObject* kid = s.CreateGameObject("Kid");
        kid->transform->SetParent(parent->transform, false);
        kid->transform->LookDirection({0.0f, 0.0f, 1.0f});
        CHECK(NearV(kid->transform->Forward(), {0.0f, 0.0f, 1.0f}));
    }
    {
        Scene s("T");
        Transform* t = s.CreateGameObject("G")->transform;
        t->LookAtYaw({3.0f, 10.0f, 4.0f});                          // the height is dropped
        CHECK(NearV(t->Forward(), {0.6f, 0.0f, 0.8f}) && NearV(t->Up(), {0.0f, 1.0f, 0.0f}));
        const Quat held = t->Rotation();
        t->LookAtYaw({0.0f, 10.0f, 0.0f});                          // straight above: no side to face
        CHECK(NearQ(t->Rotation(), held, 1e-6f));
        t->LookAtYaw({1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f});      // no up axis
        CHECK(NearQ(t->Rotation(), held, 1e-6f));
        t->localPosition = {1.0f, 2.0f, 3.0f};
        t->LookAtYaw({1.0f, -7.0f, 13.0f});                         // below and ahead: still level
        CHECK(NearV(t->Forward(), {0.0f, 0.0f, 1.0f}));
        t->LookAtYaw({6.0f, 2.0f, 9.0f}, {0.0f, 0.0f, 1.0f});       // a top-down game: up is +z, the plane is x / y
        CHECK(Near(t->Forward().z, 0.0f, 1e-4f) && NearV(t->Forward(), {1.0f, 0.0f, 0.0f}));
    }
    {
        Scene s("T");
        Transform* t = s.CreateGameObject("G")->transform;
        t->LookAt2D({0.0f, 5.0f, 99.0f});                           // z is ignored; Right() points up the screen
        CHECK(NearV(t->Right(), {0.0f, 1.0f, 0.0f}));
        t->LookAt2D({-3.0f, 0.0f, 0.0f});
        CHECK(NearV(t->Right(), {-1.0f, 0.0f, 0.0f}));
        t->LookAt2D({0.0f, 5.0f, 0.0f}, Transform::Facing2D::Up);   // a sprite that points up
        CHECK(NearV(t->Up(), {0.0f, 1.0f, 0.0f}) && Near(t->Rotation().Magnitude(), 1.0f, 1e-3f));
        t->LookAt2D({4.0f, 0.0f, 0.0f}, Transform::Facing2D::Up);
        CHECK(NearV(t->Up(), {1.0f, 0.0f, 0.0f}));
        const Quat held = t->Rotation();
        t->LookAt2D(t->Position());                                 // at its own position: no turn
        t->LookAt2D({kNaN, 1.0f, 0.0f});
        CHECK(NearQ(t->Rotation(), held, 1e-6f));
    }
    {
        Scene s("T");
        Transform* t = s.CreateGameObject("G")->transform;          // faces +z; the target is at +x: a quarter turn
        const Vec3 target{10.0f, 0.0f, 0.0f};
        CHECK(!t->RotateTowards(target, 30.0f));
        CHECK(Near(Quat::Angle(t->Rotation(), Quat::LookRotation({1.0f, 0.0f, 0.0f})), 60.0f, 1e-2f));
        CHECK(!t->RotateTowards(target, 30.0f));
        CHECK(Near(Quat::Angle(t->Rotation(), Quat::LookRotation({1.0f, 0.0f, 0.0f})), 30.0f, 1e-2f));
        CHECK(t->RotateTowards(target, 30.0f));                      // arrives exactly
        CHECK(NearV(t->Forward(), {1.0f, 0.0f, 0.0f}));
        CHECK(t->RotateTowards(target, 30.0f));                      // already facing
        // the turn does not depend on how the time is cut up
        Transform* a = s.CreateGameObject("A")->transform;
        Transform* b = s.CreateGameObject("B")->transform;
        a->RotateTowards(target, 45.0f);
        for (int i = 0; i < 9; ++i) b->RotateTowards(target, 5.0f);
        CHECK(NearV(a->Forward(), b->Forward(), 1e-3f));
        // no turn for a negative, NaN or zero allowance; the answer is still "not facing"
        Transform* c = s.CreateGameObject("C")->transform;
        const Quat held = c->Rotation();
        CHECK(!c->RotateTowards(target, -5.0f) && !c->RotateTowards(target, kNaN) && !c->RotateTowards(target, 0.0f));
        CHECK(NearQ(c->Rotation(), held, 1e-6f));
        CHECK(c->RotateTowards(c->Position(), 10.0f));               // a target at its own position: nothing to face
    }

    // =====================================================================================================
    RUN_SUITE("GetOrAddComponent");
    {
        Scene s("T");
        GameObject* g = s.CreateGameObject("G");
        CHECK(g->GetComponent<Mover>() == nullptr);
        Mover* m = g->GetOrAddComponent<Mover>();
        CHECK(m != nullptr && g->GetComponent<Mover>() == m);
        CHECK(g->GetOrAddComponent<Mover>() == m);
        CHECK(g->GetComponents<Mover>().size() == 1);
        CHECK(g->GetOrAddComponent<Transform>() == g->transform);   // the Transform is always there
        auto* life = g->GetOrAddComponent<Lifetime>();
        CHECK(life != nullptr && g->Components().size() == 3);
        // an existing one wins over a request for a different base class: asked for by the type that is there
        Component* base = g->GetOrAddComponent<Component>();
        CHECK(base == static_cast<Component*>(g->transform));        // the first component assignable to Component
    }

    // =====================================================================================================
    RUN_SUITE("ForEachDescendant");
    {
        Scene s("T");
        GameObject* r = s.CreateGameObject("R");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        GameObject* d = s.CreateGameObject("D");
        GameObject* e = s.CreateGameObject("E");
        a->transform->SetParent(r->transform, false);
        b->transform->SetParent(a->transform, false);
        c->transform->SetParent(a->transform, false);
        d->transform->SetParent(c->transform, false);
        e->transform->SetParent(r->transform, false);
        std::string seen;
        CHECK(r->ForEachDescendant([&](GameObject& g) { seen += g.name; }));
        CHECK(seen == "ABCDE");                                       // parents first, siblings in order, not R itself
        seen.clear();
        r->ForEachDescendant([&](GameObject& g) { seen += g.name; }, true, /*includeSelf=*/true);
        CHECK(seen == "RABCDE");
        seen.clear();
        const bool finished = r->ForEachDescendant([&](GameObject& g) { seen += g.name; return g.name != "C"; });   // stop at C
        CHECK(!finished && seen == "ABC");
        seen.clear();
        CHECK(r->ForEachDescendant([&](GameObject& g) { seen += g.name; return true; }));
        CHECK(seen == "ABCDE");
        c->active = false;
        seen.clear();
        r->ForEachDescendant([&](GameObject& g) { seen += g.name; }, /*includeInactive=*/false);
        CHECK(seen == "ABE");                                         // C and everything below it are skipped
        seen.clear();
        r->ForEachDescendant([&](GameObject& g) { seen += g.name; }, true);
        CHECK(seen == "ABCDE");                                       // by default every object
        r->active = false;
        seen.clear();
        CHECK(r->ForEachDescendant([&](GameObject& g) { seen += g.name; }, false, true) && seen.empty());   // a switched-off start visits nothing
        r->active = true;
        // a child added to the object just visited is visited too
        int made = 0;
        std::string order;
        r->ForEachDescendant([&](GameObject& g) {
            order += g.name;
            if (g.name == "E" && made == 0) { made = 1; GameObject* n = s.CreateGameObject("N"); n->transform->SetParent(g.transform, false); }
        });
        CHECK(order == "ABCDEN");
        // a leaf and an object with no children: nothing, and the walk completed
        CHECK(d->ForEachDescendant([&](GameObject&) { return false; }));
        // a very deep hierarchy needs no recursion
        Scene deep("Deep");
        GameObject* top = deep.CreateGameObject("0");
        GameObject* last = top;
        for (int i = 1; i < 100000; ++i) {
            GameObject* n = deep.CreateGameObject("n");
            n->transform->SetParent(last->transform, false);
            last = n;
        }
        int count = 0;
        top->ForEachDescendant([&](GameObject&) { ++count; });
        CHECK(count == 99999);
        int stopAfter = 0;
        CHECK(!top->ForEachDescendant([&](GameObject&) { return ++stopAfter < 500; }));
        CHECK(stopAfter == 500);
    }

    // =====================================================================================================
    RUN_SUITE("FindByPath with duplicate names and names that carry a slash");
    {
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        GameObject* door1 = s.CreateGameObject("Door");
        GameObject* door2 = s.CreateGameObject("Door");
        GameObject* key = s.CreateGameObject("Key");
        GameObject* lock = s.CreateGameObject("Lock");
        door1->transform->SetParent(root->transform, false);
        door2->transform->SetParent(root->transform, false);
        key->transform->SetParent(door1->transform, false);
        lock->transform->SetParent(door2->transform, false);
        CHECK(s.FindByPath("Root/Door/Key") == key);
        CHECK(s.FindByPath("Root/Door/Lock") == lock);               // the first Door has no Lock: the second is tried
        CHECK(s.FindByPath("Root/Door") == door1);                    // the first of the two
        CHECK(s.FindAllByPath("Root/Door") == (std::vector<GameObject*>{door1, door2}));
        CHECK(s.FindByPath("Root/Door/Nothing") == nullptr);
        CHECK(s.FindByPath("Root/Door/Key/Extra") == nullptr);
        CHECK(s.FindByPath("Roo") == nullptr && s.FindByPath("Root2") == nullptr && s.FindByPath("Root/Do") == nullptr);
        CHECK(s.Find("Root/Door/Lock") == lock);                      // Find reads a path too
        // slashes at the ends and doubled ones count as one
        CHECK(s.FindByPath("/Root//Door/Key/") == key);
        CHECK(s.FindByPath("") == nullptr && s.FindByPath("/") == nullptr && s.FindByPath("///") == nullptr);
        // a second root of the same name is a root too
        GameObject* root2 = s.CreateGameObject("Root");
        GameObject* extra = s.CreateGameObject("Extra");
        extra->transform->SetParent(root2->transform, false);
        CHECK(s.FindByPath("Root/Extra") == extra);
        CHECK(s.FindAllByPath("Root") == (std::vector<GameObject*>{root, root2}));
        // names with a slash in them
        GameObject* slashed = s.CreateGameObject("a/b");
        GameObject* inside = s.CreateGameObject("c");
        inside->transform->SetParent(slashed->transform, false);
        CHECK(s.FindByPath("a/b") == slashed);
        CHECK(s.FindByPath("a/b/c") == inside);
        CHECK(s.Find("a/b") == slashed);                              // literally named: the plain name lookup
        GameObject* a = s.CreateGameObject("a");
        GameObject* b = s.CreateGameObject("b");
        b->transform->SetParent(a->transform, false);
        CHECK(s.FindByPath("a/b") == slashed);                        // scene order: the object called "a/b" comes first
        CHECK(s.FindAllByPath("a/b") == (std::vector<GameObject*>{slashed, b}));
        CHECK(slashed->Path() == "a/b" && inside->Path() == "a/b/c");
        // an empty name cannot be addressed; a path of other characters
        GameObject* empty = s.CreateGameObject("");
        CHECK(s.FindByPath("//") == nullptr);
        (void)empty;
        GameObject* odd = s.CreateGameObject("Wave 2 (x) [1] \xE2\x9C\x93 \xE6\x95\xB5");
        GameObject* oddKid = s.CreateGameObject("\xE5\xAD\x90");
        oddKid->transform->SetParent(odd->transform, false);
        CHECK(s.FindByPath("Wave 2 (x) [1] \xE2\x9C\x93 \xE6\x95\xB5/\xE5\xAD\x90") == oddKid);
        // a very long path through a deep chain (no recursion)
        Scene deep("Deep");
        GameObject* last = deep.CreateGameObject("n");
        for (int i = 1; i < 20000; ++i) {
            GameObject* n = deep.CreateGameObject("n");
            n->transform->SetParent(last->transform, false);
            last = n;
        }
        std::string path = "n";
        for (int i = 1; i < 20000; ++i) path += "/n";
        CHECK(deep.FindByPath(path) == last);
        CHECK(deep.FindByPath(path + "/n") == nullptr);
    }

    // =====================================================================================================
    RUN_SUITE("FindNearest: names are read from where the asking object sits");
    {
        Scene s("T");
        GameObject* root1 = s.CreateGameObject("Root1");
        GameObject* x1 = s.CreateGameObject("X");
        GameObject* mid = s.CreateGameObject("Mid");
        GameObject* y = s.CreateGameObject("Y");
        GameObject* x2 = s.CreateGameObject("X");
        GameObject* root2 = s.CreateGameObject("Root2");
        GameObject* x3 = s.CreateGameObject("X");
        x1->transform->SetParent(root1->transform, false);
        mid->transform->SetParent(root1->transform, false);
        y->transform->SetParent(mid->transform, false);
        x2->transform->SetParent(mid->transform, false);
        x3->transform->SetParent(root2->transform, false);
        CHECK(s.Find("X") == x1);                                     // the scene-wide answer: the first
        CHECK(FindNearest(s, "X", y) == x2);                          // Mid's own X, not the one next to Mid
        CHECK(FindNearest(s, "X", mid) == x2);
        CHECK(FindNearest(s, "X", root1) == x1);
        CHECK(FindNearest(s, "X", root2) == x3);                      // the other hierarchy has its own
        CHECK(FindNearest(s, "X", x3) == x3);                         // an object is its own nearest match
        CHECK(FindNearest(s, "Mid", y) == mid);                       // an ancestor counts
        CHECK(FindNearest(s, "Y", x1) == y);                          // found one level up, below the sibling Mid
        CHECK(FindNearest(s, "Root2", y) == root2);                   // not in this hierarchy: the scene-wide lookup
        CHECK(FindNearest(s, "Nothing", y) == nullptr && FindNearest(s, "", y) == nullptr);
        CHECK(FindNearest(s, "X", nullptr) == x1);
        CHECK(FindNearest(s, "Root1/Mid", y) == mid);                 // a path is not a name: Find reads it
        Scene other("Other");
        GameObject* foreign = other.CreateGameObject("X");
        CHECK(FindNearest(s, "X", foreign) == x1);                    // an object of another scene is no starting point
        s.Destroy(y);
        s.FlushDestroyed();
        CHECK(FindNearest(s, "X", y) == x1);                          // a destroyed one neither
        // deep and wide: a name at the top of a long chain, asked for from the bottom
        Scene deep("Deep");
        GameObject* top = deep.CreateGameObject("Top");
        GameObject* last = top;
        for (int i = 0; i < 50000; ++i) {
            GameObject* n = deep.CreateGameObject("n");
            n->transform->SetParent(last->transform, false);
            last = n;
        }
        CHECK(FindNearest(deep, "Top", last) == top);
    }
    {
        // A clone of a body with a joint to its partner: the copy's joint holds the copy's partner, not the original's.
        Scene s("T");
        GameObject* rig = s.CreateGameObject("Rig");
        GameObject* a = s.CreateGameObject("A");
        a->transform->SetParent(rig->transform, false);
        a->transform->localPosition = {1.0f, 0.0f, 0.0f};
        a->AddComponent<Rigidbody2D>()->gravityScale = 0.0f;
        auto* joint = a->AddComponent<Joint2D>();
        joint->mode = static_cast<int>(Joint2D::Mode::Pin);
        joint->connectedBody = "B";
        GameObject* b = s.CreateGameObject("B");
        b->transform->SetParent(rig->transform, false);
        auto* rbB = b->AddComponent<Rigidbody2D>();
        rbB->bodyType = Rigidbody2D::BodyType::Kinematic;
        rbB->gravityScale = 0.0f;
        GameObject* copy = s.Instantiate(*rig, Vec3{0.0f, 20.0f, 0.0f});
        CHECK(copy != nullptr);
        GameObject* a2 = copy->transform->Find("A")->gameObject;
        CHECK(a2 != a && copy->transform->Find("B")->gameObject != b);
        rbB->velocity = {2.0f, 0.0f};                                 // the ORIGINAL partner drives away
        for (int i = 0; i < 30; ++i) s.Update(0.02f);
        CHECK(a->transform->Position().x > 1.5f);                     // the original follows its partner (the joint works)
        CHECK(std::fabs(a2->transform->Position().x - 1.0f) < 0.05f);   // the copy stays with its own, which has not moved
        CHECK(std::fabs(a2->transform->Position().y - 20.0f) < 0.05f);
    }

    // =====================================================================================================
    RUN_SUITE("Duplicate and Instantiate while a Clear is waiting for the end of the frame");
    {
        struct DupAndClear : public Behaviour {
            GameObject* copy = nullptr;
            GameObject* spawned = nullptr;
            bool pendingSeen = false;
            bool done = false;
            void Update(float) override {
                if (done) return;
                done = true;
                Scene* sc = gameObject->scene();
                sc->Clear();                                           // queued: the objects are in use
                pendingSeen = sc->HasPendingClear();
                copy = sc->Duplicate(*gameObject);                     // made now, goes with the old scene
                spawned = sc->Instantiate(*gameObject, Vec3{1.0f, 2.0f, 3.0f});
            }
        };
        Scene s("T");
        GameObject* root = s.CreateGameObject("Root");
        GameObject* kid = s.CreateGameObject("Kid");
        kid->transform->SetParent(root->transform, false);
        root->AddComponent<DupAndClear>();
        s.Update(0.016f);                                              // the component is freed with the scene: not read again
        CHECK(s.Objects().empty() && !s.HasPendingClear());
        GameObject* fresh = s.CreateGameObject("After");               // the emptied scene works as a new one
        s.Update(0.016f);
        CHECK(s.Objects().size() == 1 && s.Find("After") == fresh);
        // Outside a frame the same calls work as always
        GameObject* r2 = s.CreateGameObject("Sub");
        GameObject* k2 = s.CreateGameObject("SubKid");
        k2->transform->SetParent(r2->transform, false);
        GameObject* twin = s.Duplicate(*r2);
        CHECK(twin != nullptr && twin->name == "Sub (1)" && twin->transform->ChildCount() == 1);
    }

    // =====================================================================================================
    RUN_SUITE("Scene::Stats on a huge scene");
    {
        Scene s("Huge");
        const int flat = 100000, chainLength = 50000;
        for (int i = 0; i < flat; ++i) {
            GameObject* g = s.CreateGameObject("f");
            g->AddComponent<Mover>();
            g->AddComponent<Lifetime>();
        }
        GameObject* top = s.CreateGameObject("top");
        GameObject* last = top;
        for (int i = 1; i < chainLength; ++i) {
            GameObject* n = s.CreateGameObject("c");
            n->transform->SetParent(last->transform, false);
            last = n;
        }
        top->active = false;                                          // the whole chain below it is off
        const std::uint64_t walk = GameObject::ActiveWalkSteps();
        const SceneGraphStats st = s.Stats();
        CHECK(st.objects == flat + chainLength);
        CHECK(st.roots == flat + 1);
        CHECK(st.maxDepth == chainLength - 1);
        CHECK(st.inactive == chainLength && st.activeInHierarchy == flat);
        CHECK(st.components == (flat + chainLength) + 2 * flat);
        bool movers = false;
        for (const auto& t : st.byType) if (t.type == "Mover") movers = t.count == flat;
        CHECK(movers);
        CHECK(GameObject::ActiveWalkSteps() - walk <= 2u * static_cast<std::uint64_t>(flat + chainLength));   // linear, not per level
        const std::uint64_t walk2 = GameObject::ActiveWalkSteps();
        (void)s.Stats();
        CHECK(GameObject::ActiveWalkSteps() == walk2);                // a second look costs no walk at all
        CHECK(!st.ToString(3).empty() && st.Lines(2).size() >= 3);
    }

    // =====================================================================================================
    RUN_SUITE("Mover and Spinner under a parent that is destroyed");
    {
        struct Killer : public Behaviour {
            GameObject* target = nullptr;
            void Update(float) override { if (target) { gameObject->scene()->Destroy(target); target = nullptr; } }
        };
        Scene s("T");
        GameObject* killer = s.CreateGameObject("Killer");           // first in the list: it runs before the child
        auto* k = killer->AddComponent<Killer>();
        GameObject* parent = s.CreateGameObject("P");
        parent->transform->localPosition = {5.0f, 0.0f, 0.0f};
        GameObject* child = s.CreateGameObject("C");
        child->transform->SetParent(parent->transform, false);
        auto* mover = child->AddComponent<Mover>();
        mover->velocity = {1.0f, 0.0f, 0.0f};
        mover->space = Space::World;
        auto* spinner = child->AddComponent<Spinner>();
        spinner->angularVelocity = {0.0f, 90.0f, 0.0f};
        s.Update(0.5f);                                               // runs once with the parent alive
        const Vec3 at = child->transform->Position();
        CHECK(NearV(at, {5.5f, 0.0f, 0.0f}));
        k->target = parent;
        s.Update(0.5f);                                               // the parent is queued before the child's Update; both go at the end
        CHECK(parent->IsDestroyed() && child->IsDestroyed());
        CHECK(s.Find("P") == nullptr && s.Find("C") == nullptr);
        const Vec3 frozen = child->transform->Position();
        CHECK(std::isfinite(frozen.x) && NearV(frozen, {6.0f, 0.0f, 0.0f}));   // moved once more, then froze where it was
        s.Update(0.5f);
        s.Update(0.5f);
        CHECK(NearV(child->transform->Position(), frozen));            // nothing drives a shell
        CHECK(child->GetComponent<Mover>() == nullptr && child->GetComponent<Spinner>() == nullptr);
        // nothing can be parented under the shell, and the shell is no parent for a Mover that asks
        GameObject* late = s.CreateGameObject("Late");
        late->AddComponent<Mover>()->velocity = {1.0f, 0.0f, 0.0f};
        CHECK(!late->transform->SetParent(parent->transform, true));
        s.Update(1.0f);
        CHECK(NearV(late->transform->Position(), {1.0f, 0.0f, 0.0f}));
    }
    {
        // An object adopted while its new parent is being destroyed carries on as a root, with its Mover running.
        struct Adopter : public Behaviour {
            GameObject* adoptee = nullptr;
            void OnDestroy() override { if (adoptee) adoptee->transform->SetParent(gameObject->transform, true); }
        };
        Scene s("T");
        GameObject* parent = s.CreateGameObject("P");
        parent->transform->localPosition = {2.0f, 0.0f, 0.0f};
        auto* ad = parent->AddComponent<Adopter>();
        GameObject* stray = s.CreateGameObject("Stray");
        stray->transform->localPosition = {10.0f, 0.0f, 0.0f};
        auto* mover = stray->AddComponent<Mover>();
        mover->velocity = {2.0f, 0.0f, 0.0f};
        mover->space = Space::World;
        ad->adoptee = stray;
        s.Update(0.1f);
        s.Destroy(parent);
        s.Update(0.0f);
        CHECK(parent->IsDestroyed() && !stray->IsDestroyed());
        CHECK(stray->transform->Parent() == nullptr);                  // not left under a frozen shell
        const Vec3 before = stray->transform->Position();
        s.Update(1.0f);
        CHECK(NearV(stray->transform->Position(), before + Vec3{2.0f, 0.0f, 0.0f}));
    }

    // =====================================================================================================
    RUN_SUITE("tag and layer names in other languages");
    {
        // 64 characters of Japanese are 192 bytes: the limit counts characters.
        std::string jp64, jp65;
        for (int i = 0; i < 64; ++i) jp64 += "\xE6\x95\xB5";          // U+6575
        jp65 = jp64 + "\xE6\x95\xB5";
        CHECK(Tags::IsValidName(jp64) && !Tags::IsValidName(jp65));
        CHECK(Tags::WhyInvalid(jp65).find("64") != std::string::npos);
        CHECK(Tags::IsValidName("\xC3\x89nemy") && Tags::IsValidName("\xF0\x9F\x91\xBE"));   // an accent, an emoji (4 bytes)
        CHECK(!Tags::IsValidName(std::string(300, '\x80')));         // bytes that are no characters cannot grow a name without end
        CHECK(!Tags::IsValidName(std::string("a\x01" "b")));

        std::string jp32, jp33;
        for (int i = 0; i < 32; ++i) jp32 += "\xE6\x95\xB5";
        jp33 = jp32 + "\xE6\x95\xB5";
        CHECK(Layers::IsValidName(jp32) && !Layers::IsValidName(jp33));
        CHECK(Layers::SetName(9, "\xE3\x83\x97\xE3\x83\xAC\xE3\x82\xA4\xE3\x83\xA4\xE3\x83\xBC" "\xE5\xBC\xBE"));   // eight characters, 24 bytes
        CHECK(Layers::NameToLayer("\xE3\x83\x97\xE3\x83\xAC\xE3\x82\xA4\xE3\x83\xA4\xE3\x83\xBC" "\xE5\xBC\xBE") == 9);
        // case does not matter for accents, Greek and Cyrillic either
        CHECK(Layers::SetName(10, "\xC3\x89nemy"));                    // "Enemy" with an accented capital E
        CHECK(Layers::NameToLayer("\xC3\xA9nemy") == 10);              // lower case
        CHECK(Layers::NameToLayer("\xC3\x89NEMY") == 10);
        CHECK(!Layers::SetName(11, "\xC3\xA9nemy"));                    // another layer already has it, case aside
        CHECK(Layers::SetName(11, "\xD0\x98\xD0\xB3\xD1\x80\xD0\xBE\xD0\xBA"));       // Cyrillic "Igrok"
        CHECK(Layers::NameToLayer("\xD0\xB8\xD0\xB3\xD0\xA0\xD0\xBE\xD0\xBA") == 11);   // mixed case
        CHECK(Layers::SetName(12, "\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1"));       // Greek
        CHECK(Layers::NameToLayer("\xCE\xB1\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1") == 12);
        CHECK(Layers::MaskFromList("\xC3\x89NEMY, \xD0\xB8\xD0\xB3\xD1\x80\xD0\xBE\xD0\xBA") == ((1u << 10) | (1u << 11)));
        CHECK(Layers::NameToLayer("ENEMY") == -1);                    // "E" is not "E with an accent"
        CHECK(Layers::NameToLayer("DEFAULT") == 0 && Layers::NameToLayer("ui") == 5);   // plain ASCII as always
        // names that are not valid UTF-8 are kept as they are and still match themselves
        CHECK(Layers::SetName(13, "bad\xFF" "name") && Layers::NameToLayer("BAD\xFF" "NAME") == 13);

        // a project file with such names reads back what it wrote
        const std::string written = ProjectLists::Write();
        Layers::ResetNames();
        Tags::Custom().clear();
        CHECK(Layers::NameToLayer("\xC3\xA9nemy") == -1);
        ProjectLists::Apply(written);
        CHECK(Layers::NameToLayer("\xC3\xA9nemy") == 10 && Layers::NameToLayer("\xCE\xB1\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1") == 12);
        Layers::ResetNames();
        ProjectLists::Reset();

        // 10000 objects, every one with its own tag in another language
        Scene s("T");
        for (int i = 0; i < 10000; ++i) {
            GameObject* g = s.CreateGameObject("o");
            g->tag = std::string("\xE6\x95\xB5_") + std::to_string(i);
        }
        CHECK(s.CountWithTag("\xE6\x95\xB5_4242") == 1);
        CHECK(s.FindWithTag("\xE6\x95\xB5_9999") != nullptr && s.FindWithTag("\xE6\x95\xB5_10000") == nullptr);
        CHECK(s.UnlistedTags().size() == 10000);
        CHECK(s.ReplaceTag("\xE6\x95\xB5_7", "\xE5\x91\xB3\xE6\x96\xB9") == 1 && s.CountWithTag("\xE5\x91\xB3\xE6\x96\xB9") == 1);
        CHECK(s.FindWithTag("\xE6\x95\xB5_7") == nullptr);
        // the same tag on every object and a case difference are two different tags
        for (const auto& o : s.Objects()) o->tag = "Enemy";
        CHECK(s.CountWithTag("Enemy") == 10000 && s.CountWithTag("enemy") == 0);
    }

    TEST_MAIN_RESULT();
}
