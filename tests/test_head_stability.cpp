#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

// Head look and gait continuity that a Play session was reported as jitter.
// These checks are the motions we could reproduce in a scene step. They are
// not a claim that every imported clip looks smooth on screen.
namespace {

const float kDt = 1.0f / 60.0f;

float MaxOf(const std::vector<float>& v) {
    float m = 0.0f;
    for (float x : v) m = std::fmax(m, x);
    return m;
}

} // namespace

int main() {
    RUN_SUITE("head_stability");

    // A look target eases. One frame must not snap the neck onto it.
    {
        Scene ease("ease"); ease.physicsEnabled = false;
        GameObject* body = ease.CreateGameObject("Body");
        auto* ch = body->AddComponent<Character>();
        ch->Apply();
        ch->anim = 1;
        ch->lookYaw = 80.0f;
        ch->headTurnSpeed = 9.0f;
        ease.Start();
        ease.Update(kDt);
        const float one = ch->HeadYaw();
        std::printf("  built-in head: 80 deg target, one frame %.2f deg\n", one);
        CHECK(one > 4.0f && one < 25.0f);
        float prev = one;
        bool forward = true;
        for (int i = 0; i < 90; ++i) {
            ease.Update(kDt);
            const float y = ch->HeadYaw();
            if (y + 0.05f < prev) forward = false;
            prev = y;
        }
        std::printf("  built-in head: after 1.5 s, yaw %.2f\n", prev);
        CHECK(forward);
        CHECK(prev > 70.0f && prev < 80.5f);
    }

    // Built-in walk hip. The stride dips; a single sample along the cycle does not.
    {
        HumanoidMotion::Inputs in;
        in.speed = 1.4f;
        in.runW = HumanoidMotion::JogWeight(in.speed);
        const float freq = HumanoidMotion::CycleFreq(2, in.speed, in.runW);
        float prev = 0.0f, worstFine = 0.0f, worstFrame = 0.0f, lo = 0.0f, hi = 0.0f;
        const int N = 4000;
        for (int i = 0; i <= N; ++i) {
            in.phase = (float)i / (float)N;
            const float y = HumanoidMotion::HipY(2, in);
            if (i == 0) lo = hi = y;
            else worstFine = std::fmax(worstFine, std::fabs(y - prev));
            lo = std::fmin(lo, y); hi = std::fmax(hi, y);
            prev = y;
        }
        const float dPhase = freq / 60.0f;
        in.phase = 0.0f;
        prev = HumanoidMotion::HipY(2, in);
        const int frames = (int)std::ceil(2.0f / std::fmax(dPhase, 1e-4f));
        for (int i = 1; i <= frames; ++i) {
            float ph = dPhase * (float)i;
            in.phase = ph - std::floor(ph);
            const float y = HumanoidMotion::HipY(2, in);
            worstFrame = std::fmax(worstFrame, std::fabs(y - prev));
            prev = y;
        }
        std::printf("  built-in walk 1.4 m/s hip: fine %.4f m, 60 Hz %.4f m, bob %.4f m\n",
                    worstFine, worstFrame, hi - lo);
        CHECK(worstFine < 0.01f);
        CHECK(worstFrame < 0.04f);
        CHECK(hi - lo > 0.05f && hi - lo < 0.25f);
    }

    // Imported Mixamo: walk, start, stop, and a camera-yaw step.
    {
        righarness::Rig R = righarness::PlayableFromFile("mixamo_t_cm_clips.glb");
        CHECK(R.model != nullptr);
        Character* ch = R.player ? R.player->GetComponent<Character>() : nullptr;
        auto* tp = R.player ? R.player->GetComponent<ThirdPersonController>() : nullptr;
        CHECK(ch != nullptr && tp != nullptr);
        Transform* head = R.Bone(humanoid::Head);
        Transform* hip = R.Bone(humanoid::Hips);
        Transform* thigh = R.Bone(humanoid::LThigh);
        CHECK(head != nullptr && hip != nullptr && thigh != nullptr);
        if (ch && tp && head && hip && thigh) {
            R.Step(kDt, {}, 45);
            auto sample = [&](const std::vector<char>& keys, int frames, std::vector<float>& headDeg,
                              std::vector<float>& hipM, std::vector<float>& thighDeg) {
                Quat ph = head->Rotation(), pt = thigh->Rotation();
                float py = hip->Position().y;
                for (int i = 0; i < frames; ++i) {
                    R.Step(kDt, keys, 1);
                    headDeg.push_back(Quat::Angle(ph, head->Rotation()));
                    thighDeg.push_back(Quat::Angle(pt, thigh->Rotation()));
                    hipM.push_back(hip->Position().y - py);
                    ph = head->Rotation(); pt = thigh->Rotation(); py = hip->Position().y;
                }
            };
            std::vector<float> startH, startHip, startTh;
            sample({'W'}, 20, startH, startHip, startTh);
            std::vector<float> walkH, walkHip, walkTh;
            sample({'W'}, 120, walkH, walkHip, walkTh);
            std::vector<float> stopH, stopHip, stopTh;
            sample({}, 30, stopH, stopHip, stopTh);

            const float walkHead = MaxOf(walkH), walkThigh = MaxOf(walkTh);
            float walkHipM = 0.0f;
            int hipFlips = 0;
            for (std::size_t i = 0; i < walkHip.size(); ++i) {
                walkHipM = std::fmax(walkHipM, std::fabs(walkHip[i]));
                if (i > 0 && std::fabs(walkHip[i]) > 0.0005f && std::fabs(walkHip[i - 1]) > 0.0005f
                    && walkHip[i] * walkHip[i - 1] < 0.0f)
                    ++hipFlips;
            }
            const float startHead = MaxOf(startH), stopHead = MaxOf(stopH);
            std::printf("  mixamo walk head/frame %.2f deg, hip %.4f m, thigh %.2f deg, hip reversals %d\n",
                        walkHead, walkHipM, walkThigh, hipFlips);
            std::printf("  mixamo start head %.2f deg, stop head %.2f deg\n", startHead, stopHead);
            // Two seconds of a stride reverses the hip a handful of times. A buzz reverses every frame.
            CHECK(hipFlips < 12);
            // A snapped bone is tens of degrees in one frame. The walk itself moves less.
            CHECK(walkHead < 20.0f);
            CHECK(walkThigh < 35.0f);
            CHECK(walkHipM < 0.08f);
            CHECK(startHead < 35.0f);
            CHECK(stopHead < 35.0f);

            R.Step(kDt, {'W'}, 10);
            const float yaw0 = ch->HeadYaw();
            const Quat body0 = R.player->transform->Rotation();
            tp->yaw += 80.0f;
            R.Step(kDt, {'W'}, 1);
            const float yaw1 = ch->HeadYaw();
            const float headStep = std::fabs(yaw1 - yaw0);
            const float bodyStep = Quat::Angle(body0, R.player->transform->Rotation());
            std::printf("  mixamo camera +80 deg: head eased %.2f deg, body turned %.2f deg\n",
                        headStep, bodyStep);
            // W is camera-relative, so the new heading is also the move direction.
            // The body eases toward it (about a third of 80° at turnSpeed 24).
            // It must not finish that turn in one frame. The neck eases the rest.
            CHECK(headStep > 2.0f && headStep < 30.0f);
            CHECK(bodyStep > 15.0f && bodyStep < 40.0f);

            // A direction change turns the body. One frame must not finish a right-angle turn.
            const Quat face0 = R.player->transform->Rotation();
            R.Step(kDt, {'D'}, 1);
            const float turn1 = Quat::Angle(face0, R.player->transform->Rotation());
            std::printf("  mixamo W to D: first frame body %.2f deg\n", turn1);
            CHECK(turn1 < 45.0f);

            if (auto* view = R.player->GetComponent<PlayerView>()) {
                view->allowToggle = true;
                view->SetView(0);
                R.Step(kDt, {'W'}, 40);
                Camera* cam = R.scene->mainCamera;
                float prevY = cam && cam->transform ? cam->transform->Position().y : 0.0f;
                float worst = 0.0f;
                int flips = 0;
                float prevDy = 0.0f;
                for (int i = 0; i < 120; ++i) {
                    R.Step(kDt, {'W'}, 1);
                    const float y = cam && cam->transform ? cam->transform->Position().y : prevY;
                    const float dy = y - prevY;
                    worst = std::fmax(worst, std::fabs(dy));
                    if (i > 0 && std::fabs(dy) > 0.001f && std::fabs(prevDy) > 0.001f && dy * prevDy < 0.0f)
                        ++flips;
                    prevDy = dy;
                    prevY = y;
                }
                std::printf("  mixamo first person camera: step %.4f m, reversals %d\n", worst, flips);
                CHECK(worst < 0.08f);
                CHECK(flips < 16);
            } else {
                std::printf("  mixamo first person: this player has no Player View\n");
            }
        }
    }

    TEST_MAIN_RESULT();
}
