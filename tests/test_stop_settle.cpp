#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

static float Horiz(const Rigidbody3D* rb) {
    return std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
}

// Drive `ctrl` on a floor. `press` is true while the move key is held.
// Returns horizontal speed each frame after the key comes up, including the
// release frame.
template <class Ctrl>
static std::vector<float> StopCurve(bool third) {
    Scene sc(third ? "tp" : "fp");
    GameObject* floor = sc.CreateGameObject("Floor");
    floor->transform->localPosition = {0, -0.5f, 0};
    floor->AddComponent<BoxCollider3D>()->size = {40, 1, 40};
    floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    GameObject* p = sc.CreateGameObject("Player");
    p->transform->localPosition = {0, 0.2f, 0};
    p->AddComponent<Character>()->Apply();
    auto* rb = p->AddComponent<Rigidbody3D>();
    auto* box = p->AddComponent<BoxCollider3D>();
    box->size = {0.6f, 1.8f, 0.6f};
    box->offset = {0, 0.9f, 0};
    Ctrl* ctrl = p->AddComponent<Ctrl>();
    ctrl->lookSmoothing = 0.0f;
    ctrl->driveAnimation = true;

    sc.Start();
    Input::FeedKeys({});
    for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);

    Input::FeedKeys({'w'});
    for (int i = 0; i < 50; ++i) sc.Update(1.0f / 60.0f);
    const float cruise = Horiz(rb);

    Input::FeedKeys({});
    std::vector<float> speeds;
    speeds.push_back(cruise);
    for (int i = 0; i < 50; ++i) {
        sc.Update(1.0f / 60.0f);
        speeds.push_back(Horiz(rb));
        if (speeds.back() < 0.02f) break;
    }
    return speeds;
}

static int FramesBetween(const std::vector<float>& s, float hi, float lo) {
    int a = -1, b = -1;
    for (int i = 0; i < (int)s.size(); ++i) {
        if (a < 0 && s[i] <= hi) a = i;
        if (s[i] <= lo) { b = i; break; }
    }
    if (a < 0 || b < 0) return -1;
    return b - a;
}

int main() {
    RUN_SUITE("stop_settle");

    CHECK(std::fabs(StopSettleRate(55.0f, false, 1.0f) - 55.0f) < 1e-4f);
    CHECK(std::fabs(StopSettleRate(55.0f, true, 3.0f) - 55.0f) < 1e-4f);
    CHECK(std::fabs(StopSettleRate(55.0f, true, 0.05f) - 55.0f) < 1e-4f);
    CHECK(StopSettleRate(55.0f, true, 1.0f) < 20.0f);

    for (int which = 0; which < 2; ++which) {
        auto speeds = which ? StopCurve<ThirdPersonController>(true)
                            : StopCurve<FirstPersonController>(false);
        std::printf("%s stop:", which ? "third" : "first");
        for (float v : speeds) std::printf(" %.2f", v);
        std::printf("\n");
        CHECK(speeds.size() >= 4);
        CHECK(speeds.front() > 3.5f);
        CHECK(speeds.back() < 0.05f);
        const int head = FramesBetween(speeds, speeds.front(), 2.2f);
        const int tail = FramesBetween(speeds, 2.2f, 0.15f);
        std::printf("  headFrames=%d tailFrames=%d\n", head, tail);
        CHECK(head >= 1);
        CHECK(tail > head * 2);
    }

    TEST_MAIN_RESULT();
}
