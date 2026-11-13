#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Math/JointLimit.hpp>
#include <cmath>
#include <string>

using namespace okay;

// Signed spin (degrees) of `q` about a unit world axis, from a reference
// direction in the plane. Independent of HingeAngleDegrees.
static float SpinAbout(const Quat& q, const Vec3& axis) {
    Vec3 a = axis.Normalized();
    Vec3 ref = std::fabs(a.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 u = Vec3::Cross(a, ref).Normalized();
    Vec3 v = Vec3::Cross(a, u);
    Vec3 spun = q * u;
    return std::atan2(Vec3::Dot(spun, v), Vec3::Dot(spun, u)) * Mathf::Rad2Deg;
}

// 3D hinge (revolute) joint: pins a point, locks rotation to an axis, motor.
int main() {
    RUN_SUITE("hinge3d");

    // ---- Pendulum about a Z axis: swings in the XY plane, stays pinned, no Z drift ----
    {
        Scene s("pendulum");
        auto* bob = s.CreateGameObject("Bob");
        bob->transform->localPosition = {2, 0, 0};
        auto* rb = bob->AddComponent<Rigidbody3D>();
        rb->freezeRotation = false;
        auto* col = bob->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = bob->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};
        j->axis = {0, 0, 1};                            // spin about Z -> swing in XY
        s.Start();
        bool swungDown = false;
        for (int i = 0; i < 300; ++i) {
            s.Update(1.0f / 60.0f);
            Vec3 p = bob->transform->localPosition;
            float r = std::sqrt(p.x * p.x + p.y * p.y);
            CHECK(std::fabs(r - 2.0f) < 0.5f);          // still pinned at radius 2
            CHECK(std::fabs(p.z) < 0.3f);               // motion stayed in the hinge plane
            if (p.y < -1.0f) swungDown = true;
        }
        CHECK(swungDown);
    }

    // ---- Axis lock: angular velocity off the axis is removed, on-axis survives ----
    {
        Scene s("axislock");
        auto* go = s.CreateGameObject("Spinner");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};                          // pivot at center
        j->axis = {0, 1, 0};                            // hinge about Y
        rb->angularVelocity = {3, 5, 4};                // mixed spin
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(std::fabs(rb->angularVelocity.x) < 0.5f); // off-axis killed
        CHECK(std::fabs(rb->angularVelocity.z) < 0.5f);
        CHECK(rb->angularVelocity.y > 2.0f);            // on-axis spin kept
    }

    // ---- Motor: spins the body up to the target speed about the axis ----
    {
        Scene s("motor");
        auto* go = s.CreateGameObject("Wheel");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};
        j->axis = {0, 1, 0};
        j->useMotor = true; j->motorSpeed = 180.0f; j->maxMotorTorque = 500.0f;
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);
        float degPerSec = rb->angularVelocity.y * Mathf::Rad2Deg;
        CHECK(std::fabs(degPerSec - 180.0f) < 20.0f);
    }

    // ---- Hinge fields round-trip through the scene file ----
    {
        Scene s("ser");
        auto* go = s.CreateGameObject("H");
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->axis = {1, 0, 0};
        j->useMotor = true; j->motorSpeed = 90.0f; j->maxMotorTorque = 250.0f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("H")->GetComponent<Joint3D>();
        CHECK(l && l->mode == (int)Joint3D::Mode::Hinge);
        CHECK(l && std::fabs(l->axis.x - 1.0f) < 1e-3f && std::fabs(l->axis.z) < 1e-3f);
        CHECK(l && l->useMotor && std::fabs(l->motorSpeed - 90.0f) < 1e-3f);
        CHECK(l && std::fabs(l->maxMotorTorque - 250.0f) < 1e-3f);
    }

    // ---- Limits share the IK hinge clamp, and a pose already inside is kept ----
    RUN_SUITE("hinge3d limits: clamp about the axis");
    {
        const Quat rest = Quat::Identity;
        const Vec3 up{0, 1, 0};
        const Vec3 fwd{0, 0, 1};
        CHECK(std::fabs(HingeAngleDegrees(Quat::AngleAxis(80.0f, up), rest, up) - 80.0f) < 0.5f);
        CHECK(std::fabs(HingeAngleDegrees(Quat::AngleAxis(-50.0f, up), rest, up) + 50.0f) < 0.5f);
        Quat hi = ClampHingeRotation(Quat::AngleAxis(80.0f, up), rest, up, -10.0f, 30.0f);
        Quat lo = ClampHingeRotation(Quat::AngleAxis(-50.0f, up), rest, up, -10.0f, 30.0f);
        Quat ok = ClampHingeRotation(Quat::AngleAxis(12.0f, up), rest, up, -10.0f, 30.0f);
        float hiSpin = SpinAbout(hi, up), loSpin = SpinAbout(lo, up), okSpin = SpinAbout(ok, up);
        CHECK(hiSpin <= 30.5f && hiSpin >= 29.0f);
        CHECK(loSpin >= -10.5f && loSpin <= -9.0f);
        CHECK(std::fabs(okSpin - 12.0f) < 0.5f);
        // A non-identity start pose, and an axis that is not up.
        const Quat turned = Quat::AngleAxis(90.0f, up);
        Quat past = Quat::AngleAxis(50.0f, up) * turned;
        CHECK(std::fabs(HingeAngleDegrees(past, turned, up) - 50.0f) < 0.5f);
        Quat stopped = ClampHingeRotation(past, turned, up, -15.0f, 25.0f);
        float rel = HingeAngleDegrees(stopped, turned, up);
        CHECK(rel <= 25.5f && rel >= 24.0f);
        Quat flap = ClampHingeRotation(Quat::AngleAxis(70.0f, fwd), rest, fwd, -5.0f, 20.0f);
        float flapSpin = SpinAbout(flap, fwd);
        CHECK(flapSpin <= 20.5f && flapSpin >= 19.0f);
    }

    // ---- A motor-driven door stops at the max and never swings past it ----
    RUN_SUITE("hinge3d limits: motor stops at max");
    {
        Scene s("limit-max");
        auto* go = s.CreateGameObject("Door");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};
        j->axis = {0, 1, 0};
        j->useMotor = true; j->motorSpeed = 200.0f; j->maxMotorTorque = 500.0f;
        j->useLimits = true; j->minAngle = -10.0f; j->maxAngle = 30.0f;
        s.Start();
        bool parked = false;
        for (int i = 0; i < 180; ++i) {
            s.Update(1.0f / 60.0f);
            float ang = SpinAbout(go->transform->localRotation, Vec3{0, 1, 0});
            CHECK(ang <= 30.5f && ang >= -10.5f);
            if (ang >= 29.0f) parked = true;
        }
        CHECK(parked);
    }

    // ---- The same motor driving the other way stops at the min ----
    RUN_SUITE("hinge3d limits: motor stops at min");
    {
        Scene s("limit-min");
        auto* go = s.CreateGameObject("Door");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};
        j->axis = {0, 1, 0};
        j->useMotor = true; j->motorSpeed = -200.0f; j->maxMotorTorque = 500.0f;
        j->useLimits = true; j->minAngle = -10.0f; j->maxAngle = 30.0f;
        s.Start();
        bool parked = false;
        for (int i = 0; i < 180; ++i) {
            s.Update(1.0f / 60.0f);
            float ang = SpinAbout(go->transform->localRotation, Vec3{0, 1, 0});
            CHECK(ang <= 30.5f && ang >= -10.5f);
            if (ang <= -9.0f) parked = true;
        }
        CHECK(parked);
    }

    // ---- A pose shoved past the stop is pulled back on the next step ----
    RUN_SUITE("hinge3d limits: pose past the stop is pulled back");
    {
        Scene s("limit-shove");
        auto* go = s.CreateGameObject("Flap");
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};
        j->axis = {0, 0, 1};
        j->useMotor = true; j->motorSpeed = 200.0f; j->maxMotorTorque = 500.0f;
        j->useLimits = true; j->minAngle = -15.0f; j->maxAngle = 25.0f;
        s.Start();
        s.Update(1.0f / 60.0f);                         // capture the start pose
        go->transform->localRotation = Quat::AngleAxis(80.0f, Vec3{0, 0, 1});
        rb->angularVelocity = {0, 0, 3.0f};             // still pushing past the stop
        s.Update(1.0f / 60.0f);
        float ang = SpinAbout(go->transform->localRotation, Vec3{0, 0, 1});
        CHECK(ang <= 25.5f && ang >= 24.0f);
        for (int i = 0; i < 60; ++i) {
            s.Update(1.0f / 60.0f);
            ang = SpinAbout(go->transform->localRotation, Vec3{0, 0, 1});
            CHECK(ang <= 25.5f && ang >= -15.5f);
        }
    }

    // ---- A falling flap cannot swing through the lower stop ----
    RUN_SUITE("hinge3d limits: pendulum does not pass the stop");
    {
        Scene s("limit-fall");
        auto* bob = s.CreateGameObject("Bob");
        bob->transform->localPosition = {2, 0, 0};
        auto* rb = bob->AddComponent<Rigidbody3D>();
        rb->freezeRotation = false;
        auto* col = bob->AddComponent<BoxCollider3D>(); col->size = {1, 1, 1};
        auto* j = bob->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->anchor = {0, 0, 0};
        j->axis = {0, 0, 1};
        j->useLimits = true; j->minAngle = -20.0f; j->maxAngle = 40.0f;
        s.Start();
        bool reachedStop = false;
        for (int i = 0; i < 300; ++i) {
            s.Update(1.0f / 60.0f);
            Vec3 p = bob->transform->localPosition;
            float ang = std::atan2(p.y, p.x) * Mathf::Rad2Deg;
            CHECK(ang <= 40.5f && ang >= -20.5f);
            CHECK(p.y > -0.9f);                         // an unlimited hinge falls to y = -2
            if (ang <= -18.0f) reachedStop = true;
        }
        CHECK(reachedStop);
    }

    // ---- Limit fields round-trip, and an older joint line still loads ----
    RUN_SUITE("hinge3d limits: scene file");
    {
        Scene s("ser-lim");
        auto* go = s.CreateGameObject("H");
        auto* j = go->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Hinge;
        j->axis = {0, 1, 0};
        j->useMotor = true; j->motorSpeed = 12.0f; j->maxMotorTorque = 40.0f;
        j->useLimits = true; j->minAngle = -15.0f; j->maxAngle = 60.0f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("H")->GetComponent<Joint3D>();
        CHECK(l && l->useLimits && std::fabs(l->minAngle + 15.0f) < 1e-3f && std::fabs(l->maxAngle - 60.0f) < 1e-3f);
        CHECK(l && l->useMotor && std::fabs(l->motorSpeed - 12.0f) < 1e-3f);

        const std::string old =
            "okayscene 3\ngameobject 0 \"H\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  joint3d 3 \"\" 0 0 0 1 1 30 2 0 50 0 1 0 1 90 250\n  parent -1\nend\n";
        Scene s3("s3");
        CHECK(SceneSerializer::Deserialize(s3, old));
        auto* o = s3.Find("H")->GetComponent<Joint3D>();
        CHECK(o && o->mode == (int)Joint3D::Mode::Hinge);
        CHECK(o && o->useMotor && std::fabs(o->motorSpeed - 90.0f) < 1e-3f);
        CHECK(o && !o->useLimits && std::fabs(o->minAngle + 45.0f) < 1e-3f && std::fabs(o->maxAngle - 45.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
