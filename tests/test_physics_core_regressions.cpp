// Regressions found by the independent review of our physics core (2026-11-22), each a probe that failed before its fix.
// Numbers the old Improved step gave are named where the review compared with it.
//  1  a collider stays when its Rigidbody3D is switched off, and a new collider never vanishes (Improved: ball on ledge 5.30)
//  2  a joint whose body is switched off never takes an unrelated joint with it
//  3  a fast ball hitting a sleeping crate shares its momentum (Improved: 30 / 30 m/s, not 9 / 9)
//  4  a shot spawned 2 m from a 2 cm wall with its speed set, and a resting ball given 300 m/s, do not pass the wall
//  5  one body set to NaN or infinity does not poison the others
//  6  jointed bodies (a chain, a door) fall asleep
//  7  2,000 resting crates: the event pass is a small part of the step
//  8  a compound body landing gives one Enter (not Enter, Exit, Enter); changing a resting collider's friction gives no Exit
//  9  a mesh collider with no mesh still collides as its size box
// 10  a distance joint whose partner is destroyed keeps its length (no fling)
// 11  a kinematic body teleported by game code does not shove what it lands next to
// 12  determinism: the same scene with 0, 1, 3 and 7 job workers gives the same bits
#include "test_framework.hpp"
#include "physics_test_scenes.hpp"
#include <Okay.hpp>
#include "okay/Core/StringId.hpp"

#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace okay;

namespace {
constexpr float kDt = 1.0f / 60.0f;

struct Counter : public Behaviour {
    std::string log;   // E, S (first one only), X
    bool stayLogged = false;
    void OnCollisionEnter3D(const Collision3D&) override { log += "E"; stayLogged = false; }
    void OnCollisionStay3D(const Collision3D&) override { if (!stayLogged) { log += "S"; stayLogged = true; } }
    void OnCollisionExit3D(const Collision3D&) override { log += "X"; }
};

void Run(Scene& s, float seconds) { const int n = (int)std::lround(seconds / kDt); for (int i = 0; i < n; ++i) s.Update(kDt); }
GameObject* Box(Scene& s, const std::string& n, const Vec3& p, const Vec3& size, bool dynamic) {
    GameObject* g = s.CreateGameObject(n);
    g->transform->localPosition = p;
    g->AddComponent<BoxCollider3D>()->size = size;
    if (dynamic) { auto* rb = g->AddComponent<Rigidbody3D>(); rb->freezeRotation = false; }
    return g;
}
GameObject* Ball(Scene& s, const std::string& n, const Vec3& p, float r, float mass = 1.0f) {
    GameObject* g = s.CreateGameObject(n);
    g->transform->localPosition = p;
    g->AddComponent<SphereCollider3D>()->radius = r;
    auto* rb = g->AddComponent<Rigidbody3D>();
    rb->mass = mass;
    rb->freezeRotation = false;
    return g;
}
GameObject* Floor(Scene& s) { return Box(s, "Floor", {0, -0.5f, 0}, {200, 1, 200}, false); }
double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::uint64_t HashScene(Scene& s) {
    std::vector<unsigned char> b;
    for (const auto& go : s.Objects()) {
        okaytest::PutBytes(b, go->transform->localPosition);
        okaytest::PutBytes(b, go->transform->localRotation);
    }
    return StringId::HashOf(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
}
} // namespace

int main() {
    RUN_SUITE("1  switching a body off keeps its collider; new colliders never vanish");
    {
        Scene s("r1");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        Floor(s);
        GameObject* crate = Box(s, "Crate", {-5, 0.5f, 0}, {1, 1, 1}, true);
        s.Start();
        Run(s, 0.5f);
        crate->GetComponent<Rigidbody3D>()->enabled = false;   // the body goes, the collider stays (a static box now)
        Run(s, 0.1f);
        Box(s, "Ledge", {0, 5, 0}, {2, 0.2f, 2}, false);       // added the frame after
        GameObject* ball = Ball(s, "Ball", {0, 8, 0}, 0.2f);
        GameObject* onCrate = Ball(s, "OnCrate", {-5, 3, 0}, 0.2f);
        Run(s, 3.0f);
        std::cout << "  ball on the ledge at y " << ball->transform->Position().y << " (Improved 5.30), ball on the crate at "
                  << onCrate->transform->Position().y << " (1.2)\n";
        CHECK(std::fabs(ball->transform->Position().y - 5.3f) < 0.05f);
        CHECK(std::fabs(onCrate->transform->Position().y - 1.2f) < 0.05f);
    }

    RUN_SUITE("2  a joint whose body is switched off never removes an unrelated joint");
    {
        Scene s("r2");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        GameObject* a = Ball(s, "A", {0, 5, 0}, 0.2f);
        auto* ja = a->AddComponent<Joint3D>(); ja->mode = (int)Joint3D::Mode::Distance; ja->anchor = {0, 7, 0};
        GameObject* b = Ball(s, "B", {5, 5, 0}, 0.2f);
        s.Start();
        Run(s, 0.3f);
        a->GetComponent<Rigidbody3D>()->enabled = false;   // A's body (and with it A's joint) goes
        auto* jb = b->AddComponent<Joint3D>(); jb->mode = (int)Joint3D::Mode::Distance; jb->anchor = {5, 7, 0};   // a new joint
        // (B has been falling for 0.3 s, so the joint takes the length it finds when it starts: measured here)
        const float len0 = Vec3::Distance(b->transform->Position(), {5, 7, 0});
        Run(s, 3.0f);
        const float len = Vec3::Distance(b->transform->Position(), {5, 7, 0});
        std::cout << "  the new joint holds B at " << len << " m from its anchor (" << len0 << " when it was added)\n";
        CHECK(std::fabs(len - len0) < 0.05f);
    }

    RUN_SUITE("3  a fast ball hitting a sleeping crate shares its momentum");
    {
        Scene s("r3");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        s.physics3D().gravity = Vec3::Zero;
        GameObject* crate = Box(s, "Crate", {10, 0, 0}, {1, 1, 1}, true);
        crate->GetComponent<Rigidbody3D>()->mass = 2.0f;
        crate->GetComponent<Rigidbody3D>()->sleeping = true;
        GameObject* ball = Ball(s, "Ball", {0, 0, 0}, 0.3f, 2.0f);
        ball->GetComponent<Rigidbody3D>()->velocity = {60, 0, 0};
        s.Start();
        Run(s, 0.5f);
        const float vb = ball->GetComponent<Rigidbody3D>()->velocity.x, vc = crate->GetComponent<Rigidbody3D>()->velocity.x;
        std::cout << "  after the hit: ball " << vb << " m/s, crate " << vc << " m/s (Improved 30 / 30; total 60)\n";
        CHECK(std::fabs((vb + vc) - 60.0f) < 3.0f);   // momentum kept (equal masses)
        CHECK(vc > 25.0f);
    }
    {   // the same on a floor: the crate fell asleep there, the ball skims along the floor into it
        Scene s("r3b");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        Floor(s);
        GameObject* crate = Box(s, "Crate", {10, 0.5f, 0}, {1, 1, 1}, true);
        crate->GetComponent<Rigidbody3D>()->mass = 2.0f;
        s.Start();
        Run(s, 2.0f);   // asleep on the floor
        const bool slept = crate->GetComponent<Rigidbody3D>()->sleeping;
        GameObject* ball = Ball(s, "Ball", {0, 0.5f, 0}, 0.3f, 2.0f);
        ball->GetComponent<Rigidbody3D>()->velocity = {60, 0, 0};
        ball->GetComponent<Rigidbody3D>()->freezeRotation = true;
        float most = 0.0f;
        for (int i = 0; i < 20; ++i) { s.Update(kDt); most = std::max(most, crate->GetComponent<Rigidbody3D>()->velocity.x); }
        std::cout << "  on a floor: the crate was asleep " << slept << ", fastest after the hit " << most << " m/s (about 30)\n";
        CHECK(slept);
        CHECK(most > 20.0f);
    }

    RUN_SUITE("4  shots that start close to a thin wall, or get their speed while resting, do not pass it");
    {
        Scene s("r4");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        s.physics3D().gravity = Vec3::Zero;
        Box(s, "Wall", {2, 0, 0}, {0.02f, 4, 4}, false);
        GameObject* shot = Ball(s, "Shot", {0, 0, 0}, 0.05f, 0.05f);
        shot->GetComponent<Rigidbody3D>()->velocity = {300, 0, 0};
        GameObject* rest = Ball(s, "Rest", {0, 2, 0}, 0.05f, 0.05f);
        s.Start();
        Run(s, 0.5f);   // the resting ball sleeps
        rest->GetComponent<Rigidbody3D>()->velocity = {300, 0, 0};
        Run(s, 0.5f);
        std::cout << "  shot x " << shot->transform->Position().x << ", rested ball x " << rest->transform->Position().x << " (wall at 2)\n";
        CHECK(shot->transform->Position().x < 2.0f);
        CHECK(rest->transform->Position().x < 2.0f);
    }

    RUN_SUITE("5  one NaN body does not poison the others");
    {
        Scene s("r5");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        Floor(s);
        std::vector<GameObject*> crates;
        for (int i = 0; i < 5; ++i) crates.push_back(Box(s, "C" + std::to_string(i), {(float)i * 0.9f, 0.5f, 0}, {1, 1, 1}, true));
        s.Start();
        Run(s, 0.5f);
        crates[2]->transform->localPosition = {NAN, 0.5f, 0};
        crates[3]->GetComponent<Rigidbody3D>()->velocity = {INFINITY, 0, 0};
        Run(s, 1.0f);
        bool othersFine = true;
        for (int i : {0, 1, 4}) {
            const Vec3 p = crates[(std::size_t)i]->transform->Position();
            othersFine = othersFine && std::isfinite(p.x + p.y + p.z) && std::fabs(p.y - 0.5f) < 0.1f;
        }
        CHECK(othersFine);
    }

    RUN_SUITE("6  jointed bodies fall asleep");
    {
        Scene s("r6");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        std::vector<GameObject*> links;
        std::string prev;
        for (int i = 0; i < 5; ++i) {
            GameObject* g = Ball(s, "L" + std::to_string(i), {0, 9.0f - (float)i, 0}, 0.2f);
            auto* j = g->AddComponent<Joint3D>();
            j->mode = (int)Joint3D::Mode::Ball;
            j->connectedBody = prev;
            j->anchor = {0, 9.5f - (float)i, 0};
            g->GetComponent<Rigidbody3D>()->angularDrag = 0.5f;
            g->GetComponent<Rigidbody3D>()->drag = 0.3f;
            prev = g->name;
            links.push_back(g);
        }
        s.Start();
        Run(s, 12.0f);
        int asleep = 0;
        for (GameObject* g : links) asleep += g->GetComponent<Rigidbody3D>()->sleeping;
        std::cout << "  " << asleep << " of 5 hanging links asleep\n";
        CHECK(asleep == 5);
    }

    RUN_SUITE("7  the event pass stays cheap with 2,000 resting crates");
    {
        Scene s("r7");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        Floor(s);
        for (int i = 0; i < 2000; ++i)
            Box(s, "C" + std::to_string(i), {(float)(i % 45) * 1.1f - 25, 0.5f + (float)(i / 900) * 1.05f, (float)((i / 45) % 20) * 1.1f - 11}, {1, 1, 1}, true)
                ->AddComponent<Counter>();
        s.Start();
        Run(s, 1.0f);
        const double t0 = NowMs();
        Run(s, 0.5f);
        const double ms = (NowMs() - t0) / 30.0;
        std::cout << "  2,000 crates: " << ms << " ms per frame (the review measured 1,033)\n";
        CHECK(ms < 200.0);
    }

    RUN_SUITE("8  one Enter for a landing compound body; a friction change on a resting body raises no Exit");
    {
        Scene s("r8");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        Floor(s);
        GameObject* g = s.CreateGameObject("Compound");
        g->transform->localPosition = {0, 2, 0};
        auto* cc = g->AddComponent<CompoundCollider3D>();
        cc->AddBox({-0.4f, 0, 0}, {0.6f, 0.3f, 0.6f});
        cc->AddBox({0.4f, 0, 0}, {0.6f, 0.3f, 0.6f});
        g->AddComponent<Rigidbody3D>();
        auto* counter = g->AddComponent<Counter>();
        GameObject* r = Box(s, "Rest", {5, 0.5f, 0}, {1, 1, 1}, true);
        auto* rc = r->AddComponent<Counter>();
        s.Start();
        Run(s, 2.0f);
        r->GetComponent<BoxCollider3D>()->friction = 0.9f;   // rebuilt at run time
        Run(s, 0.5f);
        std::cout << "  compound callbacks " << counter->log << " (ES), friction change " << rc->log << " (ES)\n";
        CHECK(counter->log == "ES");
        CHECK(rc->log == "ES");
    }

    RUN_SUITE("9  a mesh collider with no mesh collides as its box");
    {
        Scene s("r9");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        GameObject* m = s.CreateGameObject("EmptyMesh");
        m->transform->localPosition = {0, -0.5f, 0};
        m->AddComponent<MeshCollider3D>()->size = {10, 1, 10};
        GameObject* ball = Ball(s, "Ball", {0, 3, 0}, 0.25f);
        s.Start();
        Run(s, 2.0f);
        std::cout << "  ball y " << ball->transform->Position().y << " (0.25)\n";
        CHECK(std::fabs(ball->transform->Position().y - 0.25f) < 0.05f);
    }

    RUN_SUITE("10 a distance joint whose partner is destroyed keeps its length");
    {
        Scene s("r10");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        s.physics3D().gravity = Vec3::Zero;
        GameObject* hub = Ball(s, "Hub", {0, 5, 0}, 0.2f);
        GameObject* bob = Ball(s, "Bob", {2, 5, 0}, 0.2f);
        auto* j = bob->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->connectedBody = "Hub"; j->anchor = {0, 5, 0};
        s.Start();
        Run(s, 0.2f);
        s.Destroy(hub);
        Run(s, 1.0f);
        const Vec3 p = bob->transform->Position();
        std::cout << "  bob at (" << p.x << ", " << p.y << ") after its partner went (stays near x 2)\n";
        CHECK(std::fabs(p.x - 2.0f) < 0.1f && std::fabs(p.y - 5.0f) < 0.1f);
    }

    RUN_SUITE("11 a teleported kinematic body does not shove its neighbours");
    {
        Scene s("r11");
        s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
        Floor(s);
        GameObject* k = Box(s, "Platform", {0, 0.5f, 0}, {1, 1, 1}, false);
        k->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
        GameObject* c = Box(s, "Crate", {50.0f, 0.5f, 1.2f}, {1, 1, 1}, true);
        s.Start();
        Run(s, 0.5f);
        k->transform->localPosition = {50.0f, 0.5f, 0};   // a 50 m jump next to the crate
        Run(s, 0.5f);
        const Vec3 v = c->GetComponent<Rigidbody3D>()->velocity;
        std::cout << "  crate speed after the platform appeared next to it: " << v.Magnitude() << " m/s\n";
        CHECK(v.Magnitude() < 1.0f);
    }

    RUN_SUITE("12 determinism with 0, 1, 3 and 7 job workers");
    {
        std::uint64_t h[4];
        const int workers[4] = {0, 1, 3, 7};
        for (int k = 0; k < 4; ++k) {
            JobSystem js(workers[k]);
            SetEngineJobs(&js);
            {
                Scene s("r12");
                s.physics3D().quality = PhysicsQuality::Engine;   // (no effect now; it made the old build use its engine core)
                okaytest::BuildMixedBodies(s, 300, PhysicsQuality::Engine, true);
                s.Start();
                Run(s, 1.0f);
                h[k] = HashScene(s);
            }
            SetEngineJobs(nullptr);
        }
        CHECK(h[0] == h[1] && h[1] == h[2] && h[2] == h[3]);
    }
    TEST_MAIN_RESULT();
}
