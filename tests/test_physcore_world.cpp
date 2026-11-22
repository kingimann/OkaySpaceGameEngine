// okay::phys::World, our own rigid-body engine: behaviour tests with numbers.
//  - free fall, resting contact (no sinking), sleeping and waking;
//  - a 10-box stack and a 55-box pyramid stand still and fall asleep;
//  - friction holds a box on a 20 degree slope at mu 0.6 and lets it slide at mu 0.1;
//  - restitution: a ball with e = 0.8 bounces back to about 0.64 of its drop height;
//  - continuous collision: 5 cm balls at 400 m/s and boxes at 200 m/s never pass a 2 cm wall;
//  - joints: pendulum length, hinge limits and motor speed, slider limits, fixed weld, cone limit, rope, a 20-link
//    chain, and an orientation drive that holds an arm level against gravity (powered ragdoll joint);
//  - determinism: the same scene twice, and solved with islands in a scrambled order, gives the same bits;
//  - queries: ray, overlap and shape cast;
//  - timing (printed): 1,000 and 5,000 bodies.
#include "test_framework.hpp"
#include "okay/Physics/Core/World.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace okay::phys;

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kDt = 1.0f / 60.0f;

BodyId Ground(World& w, float y = 0.0f) {
    BodyDef bd; bd.type = BodyType::Static; bd.position = V3{0, y, 0};
    const BodyId g = w.CreateBody(bd);
    ColliderDef cd; cd.shape = Shape::Box(V3{50, 0.5f, 50}); cd.local.p = V3{0, -0.5f, 0};
    w.AddCollider(g, cd);
    return g;
}
BodyId Box(World& w, const V3& p, const V3& h, float friction = 0.5f, const Q& q = Q{}) {
    BodyDef bd; bd.position = p; bd.rotation = q;
    const BodyId b = w.CreateBody(bd);
    ColliderDef cd; cd.shape = Shape::Box(h); cd.friction = friction;
    w.AddCollider(b, cd);
    return b;
}
BodyId Ball(World& w, const V3& p, float r, float restitution = 0.0f) {
    BodyDef bd; bd.position = p;
    const BodyId b = w.CreateBody(bd);
    ColliderDef cd; cd.shape = Shape::Sphere(r); cd.restitution = restitution;
    w.AddCollider(b, cd);
    return b;
}
void Run(World& w, float seconds) { const int n = (int)std::lround(seconds / kDt); for (int i = 0; i < n; ++i) w.Step(kDt); }
double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::uint64_t PileHash(bool scramble) {
    World w;
    if (scramble) {
        // islands solved in a scrambled order: a "parallel for" that runs the ranges back to front, one at a time
        w.SetParallelFor([](int count, const std::function<void(int, int)>& fn) {
            for (int i = count - 1; i >= 0; --i) fn(i, i + 1);
        });
    }
    Ground(w);
    for (int i = 0; i < 6; ++i)       // six separate towers: six islands
        for (int k = 0; k < 5; ++k) Box(w, V3{(float)i * 3.0f, 0.5f + (float)k * 1.01f, 0.0f}, V3{0.5f, 0.5f, 0.5f}, 0.5f, Q::AxisAngle(V3{0, 1, 0}, 0.1f * (float)k));
    for (int i = 0; i < 300; ++i) {   // many contacts so the narrow phase also takes the parallel path
        Ball(w, V3{-20.0f + (float)(i % 20) * 1.1f, 8.0f + (float)(i / 20) * 1.1f, 5.0f}, 0.4f);
    }
    Run(w, 2.0f);
    return w.StateHash();
}
} // namespace

int main() {
    RUN_SUITE("physcore world: free fall");
    {
        World w;
        const BodyId b = Ball(w, V3{0, 100, 0}, 0.5f);
        Run(w, 1.0f);
        // semi-implicit Euler: y = 100 - g dt^2 n (n + 1) / 2
        const double n = 60.0, expect = 100.0 - 9.81 * kDt * kDt * n * (n + 1) / 2.0;
        CHECK_NEAR(w.GetPosition(b).y, expect, 1e-3);
        CHECK_NEAR(w.GetLinearVelocity(b).y, -9.81, 1e-3);
    }

    RUN_SUITE("physcore world: a box rests without sinking and falls asleep; a hit wakes it");
    {
        World w;
        Ground(w);
        const BodyId b = Box(w, V3{0, 0.6f, 0}, V3{0.5f, 0.5f, 0.5f});
        Run(w, 3.0f);
        CHECK_NEAR(w.GetPosition(b).y, 0.5, 0.01);
        CHECK(!w.IsAwake(b));
        const BodyId ball = Ball(w, V3{0, 3, 0}, 0.3f);
        Run(w, 0.8f);
        CHECK(w.IsAwake(b) || w.GetPosition(ball).y < 1.4f);   // the ball reached it and woke it
        Run(w, 4.0f);
        CHECK(!w.IsAwake(b) && !w.IsAwake(ball));
        CHECK(w.GetPosition(ball).y > 1.0f + 0.25f);            // resting on top, not inside
    }

    RUN_SUITE("physcore world: a 10-box stack stands and sleeps");
    {
        World w;
        Ground(w);
        std::vector<BodyId> s;
        for (int i = 0; i < 10; ++i) s.push_back(Box(w, V3{0, 0.5f + (float)i * 1.0f, 0}, V3{0.5f, 0.5f, 0.5f}, 0.6f));
        Run(w, 6.0f);
        const V3 top = w.GetPosition(s.back());
        std::cout << "  top box at (" << top.x << ", " << top.y << ", " << top.z << "), stats: points " << w.Stats().points << "\n";
        CHECK(std::fabs(top.x) < 0.05f && std::fabs(top.z) < 0.05f);
        CHECK_NEAR(top.y, 9.5, 0.06);
        int asleep = 0;
        for (BodyId b : s) asleep += !w.IsAwake(b);
        CHECK(asleep == 10);
    }

    RUN_SUITE("physcore world: a 55-box pyramid stands");
    {
        World w;
        Ground(w);
        std::vector<BodyId> s;
        std::vector<V3> start;
        for (int row = 0; row < 10; ++row)
            for (int i = 0; i < 10 - row; ++i) {
                const V3 p{(float)i * 1.05f + (float)row * 0.525f, 0.5f + (float)row * 1.0f, 0.0f};
                s.push_back(Box(w, p, V3{0.5f, 0.5f, 0.5f}, 0.6f));
                start.push_back(p);
            }
        Run(w, 8.0f);
        float worst = 0.0f;
        for (std::size_t i = 0; i < s.size(); ++i) worst = std::max(worst, Len(w.GetPosition(s[i]) - start[i]));
        std::cout << "  most a box moved: " << worst << " m, awake " << w.Stats().awake << "\n";
        CHECK(worst < 0.1f);
        CHECK(w.Stats().awake == 0);
    }

    RUN_SUITE("physcore world: friction on a 20 degree slope");
    for (float mu : {0.6f, 0.1f}) {
        World w;
        const float a = 20.0f * kPi / 180.0f;
        BodyDef gd; gd.type = BodyType::Static; gd.rotation = Q::AxisAngle(V3{0, 0, 1}, a);
        const BodyId g = w.CreateBody(gd);
        ColliderDef cd; cd.shape = Shape::Box(V3{20, 0.5f, 5}); cd.local.p = V3{0, -0.5f, 0}; cd.friction = mu;
        w.AddCollider(g, cd);
        const V3 up = Rotate(gd.rotation, V3{0, 1, 0});
        const BodyId b = Box(w, up * 0.5f, V3{0.5f, 0.5f, 0.5f}, mu, gd.rotation);
        Run(w, 2.0f);
        const float moved = Len(w.GetPosition(b) - up * 0.5f);
        std::cout << "  mu " << mu << ": moved " << moved << " m\n";
        if (mu > 0.5f) CHECK(moved < 0.02f);    // tan(20) = 0.36 < 0.6: holds
        else CHECK(moved > 3.0f);               // 0.1 < 0.36: slides (about 0.5 * g (sin - mu cos) t^2 = 4.8 m)
    }

    RUN_SUITE("physcore world: restitution 0.8 bounces to about 0.64 of the height");
    {
        World w;
        BodyDef gd; gd.type = BodyType::Static;
        const BodyId g = w.CreateBody(gd);
        ColliderDef cd; cd.shape = Shape::Plane(V3{0, 1, 0}, 0.0f); cd.restitution = 0.8f;
        w.AddCollider(g, cd);
        const BodyId b = Ball(w, V3{0, 2.25f, 0}, 0.25f, 0.8f);   // bottom 2 m above the floor
        float peak = 0.0f;
        bool bounced = false;
        for (int i = 0; i < 180; ++i) {
            w.Step(kDt);
            const float vy = w.GetLinearVelocity(b).y;
            if (vy > 0.5f) bounced = true;
            if (bounced) peak = std::max(peak, w.GetPosition(b).y - 0.25f);
        }
        std::cout << "  bounce height " << peak << " m (ideal 1.28)\n";
        CHECK(bounced);
        CHECK(peak > 1.28f * 0.85f && peak < 1.28f * 1.1f);
    }

    RUN_SUITE("physcore world: fast bodies never pass a thin wall");
    {
        World w;
        w.gravity = V3{};
        BodyDef wd; wd.type = BodyType::Static; wd.position = V3{10, 0, 0};
        const BodyId wall = w.CreateBody(wd);
        ColliderDef cd; cd.shape = Shape::Box(V3{0.01f, 5, 5});
        w.AddCollider(wall, cd);
        std::vector<BodyId> shots;
        for (int i = 0; i < 8; ++i) {
            BodyDef bd; bd.position = V3{0, -3.0f + (float)i * 0.8f, 0}; bd.linearVelocity = V3{400, 0, 0};
            const BodyId b = w.CreateBody(bd);
            ColliderDef sc; sc.shape = (i % 2) ? Shape::Sphere(0.05f) : Shape::Box(V3{0.05f, 0.05f, 0.05f});
            w.AddCollider(b, sc);
            shots.push_back(b);
        }
        Run(w, 0.5f);
        int through = 0;
        for (BodyId b : shots) through += w.GetPosition(b).x > 10.0f;
        CHECK(through == 0);
    }

    RUN_SUITE("physcore world: joints");
    {
        // pendulum on a ball joint: length kept
        World w;
        BodyDef bd; bd.position = V3{2, 5, 0};
        const BodyId bob = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Sphere(0.2f);
        w.AddCollider(bob, cd);
        JointDef jd; jd.type = JointType::Ball; jd.bodyA = kInvalidId; jd.bodyB = bob;
        jd.frameA.p = V3{0, 5, 0}; jd.frameB.p = V3{-2, 0, 0};
        SetupJoint(jd);
        w.CreateJoint(jd);
        float worst = 0.0f;
        for (int i = 0; i < 300; ++i) { w.Step(kDt); worst = std::max(worst, std::fabs(Len(w.GetPosition(bob) - V3{0, 5, 0}) - 2.0f)); }
        std::cout << "  pendulum length error " << worst << " m\n";
        CHECK(worst < 0.01f);
    }
    {
        // hinge with limits: a door that swings under a push stops at 30 degrees
        World w;
        w.gravity = V3{};
        BodyDef bd; bd.position = V3{0.5f, 1, 0};
        const BodyId door = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Box(V3{0.5f, 1, 0.05f});
        w.AddCollider(door, cd);
        JointDef jd; jd.type = JointType::Hinge; jd.bodyB = door;
        jd.frameA = Xf{V3{0, 1, 0}, Q::AxisAngle(V3{0, 0, 1}, kPi / 2)};   // joint X axis = world Y
        jd.frameB = Xf{V3{-0.5f, 0, 0}, Q::AxisAngle(V3{0, 0, 1}, kPi / 2)};
        jd.angular[0].mode = AxisMode::Limited; jd.angular[0].lower = -kPi / 6; jd.angular[0].upper = kPi / 6;
        SetupJoint(jd);
        const JointId j = w.CreateJoint(jd);
        w.SetAngularVelocity(door, V3{0, 3, 0});
        float most = 0.0f;
        for (int i = 0; i < 120; ++i) { w.Step(kDt); most = std::max(most, std::fabs(w.JointAngles(j).x)); }
        std::cout << "  hinge reached " << most * 180 / kPi << " degrees (limit 30)\n";
        CHECK(most < kPi / 6 + 0.03f && most > kPi / 6 - 0.05f);
        CHECK(Len(w.GetPosition(door) - (V3{0, 1, 0} + Rotate(w.GetRotation(door), V3{0.5f, 0, 0}))) < 0.01f);
        // motor: 2 rad/s with plenty of torque (the 200 kg door has about 67 kg m^2 about the hinge)
        JointDef& e = w.EditJoint(j);
        e.angular[0].mode = AxisMode::Free; e.angular[0].motor = true; e.angular[0].motorSpeed = 2.0f; e.angular[0].motorMaxForce = 2000.0f;
        Run(w, 1.0f);
        CHECK_NEAR(Dot(w.GetAngularVelocity(door), V3{0, 1, 0}), 2.0, 0.05);
    }
    {
        // slider with limits: falls along its axis and stops at -1 m
        World w;
        BodyDef bd; bd.position = V3{0, 5, 0};
        const BodyId b = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Box(V3{0.2f, 0.2f, 0.2f});
        w.AddCollider(b, cd);
        JointDef jd; jd.type = JointType::Slider; jd.bodyB = b;
        jd.frameA = Xf{V3{0, 5, 0}, Q::AxisAngle(V3{0, 0, 1}, kPi / 2)};   // X axis = world up
        jd.frameB = Xf{V3{}, Q::AxisAngle(V3{0, 0, 1}, kPi / 2)};
        jd.linear[0].mode = AxisMode::Limited; jd.linear[0].lower = -1.0f; jd.linear[0].upper = 1.0f;
        SetupJoint(jd);
        w.CreateJoint(jd);
        w.SetAngularVelocity(b, V3{1, 2, 3});
        Run(w, 2.0f);
        CHECK_NEAR(w.GetPosition(b).y, 4.0, 0.02);
        CHECK(std::fabs(w.GetPosition(b).x) < 0.01f && std::fabs(w.GetPosition(b).z) < 0.01f);
        CHECK(Len(w.GetAngularVelocity(b)) < 0.05f);
    }
    {
        // fixed joint: a heavy weld on a beam does not sag more than a few millimetres
        World w;
        BodyDef bd; bd.position = V3{1, 2, 0};
        const BodyId b = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Box(V3{1, 0.1f, 0.1f});
        w.AddCollider(b, cd);
        JointDef jd; jd.type = JointType::Fixed; jd.bodyB = b; jd.frameA.p = V3{0, 2, 0}; jd.frameB.p = V3{-1, 0, 0};
        SetupJoint(jd);
        w.CreateJoint(jd);
        Run(w, 2.0f);
        const V3 tip = w.GetTransform(b).Apply(V3{1, 0, 0});
        std::cout << "  welded beam tip sag " << 2.0f - tip.y << " m\n";
        CHECK(std::fabs(tip.y - 2.0f) < 0.02f);
    }
    {
        // cone-twist: a limb pushed sideways stops at its 45 degree cone
        World w;
        w.gravity = V3{};
        BodyDef bd; bd.position = V3{0, -0.5f, 0};
        const BodyId b = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Capsule(0.1f, 0.4f);
        w.AddCollider(b, cd);
        JointDef jd; jd.type = JointType::ConeTwist; jd.bodyB = b;
        jd.frameA = Xf{V3{0, 0, 0}, Q::AxisAngle(V3{0, 0, 1}, -kPi / 2)};   // X axis = world down
        jd.frameB = Xf{V3{0, 0.5f, 0}, Q::AxisAngle(V3{0, 0, 1}, -kPi / 2)};
        jd.swingSpan1 = jd.swingSpan2 = kPi / 4;
        jd.angular[0].lower = -0.3f; jd.angular[0].upper = 0.3f;
        SetupJoint(jd);
        const JointId j = w.CreateJoint(jd);
        w.SetAngularVelocity(b, V3{0, 0, 4});
        float most = 0.0f;
        for (int i = 0; i < 120; ++i) { w.Step(kDt); const V3 a = w.JointAngles(j); most = std::max(most, std::sqrt(a.y * a.y + a.z * a.z)); }
        std::cout << "  cone swing reached " << most * 180 / kPi << " degrees (limit 45)\n";
        CHECK(most < kPi / 4 + 0.04f && most > kPi / 4 - 0.1f);
    }
    {
        // a rope (distance joint, 0..2 m): the weight hangs 2 m below, a rod would push back, a rope does not
        World w;
        BodyDef bd; bd.position = V3{0, 4, 0};
        const BodyId b = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Sphere(0.1f);
        w.AddCollider(b, cd);
        JointDef jd; jd.type = JointType::Distance; jd.bodyB = b; jd.frameA.p = V3{0, 5, 0}; jd.minLength = 0.0f; jd.maxLength = 2.0f;
        SetupJoint(jd);
        w.CreateJoint(jd);
        Run(w, 3.0f);
        CHECK_NEAR(w.GetPosition(b).y, 3.0, 0.02);
    }
    {
        // a chain of 20 capsules on ball joints, released sideways: every link stays attached
        World w;
        std::vector<BodyId> links;
        BodyId prev = kInvalidId;
        for (int i = 0; i < 20; ++i) {
            BodyDef bd; bd.position = V3{0.5f + (float)i * 1.0f, 10, 0}; bd.rotation = Q::AxisAngle(V3{0, 0, 1}, kPi / 2);
            const BodyId b = w.CreateBody(bd);
            ColliderDef cd; cd.shape = Shape::Capsule(0.1f, 0.4f);
            w.AddCollider(b, cd);
            JointDef jd; jd.type = JointType::Ball; jd.bodyA = prev; jd.bodyB = b;
            jd.frameA.p = prev == kInvalidId ? V3{0, 10, 0} : V3{0, -0.5f, 0};   // the world point, or the far end of the previous link
            jd.frameB.p = V3{0, 0.5f, 0};
            SetupJoint(jd);
            w.CreateJoint(jd);
            links.push_back(b);
            prev = b;
        }
        float worst = 0.0f;
        for (int s = 0; s < 240; ++s) {
            w.Step(kDt);
            for (std::size_t i = 1; i < links.size(); ++i) {
                const V3 a = w.GetTransform(links[i - 1]).Apply(V3{0, -0.5f, 0});
                const V3 b = w.GetTransform(links[i]).Apply(V3{0, 0.5f, 0});
                worst = std::max(worst, Len(a - b));
            }
        }
        std::cout << "  20-link chain worst gap " << worst << " m\n";
        CHECK(worst < 0.05f);
    }
    {
        // powered joint: an arm (1 m capsule, 5 kg) held level against gravity by an orientation drive
        World w;
        BodyDef bd; bd.position = V3{0.5f, 2, 0}; bd.rotation = Q::AxisAngle(V3{0, 0, 1}, kPi / 2);
        const BodyId arm = w.CreateBody(bd);
        ColliderDef cd; cd.shape = Shape::Capsule(0.08f, 0.42f);
        w.AddCollider(arm, cd);
        w.SetMass(arm, 5.0f);
        JointDef jd; jd.type = JointType::Ball; jd.bodyB = arm;
        jd.frameA.p = V3{0, 2, 0};
        jd.frameA.q = Q::AxisAngle(V3{0, 0, 1}, kPi / 2);
        jd.frameB.p = V3{0, 0.5f, 0};
        jd.drive = true; jd.driveTarget = Q{}; jd.driveFrequency = 4.0f; jd.driveDampingRatio = 1.0f; jd.driveMaxTorque = 200.0f;
        SetupJoint(jd);
        const JointId j = w.CreateJoint(jd);
        Run(w, 2.0f);
        const V3 tip = w.GetTransform(arm).Apply(V3{0, -0.5f, 0});
        std::cout << "  driven arm tip height " << tip.y << " (target 2.0), torque " << Len(w.JointTorque(j)) << " N*m (needs about 24.5)\n";
        CHECK(std::fabs(tip.y - 2.0f) < 0.1f);
        // a weak drive (cap 5 N*m) cannot hold it: the arm swings down
        w.EditJoint(j).driveMaxTorque = 5.0f;
        float lowest = 10.0f;
        for (int i = 0; i < 120; ++i) { w.Step(kDt); lowest = std::min(lowest, w.GetTransform(arm).Apply(V3{0, -0.5f, 0}).y); }
        CHECK(lowest < 1.2f);
        // and the drive follows a moving target: rotate the target 90 degrees down
        w.EditJoint(j).driveMaxTorque = 500.0f;
        w.EditJoint(j).driveTarget = Q::AxisAngle(V3{0, 0, 1}, -kPi / 4);
        Run(w, 2.0f);
        const V3 a = w.JointAngles(j);
        std::cout << "  drive to -45 degrees: swing " << a.z * 180 / kPi << "\n";
        CHECK(std::fabs(a.z + kPi / 4) < 0.08f);
    }

    RUN_SUITE("physcore world: determinism (twice, and islands in scrambled order)");
    {
        const std::uint64_t a = PileHash(false), b = PileHash(false), c = PileHash(true);
        CHECK(a == b);
        CHECK(a == c);
    }

    RUN_SUITE("physcore world: queries");
    {
        World w;
        Ground(w);
        const BodyId b = Box(w, V3{0, 0.5f, 5}, V3{0.5f, 0.5f, 0.5f});
        const BodyId s = Ball(w, V3{3, 0.5f, 0}, 0.5f);
        w.Step(kDt);
        RayHit h = w.RayCast(V3{0, 0.5f, 0}, V3{0, 0, 1}, 100.0f);
        CHECK(h.hit && h.body == b);
        CHECK_NEAR(h.t, 4.5, 1e-3);
        CHECK_NEAR(h.normal.z, -1.0, 1e-4);
        h = w.RayCast(V3{0, 0.5f, 0}, V3{1, 0, 0}, 100.0f);
        CHECK(h.hit && h.body == s);
        CHECK_NEAR(h.t, 2.5, 2e-3);
        h = w.RayCast(V3{0, 5, 0}, V3{0, -1, 0}, 100.0f);
        CHECK(h.hit && h.t > 4.9f && h.t < 5.01f);
        const auto over = w.Overlap(Shape::Sphere(1.0f), Xf{V3{3, 0.5f, 0.8f}, Q{}});
        bool hasBall = false;
        for (ColliderId c : over) hasBall = hasBall || w.ColliderBody(c) == s;
        CHECK(hasBall);
        RayHit sc = w.ShapeCast(Shape::Sphere(0.25f), Xf{V3{0, 0.5f, 0}, Q{}}, V3{0, 0, 1}, 20.0f, 0xFFFFFFFFu, false,
                                [&](ColliderId c) { return w.GetType(w.ColliderBody(c)) != BodyType::Static; });
        CHECK(sc.hit && sc.body == b);
        CHECK_NEAR(sc.t, 4.25, 0.01);
    }

    RUN_SUITE("physcore world: a triangle mesh floor (boxes rest, a ball rolls across edges without bumps, rays hit it)");
    {
        World w;
        std::vector<Tri3> tris;
        for (int i = -10; i < 10; ++i)
            for (int k = -10; k < 10; ++k) {
                const V3 a{(float)i, 0, (float)k}, b{(float)i + 1, 0, (float)k}, c{(float)i + 1, 0, (float)k + 1}, d{(float)i, 0, (float)k + 1};
                tris.push_back({a, c, b});
                tris.push_back({a, d, c});
            }
        BodyDef gd; gd.type = BodyType::Static;
        const BodyId g = w.CreateBody(gd);
        ColliderDef gc; gc.shape = Shape::Mesh(std::make_shared<TriMeshSource>(tris));
        w.AddCollider(g, gc);
        std::vector<BodyId> boxes;
        for (int i = 0; i < 5; ++i) boxes.push_back(Box(w, V3{-6.0f + (float)i * 2.6f, 1.0f + (float)i * 0.3f, -4.0f}, V3{0.5f, 0.5f, 0.5f}, 0.6f, Q::AxisAngle(V3{0, 1, 0}, 0.3f * (float)i)));
        BodyDef rd; rd.position = V3{-8.0f, 0.5f, 3.3f}; rd.linearVelocity = V3{5, 0, 0}; rd.angularVelocity = V3{0, 0, -10};
        const BodyId ball = w.CreateBody(rd);
        ColliderDef bc; bc.shape = Shape::Sphere(0.5f);
        w.AddCollider(ball, bc);
        float hiBall = 0.0f, loBall = 10.0f;
        for (int i = 0; i < 180; ++i) {
            w.Step(kDt);
            if (i > 10 && i < 150) { hiBall = std::max(hiBall, w.GetPosition(ball).y); loBall = std::min(loBall, w.GetPosition(ball).y); }
        }
        bool rest = true;
        for (BodyId b : boxes) rest = rest && std::fabs(w.GetPosition(b).y - 0.5f) < 0.01f;
        std::cout << "  ball height range on the mesh " << loBall << " .. " << hiBall << ", travelled to x " << w.GetPosition(ball).x << "\n";
        CHECK(rest);
        CHECK(hiBall - loBall < 0.01f);
        CHECK(w.GetPosition(ball).x > 0.0f);
        const RayHit h = w.RayCast(V3{0.3f, 5, 0.7f}, V3{0, -1, 0}, 10.0f);
        CHECK(h.hit && h.body == g);
        CHECK_NEAR(h.t, 5.0, 1e-4);
        CHECK_NEAR(h.normal.y, 1.0, 1e-5);
    }

    RUN_SUITE("physcore world: timing");
    for (int n : {1000, 5000}) {
        World w;
        Ground(w);
        const int side = (int)std::ceil(std::sqrt((double)n / 10.0));
        int made = 0;
        for (int layer = 0; made < n; ++layer)
            for (int i = 0; i < side && made < n; ++i)
                for (int k = 0; k < side && made < n; ++k, ++made) {
                    const V3 p{(float)i * 1.2f - side * 0.6f, 1.0f + (float)layer * 1.2f, (float)k * 1.2f - side * 0.6f};
                    if (made % 2) Box(w, p, V3{0.4f, 0.4f, 0.4f}); else Ball(w, p, 0.45f);
                }
        const double t0 = NowMs();
        const int steps = 120;
        for (int i = 0; i < steps; ++i) w.Step(kDt);
        const double ms = (NowMs() - t0) / steps;
        std::cout << "  " << n << " bodies: " << ms << " ms/step (broad " << w.Stats().broadMs << ", narrow " << w.Stats().narrowMs
                  << ", solve " << w.Stats().solveMs << " in the last step; " << w.Stats().islands << " islands, "
                  << w.Stats().points << " points)\n";
        CHECK(ms < 1000.0);
    }

    TEST_MAIN_RESULT();
}
