#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

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

    TEST_MAIN_RESULT();
}
