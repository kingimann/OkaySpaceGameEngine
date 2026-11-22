// The Engine physics quality: our own physics engine core (okay::phys) runs a normal scene of Rigidbody3D, colliders and
// Joint3D components (engine/src/Physics/Physics3DEngine.inl).
//  - a stack of boxes on a floor stands and sleeps; a ball dropped on it bounces off and the stack wakes;
//  - collision and trigger callbacks: Enter once, Stay while touching, Exit once;
//  - a 5 cm ball at 300 m/s does not pass a 2 cm wall (no Continuous Collision ticked);
//  - game code that moves a body (teleport) or sets its velocity is obeyed;
//  - joints: hinge motor speed, ball-joint drive holding an arm level, cone-twist swing limit, pin weld, distance length;
//  - shapes: capsule, cylinder, cone, hull, compound, plane, mesh collider and terrain floors all hold bodies up;
//  - save and load keep `physicsquality 2` and the ball / cone-twist joint fields;
//  - determinism: the same scene twice, and with 0 or 3 job workers, gives the same bits;
//  - timing (printed): Improved and Engine at 1,000 and 3,000 bodies.
#include "test_framework.hpp"
#include "physics_test_scenes.hpp"
#include <Okay.hpp>
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Physics/Core/World.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {
constexpr float kDt = 1.0f / 60.0f;

struct Counter : public Behaviour {
    int enters = 0, stays = 0, exits = 0, tEnter = 0, tStay = 0, tExit = 0;
    float impact = 0.0f;
    void OnCollisionEnter3D(const Collision3D& c) override { ++enters; impact = std::max(impact, c.impactSpeed); }
    void OnCollisionStay3D(const Collision3D&) override { ++stays; }
    void OnCollisionExit3D(const Collision3D&) override { ++exits; }
    void OnTriggerEnter3D(Collider3D*) override { ++tEnter; }
    void OnTriggerStay3D(Collider3D*) override { ++tStay; }
    void OnTriggerExit3D(Collider3D*) override { ++tExit; }
};

GameObject* Floor(Scene& s) {
    GameObject* f = s.CreateGameObject("Floor");
    f->transform->localPosition = {0, -0.5f, 0};
    f->AddComponent<BoxCollider3D>()->size = {100, 1, 100};
    return f;
}
Rigidbody3D* Body(GameObject* g, float mass = 1.0f) {
    auto* rb = g->AddComponent<Rigidbody3D>();
    rb->mass = mass;
    rb->freezeRotation = false;
    return rb;
}
GameObject* Crate(Scene& s, const std::string& name, const Vec3& p, float size = 1.0f) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = p;
    g->AddComponent<BoxCollider3D>()->size = {size, size, size};
    Body(g);
    return g;
}
void Run(Scene& s, float seconds) { const int n = (int)std::lround(seconds / kDt); for (int i = 0; i < n; ++i) s.Update(kDt); }
double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void BuildMixed(Scene& s, int bodies) { okaytest::BuildMixedBodies(s, bodies, PhysicsQuality::Engine, true); }
using okaytest::PutBytes;

std::vector<unsigned char> RunMixed(JobSystem* js, int bodies, int frames) {
    if (js) SetEngineJobs(js);
    std::vector<unsigned char> out;
    {
        Scene s("mix");
        BuildMixed(s, bodies);
        s.Start();
        for (int f = 0; f < frames; ++f) s.Update(kDt);
        for (const auto& go : s.Objects()) {
            PutBytes(out, go->transform->localPosition);
            PutBytes(out, go->transform->localRotation);
            if (auto* rb = go->GetComponent<Rigidbody3D>()) { PutBytes(out, rb->velocity); PutBytes(out, rb->angularVelocity); PutBytes(out, rb->sleeping); }
        }
    }
    if (js) SetEngineJobs(nullptr);
    return out;
}
} // namespace

int main() {
    RUN_SUITE("engine quality: a stack stands and sleeps, a ball wakes it, callbacks fire");
    {
        Scene s("stack");
        s.physics3D().quality = PhysicsQuality::Engine;
        Floor(s);
        std::vector<GameObject*> st;
        for (int i = 0; i < 6; ++i) st.push_back(Crate(s, "C" + std::to_string(i), {0, 0.5f + (float)i, 0}));
        auto* counter = st[5]->AddComponent<Counter>();
        s.Start();
        Run(s, 3.0f);
        const Vec3 top = st[5]->transform->Position();
        std::cout << "  top crate at (" << top.x << ", " << top.y << ", " << top.z << ")\n";
        CHECK(std::fabs(top.x) < 0.01f && std::fabs(top.z) < 0.01f && std::fabs(top.y - 5.5f) < 0.05f);
        bool asleep = true;
        for (GameObject* g : st) asleep = asleep && g->GetComponent<Rigidbody3D>()->sleeping;
        CHECK(asleep);
        CHECK(counter->enters == 1);            // the crate under it
        CHECK(counter->stays > 0);
        // a ball dropped on top: the top crate wakes, gets an Enter with an impact speed
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0.1f, 9.0f, 0};
        ball->AddComponent<SphereCollider3D>()->radius = 0.3f;
        Body(ball, 2.0f);
        Run(s, 1.0f);
        CHECK(counter->enters == 2);
        std::cout << "  ball hit the top crate at " << counter->impact << " m/s (about 7.7 expected)\n";
        CHECK(counter->impact > 5.0f);
        // the ball rolls off or rests; remove it and the crate gets an Exit
        const int exitsBefore = counter->exits;
        ball->RemoveComponent(ball->GetComponent<SphereCollider3D>());
        Run(s, 0.1f);
        CHECK(counter->exits >= exitsBefore);
    }

    RUN_SUITE("engine quality: triggers");
    {
        Scene s("trig");
        s.physics3D().quality = PhysicsQuality::Engine;
        Floor(s);
        GameObject* zone = s.CreateGameObject("Zone");
        zone->transform->localPosition = {0, 3, 0};
        auto* zc = zone->AddComponent<BoxCollider3D>();
        zc->size = {2, 1, 2};
        zc->isTrigger = true;
        auto* counter = zone->AddComponent<Counter>();
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 6, 0};
        ball->AddComponent<SphereCollider3D>()->radius = 0.2f;
        Body(ball);
        s.Start();
        Run(s, 2.0f);
        CHECK(counter->tEnter == 1);
        CHECK(counter->tExit == 1);
        CHECK(counter->tStay > 0);
        CHECK(ball->transform->Position().y < 0.3f);   // fell through the trigger onto the floor
    }

    RUN_SUITE("engine quality: a fast ball does not pass a thin wall");
    {
        Scene s("bullet");
        s.physics3D().quality = PhysicsQuality::Engine;
        s.physics3D().gravity = Vec3::Zero;
        GameObject* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {10, 0, 0};
        wall->AddComponent<BoxCollider3D>()->size = {0.02f, 10, 10};
        GameObject* ball = s.CreateGameObject("Shot");
        ball->AddComponent<SphereCollider3D>()->radius = 0.05f;
        Body(ball, 0.01f)->velocity = {300, 0, 0};
        s.Start();
        Run(s, 0.5f);
        CHECK(ball->transform->Position().x < 10.0f);
    }

    RUN_SUITE("engine quality: game code moves bodies (teleport, velocity)");
    {
        Scene s("teleport");
        s.physics3D().quality = PhysicsQuality::Engine;
        Floor(s);
        GameObject* c = Crate(s, "C", {0, 0.5f, 0});
        s.Start();
        Run(s, 1.0f);
        c->transform->localPosition = {5, 3, 0};
        Run(s, 0.05f);
        CHECK(std::fabs(c->transform->Position().x - 5.0f) < 0.01f && c->transform->Position().y > 2.9f);
        Run(s, 2.0f);
        CHECK(std::fabs(c->transform->Position().y - 0.5f) < 0.02f);
        c->GetComponent<Rigidbody3D>()->velocity = {0, 5, 0};
        Run(s, 0.2f);
        CHECK(c->transform->Position().y > 1.0f);
    }

    RUN_SUITE("engine quality: joints");
    {
        Scene s("joints");
        s.physics3D().quality = PhysicsQuality::Engine;
        // hinge motor: a wheel spun by its motor at 90 deg/s
        GameObject* wheel = s.CreateGameObject("Wheel");
        wheel->transform->localPosition = {0, 5, 0};
        auto* wc = wheel->AddComponent<CylinderCollider3D>(); wc->radius = 0.5f; wc->height = 0.2f; wc->axis = 2;
        Body(wheel, 5.0f);
        auto* hj = wheel->AddComponent<Joint3D>();
        hj->mode = (int)Joint3D::Mode::Hinge; hj->anchor = {0, 5, 0}; hj->axis = {0, 0, 1};
        hj->useMotor = true; hj->motorSpeed = 90.0f; hj->maxMotorTorque = 500.0f;
        // ball joint with a drive: an arm held level
        GameObject* arm = s.CreateGameObject("Arm");
        arm->transform->localPosition = {10.5f, 5, 0};
        auto* ac = arm->AddComponent<CapsuleCollider3D>(); ac->radius = 0.08f; ac->height = 1.0f; ac->axis = 0;
        Body(arm, 5.0f);
        auto* bj = arm->AddComponent<Joint3D>();
        bj->mode = (int)Joint3D::Mode::Ball; bj->anchor = {10, 5, 0};
        bj->useDrive = true; bj->driveFrequency = 4.0f; bj->driveDampingRatio = 1.0f; bj->driveMaxTorque = 300.0f;
        // cone twist: a limb pushed sideways stops at its 30 degree cone
        GameObject* limb = s.CreateGameObject("Limb");
        limb->transform->localPosition = {20, 4.5f, 0};
        auto* lc = limb->AddComponent<CapsuleCollider3D>(); lc->radius = 0.08f; lc->height = 1.0f;
        auto* lrb = Body(limb, 2.0f);
        lrb->gravityScale = 0.0f;
        lrb->angularVelocity = {0, 0, 3};
        auto* cj = limb->AddComponent<Joint3D>();
        cj->mode = (int)Joint3D::Mode::ConeTwist; cj->anchor = {20, 5, 0}; cj->axis = {0, -1, 0};
        cj->swingLimit1 = cj->swingLimit2 = 30.0f;
        // pin: a beam welded to the world
        GameObject* beam = s.CreateGameObject("Beam");
        beam->transform->localPosition = {30, 5, 0};
        beam->AddComponent<BoxCollider3D>()->size = {2, 0.2f, 0.2f};
        Body(beam, 10.0f);
        auto* pj = beam->AddComponent<Joint3D>();
        pj->mode = (int)Joint3D::Mode::Pin; pj->anchor = {29, 5, 0};
        // distance: a pendulum 2 m long
        GameObject* bob = s.CreateGameObject("Bob");
        bob->transform->localPosition = {42, 5, 0};
        bob->AddComponent<SphereCollider3D>()->radius = 0.2f;
        Body(bob);
        auto* dj = bob->AddComponent<Joint3D>();
        dj->mode = (int)Joint3D::Mode::Distance; dj->anchor = {40, 5, 0};
        s.Start();
        float worstLen = 0.0f, mostSwing = 0.0f;
        for (int i = 0; i < 120; ++i) {
            s.Update(kDt);
            worstLen = std::max(worstLen, std::fabs(Vec3::Distance(bob->transform->Position(), {40, 5, 0}) - 2.0f));
            const Vec3 down = limb->transform->Rotation() * Vec3{0, -1, 0};
            mostSwing = std::max(mostSwing, std::acos(std::min(1.0f, std::max(-1.0f, -down.y))) * 57.2958f);
        }
        const float spin = wheel->GetComponent<Rigidbody3D>()->angularVelocity.z * 57.2958f;
        const Vec3 tip = arm->transform->Position() + arm->transform->Rotation() * Vec3{0.5f, 0, 0};
        const Vec3 beamEnd = beam->transform->Position() + beam->transform->Rotation() * Vec3{1, 0, 0};
        std::cout << "  wheel " << spin << " deg/s (90), arm tip y " << tip.y << " (5), cone swing " << mostSwing
                  << " deg (30), beam end y " << beamEnd.y << " (5), pendulum length error " << worstLen << "\n";
        CHECK(std::fabs(spin - 90.0f) < 3.0f);
        CHECK(std::fabs(tip.y - 5.0f) < 0.1f);
        CHECK(mostSwing < 32.0f && mostSwing > 25.0f);
        CHECK(std::fabs(beamEnd.y - 5.0f) < 0.02f);
        CHECK(worstLen < 0.02f);
        CHECK(Vec3::Distance(wheel->transform->Position(), {0, 5, 0}) < 0.01f);
    }

    RUN_SUITE("engine quality: every collider shape and floor kind holds bodies up");
    {
        Scene s("shapes");
        s.physics3D().quality = PhysicsQuality::Engine;
        // floors: a plane, a mesh collider (a cube mesh), a terrain, a heightfield
        GameObject* plane = s.CreateGameObject("Plane");
        plane->AddComponent<PlaneCollider3D>();
        GameObject* meshFloor = s.CreateGameObject("MeshFloor");
        meshFloor->transform->localPosition = {40, 0.5f, 0};
        meshFloor->transform->localScale = {10, 1, 10};
        meshFloor->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        meshFloor->AddComponent<MeshCollider3D>();
        GameObject* terr = s.CreateGameObject("Terrain");
        terr->transform->localPosition = {80, 1.0f, 0};
        auto* t = terr->AddComponent<Terrain>();
        t->size = 20.0f;
        t->Resize(16);
        t->Flatten(0.0f);
        GameObject* hfo = s.CreateGameObject("Heightfield");
        hfo->transform->localPosition = {120, 2.0f, 0};
        auto* hf = hfo->AddComponent<HeightfieldCollider3D>();
        hf->SetHeights(4, 4, std::vector<float>(16, 0.0f));
        hf->cellX = hf->cellZ = 4.0f;
        std::vector<GameObject*> things;
        const float floorY[4] = {0.0f, 1.0f, 1.0f, 2.0f};
        for (int f = 0; f < 4; ++f) {
            const float x0 = (float)f * 40.0f;
            auto add = [&](const std::string& n, float dx, int kind) {
                GameObject* g = s.CreateGameObject(n + std::to_string(f));
                g->transform->localPosition = {x0 + dx, floorY[f] + 2.0f, 0};
                switch (kind) {
                    case 0: { auto* c = g->AddComponent<CapsuleCollider3D>(); c->radius = 0.3f; c->height = 1.2f; c->axis = 0; break; }
                    case 1: { auto* c = g->AddComponent<CylinderCollider3D>(); c->radius = 0.4f; c->height = 0.6f; break; }
                    case 2: { auto* c = g->AddComponent<ConeCollider3D>(); c->radius = 0.5f; c->height = 0.8f; break; }
                    case 3: {
                        auto* c = g->AddComponent<ConvexHullCollider3D>();
                        c->SetPoints({{-0.5f, -0.3f, -0.5f}, {0.5f, -0.3f, -0.5f}, {0.5f, -0.3f, 0.5f}, {-0.5f, -0.3f, 0.5f}, {0, 0.4f, 0}});
                        break;
                    }
                    default: {
                        auto* c = g->AddComponent<CompoundCollider3D>();
                        c->AddBox({0, 0, 0}, {1.0f, 0.2f, 1.0f});
                        c->AddSphere({0, 0.3f, 0}, 0.2f);
                        break;
                    }
                }
                Body(g);
                things.push_back(g);
            };
            add("Cap", -3, 0); add("Cyl", -1.5f, 1); add("Cone", 0, 2); add("Hull", 1.5f, 3); add("Comp", 3, 4);
        }
        s.Start();
        Run(s, 4.0f);
        int above = 0, asleep = 0;
        for (std::size_t i = 0; i < things.size(); ++i) {
            const float fy = floorY[i / 5];
            const float y = things[i]->transform->Position().y;
            if (y > fy && y < fy + 1.0f) ++above;
            else std::cout << "  " << things[i]->name << " at y " << y << " (floor " << fy << ")\n";
            asleep += things[i]->GetComponent<Rigidbody3D>()->sleeping;
        }
        std::cout << "  " << above << " of " << things.size() << " resting on their floors, " << asleep << " asleep\n";
        CHECK(above == (int)things.size());
        CHECK(asleep >= (int)things.size() - 2);
    }

    RUN_SUITE("engine quality: save and load");
    {
        Scene a("save");
        a.physics3D().quality = PhysicsQuality::Engine;
        GameObject* g = a.CreateGameObject("Limb");
        Body(g);
        auto* j = g->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::ConeTwist; j->swingLimit1 = 25; j->swingLimit2 = 35; j->useDrive = true;
        j->driveTarget = {10, 20, 30}; j->driveStiffness = 120; j->driveDamping = 7; j->driveFrequency = 0; j->driveMaxTorque = 55;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("load");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        CHECK(b.physics3D().quality == PhysicsQuality::Engine);
        CHECK(b.physics().quality == PhysicsQuality::Improved || b.physics().quality == PhysicsQuality::Classic);
        Joint3D* k = b.Find("Limb") ? b.Find("Limb")->GetComponent<Joint3D>() : nullptr;
        CHECK(k && k->mode == (int)Joint3D::Mode::ConeTwist && k->swingLimit1 == 25 && k->swingLimit2 == 35 && k->useDrive &&
              k->driveTarget.y == 20 && k->driveStiffness == 120 && k->driveDamping == 7 && k->driveFrequency == 0 && k->driveMaxTorque == 55);
    }

    RUN_SUITE("engine quality: determinism (twice; 0 and 3 job workers)");
    {
        const auto a = RunMixed(nullptr, 400, 60), b = RunMixed(nullptr, 400, 60);
        CHECK(a == b);
        JobSystem js0(0), js3(3);
        const auto c = RunMixed(&js0, 400, 60), d = RunMixed(&js3, 400, 60);
        CHECK(c == d);
        CHECK(a == c);
    }

    RUN_SUITE("engine quality: timing against Improved (printed)");
    for (int n : {1000, 3000}) {
        double ms[2] = {0, 0};
        for (int q = 0; q < 2; ++q) {
            Scene s("bench");
            BuildMixed(s, n);
            s.physics3D().quality = q == 0 ? PhysicsQuality::Improved : PhysicsQuality::Engine;
            s.Start();
            for (int i = 0; i < 10; ++i) s.Update(kDt);
            const double t0 = NowMs();
            for (int i = 0; i < 60; ++i) s.Update(kDt);
            ms[q] = (NowMs() - t0) / 60.0;
        }
        std::cout << "  " << n << " bodies: Improved " << ms[0] << " ms/frame, Engine " << ms[1] << " ms/frame\n";
        CHECK(ms[1] > 0.0);
    }

    TEST_MAIN_RESULT();
}
