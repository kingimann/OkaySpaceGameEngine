// Physics bugs found by the audit in docs/physics/physics_plan.md. Each case ran against the unchanged step first and failed there
// (the numbers are in the plan); the fixes hold in BOTH physics qualities, so every case runs twice.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct World {
    Scene s{"w"};
    float dt = 1.0f / 50.0f;
    explicit World(PhysicsQuality q) { s.physics3D().quality = q; s.physics().quality = q; }
    GameObject* Floor(float top = 0.0f, bool withBody = false) {
        GameObject* f = s.CreateGameObject("Floor");
        f->transform->localPosition = {0, top - 0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
        if (withBody) f->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        return f;
    }
    GameObject* Crate(Vec3 p, float mass = 1.0f, bool freezeRot = true, float friction = 0.5f) {
        GameObject* o = s.CreateGameObject("Crate" + std::to_string(n++));
        o->transform->localPosition = p;
        o->AddComponent<BoxCollider3D>();
        auto* r = o->AddComponent<Rigidbody3D>();
        r->mass = mass; r->freezeRotation = freezeRot; r->friction = friction;
        return o;
    }
    void Steps(float seconds) { const int k = (int)std::lround(seconds / dt); for (int i = 0; i < k; ++i) s.physics3D().Step(s, dt); }
    int n = 0;
};
Vec3 P(GameObject* o) { return o->transform->Position(); }
Rigidbody3D* RB(GameObject* o) { return o->GetComponent<Rigidbody3D>(); }
const char* Name(PhysicsQuality q) { return q == PhysicsQuality::Improved ? "Improved" : "Classic"; }

// A box on a ramp turned `deg` about z. Returns how far it slid down the slope in `seconds`, after a second of settling.
float SlideOnRamp(PhysicsQuality q, float deg, float mu, float seconds, float* startSpeed = nullptr) {
    World w(q);
    GameObject* r = w.s.CreateGameObject("Ramp");
    r->transform->localRotation = Quat::Euler(0, 0, deg);
    r->AddComponent<BoxCollider3D>()->size = {60, 1, 20};
    const float a = deg * 0.0174533f;
    const Vec3 up{-std::sin(a), std::cos(a), 0};
    GameObject* c = w.Crate(up * 1.0f, 1.0f, true, mu);
    c->transform->localRotation = Quat::Euler(0, 0, deg);
    RB(c)->allowSleep = false;
    w.Steps(1.0f);
    const Vec3 p0 = P(c);
    if (startSpeed) *startSpeed = Vec3::Dot(RB(c)->velocity, Vec3{-std::cos(a), -std::sin(a), 0});
    w.Steps(seconds);
    return Vec3::Dot(P(c) - p0, Vec3{-std::cos(a), -std::sin(a), 0});
}

} // namespace

int main() {
    RUN_SUITE("physics_deep_bugs");
    const PhysicsQuality both[] = {PhysicsQuality::Classic, PhysicsQuality::Improved};

    for (PhysicsQuality q : both) {
        std::cout << "  [" << Name(q) << "]\n";

        // ---- A slope: a box with friction 0.1 on a 30 degree ramp slides (the old step lifted it every step: 0.23 m in 2 s).
        {
            float v0 = 0.0f;
            float slid = SlideOnRamp(q, 30.0f, 0.1f, 2.0f, &v0);
            const float mc = std::sqrt(0.1f * 0.6f), acc = 9.81f * (0.5f - mc * 0.8660254f);
            const float want = v0 * 2.0f + 0.5f * acc * 4.0f;
            std::cout << "    30 deg ramp slid " << slid << " m in 2 s (theory " << want << ")\n";
            CHECK(slid > 0.6f * want);
            CHECK(slid < 1.5f * want);
        }
        // ---- A grippy ramp (friction 0.5, 20 degrees: tan 20 = 0.36 < 0.5) holds the box still.
        {
            float slid = SlideOnRamp(q, 20.0f, 0.5f, 2.0f);
            std::cout << "    20 deg grippy ramp slid " << slid << " m in 2 s\n";
            CHECK(std::fabs(slid) < (q == PhysicsQuality::Classic ? 0.3f : 0.05f));   // Classic creeps a little: friction is only applied to approaching contacts
        }

        // ---- A sleeping box whose support goes away falls: the collider removed, the support teleported away, its
        //      collider disabled, or its object switched off (the old step left it floating for ever).
        for (int how = 0; how < 4; ++how) {
            World w(q);
            w.Floor();
            GameObject* a = w.Crate({0, 0.5f, 0});
            GameObject* b = w.Crate({0, 1.5f, 0});
            w.Steps(1.0f);
            for (GameObject* g : {a, b}) { RB(g)->velocity = Vec3::Zero; RB(g)->sleeping = true; }
            w.Steps(0.5f);
            if (how == 0) a->RemoveComponent<Collider3D>();
            if (how == 1) a->transform->localPosition = {10, 0.5f, 0};
            if (how == 2) a->GetComponent<Collider3D>()->enabled = false;
            if (how == 3) a->active = false;
            w.Steps(2.0f);
            std::cout << "    support removed (case " << how << "): upper crate at y " << P(b).y << "\n";
            CHECK(P(b).y < 1.0f);
        }

        // ---- A box asleep on a moving platform is carried along (the old step left it behind, in the air).
        {
            World w(q);
            GameObject* pf = w.s.CreateGameObject("Platform");
            pf->transform->localPosition = {0, -0.25f, 0};
            pf->AddComponent<BoxCollider3D>()->size = {40, 0.5f, 6};
            auto* pr = pf->AddComponent<Rigidbody3D>();
            pr->bodyType = Rigidbody3D::BodyType::Kinematic; pr->friction = 0.8f;
            GameObject* c = w.Crate({0, 0.5f, 0}, 1.0f, true, 0.8f);
            w.Steps(1.5f);
            CHECK(RB(c)->sleeping);
            pr->velocity = {2.0f, 0, 0};
            w.Steps(1.5f);
            std::cout << "    box on a moving platform: platform at x " << P(pf).x << ", box at x " << P(c).x << "\n";
            CHECK(P(c).x > 1.5f);                          // carried most of the way
            CHECK(P(c).y > 0.45f && P(c).y < 0.55f);       // still on it
        }

        // ---- A ray that starts inside a box gets a unit normal (the old step gave a zero-length one).
        {
            World w(q);
            GameObject* b = w.s.CreateGameObject("Box"); b->AddComponent<BoxCollider3D>()->size = {4, 4, 4};
            RaycastHit3D h = w.s.physics3D().Raycast(w.s, {0.5f, 0, 0}, {1, 0, 0}, 100.0f);
            CHECK(h.hit);
            CHECK_NEAR(h.normal.Magnitude(), 1.0f, 1e-3);
        }

        // ---- Stiff springs stay finite and come to rest (stiffness 20000 and 100000 at 50 Hz, a light ball with 1000, and a
        //      slow 30 Hz step with 5000 all went to NaN before).
        {
            struct Case { float k, mass, hz; };
            for (Case cs : {Case{20000.0f, 1.0f, 50.0f}, Case{100000.0f, 1.0f, 50.0f}, Case{1000.0f, 0.1f, 50.0f}, Case{5000.0f, 1.0f, 30.0f}}) {
                World w(q); w.dt = 1.0f / cs.hz;
                GameObject* b = w.s.CreateGameObject("Ball"); b->transform->localPosition = {0, -2.0f, 0};
                b->AddComponent<SphereCollider3D>()->radius = 0.2f;
                auto* rb = b->AddComponent<Rigidbody3D>(); rb->mass = cs.mass; rb->freezeRotation = true;
                auto* j = b->AddComponent<Joint3D>();
                j->mode = (int)Joint3D::Mode::Spring; j->anchor = {0, 0, 0}; j->autoConfigure = false; j->distance = 1.0f;
                j->spring = cs.k; j->damper = 2.0f;
                float maxAbs = 0; bool bad = false;
                for (int i = 0; i < (int)(cs.hz * 6); ++i) {
                    w.s.physics3D().Step(w.s, w.dt);
                    const Vec3 p = P(b);
                    if (!std::isfinite(p.x + p.y + p.z)) bad = true;
                    maxAbs = std::max(maxAbs, std::fabs(p.y));
                }
                const float eq = -1.0f - cs.mass * 9.81f / cs.k;
                std::cout << "    spring k=" << cs.k << " m=" << cs.mass << " at " << cs.hz << " Hz: end y " << P(b).y << " (rest " << eq << ")\n";
                CHECK(!bad);
                CHECK(maxAbs < 3.0f);
                CHECK_NEAR(P(b).y, eq, 0.15);
            }
        }
        // ---- A soft spring behaves as it always did (stiffness 30 at 50 Hz settles at -1 - m g / k).
        {
            World w(q);
            GameObject* b = w.s.CreateGameObject("Ball"); b->transform->localPosition = {0, -2.0f, 0};
            b->AddComponent<SphereCollider3D>()->radius = 0.2f;
            auto* rb = b->AddComponent<Rigidbody3D>(); rb->freezeRotation = true;
            auto* j = b->AddComponent<Joint3D>();
            j->mode = (int)Joint3D::Mode::Spring; j->anchor = {0, 0, 0}; j->autoConfigure = false; j->distance = 1.0f; j->spring = 30.0f; j->damper = 2.0f;
            w.Steps(8.0f);
            CHECK_NEAR(P(b).y, -1.0f - 9.81f / 30.0f, 0.05);
        }

        // ---- Continuous Collision (ticked) still stops a fast ball and a fast box at a thin wall.
        for (bool box : {false, true}) {
            World w(q);
            w.Floor(-50.0f);
            GameObject* wall = w.s.CreateGameObject("Wall"); wall->transform->localPosition = {10, 0, 0};
            wall->AddComponent<BoxCollider3D>()->size = {0.1f, 10, 10};
            GameObject* b = w.s.CreateGameObject("Bullet");
            if (box) b->AddComponent<BoxCollider3D>()->size = {0.2f, 0.2f, 0.2f}; else b->AddComponent<SphereCollider3D>()->radius = 0.1f;
            auto* rb = b->AddComponent<Rigidbody3D>(); rb->mass = 0.1f; rb->gravityScale = 0.0f; rb->freezeRotation = true;
            rb->velocity = {box ? 100.0f : 60.0f, 0, 0}; rb->continuousCollision = true;
            float maxX = -1e9f;
            for (int i = 0; i < 50; ++i) { w.s.physics3D().Step(w.s, w.dt); maxX = std::max(maxX, P(b).x); }
            std::cout << "    ticked bullet (" << (box ? "box" : "ball") << ") max x " << maxX << "\n";
            CHECK(maxX < 10.2f);
        }
    }

    // ---- Classic keeps its old behaviour: an unticked fast ball passes through a thin wall. (Improved stops it: see
    //      test_physics_deep_improved.) This guards the promise that old scenes play exactly as they did.
    {
        World w(PhysicsQuality::Classic);
        GameObject* wall = w.s.CreateGameObject("Wall"); wall->transform->localPosition = {10, 0, 0};
        wall->AddComponent<BoxCollider3D>()->size = {0.1f, 10, 10};
        GameObject* b = w.s.CreateGameObject("Bullet");
        b->AddComponent<SphereCollider3D>()->radius = 0.1f;
        auto* rb = b->AddComponent<Rigidbody3D>(); rb->mass = 0.1f; rb->gravityScale = 0.0f; rb->freezeRotation = true; rb->velocity = {60.0f, 0, 0};
        float maxX = -1e9f;
        for (int i = 0; i < 50; ++i) { w.s.physics3D().Step(w.s, w.dt); maxX = std::max(maxX, P(b).x); }
        CHECK(maxX > 30.0f);
    }
    TEST_MAIN_RESULT();
}
