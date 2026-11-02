// Jump dip, turn-in-place step, and accel/turn lean on the blocky Character.
#include "test_framework.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>

using namespace okay;

static Character::Locomotion Still(float dt) {
    Character::Locomotion lo;
    lo.onGround = true;
    lo.dt = dt;
    lo.walkSpeed = 4.5f;
    lo.runSpeed = 8.0f;
    lo.facing = {0, 0, -1};
    return lo;
}

int main() {
    RUN_SUITE("character_feel");

    // The jump starts this frame. The knees dip; nothing waits on the dip.
    {
        Scene s; s.physicsEnabled = false;
        auto* ch = s.CreateGameObject("Jumper")->AddComponent<Character>();
        auto lo = Still(1.0f / 60.0f);
        lo.jumped = true;
        lo.velocity = {0, 6, 0};
        ch->DriveLocomotion(lo);
        CHECK(ch->anim == 5);
        ch->Update(1.0f / 60.0f);
        CHECK(ch->JumpCrouchWeight() > 0.9f);
        const auto dipped = ch->PoseAt(0.0f);
        ch->jumpCrouchTime = 0.0f;
        ch->Update(1.0f / 60.0f);
        CHECK(ch->JumpCrouchWeight() == 0.0f);
        const auto plain = ch->PoseAt(0.0f);
        CHECK(std::fabs(dipped[HumanoidMotion::B_LTHIGH].x - plain[HumanoidMotion::B_LTHIGH].x) > 10.0f);
    }

    // A standing turn past the threshold takes one step and lets the foot pins go.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Turner");
        auto* ch = go->AddComponent<Character>();
        auto* fik = go->AddComponent<FootIK>();
        fik->footLock = true;
        ch->turnStepAngle = 50.0f;
        bool stepped = false;
        for (int i = 0; i < 6 && !stepped; ++i) {
            auto lo = Still(0.05f);
            lo.yawRate = 400.0f;
            ch->DriveLocomotion(lo);
            stepped = ch->TurnStepping();
            ch->Update(0.05f);
        }
        CHECK(stepped);
        CHECK(ch->TurnStepping() || ch->TurnStepLeg() == 0 || ch->TurnStepLeg() == 1);
        CHECK(fik->LockSuppressed());
        // A small turn does not step.
        Scene s2; s2.physicsEnabled = false;
        auto* quiet = s2.CreateGameObject("Quiet")->AddComponent<Character>();
        for (int i = 0; i < 4; ++i) {
            auto lo = Still(0.05f);
            lo.yawRate = 20.0f;
            quiet->DriveLocomotion(lo);
        }
        CHECK(!quiet->TurnStepping());
        CHECK(std::fabs(quiet->BodyTwist()) < quiet->turnStepAngle);
    }

    // Speeding up along the facing tips the chest.
    {
        Scene s; s.physicsEnabled = false;
        auto* ch = s.CreateGameObject("Leaner")->AddComponent<Character>();
        ch->motionLean = 1.0f;
        ch->motionLeanMax = 8.0f;
        Vec3 p{0, 0, 0};
        float spd = 0.0f;
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 45; ++i) {
            spd += 0.5f;
            p.z -= spd * dt;
            ch->gameObject->transform->localPosition = p;
            ch->Update(dt);
        }
        CHECK(std::fabs(ch->MotionPitch()) > 0.4f);
        ch->motionLean = 0.0f;
        for (int i = 0; i < 30; ++i) ch->Update(dt);
        CHECK(std::fabs(ch->MotionPitch()) < 0.5f);
    }

    TEST_MAIN_RESULT();
}
