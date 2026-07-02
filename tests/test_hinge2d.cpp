#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// 2D hinge (revolute) joint: pins a point, lets the body spin; motor + limits.
int main() {
    RUN_SUITE("hinge2d");

    // ---- Pendulum: a body offset from a world pivot swings but stays pinned ----
    {
        Scene s("pendulum");
        auto* bob = s.CreateGameObject("Bob");
        bob->transform->localPosition = {2, 0, 0};      // 2 units right of the pivot
        auto* rb = bob->AddComponent<Rigidbody2D>();
        rb->freezeRotation = false;                     // hinge needs spin enabled
        auto* col = bob->AddComponent<BoxCollider2D>(); col->size = {1, 1};
        auto* j = bob->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Hinge;
        j->anchor = {0, 0};                             // pivot at the origin
        s.Start();
        bool swungDown = false;
        for (int i = 0; i < 300; ++i) {
            s.Update(1.0f / 60.0f);
            Vec3 p = bob->transform->localPosition;
            // The anchor point (2 units along the body's local +x from center) must
            // stay ~on the pivot, so the center stays ~2 units from the origin.
            float r = std::sqrt(p.x * p.x + p.y * p.y);
            CHECK(std::fabs(r - 2.0f) < 0.5f);          // still pinned at radius 2
            if (p.y < -1.0f) swungDown = true;          // gravity swings it down
        }
        CHECK(swungDown);
    }

    // ---- Motor: spins the body up toward the target speed ----
    {
        Scene s("motor");
        auto* go = s.CreateGameObject("Wheel");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider2D>(); col->size = {1, 1};
        auto* j = go->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Hinge;
        j->anchor = {0, 0};                             // pivot at its own center
        j->useMotor = true; j->motorSpeed = 180.0f; j->maxMotorTorque = 500.0f;
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        CHECK(std::fabs(rb->angularVelocity - 180.0f) < 20.0f);   // reached ~target
    }

    // ---- Limits: a motor-driven body stops at the max angle ----
    {
        Scene s("limit");
        auto* go = s.CreateGameObject("Door");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f; rb->freezeRotation = false; rb->angularDrag = 0.0f;
        auto* col = go->AddComponent<BoxCollider2D>(); col->size = {1, 1};
        auto* j = go->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Hinge;
        j->anchor = {0, 0};
        j->useMotor = true; j->motorSpeed = 200.0f; j->maxMotorTorque = 500.0f;
        j->useLimits = true; j->minAngle = -10.0f; j->maxAngle = 30.0f;
        s.Start();
        for (int i = 0; i < 180; ++i) s.Update(1.0f / 60.0f);
        float ang = go->transform->localRotation.ToEuler().z;
        CHECK(ang <= 30.5f && ang >= 29.0f);            // parked at the upper limit
    }

    // ---- Hinge fields round-trip through the scene file ----
    {
        Scene s("ser");
        auto* go = s.CreateGameObject("H");
        auto* j = go->AddComponent<Joint2D>();
        j->mode = (int)Joint2D::Mode::Hinge;
        j->useMotor = true; j->motorSpeed = 90.0f; j->maxMotorTorque = 250.0f;
        j->useLimits = true; j->minAngle = -15.0f; j->maxAngle = 60.0f;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("s2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* l = s2.Find("H")->GetComponent<Joint2D>();
        CHECK(l && l->mode == (int)Joint2D::Mode::Hinge);
        CHECK(l && l->useMotor && std::fabs(l->motorSpeed - 90.0f) < 1e-3f);
        CHECK(l && std::fabs(l->maxMotorTorque - 250.0f) < 1e-3f);
        CHECK(l && l->useLimits && std::fabs(l->minAngle + 15.0f) < 1e-3f && std::fabs(l->maxAngle - 60.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
