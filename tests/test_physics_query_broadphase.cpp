// 3D scene queries through the broad phase (Physics3D: rays, overlaps, shape casts, closest point).
//
//  1. Same answers: on seeded random scenes (boxes, spheres, capsules, cylinders, cones, hulls, compounds, meshes,
//     a plane, a heightfield, a terrain; triggers, layers, ignored objects, children, bodies) every query is asked
//     three ways and the three answers must be the same, bit for bit and in the same order:
//       a) the scene's own Physics3D (its broad phase is kept up to date as things change),
//       b) the same with Physics3D::QueryFullScan() on: every collider goes to the exact tests,
//       c) a Physics3D made for that one question (a broad phase built from nothing).
//     Between rounds objects move, turn, grow, change layer, are switched off, deleted and added, and physics steps.
//  2. Less work: on 2,000 colliders a query looks at a small part of the scene, and a collider's box is only worked out
//     again when the collider changed.
//  3. ClosestPoint builds the height grid of a terrain or a heightfield once, and again only when its data changed.
//
// The line "digest" is a hash of every answer of part 1; it is printed so a run before a change and a run after it can
// be compared by hand.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Random.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {

std::uint64_t g_digest = 0xcbf29ce484222325ull;
void Mix(const void* p, std::size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) { g_digest ^= b[i]; g_digest *= 0x100000001b3ull; }
}
void MixF(float f) { Mix(&f, sizeof f); }
void MixV(const Vec3& v) { MixF(v.x); MixF(v.y); MixF(v.z); }
void MixGo(const GameObject* g) {
    if (!g) { Mix("-", 1); return; }
    Mix(g->name.data(), g->name.size());
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool SameV(const Vec3& a, const Vec3& b) { return SameBits(a.x, b.x) && SameBits(a.y, b.y) && SameBits(a.z, b.z); }
bool SameHit(const RaycastHit3D& a, const RaycastHit3D& b) {
    return a.hit == b.hit && a.collider == b.collider && a.gameObject == b.gameObject && SameBits(a.distance, b.distance) &&
           SameV(a.point, b.point) && SameV(a.normal, b.normal);
}
bool SameHits(const std::vector<RaycastHit3D>& a, const std::vector<RaycastHit3D>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!SameHit(a[i], b[i])) return false;
    return true;
}
bool SameNear(const NearestSolid3D& a, const NearestSolid3D& b) {
    return a.hit == b.hit && a.collider == b.collider && a.gameObject == b.gameObject && SameBits(a.distance, b.distance) &&
           SameV(a.point, b.point);
}
void MixHit(const RaycastHit3D& h) {
    Mix(&h.hit, 1);
    if (!h.hit) return;
    MixGo(h.gameObject); MixF(h.distance); MixV(h.point); MixV(h.normal);
}
void MixHits(const std::vector<RaycastHit3D>& v) { for (const RaycastHit3D& h : v) MixHit(h); Mix("|", 1); }

int g_asked = 0, g_withHits = 0;

// Ask one question the three ways. `ask` takes the Physics3D to ask; `same` compares two answers.
template <class Ask, class Same> auto Three(Scene& s, Ask ask, Same same) -> decltype(ask(s.physics3D())) {
    Physics3D::QueryFullScan() = false;
    auto viaBroad = ask(s.physics3D());
    Physics3D::QueryFullScan() = true;
    auto viaScan = ask(s.physics3D());
    Physics3D::QueryFullScan() = false;
    Physics3D fresh;
    auto viaFresh = ask(fresh);
    ++g_asked;
    CHECK(same(viaBroad, viaScan));
    CHECK(same(viaBroad, viaFresh));
    return viaBroad;
}

struct World {
    Scene scene{"QB"};
    std::vector<GameObject*> objs;   // live objects with a collider
    Random rng;
    int made = 0;
    explicit World(std::uint64_t seed) : rng(seed) { scene.physicsEnabled = false; }

    Vec3 Point(float r) { return {rng.Range(-r, r), rng.Range(-r * 0.4f, r * 0.4f), rng.Range(-r, r)}; }
    Vec3 Dir() {
        Vec3 d{rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f)};
        return d.SqrMagnitude() > 1e-4f ? d.Normalized() : Vec3{1, 0, 0};
    }
    Quat Rot() { return Quat::Euler(rng.Range(-180.0f, 180.0f), rng.Range(-180.0f, 180.0f), rng.Range(-180.0f, 180.0f)); }

    GameObject* Add(float area) {
        GameObject* g = scene.CreateGameObject("o" + std::to_string(made++));
        g->transform->localPosition = Point(area);
        if (rng.Range(0, 3) != 0) g->transform->localRotation = Rot();
        if (rng.Range(0, 4) == 0) g->transform->localScale = {rng.Range(0.5f, 2.0f), rng.Range(0.5f, 2.0f), rng.Range(0.5f, 2.0f)};
        Collider3D* c = nullptr;
        switch (rng.Range(0, 9)) {
        case 0: case 1: {
            auto* b = g->AddComponent<BoxCollider3D>();
            b->size = {rng.Range(0.3f, 3.0f), rng.Range(0.3f, 3.0f), rng.Range(0.3f, 3.0f)};
            c = b; break;
        }
        case 2: { auto* sp = g->AddComponent<SphereCollider3D>(); sp->radius = rng.Range(0.2f, 1.5f); c = sp; break; }
        case 3: {
            auto* cp = g->AddComponent<CapsuleCollider3D>();
            cp->radius = rng.Range(0.2f, 0.8f); cp->height = rng.Range(0.5f, 3.0f); cp->axis = rng.Range(0, 3);
            c = cp; break;
        }
        case 4: {
            auto* cy = g->AddComponent<CylinderCollider3D>();
            cy->radius = rng.Range(0.2f, 1.0f); cy->height = rng.Range(0.4f, 2.5f); cy->axis = rng.Range(0, 3);
            c = cy; break;
        }
        case 5: {
            auto* co = g->AddComponent<ConeCollider3D>();
            co->radius = rng.Range(0.3f, 1.0f); co->height = rng.Range(0.5f, 2.0f); co->axis = rng.Range(0, 3);
            c = co; break;
        }
        case 6: {
            auto* h = g->AddComponent<ConvexHullCollider3D>();
            std::vector<Vec3> pts;
            for (int i = 0; i < 9; ++i) pts.push_back({rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f)});
            h->SetPoints(pts);
            c = h; break;
        }
        case 7: {
            auto* cc = g->AddComponent<CompoundCollider3D>();
            cc->AddBox({0, 0.5f, 0}, {1.6f, 0.2f, 1.0f});
            cc->AddSphere({rng.Range(-1.0f, 1.0f), 0, 0}, 0.4f);
            cc->AddCapsule({0, -0.5f, 0.3f}, 0.2f, 1.2f, rng.Range(0, 3));
            c = cc; break;
        }
        default: {   // a triangle mesh
            g->AddComponent<MeshRenderer>()->mesh = rng.Range(0, 2) ? Mesh::Cube(rng.Range(0.6f, 2.0f)) : Mesh::Sphere(rng.Range(0.4f, 1.2f), 6, 8);
            c = g->AddComponent<MeshCollider3D>();
            break;
        }
        }
        c->layer = rng.Range(0, 5);
        c->isTrigger = rng.Range(0, 5) == 0;
        if (rng.Range(0, 6) == 0) c->offset = {rng.Range(-0.5f, 0.5f), rng.Range(-0.5f, 0.5f), rng.Range(-0.5f, 0.5f)};
        if (rng.Range(0, 8) == 0) c->contactOffset = rng.Range(0.0f, 0.2f);
        if (rng.Range(0, 5) == 0) {
            auto* rb = g->AddComponent<Rigidbody3D>();
            rb->bodyType = rng.Range(0, 2) == 0 ? Rigidbody3D::BodyType::Kinematic : Rigidbody3D::BodyType::Dynamic;
            rb->gravityScale = 0.0f;
        }
        if (!objs.empty() && rng.Range(0, 7) == 0) g->transform->SetParent(objs[(std::size_t)rng.Range(0, (int)objs.size())]->transform, true);
        objs.push_back(g);
        return g;
    }

    GameObject* Pick() { return objs.empty() ? nullptr : objs[(std::size_t)rng.Range(0, (int)objs.size())]; }

    // One change to the scene, of every kind a game makes between two queries.
    void Change(float area) {
        GameObject* g = Pick();
        if (!g) { Add(area); return; }
        Collider3D* c = g->GetComponent<Collider3D>();
        switch (rng.Range(0, 12)) {
        case 0: g->transform->localPosition = Point(area); break;
        case 1: g->transform->localPosition = g->transform->localPosition + Dir() * rng.Range(0.01f, 0.3f); break;   // a small move
        case 2: g->transform->localRotation = Rot(); break;
        case 3: g->transform->localScale = {rng.Range(0.4f, 2.5f), rng.Range(0.4f, 2.5f), rng.Range(0.4f, 2.5f)}; break;
        case 4:
            if (auto* b = dynamic_cast<BoxCollider3D*>(c)) b->size = {rng.Range(0.3f, 4.0f), rng.Range(0.3f, 4.0f), rng.Range(0.3f, 4.0f)};
            else if (auto* sp = dynamic_cast<SphereCollider3D*>(c)) sp->radius = rng.Range(0.2f, 2.5f);
            else if (auto* cp = dynamic_cast<CapsuleCollider3D*>(c)) { cp->radius = rng.Range(0.2f, 1.0f); cp->height = rng.Range(0.4f, 4.0f); cp->axis = rng.Range(0, 3); }
            else if (auto* co = dynamic_cast<ConeCollider3D*>(c)) { co->radius = rng.Range(0.2f, 1.5f); co->height = rng.Range(0.4f, 3.0f); co->axis = rng.Range(0, 3); }
            else if (auto* cc = dynamic_cast<CompoundCollider3D*>(c)) cc->AddBox({rng.Range(-2.0f, 2.0f), 0, 0}, {0.5f, 0.5f, 0.5f});
            else if (auto* mr = g->GetComponent<MeshRenderer>()) { mr->mesh = Mesh::Cube(rng.Range(0.5f, 3.0f)); MeshCollider3D::AdvanceEpoch(); }
            break;
        case 5: if (c) c->layer = rng.Range(0, 5); break;
        case 6: if (c) c->isTrigger = !c->isTrigger; break;
        case 7: if (c) c->enabled = !c->enabled; break;
        case 8: g->active = !g->active; break;
        case 9: if (c) c->offset = {rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f)}; break;
        case 10: {   // delete it (and whatever hangs under it)
            scene.Destroy(g);
            scene.FlushDestroyed();
            std::vector<GameObject*> left;   // what is still there (its children went with it)
            for (Collider3D* k : scene.FindObjectsOfType<Collider3D>())
                if (k->gameObject->name[0] == 'o') left.push_back(k->gameObject);
            objs.swap(left);
            break;
        }
        default: Add(area); break;
        }
    }

    // One round of every kind of query, each asked the three ways.
    void Ask(float area) {
        Scene& s = scene;
        const std::uint32_t mask = rng.Range(0, 2) == 0 ? Physics3D::kAllLayers : (std::uint32_t)rng.Range(1, 63);
        const bool trig = rng.Range(0, 2) == 0;
        GameObject* ignore = rng.Range(0, 2) == 0 ? Pick() : nullptr;
        GameObject* tree = rng.Range(0, 5) == 0 ? Pick() : nullptr;
        Physics3D::IgnoreHierarchy own(tree);
        const Vec3 o = Point(area * 1.1f), d = Dir();
        const float reach = rng.Range(0, 3) == 0 ? 1e9f : rng.Range(0.5f, area * 2.0f);
        const float r = rng.Range(0.1f, 2.5f);
        const Vec3 half{rng.Range(0.1f, 2.0f), rng.Range(0.1f, 2.0f), rng.Range(0.1f, 2.0f)};
        const Quat rot = Rot();
        const Vec3 a = o - d * rng.Range(0.0f, 1.5f), b = o + Dir() * rng.Range(0.0f, 1.5f);
        const float castReach = rng.Range(0, 4) == 0 ? 0.0f : rng.Range(0.5f, area);

        RaycastHit3D h = Three(s, [&](Physics3D& p) { return p.Raycast(s, o, d, reach, ignore, mask, trig); }, SameHit);
        MixHit(h); g_withHits += h.hit;
        MixHits(Three(s, [&](Physics3D& p) { return p.RaycastAll(s, o, d, reach, ignore, mask, trig); }, SameHits));
        MixHit(Three(s, [&](Physics3D& p) { return p.Linecast(s, o, o + d * 6.0f, ignore, mask, trig); }, SameHit));

        auto sameCols = [](const std::vector<Collider3D*>& x, const std::vector<Collider3D*>& y) { return x == y; };
        auto mixCols = [](const std::vector<Collider3D*>& v) { for (Collider3D* c : v) MixGo(c->gameObject); Mix("|", 1); };
        auto os = Three(s, [&](Physics3D& p) { return p.OverlapSphere(s, o, r, mask, trig, ignore); }, sameCols);
        mixCols(os); g_withHits += !os.empty();
        mixCols(Three(s, [&](Physics3D& p) { return p.OverlapBox(s, o, half, rot, mask, trig, ignore); }, sameCols));
        mixCols(Three(s, [&](Physics3D& p) { return p.OverlapCapsule(s, a, b, r * 0.5f, mask, trig, ignore); }, sameCols));

        h = Three(s, [&](Physics3D& p) { return p.SphereCast(s, o, d, r * 0.5f, castReach, ignore, mask, trig); }, SameHit);
        MixHit(h); g_withHits += h.hit;
        MixHit(Three(s, [&](Physics3D& p) { return p.BoxCast(s, o, half * 0.5f, d, rot, castReach, ignore, mask, trig); }, SameHit));
        MixHit(Three(s, [&](Physics3D& p) { return p.CapsuleCast(s, a, b, r * 0.4f, d, castReach, ignore, mask, trig); }, SameHit));
        MixHits(Three(s, [&](Physics3D& p) { return p.SphereCastAll(s, o, d, r * 0.5f, castReach, ignore, mask, trig); }, SameHits));
        MixHits(Three(s, [&](Physics3D& p) { return p.BoxCastAll(s, o, half * 0.5f, d, rot, castReach, ignore, mask, trig); }, SameHits));
        MixHits(Three(s, [&](Physics3D& p) { return p.CapsuleCastAll(s, a, b, r * 0.4f, d, castReach, ignore, mask, trig); }, SameHits));

        const float nearReach = rng.Range(0, 2) == 0 ? 1e9f : rng.Range(0.2f, area);
        NearestSolid3D n = Three(s, [&](Physics3D& p) { return p.ClosestPoint(s, o, nearReach, ignore, mask); }, SameNear);
        Mix(&n.hit, 1);
        if (n.hit) { MixGo(n.gameObject); MixF(n.distance); MixV(n.point); ++g_withHits; }
    }
};

GameObject* GridBox(Scene& s, int i, const Vec3& pos) {
    GameObject* g = s.CreateGameObject("g" + std::to_string(i));
    g->transform->localPosition = pos;
    g->AddComponent<BoxCollider3D>();
    return g;
}

} // namespace

int main() {
    RUN_SUITE("physics_query_broadphase");

    // ---- 1. Same answers on random scenes that keep changing -------------------------------------------------------
    for (int seed = 1; seed <= 6; ++seed) {
        World w((std::uint64_t)seed * 7919u);
        const float area = seed % 2 ? 12.0f : 30.0f;   // crowded scenes and sparse ones
        const int count = 40 + seed * 25;
        for (int i = 0; i < count; ++i) w.Add(area);
        if (seed % 2 == 0) w.scene.CreateGameObject("plane")->AddComponent<PlaneCollider3D>()->transform->localPosition = {0, -area * 0.4f, 0};
        if (seed % 3 == 0) {
            GameObject* hg = w.scene.CreateGameObject("field");
            hg->transform->localPosition = {3, -4, 2};
            auto* hf = hg->AddComponent<HeightfieldCollider3D>();
            hf->cellX = hf->cellZ = 2.0f;
            std::vector<float> hs;
            for (int i = 0; i < 81; ++i) hs.push_back(w.rng.Range(-1.0f, 1.5f));
            hf->SetHeights(9, 9, hs);
        }
        if (seed % 3 == 1) {
            GameObject* tg = w.scene.CreateGameObject("terrain");
            tg->transform->localPosition = {0, -5, 0};
            auto* t = tg->AddComponent<Terrain>();
            t->Resize(16); t->size = 40.0f;
            t->Generate(0, 3.0f, 0.1f, 2, (unsigned)seed);
        }
        w.scene.Start();
        for (int round = 0; round < 30; ++round) {
            w.Ask(area);
            w.Ask(area);
            const int changes = 1 + w.rng.Range(0, 5);
            for (int k = 0; k < changes; ++k) w.Change(area);
            if (seed >= 5 && round % 4 == 3) w.scene.physics3D().Step(w.scene, 1.0f / 60.0f);   // the engine world moves bodies too
        }
    }
    std::printf("part 1: %d questions asked three ways, %d of the sampled answers hit something\n", g_asked, g_withHits);
    std::printf("digest %016llx\n", (unsigned long long)g_digest);
    CHECK(g_asked > 4000);
    CHECK(g_withHits > 300);   // the questions really find things (an empty scene would agree with itself too)

    // Edge cases: a shape that only just touches, and one that only just does not; a ray along a face; a query far away.
    {
        Scene s("Edges");
        s.physicsEnabled = false;
        GridBox(s, 0, {0, 0, 0});
        s.CreateGameObject("ball")->AddComponent<SphereCollider3D>()->transform->localPosition = {5, 0, 0};
        s.Start();
        auto sameCols = [](const std::vector<Collider3D*>& x, const std::vector<Collider3D*>& y) { return x == y; };
        for (float gap : {-1e-3f, -1e-5f, 0.0f, 1e-5f, 1e-3f}) {
            Three(s, [&](Physics3D& p) { return p.OverlapSphere(s, {1.5f + gap, 0, 0}, 1.0f); }, sameCols);
            Three(s, [&](Physics3D& p) { return p.OverlapSphere(s, {5.0f, 1.5f + gap, 0}, 1.0f); }, sameCols);
            Three(s, [&](Physics3D& p) { return p.OverlapBox(s, {0, 1.0f + gap, 0}, {0.5f, 0.5f, 0.5f}); }, sameCols);
            Three(s, [&](Physics3D& p) { return p.Raycast(s, {-5, 0.5f + gap, 0}, {1, 0, 0}, 100.0f); }, SameHit);
            Three(s, [&](Physics3D& p) { return p.SphereCast(s, {-5, 1.0f + gap, 0}, {1, 0, 0}, 0.5f, 100.0f); }, SameHit);
        }
        CHECK(s.physics3D().OverlapSphere(s, {1.4f, 0, 0}, 1.0f).size() == 1);
        CHECK(s.physics3D().OverlapSphere(s, {1.6f, 0, 0}, 1.0f).empty());
        CHECK(s.physics3D().OverlapSphere(s, {4000, 0, 0}, 1.0f).empty());
        CHECK(s.physics3D().Raycast(s, {-5, 0, 0}, {1, 0, 0}, 100.0f).gameObject->name == "g0");
        CHECK(!s.physics3D().Raycast(s, {-5, 0, 0}, {1, 0, 0}, 4.0f).hit);
        CHECK_NEAR(s.physics3D().ClosestPoint(s, {2.5f, 0, 0}).distance, 2.0f, 1e-4);
    }

    // ---- 2. Less work on 2,000 colliders ---------------------------------------------------------------------------
    {
        Scene s("Many");
        s.physicsEnabled = false;
        const int side = 13, n = 2000;
        std::vector<GameObject*> boxes;
        for (int i = 0; i < n; ++i)
            boxes.push_back(GridBox(s, i, {(float)(i % side) * 3.0f, (float)((i / side) % side) * 3.0f, (float)(i / (side * side)) * 3.0f}));
        s.Start();
        Physics3D& p = s.physics3D();
        Random rng(99);
        auto at = [&]() { return Vec3{rng.Range(0.0f, 36.0f), rng.Range(0.0f, 36.0f), rng.Range(0.0f, 33.0f)}; };
        p.OverlapSphere(s, {0, 0, 0}, 1.0f);   // the first query looks at everything once
        const int calls = 200;

        auto perQuery = [&](const char* what, std::uint64_t most) {
            const PhysicsQueryStats& q = p.QueryStats();
            const double considered = (double)q.considered / (double)(q.queries ? q.queries : 1);
            const double tests = (double)q.shapeTests / (double)(q.queries ? q.queries : 1);
            std::printf("  %-14s %4llu calls: %8.1f colliders looked at per call, %6.1f exact tests per call, %llu boxes worked out again\n",
                        what, (unsigned long long)q.queries, considered, tests, (unsigned long long)q.boundsUpdates);
            CHECK(q.queries >= (std::uint64_t)calls);
            CHECK(q.considered <= most * q.queries);   // a small part of the 2,000 (the old code looked at all of them)
            CHECK(q.shapeTests <= most * q.queries);
            CHECK(q.boundsUpdates == 0);               // nothing changed: no box is worked out again
        };
        std::printf("part 2: %d box colliders\n", n);
        int found = 0;
        p.ResetQueryStats();
        for (int i = 0; i < calls; ++i) found += (int)p.OverlapSphere(s, at(), 2.0f).size();
        perQuery("OverlapSphere", 60);
        CHECK(found > calls / 2);
        p.ResetQueryStats();
        for (int i = 0; i < calls; ++i) found += (int)p.OverlapBox(s, at(), {1.5f, 1.5f, 1.5f}).size();
        perQuery("OverlapBox", 60);
        p.ResetQueryStats();
        for (int i = 0; i < calls; ++i) found += p.Raycast(s, at(), Vec3{0.3f, 1.0f, 0.2f}, 8.0f).hit;
        perQuery("Raycast", 60);
        p.ResetQueryStats();
        for (int i = 0; i < calls; ++i) found += p.SphereCast(s, at(), Vec3{1.0f, 0.2f, 0.1f}, 0.5f, 8.0f).hit;
        perQuery("SphereCast", 80);
        p.ResetQueryStats();
        for (int i = 0; i < calls; ++i) {
            const Vec3 foot = at();
            found += (int)p.CapsuleCastAll(s, foot, foot + Vec3{0, 1, 0}, 0.3f, Vec3{0, 0, 1}, 6.0f).size();
        }
        perQuery("CapsuleCastAll", 80);
        p.ResetQueryStats();
        for (int i = 0; i < calls; ++i) found += p.ClosestPoint(s, at()).hit;   // no reach given: the nearest of all 2,000
        perQuery("ClosestPoint", 120);

        // 50 boxes move: exactly those 50 boxes are worked out again, once, and the answers follow them.
        for (int i = 0; i < 50; ++i) boxes[(std::size_t)i * 7]->transform->localPosition = Vec3{-20.0f - (float)i * 3.0f, 0, 0};
        p.ResetQueryStats();
        CHECK(p.OverlapSphere(s, {-20, 0, 0}, 1.0f).size() == 1);
        CHECK(p.OverlapSphere(s, {0, 0, 0}, 1.0f).empty());              // box 0 left its old place
        CHECK(p.QueryStats().boundsUpdates == 50);
        CHECK(p.Raycast(s, {-23, 10, 0}, {0, -1, 0}, 50.0f).gameObject == boxes[7]);
        CHECK(p.QueryStats().boundsUpdates == 50);                       // (not again)
        // a resize, a layer change and a switch-off are seen by the next query
        boxes[7]->GetComponent<BoxCollider3D>()->size = {1, 8, 1};
        CHECK(p.OverlapSphere(s, {-23, 4.4f, 0}, 0.5f).size() == 1);
        CHECK(p.QueryStats().boundsUpdates == 51);
        boxes[7]->GetComponent<BoxCollider3D>()->layer = 3;
        CHECK(p.OverlapSphere(s, {-23, 4.4f, 0}, 0.5f, 1u << 0).empty());
        CHECK(p.OverlapSphere(s, {-23, 4.4f, 0}, 0.5f, 1u << 3).size() == 1);
        boxes[7]->GetComponent<BoxCollider3D>()->enabled = false;
        CHECK(p.OverlapSphere(s, {-23, 4.4f, 0}, 0.5f).empty());
        boxes[7]->GetComponent<BoxCollider3D>()->enabled = true;
        boxes[7]->transform->localPosition = {-23, 100, 0};              // moved while it was off? no: moved after it came back
        CHECK(p.OverlapSphere(s, {-23, 100, 0}, 0.5f).size() == 1);
        // deleting and adding objects keeps the rest of the broad phase (no box of an untouched collider is redone)
        s.Destroy(boxes[14]);
        s.FlushDestroyed();
        GridBox(s, n, {-20, 50, 0});
        p.ResetQueryStats();
        CHECK(p.OverlapSphere(s, {-20, 50, 0}, 1.0f).size() == 1);
        CHECK(p.OverlapSphere(s, {-26, 0, 0}, 1.0f).empty());            // boxes[14] stood here
        CHECK(p.QueryStats().boundsUpdates <= 2);
        (void)found;
    }

    // ---- 3. ClosestPoint keeps the height grid of a terrain and of a heightfield -----------------------------------
    {
        Scene s("Grids");
        s.physicsEnabled = false;
        GameObject* tg = s.CreateGameObject("terrain");
        auto* t = tg->AddComponent<Terrain>();
        t->Resize(32); t->size = 64.0f; t->Flatten(0.0f);
        GameObject* hg = s.CreateGameObject("field");
        hg->transform->localPosition = {200, 0, 0};
        auto* hf = hg->AddComponent<HeightfieldCollider3D>();
        hf->cellX = hf->cellZ = 1.0f;
        hf->SetHeights(17, 17, std::vector<float>(17 * 17, 1.0f));
        s.Start();
        Physics3D& p = s.physics3D();
        auto freshAnswer = [&](const Vec3& at) { Physics3D f; return f.ClosestPoint(s, at); };

        p.ResetQueryStats();
        const int calls = 100;
        for (int i = 0; i < calls; ++i) {
            CHECK_NEAR(p.ClosestPoint(s, {0.3f * (float)(i % 10), 5.0f, 0.1f}).distance, 5.0f, 1e-3);
            CHECK_NEAR(p.ClosestPoint(s, {200.0f, 4.0f, 0.2f * (float)(i % 10)}).distance, 3.0f, 1e-3);
        }
        std::printf("part 3: %d ClosestPoint calls over a terrain and a heightfield built %llu height grids\n", calls * 2,
                    (unsigned long long)p.QueryStats().gridBuilds);
        CHECK(p.QueryStats().gridBuilds <= 2);   // one per grid (the old code built 2 per call)

        // an edit of the heights is seen by the very next call, and costs one build
        p.ResetQueryStats();
        for (int z = 0; z < t->Dim(); ++z)
            for (int x = 0; x < t->Dim(); ++x) t->SetHeight(x, z, 2.0f);
        NearestSolid3D up = p.ClosestPoint(s, {0.5f, 5.0f, 0.5f});
        CHECK_NEAR(up.distance, 3.0f, 1e-3);
        CHECK(SameNear(up, freshAnswer({0.5f, 5.0f, 0.5f})));
        t->heights[(std::size_t)(16 * t->Dim() + 16)] = 4.0f;            // a direct write to one sample (the middle vertex)
        NearestSolid3D peak = p.ClosestPoint(s, {0.0f, 5.0f, 0.0f});
        CHECK_NEAR(peak.distance, 1.0f, 1e-3);
        CHECK(SameNear(peak, freshAnswer({0.0f, 5.0f, 0.0f})));
        CHECK(p.QueryStats().gridBuilds == 2);
        hf->SetHeights(17, 17, std::vector<float>(17 * 17, 2.5f));
        NearestSolid3D field = p.ClosestPoint(s, {200.0f, 4.0f, 0.0f});
        CHECK_NEAR(field.distance, 1.5f, 1e-3);
        CHECK(SameNear(field, freshAnswer({200.0f, 4.0f, 0.0f})));
        hf->heightScale = 2.0f;                                          // a setting, not the data
        CHECK_NEAR(p.ClosestPoint(s, {200.0f, 6.0f, 0.0f}).distance, 1.0f, 1e-3);
        tg->transform->localScale = {1, 0.5f, 1};                        // the object's scale is part of the grid
        NearestSolid3D squashed = p.ClosestPoint(s, {10.0f, 5.0f, 10.0f});
        CHECK_NEAR(squashed.distance, 4.0f, 1e-3);
        CHECK(SameNear(squashed, freshAnswer({10.0f, 5.0f, 10.0f})));
        tg->transform->localPosition = {0, -1, 0};                       // a move is not a new grid
        const std::uint64_t before = p.QueryStats().gridBuilds;
        CHECK_NEAR(p.ClosestPoint(s, {10.0f, 5.0f, 10.0f}).distance, 5.0f, 1e-3);
        CHECK(p.QueryStats().gridBuilds == before);
    }

    TEST_MAIN_RESULT();
}
