// Broad phase (okay::broadphase): the sort-and-sweep pair finder must give EXACTLY the list the old all-pairs loop gave,
// in the same order, and a whole physics scene must move bit-for-bit the same with either one.
//  - Pairs() against PairsBrute() on many random box sets, including the awkward ones: identical boxes, touching boxes,
//    zero-size boxes, inverted boxes (min above max), one huge box over many small ones, boxes that must be ignored.
//  - A 3D scene with piles, triggers and a static floor, and a 2D scene, stepped with the sweep and with the brute
//    force switch: every body ends up at the exact same bits, and the contact callbacks fire the same number of times.
//  - Fast bodies falling through a thin floor (the tunnelled floor catch casts a vertical ray per falling body): the
//    grid of boxes that answers those rays must pick the same hit as Physics3D::Raycast (the brute force switch brings the
//    plain Raycast back): same bits at the end, and no body is left under the floor (so the catch really ran).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/Broadphase.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace okay;
using namespace okay::broadphase;

namespace {

struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (std::uint32_t)(s >> 33); }
    float Unit() { return (float)(Next() & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
    int Below(int n) { return (int)(Next() % (std::uint32_t)n); }
};

bool Same(const PairList& a, const PairList& b) { return a == b; }

// A set of boxes of one kind; `kind` picks the awkward shape.
void MakeBoxes(Lcg& r, int kind, int n, std::vector<Box>& boxes, std::vector<char>& ok) {
    boxes.assign((std::size_t)n, Box{});
    ok.assign((std::size_t)n, 1);
    for (int i = 0; i < n; ++i) {
        Box& b = boxes[(std::size_t)i];
        float c[3], h[3];
        for (int k = 0; k < 3; ++k) {
            switch (kind) {
                case 0: c[k] = r.Range(-20, 20); h[k] = r.Range(0.1f, 2.0f); break;                       // scattered
                case 1: c[k] = (float)r.Below(8) * 1.0f; h[k] = 0.5f; break;                              // touching grid cells
                case 2: c[k] = 3.0f; h[k] = 1.0f; break;                                                  // all identical
                case 3: c[k] = r.Range(-5, 5); h[k] = 0.0f; break;                                        // zero size
                case 4: c[k] = r.Range(-20, 20); h[k] = r.Range(-1.5f, 1.5f); break;                      // some inverted (hi < lo)
                case 5: c[k] = (k == 0) ? r.Range(-50, 50) : r.Range(-1, 1); h[k] = r.Range(0.2f, 1.0f); break;   // a long corridor on x
                default: c[k] = r.Range(-3, 3); h[k] = r.Range(0.0f, 4.0f); break;                        // heavy overlap
            }
            b.lo[k] = c[k] - h[k]; b.hi[k] = c[k] + h[k];
        }
        if (kind == 6 && i == 0) for (int k = 0; k < 3; ++k) { b.lo[k] = -1000; b.hi[k] = 1000; }   // one huge box
        if (r.Below(11) == 0) ok[(std::size_t)i] = 0;                                                  // some are not usable
        if (!ok[(std::size_t)i] && r.Below(2) == 0) { b.lo[0] = b.hi[0] = 1e30f; }                      // an ignored box can hold junk
    }
}

// ---- scene part --------------------------------------------------------------------------------------------------------
struct Counter : public Behaviour {
    int* enters = nullptr; int* stays = nullptr; int* exits = nullptr; int* trig = nullptr;
    void OnCollisionEnter3D(const Collision3D&) override { if (enters) ++*enters; }
    void OnCollisionStay3D(const Collision3D&) override { if (stays) ++*stays; }
    void OnCollisionExit3D(const Collision3D&) override { if (exits) ++*exits; }
    void OnTriggerEnter3D(Collider3D*) override { if (trig) ++*trig; }
};

std::uint64_t Fnv(std::uint64_t h, const void* d, std::size_t n) {
    const unsigned char* p = (const unsigned char*)d;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

struct Outcome { std::uint64_t hash = 0; int enters = 0, stays = 0, exits = 0, trig = 0; };

Outcome Run3D(bool brute, int frames) {
    ForceBruteForce() = brute;
    Outcome o;
    Scene s("p3");
    s.UseFixedStepPhysics(true);
    Lcg r(77);
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider3D>()->size = {60, 1, 60};
    floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    for (int i = 0; i < 160; ++i) {   // static pillars
        GameObject* g = s.CreateGameObject("Pillar");
        g->transform->localPosition = {((i % 16) - 8) * 3.0f, 0.75f, ((i / 16) - 5) * 3.0f};
        g->AddComponent<BoxCollider3D>()->size = {1, 1.5f, 1};
    }
    GameObject* zone = s.CreateGameObject("Zone");   // a trigger volume bodies fall through
    zone->transform->localPosition = {0, 3, 0};
    auto* zc = zone->AddComponent<BoxCollider3D>(); zc->size = {10, 1, 10}; zc->isTrigger = true;
    zone->AddComponent<Counter>()->trig = &o.trig;
    for (int i = 0; i < 260; ++i) {
        GameObject* g = s.CreateGameObject("Body");
        g->transform->localPosition = {r.Range(-12, 12), 2.0f + r.Range(0, 14), r.Range(-12, 12)};
        if (i % 3 == 0) g->AddComponent<SphereCollider3D>()->radius = r.Range(0.3f, 0.6f);
        else g->AddComponent<BoxCollider3D>()->size = {r.Range(0.5f, 1.2f), r.Range(0.5f, 1.2f), r.Range(0.5f, 1.2f)};
        g->AddComponent<Rigidbody3D>();
        auto* c = g->AddComponent<Counter>(); c->enters = &o.enters; c->stays = &o.stays; c->exits = &o.exits;
    }
    s.Start();
    for (int f = 0; f < frames; ++f) s.Update(1.0f / 60.0f);
    for (const auto& go : s.Objects()) {
        Vec3 p = go->transform->Position();
        Quat q = go->transform->Rotation();
        o.hash = Fnv(o.hash ? o.hash : 1469598103934665603ULL, &p, sizeof(p));
        o.hash = Fnv(o.hash, &q, sizeof(q));
    }
    ForceBruteForce() = false;
    return o;
}

struct TunnelResult { std::uint64_t hash = 0; int below = 0, nearFloor = 0; };

TunnelResult RunTunnel3D(bool brute, int steps) {
    ForceBruteForce() = brute;
    TunnelResult o;
    Scene s("tunnel");
    s.UseFixedStepPhysics(true);
    s.SetPhysicsStepsPerSecond(30.0f);               // big steps: a fast body moves metres per step
    Lcg r(4242);
    GameObject* floor = s.CreateGameObject("ThinFloor");
    floor->transform->localPosition = {0, 0, 0};
    floor->AddComponent<BoxCollider3D>()->size = {60, 0.2f, 60};
    floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    for (int i = 0; i < 120; ++i) {                  // static clutter on the floor: boxes of many sizes, some huge, one tilted
        GameObject* g = s.CreateGameObject("Clutter");
        g->transform->localPosition = {r.Range(-25, 25), 0.3f, r.Range(-25, 25)};
        g->AddComponent<BoxCollider3D>()->size = {r.Range(0.5f, 3.0f), r.Range(0.2f, 0.6f), r.Range(0.5f, 3.0f)};
    }
    GameObject* slab = s.CreateGameObject("HugeSlab");
    slab->transform->localPosition = {0, 30, 0};
    slab->AddComponent<BoxCollider3D>()->size = {200, 0.2f, 200};
    slab->GetComponent<BoxCollider3D>()->isTrigger = true;
    std::vector<Rigidbody3D*> bodies;
    for (int i = 0; i < 150; ++i) {
        GameObject* g = s.CreateGameObject("Fast");
        g->transform->localPosition = {r.Range(-24, 24), r.Range(4, 35), r.Range(-24, 24)};
        if (i % 3 == 0) g->AddComponent<SphereCollider3D>()->radius = 0.4f;
        else g->AddComponent<BoxCollider3D>()->size = {0.8f, 0.8f, 0.8f};
        auto* rb = g->AddComponent<Rigidbody3D>();
        rb->velocity = {r.Range(-3, 3), -r.Range(30, 120), r.Range(-3, 3)};
        bodies.push_back(rb);
    }
    s.Start();
    for (int f = 0; f < steps; ++f) s.Update(1.0f / 30.0f);
    for (const auto& go : s.Objects()) {
        Vec3 p = go->transform->Position();
        o.hash = Fnv(o.hash ? o.hash : 1469598103934665603ULL, &p, sizeof(p));
    }
    for (Rigidbody3D* rb : bodies) {
        const float y = rb->transform->Position().y;
        if (y < -1.0f) ++o.below;
        if (y > -1.0f && y < 3.0f) ++o.nearFloor;
    }
    ForceBruteForce() = false;
    return o;
}

Outcome Run2D(bool brute, int frames) {
    ForceBruteForce() = brute;
    Outcome o;
    Scene s("p2");
    s.UseFixedStepPhysics(true);
    Lcg r(99);
    GameObject* floor = s.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider2D>()->size = {60, 1};
    floor->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Static;
    for (int i = 0; i < 200; ++i) {
        GameObject* g = s.CreateGameObject("Body2D");
        g->transform->localPosition = {r.Range(-14, 14), 1.0f + r.Range(0, 12), 0};
        if (i % 2) g->AddComponent<CircleCollider2D>()->radius = r.Range(0.3f, 0.5f);
        else g->AddComponent<BoxCollider2D>()->size = {r.Range(0.5f, 1.0f), r.Range(0.5f, 1.0f)};
        g->AddComponent<Rigidbody2D>();
    }
    s.Start();
    for (int f = 0; f < frames; ++f) s.Update(1.0f / 60.0f);
    for (const auto& go : s.Objects()) {
        Vec3 p = go->transform->Position();
        Quat q = go->transform->Rotation();
        o.hash = Fnv(o.hash ? o.hash : 1469598103934665603ULL, &p, sizeof(p));
        o.hash = Fnv(o.hash, &q, sizeof(q));
    }
    ForceBruteForce() = false;
    return o;
}

} // namespace

int main() {
    RUN_SUITE("broadphase: sweep gives the brute force pair list, in order");
    {
        Lcg r(2024);
        int sets = 0, pairsSeen = 0;
        for (int kind = 0; kind <= 6; ++kind)
            for (int rep = 0; rep < 40; ++rep) {
                const int n = (rep == 0) ? 0 : (rep == 1 ? 1 : (rep == 2 ? 2 : 3 + r.Below(120)));
                std::vector<Box> boxes; std::vector<char> ok;
                MakeBoxes(r, kind, n, boxes, ok);
                const PairList a = PairsBrute(boxes, ok);
                const PairList b = Pairs(boxes, ok);
                CHECK(Same(a, b));
                if (!Same(a, b)) std::printf("  mismatch: kind %d rep %d n %d (%zu vs %zu pairs)\n", kind, rep, n, a.size(), b.size());
                ++sets; pairsSeen += (int)a.size();
            }
        CHECK(sets == 7 * 40);
        CHECK(pairsSeen > 1000);   // the sets really had pairs in them
    }

    RUN_SUITE("broadphase: a big grid");
    {
        std::vector<Box> boxes; std::vector<char> ok;
        for (int i = 0; i < 2500; ++i) {
            Box b; const float x = (float)(i % 50) * 1.0f, z = (float)(i / 50) * 1.0f;   // touching neighbours
            b.lo[0] = x; b.hi[0] = x + 1; b.lo[1] = 0; b.hi[1] = 1; b.lo[2] = z; b.hi[2] = z + 1;
            boxes.push_back(b); ok.push_back(1);
        }
        CHECK(Same(PairsBrute(boxes, ok), Pairs(boxes, ok)));
    }

    RUN_SUITE("broadphase: the brute force switch");
    {
        std::vector<Box> boxes(3); std::vector<char> ok(3, 1);
        boxes[0].hi[0] = boxes[0].hi[1] = boxes[0].hi[2] = 1;
        boxes[1].lo[0] = 0.5f; boxes[1].hi[0] = 2; boxes[1].hi[1] = boxes[1].hi[2] = 1;
        boxes[2].lo[0] = 10; boxes[2].hi[0] = 11;
        ForceBruteForce() = true;
        CHECK(Pairs(boxes, ok).size() == 1);
        ForceBruteForce() = false;
        CHECK(Pairs(boxes, ok).size() == 1);
    }

    RUN_SUITE("broadphase: a 3D physics scene moves the same either way");
    {
        const Outcome sweep = Run3D(false, 240), brute = Run3D(true, 240);
        CHECK(sweep.hash == brute.hash);
        CHECK(sweep.enters == brute.enters && sweep.stays == brute.stays && sweep.exits == brute.exits && sweep.trig == brute.trig);
        CHECK(sweep.enters > 50 && sweep.stays > 500);   // the bodies really collided
        CHECK(sweep.trig > 0);                            // and fell through the trigger
        std::printf("  3D: %d enters, %d stays, %d exits, %d trigger enters\n", sweep.enters, sweep.stays, sweep.exits, sweep.trig);
    }

    RUN_SUITE("broadphase: fast bodies and a thin floor: the vertical rays of the floor catch pick the same hits");
    {
        const TunnelResult grid = RunTunnel3D(false, 90), plain = RunTunnel3D(true, 90);
        CHECK(grid.hash == plain.hash);
        CHECK(grid.below == plain.below && grid.nearFloor == plain.nearFloor);
        std::printf("  %d of 150 bodies ended near the floor, %d under it (with the catch on)\n", grid.nearFloor, grid.below);
        CHECK(grid.nearFloor > 60);        // the catch (or the contact) held most of them up: the rays were really cast and used
    }

    RUN_SUITE("broadphase: a 2D physics scene moves the same either way");
    {
        const Outcome sweep = Run2D(false, 240), brute = Run2D(true, 240);
        CHECK(sweep.hash == brute.hash);
    }

    TEST_MAIN_RESULT();
}
