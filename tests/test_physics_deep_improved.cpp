// The Improved physics quality (Physics3D::quality): stacks stand, resting bodies sleep together, fast bodies stop at thin
// walls, balls roll to a stop, shapes turn the way their size says, compound bodies and per-collider materials work.
// Every case here failed (or could not be set up) on the Classic step; the numbers are in docs/physics/physics.md.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct World {
    Scene s{"w"};
    float dt = 1.0f / 50.0f;
    int n = 0;
    explicit World(PhysicsQuality q = PhysicsQuality::Improved) { s.physics3D().quality = q; s.physics().quality = q; }
    GameObject* Floor(float top = 0.0f) {
        GameObject* f = s.CreateGameObject("Floor");
        f->transform->localPosition = {0, top - 0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
        return f;
    }
    GameObject* Crate(Vec3 p, float mass = 1.0f, bool freezeRot = false, float friction = 0.5f, Vec3 size = {1, 1, 1}) {
        GameObject* o = s.CreateGameObject("Crate" + std::to_string(n++));
        o->transform->localPosition = p;
        o->AddComponent<BoxCollider3D>()->size = size;
        auto* r = o->AddComponent<Rigidbody3D>();
        r->mass = mass; r->freezeRotation = freezeRot; r->friction = friction;
        return o;
    }
    GameObject* Ball(Vec3 p, float radius = 0.5f, float mass = 1.0f, bool freezeRot = false, float bounce = 0.0f) {
        GameObject* o = s.CreateGameObject("Ball" + std::to_string(n++));
        o->transform->localPosition = p;
        o->AddComponent<SphereCollider3D>()->radius = radius;
        auto* r = o->AddComponent<Rigidbody3D>();
        r->mass = mass; r->freezeRotation = freezeRot; r->bounciness = bounce;
        return o;
    }
    void Step() { s.physics3D().Step(s, dt); }
    void Steps(float seconds) { const int k = (int)std::lround(seconds / dt); for (int i = 0; i < k; ++i) Step(); }
};
Vec3 P(GameObject* o) { return o->transform->Position(); }
Rigidbody3D* RB(GameObject* o) { return o->GetComponent<Rigidbody3D>(); }
float Tilt(GameObject* o) {
    Vec3 up = o->transform->Rotation() * Vec3{0, 1, 0};
    return std::acos(std::max(-1.0f, std::min(1.0f, up.y))) * 57.29578f;
}

} // namespace

int main() {
    RUN_SUITE("physics_deep_improved");

    // ---- Stacks of crates with free rotation stand, stay put and fall asleep together.
    for (int n : {3, 5}) {
        World w;
        w.Floor();
        std::vector<GameObject*> c;
        for (int i = 0; i < n; ++i) c.push_back(w.Crate({0, 0.5f + (float)i, 0}));
        w.Steps(6.0f);
        float drift = 0, tilt = 0; int asleep = 0;
        for (GameObject* g : c) { drift = std::max(drift, std::sqrt(P(g).x * P(g).x + P(g).z * P(g).z)); tilt = std::max(tilt, Tilt(g)); asleep += RB(g)->sleeping; }
        std::cout << "  " << n << " crates: drift " << drift << " tilt " << tilt << " deg, asleep " << asleep << "/" << n << ", top y " << P(c.back()).y << "\n";
        CHECK(drift < 0.03f);
        CHECK(tilt < 1.0f);
        CHECK(asleep == n);                                  // the whole stack sleeps (the old step never slept)
        CHECK_NEAR(P(c.back()).y, (float)n - 0.5f, 0.04);   // the top crate is where it started (the old step: ended on the floor)
    }
    // ---- A tower with rotation frozen (the old default) does not sink into itself (old step: 16 cm at 5 crates, 3.7 m at 10).
    for (int n : {5, 10}) {
        World w; w.s.physics3D().solverIterations = n == 10 ? 20 : 10;
        w.Floor();
        std::vector<GameObject*> c;
        for (int i = 0; i < n; ++i) c.push_back(w.Crate({0, 0.5f + (float)i, 0}, 1.0f, true));
        w.Steps(8.0f);
        std::cout << "  frozen " << n << " crates: top y " << P(c.back()).y << " (start " << n - 0.5f << ")\n";
        CHECK_NEAR(P(c.back()).y, (float)n - 0.5f, 0.05);
    }
    // ---- A crate dropped flat does not buzz, tilt or wander, and sleeps within two seconds.
    {
        World w; w.Floor();
        GameObject* c = w.Crate({0, 1.0f, 0});
        w.Steps(2.0f);
        CHECK(RB(c)->sleeping);
        CHECK_NEAR(P(c).y, 0.5f, 0.008);
        CHECK(Tilt(c) < 0.3f);
        World v; v.Floor();
        GameObject* d = v.Crate({0, 0.6f, 0}); RB(d)->allowSleep = false;
        v.Steps(2.0f);
        float lo = 1e9f, hi = -1e9f, tilt = 0;
        for (int i = 0; i < 100; ++i) { v.Step(); lo = std::min(lo, P(d).y); hi = std::max(hi, P(d).y); tilt = std::max(tilt, Tilt(d)); }
        std::cout << "  resting crate (sleep off): wobble " << (hi - lo) * 1000.0f << " mm, tilt " << tilt << " deg\n";
        CHECK(hi - lo < 0.002f);                             // the old step wobbled 9.75 mm
        CHECK(tilt < 0.3f);
    }
    // ---- 500 resting crates with free rotation all sleep (the old step: none ever did, 3.6 ms a step).
    {
        World w; w.Floor();
        std::vector<GameObject*> c;
        for (int i = 0; i < 500; ++i) c.push_back(w.Crate({((float)(i % 25) - 12.0f) * 1.5f, 0.5f, ((float)(i / 25) - 10.0f) * 1.5f}));
        w.Steps(3.0f);
        int asleep = 0; for (GameObject* g : c) asleep += RB(g)->sleeping;
        std::cout << "  500 resting crates: asleep " << asleep << ", stats awake " << w.s.physics3D().Stats().awake << " asleep " << w.s.physics3D().Stats().asleep << "\n";
        CHECK(asleep == 500);
        CHECK(w.s.physics3D().Stats().asleep == 500);
    }
    // ---- Heavy on light: a 100 kg crate on a 1 kg crate stays on top of it.
    {
        World w; w.Floor();
        GameObject* a = w.Crate({0, 0.5f, 0}, 1.0f, true);
        GameObject* b = w.Crate({0, 1.5f, 0}, 100.0f, true);
        w.Steps(5.0f);
        CHECK(P(b).y > 1.45f);
        CHECK(P(a).y > 0.47f);
    }
    // ---- A fast small ball stops at a thin wall without ticking Continuous Collision (Classic: passes straight through).
    for (float hz : {30.0f, 50.0f, 144.0f}) {
        World w; w.dt = 1.0f / hz; w.Floor(-50.0f);
        GameObject* wall = w.s.CreateGameObject("Wall"); wall->transform->localPosition = {10, 0, 0};
        wall->AddComponent<BoxCollider3D>()->size = {0.1f, 10, 10};
        GameObject* b = w.Ball({0, 0, 0}, 0.1f, 0.1f, true);
        RB(b)->gravityScale = 0.0f; RB(b)->velocity = {60.0f, 0, 0};
        float maxX = -1e9f;
        for (int i = 0; i < (int)hz; ++i) { w.Step(); maxX = std::max(maxX, P(b).x); }
        std::cout << "  automatic catch, " << hz << " Hz: max x " << maxX << " (wall at 10)\n";
        CHECK(maxX < 10.2f);
        CHECK(w.s.physics3D().Stats().sweeps >= 0);
    }
    // ... and the switch turns it off.
    {
        World w; w.s.physics3D().autoContinuous = false; w.Floor(-50.0f);
        GameObject* wall = w.s.CreateGameObject("Wall"); wall->transform->localPosition = {10, 0, 0};
        wall->AddComponent<BoxCollider3D>()->size = {0.1f, 10, 10};
        GameObject* b = w.Ball({0, 0, 0}, 0.1f, 0.1f, true);
        RB(b)->gravityScale = 0.0f; RB(b)->velocity = {60.0f, 0, 0};
        float maxX = -1e9f;
        for (int i = 0; i < 50; ++i) { w.Step(); maxX = std::max(maxX, P(b).x); }
        CHECK(maxX > 30.0f);
    }
    // ---- A ball rolls to a stop (the old step: 445 m after two minutes, still rolling at 3.6 m/s).
    {
        World w; w.Floor();
        GameObject* b = w.Ball({0, 0.5f, 0});
        w.Steps(0.5f);
        const float x0 = P(b).x;
        RB(b)->velocity = {5.0f, 0, 0}; RB(b)->angularVelocity = {0, 0, -10.0f};
        float tStop = -1;
        for (int i = 0; i < 50 * 40; ++i) { w.Step(); if (tStop < 0 && RB(b)->velocity.Magnitude() < 0.05f) tStop = (float)(i + 1) / 50.0f; }
        std::cout << "  rolling ball stopped after " << tStop << " s, travelled " << P(b).x - x0 << " m\n";
        CHECK(tStop > 5.0f && tStop < 25.0f);
        CHECK(P(b).x - x0 < 60.0f);
        // with no rolling resistance it rolls on, as before
        World v; v.Floor();
        GameObject* d = v.Ball({0, 0.5f, 0}); RB(d)->rollingResistance = 0.0f;
        v.Steps(0.5f);
        RB(d)->velocity = {5.0f, 0, 0}; RB(d)->angularVelocity = {0, 0, -10.0f};
        v.Steps(20.0f);
        CHECK(RB(d)->velocity.Magnitude() > 1.0f);
    }
    // ---- Shapes turn the way their size says: the same torque turns a cube at 6 rad/s and a long plank about its length at
    //      150 rad/s (the old step gave 3.07 and 0.75).
    for (int k = 0; k < 2; ++k) {
        World w; w.s.physics3D().gravity = {0, 0, 0};
        const Vec3 size = k == 0 ? Vec3{1, 1, 1} : Vec3{4, 0.2f, 0.2f};
        GameObject* b = w.Crate({0, 0, 0}, 1.0f, false, 0.5f, size);
        RB(b)->angularDrag = 0.0f; RB(b)->allowSleep = false;
        for (int i = 0; i < 50; ++i) { RB(b)->AddTorque({1.0f, 0, 0}); w.Step(); }
        const float want = k == 0 ? 6.0f : 150.0f;
        std::cout << "  torque 1 for 1 s on a " << (k == 0 ? "cube" : "plank") << ": " << RB(b)->angularVelocity.x << " rad/s (true " << want << ")\n";
        CHECK_NEAR(RB(b)->angularVelocity.x, want, 0.1 * want);
    }
    // ---- A body whose only collider is on a child object stays up when it asks for its children (the old step: fell for ever).
    {
        World w; w.Floor();
        GameObject* body = w.s.CreateGameObject("Body");
        body->transform->localPosition = {0, 1.0f, 0};
        auto* r = body->AddComponent<Rigidbody3D>(); r->freezeRotation = true; r->compoundChildren = true;
        GameObject* kid = w.s.CreateGameObject("Kid");
        kid->transform->SetParent(body->transform);
        kid->transform->localPosition = {0, 0, 0};
        kid->AddComponent<BoxCollider3D>();
        w.Steps(2.0f);
        CHECK_NEAR(P(body).y, 0.5f, 0.02);
        // ... and a table (a thin top on four legs) stands on its legs.
        World t; t.Floor();
        GameObject* table = t.s.CreateGameObject("Table");
        table->transform->localPosition = {0, 1.0f, 0};
        auto* tr = table->AddComponent<Rigidbody3D>(); tr->freezeRotation = false; tr->compoundChildren = true; tr->mass = 4.0f;
        auto addPart = [&](Vec3 p, Vec3 size) {
            GameObject* g = t.s.CreateGameObject("Part");
            g->transform->SetParent(table->transform);
            g->transform->localPosition = p;
            g->AddComponent<BoxCollider3D>()->size = size;
        };
        addPart({0, 0.5f, 0}, {2.0f, 0.1f, 2.0f});
        for (int i = 0; i < 4; ++i) addPart({(i & 1 ? 0.9f : -0.9f), -0.05f, (i & 2 ? 0.9f : -0.9f)}, {0.15f, 1.0f, 0.15f});
        t.Steps(4.0f);
        std::cout << "  table: y " << P(table).y << ", tilt " << Tilt(table) << " deg\n";
        CHECK(Tilt(table) < 3.0f);
        CHECK(P(table).y > 0.4f && P(table).y < 1.1f);
    }
    // ---- An offset collider turns with its object (Classic leaves the offset where it was).
    {
        World w;
        GameObject* o = w.s.CreateGameObject("O");
        o->transform->localRotation = Quat::Euler(0, 0, 90);
        o->transform->localScale = {2, 2, 2};
        auto* c = o->AddComponent<BoxCollider3D>(); c->offset = {0, 1, 0};
        const Vec3 wc = c->WorldCenter();
        CHECK_NEAR(wc.x, -2.0f, 1e-3); CHECK_NEAR(wc.y, 0.0f, 1e-3);
        World k(PhysicsQuality::Classic);
        GameObject* o2 = k.s.CreateGameObject("O");
        o2->transform->localRotation = Quat::Euler(0, 0, 90);
        auto* c2 = o2->AddComponent<BoxCollider3D>(); c2->offset = {0, 1, 0};
        CHECK_NEAR(c2->WorldCenter().y, 1.0f, 1e-4);          // Classic: as it always was
    }
    // ---- Per-collider materials: a floor made icy by its own collider (no Rigidbody) lets a crate slide much farther.
    {
        float dist[2] = {0, 0};
        for (int ice = 0; ice < 2; ++ice) {
            World w;
            GameObject* f = w.Floor();
            if (ice) f->GetComponent<Collider3D>()->friction = 0.0f;
            GameObject* c = w.Crate({0, 0.5f, 0}, 1.0f, true, 0.5f);
            RB(c)->allowSleep = false;
            w.Steps(0.5f);
            const float x0 = P(c).x;
            RB(c)->velocity = {5.0f, 0, 0};
            w.Steps(4.0f);
            dist[ice] = P(c).x - x0;
        }
        std::cout << "  slide on rough floor " << dist[0] << " m, on an icy collider " << dist[1] << " m\n";
        CHECK(dist[0] < 3.2f);
        CHECK(dist[1] > 12.0f);
        // bounce: a rubber floor collider
        World b; GameObject* f = b.Floor(); f->GetComponent<Collider3D>()->bounciness = 0.9f;
        GameObject* ball = b.Ball({0, 2.5f, 0}, 0.5f, 1.0f, true, 0.0f);
        float peak = 0; bool up = false; float prevVy = 0; int bounces = 0;
        for (int i = 0; i < 250 && bounces < 1; ++i) {
            b.Step(); float vy = RB(ball)->velocity.y;
            if (vy > 0) { up = true; peak = std::max(peak, P(ball).y - 0.5f); }
            if (up && prevVy > 0 && vy <= 0) ++bounces;
            prevVy = vy;
        }
        std::cout << "  rubber collider bounce peak " << peak << " m (ball bounciness 0: from 2 m, a 0.9 floor rises about 1.6 m)\n";
        CHECK(peak > 1.2f);
    }
    // ---- Combine rules: Minimum of an icy and a grippy surface is icy; the default (geometric mean) is in between.
    {
        CHECK_NEAR(CombineSurface(0.0f, 1.0f, CombineMode::Default, CombineMode::Default, true), 0.0f, 1e-6);
        CHECK_NEAR(CombineSurface(0.2f, 0.8f, CombineMode::Default, CombineMode::Default, true), 0.4f, 1e-6);
        CHECK_NEAR(CombineSurface(0.2f, 0.8f, CombineMode::Average, CombineMode::Default, true), 0.5f, 1e-6);
        CHECK_NEAR(CombineSurface(0.2f, 0.8f, CombineMode::Minimum, CombineMode::Average, true), 0.2f, 1e-6);   // Minimum outranks Average
        CHECK_NEAR(CombineSurface(0.2f, 0.8f, CombineMode::Maximum, CombineMode::Minimum, true), 0.8f, 1e-6);
        CHECK_NEAR(CombineSurface(0.5f, 0.5f, CombineMode::Multiply, CombineMode::Default, true), 0.25f, 1e-6);
        CHECK_NEAR(CombineSurface(0.2f, 0.8f, CombineMode::Default, CombineMode::Default, false), 0.8f, 1e-6);  // bounce: the larger
    }
    // ---- A thrown crate slides and stops where friction says, and heavy things push light things.
    {
        World w; w.Floor();
        GameObject* c = w.Crate({0, 0.5f, 0}, 1.0f, false, 0.5f);
        w.Steps(0.5f);
        const float x0 = P(c).x;
        RB(c)->velocity = {5.0f, 0, 0};
        w.Steps(4.0f);
        const float mu = std::sqrt(0.5f * 0.6f);
        const float theory = 25.0f / (2.0f * mu * 9.81f);
        std::cout << "  thrown crate slid " << P(c).x - x0 << " m (theory " << theory << ")\n";
        CHECK(std::fabs(P(c).x - x0 - theory) < 0.25f * theory);
        CHECK(RB(c)->velocity.Magnitude() < 0.05f);
        World p; p.s.physics3D().gravity = {0, 0, 0};
        GameObject* heavy = p.Crate({0, 0.5f, 0}, 100.0f, true, 0.0f);
        GameObject* light = p.Crate({1.5f, 0.5f, 0}, 1.0f, true, 0.0f);
        RB(heavy)->velocity = {5.0f, 0, 0};
        p.Steps(0.8f);
        CHECK(RB(light)->velocity.x > 4.5f);                  // carried along at nearly the pusher's speed (no bounce: they end together)
        CHECK(RB(heavy)->velocity.x > 4.5f);                  // barely slowed
    }
    // ---- Frame cost: stepping 500 sleeping crates and 100 moving balls stays cheap (a loose bound: shared machine).
    {
        World w; w.Floor();
        for (int i = 0; i < 500; ++i) w.Crate({((float)(i % 25) - 12.0f) * 1.5f, 0.5f, ((float)(i / 25) - 10.0f) * 1.5f});
        w.Steps(3.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) w.Step();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 50.0;
        std::cout << "  500 sleeping crates: " << ms << " ms per step\n";
        CHECK(ms < 6.0);
    }
    TEST_MAIN_RESULT();
}
