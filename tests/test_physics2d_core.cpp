#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

// 2D rigid bodies on the physics core (docs/physics/physics_core.md): what a 2D game needs from a contact solver.
//  1. a stack of 10 crates stays standing and does not sink for 5 seconds at 30, 60 and 120 steps a second;
//  2. a sleeping crate wakes and falls when what holds it up goes away;
//  3. a heavy crate on a light crate does not squash it;
//  4. a fast small ball does not pass a thin wall (with and without Continuous Collision ticked);
//  5. a platform moved by its transform lifts and carries a crate;
//  6. solid colliders that cannot be pushed still raise OnCollision while they overlap.
// Every case prints the numbers it measured, so a failure says how far off it was.

namespace {

struct Probe : Component {
    int enter = 0, stay = 0, exit = 0;
    void OnCollisionEnter2D(const Collision2D&) override { ++enter; }
    void OnCollisionStay2D(const Collision2D&) override { ++stay; }
    void OnCollisionExit2D(const Collision2D&) override { ++exit; }
};

GameObject* Box(Scene& s, const char* name, float x, float y, float w, float h, bool dynamic, float mass = 1.0f) {
    auto* go = s.CreateGameObject(name);
    go->transform->localPosition = {x, y, 0};
    auto* c = go->AddComponent<BoxCollider2D>();
    c->size = {w, h};
    if (dynamic) {
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->mass = mass;
        rb->freezeRotation = false;
    }
    return go;
}

float TiltDeg(GameObject* go) { return std::fabs(Mathf::DeltaAngle(0.0f, go->transform->localRotation.ToEuler().z)); }

void Stack(int hz) {
    Scene s("stack");
    Box(s, "Ground", 0, -0.5f, 40, 1, false);
    const int n = 10;
    std::vector<GameObject*> crates;
    for (int i = 0; i < n; ++i) crates.push_back(Box(s, "Crate", 0, 0.5f + (float)i, 1, 1, true));
    s.Start();
    const float dt = 1.0f / (float)hz;
    for (int i = 0; i < 5 * hz; ++i) s.Update(dt);
    float worstSink = 0, worstDrift = 0, worstTilt = 0, worstOverlap = 0;
    for (int i = 0; i < n; ++i) {
        const Vec3 p = crates[(std::size_t)i]->transform->Position();
        worstSink = std::max(worstSink, (0.5f + (float)i) - p.y);
        worstDrift = std::max(worstDrift, std::fabs(p.x));
        worstTilt = std::max(worstTilt, TiltDeg(crates[(std::size_t)i]));
        const float below = i == 0 ? 0.0f : crates[(std::size_t)i - 1]->transform->Position().y + 0.5f;
        worstOverlap = std::max(worstOverlap, below - (p.y - 0.5f));
    }
    const float top = crates.back()->transform->Position().y;
    std::printf("  stack %3d Hz: top y %.4f (ideal 9.5), sink %.4f, overlap %.4f, drift %.4f, tilt %.3f deg\n", hz, top,
                worstSink, worstOverlap, worstDrift, worstTilt);
    CHECK(worstSink < 0.03f);      // nothing more than 3 cm below where it was stacked
    CHECK(worstSink > -0.03f);     // and nothing pushed up either
    CHECK(worstOverlap < 0.02f);   // no crate inside the one below
    CHECK(worstDrift < 0.03f);
    CHECK(worstTilt < 1.0f);
}

} // namespace

int main() {
    RUN_SUITE("physics2d_core");

    // ---- 1. a stack of 10 crates at 30, 60 and 120 steps a second ----
    Stack(30);
    Stack(60);
    Stack(120);

    // ---- 2. a sleeping crate wakes and falls when its support is removed ----
    for (int how = 0; how < 3; ++how) {
        Scene s("wake");
        Box(s, "Floor", 0, -10.5f, 40, 1, false);
        GameObject* shelf = Box(s, "Shelf", 0, -0.5f, 4, 1, false);
        GameObject* crate = Box(s, "Crate", 0, 0.5f, 1, 1, true);
        auto* rb = crate->GetComponent<Rigidbody2D>();
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        const bool slept = rb->sleeping;
        const float restY = crate->transform->Position().y;
        if (how == 0) shelf->GetComponent<BoxCollider2D>()->enabled = false;   // switched off
        else if (how == 1) shelf->transform->localPosition = {100, -0.5f, 0};   // moved away
        else s.Destroy(shelf);                                                   // destroyed
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        const float y = crate->transform->Position().y;
        std::printf("  wake (%s): asleep before %d, rest y %.3f, y two seconds after %.3f (floor rest is -9.5)\n",
                    how == 0 ? "collider off" : how == 1 ? "shelf moved" : "shelf destroyed", (int)slept, restY, y);
        CHECK(slept);                         // it did fall asleep on the shelf
        CHECK(std::fabs(restY - 0.5f) < 0.03f);
        CHECK(y < -9.0f);                     // and it fell to the floor afterwards
        CHECK(y > -9.6f);                     // (and stopped on it)
    }

    // ---- 3. a heavy crate on a light crate ----
    for (int hz : {30, 60}) {
        Scene s("heavy");
        Box(s, "Ground", 0, -0.5f, 40, 1, false);
        GameObject* light = Box(s, "Light", 0, 0.5f, 1, 1, true, 1.0f);
        GameObject* heavy = Box(s, "Heavy", 0, 1.5f, 1, 1, true, 50.0f);
        s.Start();
        float worstOverlap = 0, worstSink = 0;
        for (int i = 0; i < 5 * hz; ++i) {
            s.Update(1.0f / (float)hz);
            const float ly = light->transform->Position().y, hy = heavy->transform->Position().y;
            worstOverlap = std::max(worstOverlap, (ly + 0.5f) - (hy - 0.5f));
            worstSink = std::max(worstSink, 0.5f - ly);
        }
        const Vec3 lp = light->transform->Position(), hp = heavy->transform->Position();
        std::printf("  heavy on light %3d Hz: light y %.4f (0.5), heavy y %.4f (1.5), worst overlap %.4f, worst sink %.4f, "
                    "light x %.4f, tilt %.3f deg\n", hz, lp.y, hp.y, worstOverlap, worstSink, lp.x, TiltDeg(light));
        CHECK(worstOverlap < 0.03f);          // the heavy crate never sits inside the light one
        CHECK(worstSink < 0.03f);             // and the light one is never pressed into the ground
        CHECK(std::fabs(hp.y - 1.5f) < 0.03f);
        CHECK(std::fabs(lp.x) < 0.03f && std::fabs(hp.x) < 0.03f);
    }

    // ---- 4. a fast small ball against a thin wall ----
    for (int ccd = 0; ccd < 2; ++ccd) {
        for (int hz : {30, 60, 120}) {
            Scene s("fast");
            s.physics().gravity = {0, 0};
            Box(s, "Wall", 10.0f, 0, 0.02f, 4, false);   // 2 cm thick
            auto* ball = s.CreateGameObject("Ball");
            ball->transform->localPosition = {0, 0, 0};
            auto* c = ball->AddComponent<CircleCollider2D>();
            c->radius = 0.05f;
            auto* rb = ball->AddComponent<Rigidbody2D>();
            rb->velocity = {200.0f, 0};       // 200 m/s: metres per step, far more than the wall is thick
            rb->continuousCollision = ccd != 0;
            s.Start();
            float maxX = 0;
            for (int i = 0; i < hz; ++i) {
                s.Update(1.0f / (float)hz);
                maxX = std::max(maxX, ball->transform->Position().x);
            }
            std::printf("  fast ball, continuous %d, %3d Hz: furthest x %.3f (wall face at 9.99)\n", ccd, hz, maxX);
            CHECK(maxX < 10.0f);              // never got to the far side of the wall
            CHECK(maxX > 9.0f);               // and did reach it
        }
    }

    // ---- 5. a platform moved by its transform (no Rigidbody2D) lifts and carries what stands on it ----
    // (A guard added with the move to the core, not one of the cases measured on the old step.)
    for (int sideways = 0; sideways < 2; ++sideways) {
        Scene s("lift");
        GameObject* plat = Box(s, "Platform", 0, -0.5f, 4, 1, false);
        GameObject* crate = Box(s, "Crate", 0, 0.5f, 1, 1, true);
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);   // settle
        float worstGap = 0, xAt50 = 0;
        for (int i = 0; i < 60; ++i) {                          // one second at 2 units a second
            if (i == 50) xAt50 = crate->transform->Position().x;
            plat->transform->localPosition += sideways ? Vec3{2.0f / 60.0f, 0, 0} : Vec3{0, 2.0f / 60.0f, 0};
            s.Update(1.0f / 60.0f);
            const float top = plat->transform->Position().y + 0.5f;
            worstGap = std::max(worstGap, std::fabs((crate->transform->Position().y - 0.5f) - top));
        }
        const Vec3 pp = plat->transform->Position(), cp = crate->transform->Position();
        std::printf("  platform moved %s: platform at (%.3f, %.3f), crate at (%.3f, %.3f), worst gap or overlap %.4f, "
                    "crate moved %.3f in the last 10 steps (platform 0.333)\n",
                    sideways ? "sideways" : "up", pp.x, pp.y, cp.x, cp.y, worstGap, cp.x - xAt50);
        CHECK(worstGap < 0.03f);                       // the crate stays on the platform's top
        if (sideways) {
            // the platform starts at full speed, so the crate slips first (friction 0.4: about v*v / (2 * 0.4 * g) = 0.51
            // behind) and then rides along at the platform's speed
            CHECK(pp.x - cp.x > 0.3f && pp.x - cp.x < 0.7f);
            CHECK(std::fabs((cp.x - xAt50) - 10.0f * 2.0f / 60.0f) < 0.02f);
        } else {
            CHECK(std::fabs(cp.y - (pp.y + 1.0f)) < 0.03f);
        }
    }

    // ---- 6. solid colliders that cannot be pushed still report their overlap (as the old 2D step did) ----
    // (Also a guard added with the move to the core.) A collider moved by its transform, then a kinematic body moved by
    // its speed, pass through a wall that has no body: Enter once, Stay while inside, Exit once, and nothing is pushed.
    for (int kinematic = 0; kinematic < 2; ++kinematic) {
        Scene s("still");
        GameObject* wall = Box(s, "Wall", 0, 0, 1, 4, false);
        GameObject* mover = Box(s, "Mover", -3, 0, 1, 1, false);
        auto* probe = mover->AddComponent<Probe>();
        auto* wallProbe = wall->AddComponent<Probe>();
        if (kinematic) {
            auto* rb = mover->AddComponent<Rigidbody2D>();
            rb->bodyType = Rigidbody2D::BodyType::Kinematic;
            rb->velocity = {3.0f, 0};
        }
        s.Start();
        for (int i = 0; i < 120; ++i) {   // 2 seconds at 3 units a second: from x = -3 to x = +3
            if (!kinematic) mover->transform->localPosition.x += 3.0f / 60.0f;
            s.Update(1.0f / 60.0f);
        }
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        std::printf("  %s through a wall: enter %d, stay %d, exit %d (the wall saw %d / %d / %d), mover x %.3f, wall x %.3f\n",
                    kinematic ? "kinematic body" : "collider moved by its transform", probe->enter, probe->stay, probe->exit,
                    wallProbe->enter, wallProbe->stay, wallProbe->exit, mover->transform->Position().x, wall->transform->Position().x);
        CHECK(probe->enter == 1 && probe->exit == 1);
        CHECK(probe->stay > 30 && probe->stay < 45);     // the two overlap for 2 units of travel: 40 steps
        CHECK(wallProbe->enter == 1 && wallProbe->exit == 1);
        // nothing stopped it (the kinematic body kept its speed for the 5 extra steps: 125 steps in all)
        CHECK(std::fabs(mover->transform->Position().x - (kinematic ? 3.25f : 3.0f)) < 0.05f);
        CHECK(wall->transform->Position().x == 0.0f);
    }

    TEST_MAIN_RESULT();
}
