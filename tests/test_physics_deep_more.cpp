// Second batch from the physics audit (docs/physics_plan.md): the joint partner is remembered (not searched by name every step),
// ropes stay near their length with the Improved solver, a Slider joint, exit callbacks in scene order, the debug view's bodies
// and joints, and the pooled debris helper.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
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
    GameObject* Floor() {
        GameObject* f = s.CreateGameObject("Floor");
        f->transform->localPosition = {0, -0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
        return f;
    }
    GameObject* Ball(Vec3 p, float radius, float mass, const std::string& name) {
        GameObject* o = s.CreateGameObject(name);
        o->transform->localPosition = p;
        o->AddComponent<SphereCollider3D>()->radius = radius;
        auto* r = o->AddComponent<Rigidbody3D>();
        r->mass = mass; r->freezeRotation = true;
        return o;
    }
    void Step() { s.physics3D().Step(s, dt); }
    void Steps(float seconds) { const int k = (int)std::lround(seconds / dt); for (int i = 0; i < k; ++i) Step(); }
};
Vec3 P(GameObject* o) { return o->transform->Position(); }
Rigidbody3D* RB(GameObject* o) { return o->GetComponent<Rigidbody3D>(); }

// A rope of `n` balls hanging from a hook, `endMass` on the last one. Returns how far the end hangs below the hook.
float RopeLength(PhysicsQuality q, int n, float endMass) {
    World w(q);
    GameObject* prev = nullptr;
    GameObject* last = nullptr;
    for (int i = 0; i < n; ++i) {
        GameObject* b = w.Ball({0, -1.0f - (float)i, 0}, 0.2f, i == n - 1 ? endMass : 1.0f, "Link" + std::to_string(i));
        auto* j = b->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance;
        if (prev) { j->connectedBody = prev->name; j->autoConfigure = true; }
        else { j->anchor = {0, 0, 0}; j->autoConfigure = false; j->distance = 1.0f; }
        prev = last = b;
    }
    w.Steps(4.0f);
    return -P(last).y;
}

class ExitLog : public Behaviour {
public:
    static std::vector<std::string>& Log() { static std::vector<std::string> l; return l; }
    void OnCollisionExit3D(const Collision3D&) override { Log().push_back(gameObject->name); }
};

class Counts : public Behaviour {
public:
    int enter = 0, exit = 0;
    void OnCollisionEnter3D(const Collision3D&) override { ++enter; }
    void OnCollisionExit3D(const Collision3D&) override { ++exit; }
};

} // namespace

int main() {
    RUN_SUITE("physics_deep_more");

    // ---- Joint partner: found once, followed when objects change (a renamed or replaced partner is found again).
    {
        World w(PhysicsQuality::Classic);
        w.s.physics3D().gravity = Vec3::Zero;
        GameObject* a = w.Ball({0, 0, 0}, 0.2f, 1.0f, "A");
        GameObject* b = w.Ball({2, 0, 0}, 0.2f, 1.0f, "B");
        auto* j = a->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance; j->connectedBody = "B"; j->autoConfigure = true;
        w.Steps(0.2f);
        CHECK(j->partner.object == b);
        const std::uint64_t v0 = j->partner.version;
        w.Steps(0.2f);
        CHECK(j->partner.version == v0);                     // not looked up again while nothing changes
        RB(b)->velocity = {0, 0, 3.0f};
        w.Steps(1.0f);
        CHECK_NEAR((P(a) - P(b)).Magnitude(), 2.0f, 0.1f);   // still holds the pair together
        // The partner is destroyed and another object takes the name: the joint follows the new one.
        w.s.DestroyImmediate(b);
        GameObject* b2 = w.Ball({0, 2, 0}, 0.2f, 1.0f, "B");
        w.Steps(0.1f);
        CHECK(j->partner.object == b2);
        // A copy of the joint starts with nothing remembered.
        Joint3D copy(*j);
        CHECK(copy.partner.object == nullptr);
    }

    // ---- A rope of balls stays near its length with the Improved solver (Classic stretched it to 10.9 m; 64 m with a heavy end).
    for (float endMass : {1.0f, 50.0f}) {
        const float classic = RopeLength(PhysicsQuality::Classic, 8, endMass);
        const float improved = RopeLength(PhysicsQuality::Improved, 8, endMass);
        std::cout << "  rope of 8, end mass " << endMass << ": hangs " << classic << " m (Classic), " << improved << " m (Improved), rest 8 m\n";
        CHECK(improved < classic);
        CHECK(improved < (endMass > 10.0f ? 14.0f : 8.2f));   // (a 50 kg weight on 1 kg links is still 5 m too long: see docs/physics_plan.md)
    }

    // ---- Slider: moves along its axis only, stops at its limits, a motor drives it, it cannot turn.
    {
        World w;
        w.s.physics3D().gravity = Vec3::Zero;
        GameObject* box = w.Ball({0, 0, 0}, 0.3f, 1.0f, "Slide");
        auto* j = box->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Slider; j->anchor = {0, 0, 0}; j->axis = {1, 0, 0};
        j->useLimits = true; j->minAngle = -0.5f; j->maxAngle = 1.5f;
        RB(box)->freezeRotation = false;
        RB(box)->velocity = {3.0f, 2.0f, -1.0f};             // pushed along and sideways
        RB(box)->angularVelocity = {0, 4.0f, 0};
        w.Steps(2.0f);
        std::cout << "  slider pushed at (3,2,-1): at " << P(box).x << " " << P(box).y << " " << P(box).z << "\n";
        CHECK_NEAR(P(box).y, 0.0f, 0.02f);
        CHECK_NEAR(P(box).z, 0.0f, 0.02f);
        CHECK(P(box).x <= 1.52f);                             // stopped at the upper limit
        CHECK(P(box).x > 1.3f);
        CHECK(RB(box)->angularVelocity.Magnitude() < 0.01f);
        j->useMotor = true; j->motorSpeed = -2.0f; j->maxMotorTorque = 100.0f;
        w.Steps(1.0f);
        CHECK_NEAR(P(box).x, -0.5f, 0.05f);                   // driven back to the lower limit
        // With gravity along the axis a vertical slider hangs at its lower limit, a "lift" held up by the motor.
        World g;
        GameObject* lift = g.Ball({0, 0, 0}, 0.3f, 2.0f, "Lift");
        auto* jl = lift->AddComponent<Joint3D>();
        jl->mode = (int)Joint3D::Mode::Slider; jl->anchor = {0, 0, 0}; jl->axis = {0, 1, 0};
        jl->useLimits = true; jl->minAngle = -2.0f; jl->maxAngle = 3.0f;
        g.Steps(2.0f);
        CHECK_NEAR(P(lift).y, -2.0f, 0.05f);                  // fell to the lower stop
        jl->useMotor = true; jl->motorSpeed = 1.5f; jl->maxMotorTorque = 500.0f;
        g.Steps(2.0f);
        std::cout << "  lift driven up at 1.5 m/s for 2 s from -2: y " << P(lift).y << "\n";
        CHECK(P(lift).y > 0.5f);
    }

    // ---- Exit callbacks arrive in the order the scene lists the colliders, not memory order.
    for (PhysicsQuality q : {PhysicsQuality::Classic, PhysicsQuality::Improved}) {
        World w(q);
        w.Floor();
        std::vector<GameObject*> crates;
        // Make and free some objects first so new ones do not land at rising addresses.
        for (int i = 0; i < 40; ++i) { GameObject* junk = w.Ball({100, 0, (float)i}, 0.1f, 1.0f, "junk"); w.s.DestroyImmediate(junk); }
        for (int i = 0; i < 12; ++i) {
            GameObject* c = w.s.CreateGameObject("C" + std::to_string(100 + i));
            c->transform->localPosition = {(float)i * 3.0f, 0.5f, 0};
            c->AddComponent<BoxCollider3D>();
            auto* r = c->AddComponent<Rigidbody3D>(); r->freezeRotation = true; r->allowSleep = false;
            c->AddComponent<ExitLog>();
            crates.push_back(c);
        }
        w.Steps(0.5f);
        ExitLog::Log().clear();
        for (GameObject* c : crates) c->transform->localPosition = c->transform->localPosition + Vec3{0, 5.0f, 0};   // all leave the floor in one step
        w.Step();
        std::vector<std::string> want;
        for (GameObject* c : crates) want.push_back(c->name);
        std::cout << "  exit callbacks (" << (q == PhysicsQuality::Improved ? "Improved" : "Classic") << "): " << ExitLog::Log().size() << " of 12\n";
        CHECK(ExitLog::Log().size() == 12);
        CHECK(ExitLog::Log() == want);
    }

    // ---- A box that comes to rest and sleeps does not flicker: one enter, at most one exit, no wake-and-sleep cycles
    //      (waking a body whose support is "lost" must not trigger on a sleeper that rests exactly touching).
    for (PhysicsQuality q : {PhysicsQuality::Classic, PhysicsQuality::Improved}) {
        World w(q);
        w.Floor();
        GameObject* c = w.s.CreateGameObject("Crate");
        c->transform->localPosition = {0, 0.5f, 0};
        c->AddComponent<BoxCollider3D>();
        auto* r = c->AddComponent<Rigidbody3D>(); r->freezeRotation = true;
        auto* n = c->AddComponent<Counts>();
        w.Steps(5.0f);
        std::cout << "  resting box (" << (q == PhysicsQuality::Improved ? "Improved" : "Classic") << "): enter " << n->enter << ", exit " << n->exit << ", asleep " << r->sleeping << "\n";
        CHECK(n->enter == 1);
        CHECK(n->exit <= 1);
        CHECK(r->sleeping);
    }

    // ---- The debug view lists bodies (sleeping marked) and joints while it collects.
    {
        World w;
        w.Floor();
        GameObject* a = w.Ball({0, 0.3f, 0}, 0.3f, 1.0f, "A");
        GameObject* b = w.Ball({2, 0.3f, 0}, 0.3f, 1.0f, "B");
        auto* j = b->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance; j->connectedBody = "A"; j->autoConfigure = true;
        PhysicsDebug::Collect() = true;
        w.s.physics3D().BeginDebugFrame();
        w.Step();
        CHECK(w.s.physics3D().DebugBodies().size() == 2);
        CHECK(w.s.physics3D().DebugJoints().size() == 1);
        w.Steps(3.0f);
        w.s.physics3D().BeginDebugFrame();
        w.Step();
        bool anySleeping = false;
        for (const auto& d : w.s.physics3D().DebugBodies()) anySleeping |= d.sleeping;
        CHECK(anySleeping);
        PhysicsDebug::Collect() = false;
        (void)a;
    }

    // ---- Debris pool: capped, recycled, settles, expires.
    {
        World w;
        w.Floor();
        DebrisSettings ds; ds.maxActive = 50; ds.lifetime = 12.0f; ds.settledLife = 4.0f; ds.fadeTime = 0.5f;
        DebrisPool pool(w.s, ds);
        for (int i = 0; i < 120; ++i) {
            pool.Spawn({(float)(i % 10) * 0.5f, 2.0f + (float)(i / 10) * 0.3f, 0.0f}, {0.2f, 0.2f, 0.2f}, {1.0f, 0, 0.5f}, Color::FromBytes(120, 90, 60));
            w.Step(); pool.Update(w.dt);
        }
        std::cout << "  debris: spawned 120, alive " << pool.ActiveCount() << ", recycled " << pool.RecycledTotal() << "\n";
        CHECK(pool.ActiveCount() == 50);
        CHECK(pool.RecycledTotal() == 70);
        int rbCount = 0; for (Rigidbody3D* r : w.s.FindObjectsOfType<Rigidbody3D>()) rbCount += r->gameObject->active ? 1 : 0;
        CHECK(rbCount <= 52);                                 // never more than the cap switched on
        for (int i = 0; i < 150; ++i) { w.Step(); pool.Update(w.dt); }     // 3 s: they land and fall asleep
        std::cout << "  debris after 3 s: asleep " << pool.SleepingCount() << " of " << pool.ActiveCount() << "\n";
        CHECK(pool.SleepingCount() > 40);
        for (Rigidbody3D* r : w.s.FindObjectsOfType<Rigidbody3D>()) if (r->gameObject->active && r->gameObject->name != "Floor") CHECK(P(r->gameObject).y > -0.05f);
        for (int i = 0; i < 500; ++i) { w.Step(); pool.Update(w.dt); }     // 10 more seconds: all expired (asleep 4 s, then the fade)
        CHECK(pool.ActiveCount() == 0);
        CHECK(pool.ExpiredTotal() >= 50);
        // Reuse: spawning again works and takes from the pool.
        GameObject* g = pool.Spawn({0, 1, 0}, {0.3f, 0.3f, 0.3f}, Vec3::Zero, Color::White);
        CHECK(g != nullptr && g->active);
        CHECK(pool.pool().ReuseCount() > 0);
        pool.Clear();
        CHECK(pool.ActiveCount() == 0);
    }
    TEST_MAIN_RESULT();
}
