// Physics upgrade, step 1 (solver quality): steady towers at every step rate, fast bodies that cannot jump over thin moving
// plates, momentum and energy kept, nothing turns into NaN. Numbers before and after are in docs/physics_upgrade.md.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Joint3D.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace {
struct World {
    Scene s{"w"}; float dt; int n = 0;
    explicit World(float hz = 50.0f) : dt(1.0f / hz) { s.physics3D().quality = PhysicsQuality::Improved; }
    GameObject* Floor() { auto* f = s.CreateGameObject("Floor"); f->transform->localPosition = {0, -0.5f, 0}; f->AddComponent<BoxCollider3D>()->size = {400, 1, 400}; return f; }
    GameObject* Box(Vec3 p, Vec3 sz = {1, 1, 1}, float m = 1.0f) {
        auto* o = s.CreateGameObject("B" + std::to_string(n++)); o->transform->localPosition = p;
        o->AddComponent<BoxCollider3D>()->size = sz; auto* r = o->AddComponent<Rigidbody3D>(); r->mass = m; r->freezeRotation = false; return o;
    }
    GameObject* Ball(Vec3 p, float rad = 0.5f, float m = 1.0f) {
        auto* o = s.CreateGameObject("S" + std::to_string(n++)); o->transform->localPosition = p;
        o->AddComponent<SphereCollider3D>()->radius = rad; auto* r = o->AddComponent<Rigidbody3D>(); r->mass = m; r->freezeRotation = false; return o;
    }
    void Run(float sec) { const int k = (int)std::lround(sec / dt); for (int i = 0; i < k; ++i) s.physics3D().Step(s, dt); }
};
Vec3 P(GameObject* o) { return o->transform->Position(); }
Rigidbody3D* R(GameObject* o) { return o->GetComponent<Rigidbody3D>(); }
float Tilt(GameObject* o) { Vec3 up = o->transform->Rotation() * Vec3{0, 1, 0}; return std::acos(std::max(-1.0f, std::min(1.0f, up.y))) * 57.29578f; }
bool Finite(GameObject* o) { Vec3 p = P(o); return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
}

// A callback that sets a speed while a contact is being handled (a jump pad, a blast) must keep it.
struct Kick : Component {
    Vec3 v{0, 12, 0};
    void OnCollisionEnter3D(const Collision3D& c) override {
        if (c.gameObject && c.gameObject->name == "Floor") return;
        if (auto* r = gameObject->GetComponent<Rigidbody3D>()) r->velocity = v;
    }
};

int main() {
    RUN_SUITE("physics_upgrade");

    // ---- A tower of ten crates (default 10 solver passes, free rotation) stands, and sleeps, at every step rate. Before: it
    // fell over at 30 steps a second (drift 11.5 m) and leaned 11.8 degrees at 50.
    for (float hz : {30.0f, 50.0f, 60.0f, 144.0f}) {
        World w(hz); w.Floor();
        std::vector<GameObject*> c;
        for (int i = 0; i < 10; ++i) c.push_back(w.Box({0, 0.5f + (float)i, 0}));
        w.Run(10.0f);
        float drift = 0, tilt = 0; int asleep = 0; bool fin = true;
        for (GameObject* g : c) { drift = std::max(drift, std::hypot(P(g).x, P(g).z)); tilt = std::max(tilt, Tilt(g)); asleep += R(g)->sleeping; fin = fin && Finite(g); }
        std::cout << "  10-stack " << hz << " Hz: top " << P(c.back()).y << " drift " << drift << " tilt " << tilt << " asleep " << asleep << "/10\n";
        CHECK(fin);
        CHECK_NEAR(P(c.back()).y, 9.5f, 0.03);
        CHECK(drift < 0.1f);
        CHECK(tilt < 1.0f);
        CHECK(asleep == 10);
    }
    // ---- The same tower built top first (the solver must not depend on the order the objects were made in).
    {
        World w(50.0f); w.Floor();
        std::vector<GameObject*> c(10);
        for (int i = 9; i >= 0; --i) c[(std::size_t)i] = w.Box({0, 0.5f + (float)i, 0});
        w.Run(10.0f);
        std::cout << "  10-stack made top first: top " << P(c[9]).y << "\n";
        CHECK_NEAR(P(c[9]).y, 9.5f, 0.03);
    }
    // ---- A heavy crate on top of the tower (30 kg on 1 kg crates) holds at every rate.
    for (float hz : {30.0f, 60.0f, 144.0f}) {
        World w(hz); w.Floor();
        std::vector<GameObject*> c;
        for (int i = 0; i < 5; ++i) c.push_back(w.Box({0, 0.5f + (float)i, 0}, {1, 1, 1}, i == 4 ? 30.0f : 1.0f));
        w.Run(10.0f);
        CHECK_NEAR(P(c.back()).y, 4.5f, 0.03);
        CHECK(Tilt(c.back()) < 1.0f);
    }
    // ---- A fast ball cannot jump over a thin plate that is itself free to move (it used to pass through it at 30, 60 and 144
    // Hz: the automatic sweep only looked at walls and floors that cannot move).
    for (float hz : {30.0f, 60.0f, 144.0f}) {
        World w(hz); w.Floor();
        GameObject* plate = w.Box({-10, 1, 0}, {0.05f, 2, 2}); R(plate)->gravityScale = 0;
        GameObject* ball = w.Ball({-30, 1, 0}, 0.1f); R(ball)->velocity = {100, 0, 0}; R(ball)->gravityScale = 0;
        w.Run(0.5f);
        std::cout << "  ball at 100 m/s vs thin plate, " << hz << " Hz: ball x " << P(ball).x << " plate x " << P(plate).x << "\n";
        CHECK(P(plate).x > -9.0f);                 // the plate was hit and was sent on its way
        CHECK(P(ball).x < P(plate).x + 0.5f);       // the ball did not get ahead of it
    }
    // ---- Momentum and energy of an equal-bounce hit are kept at every rate; nothing goes NaN.
    for (float hz : {30.0f, 60.0f, 144.0f}) {
        World w(hz); w.s.physics3D().gravity = Vec3::Zero;
        GameObject* a = w.Ball({-2, 0, 0}, 0.5f, 1.0f); GameObject* b = w.Ball({2, 0, 0}, 0.5f, 3.0f);
        R(a)->bounciness = 1; R(b)->bounciness = 1; R(a)->velocity = {4, 0, 0}; R(b)->velocity = {-1, 0, 0};
        w.Run(2.0f);
        const float p = R(a)->velocity.x + 3.0f * R(b)->velocity.x;
        const float e = 0.5f * (R(a)->velocity.x * R(a)->velocity.x + 3.0f * R(b)->velocity.x * R(b)->velocity.x);
        CHECK_NEAR(p, 1.0f, 0.01); CHECK_NEAR(e, 9.5f, 0.05); CHECK(Finite(a) && Finite(b));
    }
    // ---- A crate resting for 10 seconds does not buzz (asleep, never moves).
    {
        World w(50.0f); w.Floor(); GameObject* c = w.Box({0, 0.6f, 0}); w.Run(10.0f);
        CHECK(R(c)->sleeping); CHECK_NEAR(P(c).y, 0.5f, 0.01);
    }
    // ---- A speed set inside OnCollisionEnter3D is not overwritten by the solve (a jump pad launched nothing before).
    {
        World w(50.0f); w.Floor();
        GameObject* pad = w.Box({0, 0.1f, 0}, {2, 0.2f, 2}); R(pad)->bodyType = Rigidbody3D::BodyType::Static;
        GameObject* ball = w.Ball({0, 1.5f, 0}); ball->AddComponent<Kick>();
        float peak = 0;
        for (int i = 0; i < 100; ++i) { w.Run(0.02f); peak = std::max(peak, P(ball).y); }
        std::cout << "  ball launched by a callback: peak " << peak << " m (12 m/s should reach about 7)\n";
        CHECK(peak > 5.0f);
    }
    // ---- Joints under a 50:1 mass ratio: a rope of eight 1 kg balls with a 50 kg ball on the end hangs close to its real length at every
    // step rate (it hung 0.7 m, 6 m and 10.1 m of the true 11 m at 30, 60 and 144 Hz before the joint passes were ordered both ways).
    for (float hz : {30.0f, 50.0f, 60.0f, 144.0f}) {
        World w(hz); w.Floor();
        GameObject* prev = nullptr;
        for (int i = 0; i < 8; ++i) {
            GameObject* b = w.Ball({0, 19.0f - (float)i, 0}, 0.2f, 1.0f); R(b)->freezeRotation = true;
            auto* j = b->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance;
            if (prev) j->connectedBody = prev->name; else j->anchor = {0, 20, 0};
            prev = b;
        }
        GameObject* heavy = w.Ball({0, 11.0f, 0}, 0.5f, 50.0f); R(heavy)->freezeRotation = true;
        auto* jh = heavy->AddComponent<Joint3D>(); jh->mode = (int)Joint3D::Mode::Distance; jh->connectedBody = prev->name;
        w.Run(10.0f);
        std::cout << "  50:1 rope " << hz << " Hz: end at y " << P(heavy).y << " (true 11)\n";
        CHECK(Finite(heavy));
        CHECK(P(heavy).y > 10.2f && P(heavy).y < 11.05f);
    }
    // ---- A fixed (pin) joint between a 50 kg box and a 1 kg box keeps their gap and does not fall or spin off.
    for (float hz : {30.0f, 60.0f, 144.0f}) {
        World w(hz); w.Floor();
        GameObject* a = w.Box({0, 5, 0}, {1, 1, 1}, 50.0f); GameObject* b = w.Box({1.2f, 5, 0}, {0.5f, 0.5f, 0.5f}, 1.0f);
        auto* j0 = a->AddComponent<Joint3D>(); j0->mode = (int)Joint3D::Mode::Pin; j0->anchor = {0, 5, 0};
        auto* j = b->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Pin; j->connectedBody = a->name;
        w.Run(5.0f);
        CHECK(Finite(a) && Finite(b));
        CHECK((P(b) - P(a) - Vec3{1.2f, 0, 0}).Magnitude() < 0.02f);
        CHECK_NEAR(P(a).y, 5.0, 0.05);
    }
    // ---- A pendulum keeps its energy (the old step lost 31 percent in 20 s at 50 Hz; now under 6 percent in 10 s at any rate).
    for (float hz : {30.0f, 60.0f, 144.0f}) {
        World w(hz);
        GameObject* b = w.Ball({2.12f, 10.0f - 2.12f, 0}, 0.2f, 1.0f);
        auto* j = b->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->anchor = {0, 10, 0};
        w.Run(10.0f);
        const float v2 = R(b)->velocity.SqrMagnitude();
        const float e = 0.5f * v2 + 9.81f * P(b).y, e0 = 9.81f * (10.0f - 2.12f);
        CHECK(e / e0 > 0.93f && e / e0 < 1.02f);
    }
    // ---- A breakable joint snaps when pulled hard and then lets the body go.
    {
        World w(60.0f); w.s.physics3D().gravity = Vec3::Zero;
        GameObject* b = w.Ball({1, 0, 0}, 0.2f, 1.0f);
        auto* j = b->AddComponent<Joint3D>(); j->mode = (int)Joint3D::Mode::Distance; j->anchor = {0, 0, 0}; j->breakable = true; j->breakForce = 0.3f;
        w.Run(0.5f); CHECK(!j->broken);
        R(b)->velocity = {20, 0, 0}; w.Run(0.5f);
        CHECK(j->broken); CHECK(P(b).x > 5.0f);
    }
    TEST_MAIN_RESULT();
}
